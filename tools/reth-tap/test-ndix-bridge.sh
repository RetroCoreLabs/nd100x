#!/bin/bash
#
# test-ndix-bridge.sh - the whole chain, end to end, with a real NDIX guest.
#
#   NDIX et0  ->  gateway ethernet segment (TCP 3094)  ->  reth-tap  ->  nd0
#                                                                        |
#                                            ping 223.255.254.8 <- host stack
#
# Every other test in this directory stops short of the TAP device, because
# creating one needs root. This is the one that does not, so it is a script the
# developer runs rather than something a tool runs unattended.
#
# WHAT EACH LEG ALREADY HAS ITS OWN PROOF FOR, AND WHAT THIS ADDS
#   make test        the framing, over socketpairs, no privilege
#   make test-gw     reth-tap joining a real gateway, frames both ways
#   nd500x method 4  NDIX on the gateway segment, guest to guest, 6/6 packets
# None of those put the HOST's own network stack on the wire. That is the only
# thing this script proves, and it is the reason reth-tap exists.
#
# THE TWO TRAPS, both from nd500x/docs/HOWTO-TEST-NDIX-NETWORKING.md:
#   * `-trailers` is MANDATORY. NDIX's ARP advertises trailer encapsulation its
#     own et driver refuses to send, so without it every IP packet dies inside
#     the driver while the interface looks perfectly healthy - UP, RUNNING, ARP
#     filling, counters moving.
#   * etconfig MUST run before ifconfig, and it wants SIX separate byte
#     arguments; it rejects the 08:00:26:.. colon form.
#
# The guest configures itself: rather than waiting for `login:` and typing two
# commands over a console that drops the odd character, the two lines go INTO
# /etc/rc on a SCRATCH COPY of the image. The original image is never touched.
#
# Processes: this script stops only the three it started itself, on exit. It
# leaves the nd0 device in place - removing it needs root again, and a device
# that outlives the test is harmless. `--down` at the end if you want it gone.
#
# Usage:   ./test-ndix-bridge.sh            (asks for sudo once, for nd0)
#          ND500X_ROOT=/path ./test-ndix-bridge.sh
#          NDIX_IMAGE=/path/to/rootfs_net.img ./test-ndix-bridge.sh

set -uo pipefail
cd "$(dirname "$0")"

# ---------------------------------------------------------------- settings --
# Paths are derived from this script's own location, never written out: the
# repo must work on any checkout. ND500X_ROOT overrides when nd500x is not a
# sibling of this repository.
ND500X_ROOT="${ND500X_ROOT:-$(cd ../../../nd500x 2>/dev/null && pwd || echo '')}"
NDIX_IMAGE="${NDIX_IMAGE:-${ND500X_ROOT}/docker/disk/rootfs_net.img}"
GATEWAY="../nd100-gateway/gateway.js"

DEV="${NDIX_TAP_DEV:-nd0}"
ETH_PORT="${ETH_PORT:-3094}"
WS_PORT="${WS_PORT:-18766}"      # not 8765: a real gateway may be running
HOST_IP=223.255.254.1
GUEST_IP=223.255.254.8
GUEST_MAC="0x08 0x00 0x26 0xF4 0x01 0x00"
BOOT_TIMEOUT="${BOOT_TIMEOUT:-240}"

say()  { printf '\n=== %s\n' "$*"; }
fail() { printf '\nFAILED: %s\n' "$*" >&2; exit 1; }

# ------------------------------------------------------------- what we need --
say "Checking what is here"

[ -n "$ND500X_ROOT" ] || fail "cannot find an nd500x checkout. Set ND500X_ROOT."
NDX="$ND500X_ROOT/build/bin/nd500x"
[ -x "$NDX" ]         || fail "no nd500x binary at $NDX - build it first."
[ -f "$NDIX_IMAGE" ]  || fail "no NDIX image at $NDIX_IMAGE - set NDIX_IMAGE."
[ -f "$GATEWAY" ]     || fail "no gateway at $GATEWAY"
command -v node >/dev/null    || fail "node is not on PATH"
command -v python3 >/dev/null || fail "python3 is not on PATH"

NDIXPUT="$ND500X_ROOT/tools/ndix/ndixput.py"
TAPSH="$ND500X_ROOT/tools/ndix-tap.sh"
[ -f "$NDIXPUT" ] || fail "no ndixput.py at $NDIXPUT"
[ -f "$TAPSH" ]   || fail "no ndix-tap.sh at $TAPSH"

make -s reth-tap || fail "reth-tap did not build"
echo "  nd500x     $NDX"
echo "  image      $NDIX_IMAGE"
echo "  bridge     $(pwd)/reth-tap"

# ------------------------------------------------------------ the tap device --
# The one step that needs root, and the reason this is a script rather than
# something run unattended. reth-tap will not create the device itself: one it
# made would have no address and no route, so the bridge would report frames
# moving while the host was not actually on the wire.
if ip link show "$DEV" >/dev/null 2>&1; then
    say "TAP device $DEV already exists"
else
    say "Creating TAP device $DEV - this needs root"
    sudo "$TAPSH" up || fail "could not create $DEV"
fi
ip addr show "$DEV" | grep -q "inet $HOST_IP/" \
    || fail "$DEV has no $HOST_IP address - run: sudo $TAPSH up"

# -------------------------------------------------------- scratch disk image --
# NEVER patch the image in place. It is 71 MB and it is the only copy.
WORK=$(mktemp -d)
IMG="$WORK/rootfs_test.img"
say "Copying the image to a scratch file (71 MB, a few seconds)"
cp "$NDIX_IMAGE" "$IMG" || fail "could not copy the image"

say "Patching /etc/rc so the guest brings up et0 by itself"
RC="$WORK/rc"
python3 "$NDIXPUT" "$IMG" /etc/rc "$RC" --get || fail "could not read /etc/rc"

python3 - "$RC" "$GUEST_IP" "$GUEST_MAC" <<'PY' || fail "could not patch /etc/rc"
import sys
rc, ip, mac = sys.argv[1], sys.argv[2], sys.argv[3]
lines = open(rc).read().split('\n')

# Drop a block from a previous run rather than stacking a second one on top:
# the file may only grow into the space it already owns.
out, skip = [], False
for l in lines:
    if 'reth-tap: network - BEGIN' in l: skip = True; continue
    if 'reth-tap: network - END'   in l: skip = False; continue
    if not skip: out.append(l)

# TRAP: /etc/rc ends with `exit 0`. Anything after it is on disk, is perfectly
# correct, and never runs. Insert BEFORE the last one.
idx = max(i for i, l in enumerate(out) if l.strip() == 'exit 0')

block = [
    '# reth-tap: network - BEGIN',
    # etconfig FIRST, and six separate bytes - it rejects the colon form.
    '/etc/etconfig et0 %s > /dev/console 2>&1' % mac,
    # -trailers is NOT optional; without it every IP packet dies in the driver.
    '/etc/ifconfig et0 inet %s netmask 255.255.255.0 -trailers up > /dev/console 2>&1' % ip,
    '# reth-tap: network - END',
]
open(rc, 'w').write('\n'.join(out[:idx] + block + out[idx:]))
print('  inserted %d lines before the final "exit 0"' % len(block))
PY

python3 "$NDIXPUT" "$IMG" /etc/rc "$RC" --pad '\n' || fail "could not write /etc/rc back"

# ------------------------------------------------------------------ start up --
GW_LOG=$(mktemp);  BR_LOG=$(mktemp);  ND_LOG=$(mktemp);  CONF=$(mktemp --suffix=.json)
GW_PID=""; BR_PID=""; ND_PID=""

cleanup() {
    # Only the three processes this script started itself.
    for p in "$ND_PID" "$BR_PID" "$GW_PID"; do
        [ -n "$p" ] && kill "$p" 2>/dev/null
    done
    wait 2>/dev/null
    rm -rf "$WORK" "$CONF"
    echo
    echo "Logs kept:  gateway $GW_LOG"
    echo "            bridge  $BR_LOG"
    echo "            nd500x  $ND_LOG"
    echo
    echo "The $DEV device was left in place. To remove it:  sudo $TAPSH down"
}
trap cleanup EXIT

cat > "$CONF" <<JSON
{
  "websocket": { "port": $WS_PORT },
  "staticDir": "",
  "terminals": { "port": 15002, "welcome": "reth-tap ndix test" },
  "hdlc": [],
  "ethernet": [{ "name": "ETH-0", "segment": 0, "port": $ETH_PORT, "enabled": true }],
  "smd": { "images": [] }, "floppy": { "images": [] }, "scsi": { "images": [] }
}
JSON

say "Starting the gateway (ethernet segment 0 on TCP $ETH_PORT)"
node "$GATEWAY" --config "$CONF" > "$GW_LOG" 2>&1 &
GW_PID=$!
# Wait for the port to answer rather than sleeping a fixed time - a fixed
# sleep passes on a quiet machine and fails on a busy one.
#
# ONE connection, not two. This probe is a real TCP client as far as the
# gateway is concerned, so each attempt shows up in its log as
#     ETH seg=0 TCP client connected from ...
#     ETH seg=0 TCP client disconnected ...
# The first version of this probed twice - once here and once again to confirm
# - which put FOUR connections in the log for a run with only TWO members
# (reth-tap and nd500x), and reads exactly like the bridge flapping. It was
# not. Expect one short-lived connection here, then two that stay.
READY=no
for _ in $(seq 1 50); do
    if (exec 3<>/dev/tcp/127.0.0.1/$ETH_PORT) 2>/dev/null; then READY=yes; break; fi
    sleep 0.1
done
[ "$READY" = yes ] || fail "the gateway is not listening on $ETH_PORT - see $GW_LOG"
echo "  listening"

say "Starting reth-tap ($DEV <-> 127.0.0.1:$ETH_PORT)"
./reth-tap --dev "$DEV" --host 127.0.0.1 --port "$ETH_PORT" > "$BR_LOG" 2>&1 &
BR_PID=$!
sleep 1
kill -0 "$BR_PID" 2>/dev/null || { cat "$BR_LOG"; fail "reth-tap exited immediately"; }
grep -q "joined" "$BR_LOG" || { cat "$BR_LOG"; fail "reth-tap did not join the segment"; }
echo "  $(grep joined "$BR_LOG")"

say "Booting NDIX onto the segment (this takes a minute or two)"
# ND500X_NOXMSG=0 IS NOT OPTIONAL HERE.
#
# nd500x's own --ndix path does setenv_default("ND500X_NOXMSG", "1")
# (src/frontend/nd500x/nd500x_ndix.c:221) - XMSG is bypassed by DEFAULT,
# because without the bypass proc0 sleeps forever on a machine with no
# networking to do. But XMSG is exactly what the ethernet rides on: with it
# off, nd500_fecall.c:390 answers the guest's XMSG device init with completion
# code 1, xgattach gives up, et0 is never created, and /etc/rc then reports
#     etconfig: ioctl SIOCSETADDR: no such interface
#     ifconfig: ioctl (SIOCGIFFLAGS): no such interface
# which reads like a patching mistake and is not one. docker/entrypoint.sh:542
# sets the same variable to 0 for the same reason.
#
# env_flag() treats "set and not 0" as true (nd500_settings.c:28-31), so the
# literal "0" here really does turn the bypass off.
ND500X_NOXMSG=0 ND500X_ETH_UPLINK="tcp:127.0.0.1:$ETH_PORT" \
    "$NDX" --ndix "$IMG" --telnet > "$ND_LOG" 2>&1 &
ND_PID=$!

# ------------------------------------------------------------------- the test --
# The success condition IS the ping: nothing else proves the host's own stack
# reached a 1988 guest through the bridge. Poll for the condition rather than
# sleeping a fixed time - a fixed sleep passes on a quiet machine and fails on
# a busy one.
say "Waiting for $GUEST_IP to answer (up to ${BOOT_TIMEOUT}s)"
UP=no
for i in $(seq 1 "$BOOT_TIMEOUT"); do
    if ! kill -0 "$ND_PID" 2>/dev/null; then
        tail -20 "$ND_LOG"; fail "nd500x exited during boot - see $ND_LOG"
    fi
    if ping -c1 -W1 "$GUEST_IP" >/dev/null 2>&1; then UP=yes; break; fi
    [ $((i % 15)) -eq 0 ] && echo "  ${i}s..."
done

echo
if [ "$UP" = yes ]; then
    say "THE GUEST ANSWERS - running a real ping"
    ping -c 6 "$GUEST_IP"
    PING_RC=$?
    say "Bridge counters"
    # NOT kill -USR1: reth-tap installs handlers for SIGINT and SIGTERM only,
    # and the default action for SIGUSR1 is to terminate - that would kill the
    # bridge in the middle of its own test.
    #
    # The sleep is not padding. reth-tap prints its counters only on a poll
    # that TIMED OUT with the totals changed (an idle second), so reading the
    # log the instant the ping finishes catches the last line printed BEFORE
    # the ping - which showed "host->segment 4, segment->host 0" on a run where
    # six replies had just come back, making the bridge look dead when it had
    # just carried the whole exchange. Give it an idle second to publish.
    sleep 2
    grep "host->segment" "$BR_LOG" | tail -1
    echo
    if [ $PING_RC -eq 0 ]; then
        echo "PASS: the host reached NDIX at $GUEST_IP through reth-tap."
    else
        echo "PARTIAL: the guest came up but the 6-packet ping was not clean."
    fi
    exit $PING_RC
fi

say "NO ANSWER from $GUEST_IP after ${BOOT_TIMEOUT}s"
cat <<EOF
Where to look, in order:

  1. Did the guest boot at all?      tail -40 $ND_LOG
     Look for "NDIX Release 3" and then the /etc/rc output.

  2. Was XMSG on?                    grep -i xgattach $ND_LOG
     "xgattach: bad completion code 01 from feidev" means ND500X_NOXMSG was
     set - et0 is never created and both config commands say "no such
     interface". This script forces it to 0; a value inherited from the shell
     would have to be exported as 0 too.

  3. Did et0 come up in the guest?   grep -i "et0" $ND_LOG
     "et0: flags=63<UP,BROADCAST,NOTRAILERS,RUNNING>" is what you want.
     NOTRAILERS missing means the -trailers flag did not take, and every IP
     packet is being dropped inside the driver with the interface looking fine.

  4. Did frames reach the bridge?    cat $BR_LOG
     "host->segment" counting up with "segment->host" at zero means the guest
     is not transmitting; the reverse means the host is not.

  5. Did the gateway see a member?   grep ETH $GW_LOG
     Two members are expected: reth-tap and nd500x.

EOF
exit 1
