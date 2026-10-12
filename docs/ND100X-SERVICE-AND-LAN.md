# Setting up nd100x as a service and on your network

(File: `docs/ND100X-SERVICE-AND-LAN.md`.)

Goal: nd100x boots by itself, with nobody logged in and no console, and the
emulated ND answers on the real LAN (here 192.168.1.40 on 192.168.1.0/24),
through **eth0 or Wi-Fi** (wired PCs need eth0, section 6).

Written for and tested on a **Raspberry Pi** (Debian 11 bullseye, aarch64,
dhcpcd, no NetworkManager). Everything except the dhcpcd line is generic
Linux (Ubuntu too); differences are marked **Pi** / **Ubuntu**.

The four files you create (`nd100x-net.sh`, `nd100x-net.service`,
`nd100x.service`, `/etc/default/nd100x`) are listed in full in section 3;
nothing else in the repository changes.

## 1. How the traffic gets onto the LAN

The Ethernet II card is attached to a TAP interface `nd0`
(`net = tap:nd0` in the INI). The ND's IP address is **not** an INI
setting; it lives in the disc image (section 2).

| Method | eth0 | Wi-Fi | Touches the host's own network? |
|---|---|---|---|
| **proxy ARP + routing** (default, `ND_MODE=proxyarp`) | yes | **yes** | no |
| bridge (`ND_MODE=bridge`) | yes | **no** | yes: eth0 moves into a bridge |

Why not bridge Wi-Fi: an access point accepts one MAC address per client
station, so frames from the ND's MAC are dropped. Proxy ARP avoids this: the
host answers ARP for the ND's address with its own Wi-Fi MAC and routes the
packets to `nd0`.

What `nd100x-net.sh start` does in proxy-ARP mode:

1. creates TAP `nd0` owned by the nd100x user, brings it up;
2. `ip route replace 192.168.1.40/32 dev nd0`;
3. `ip_forward=1`, `proxy_arp=1` on `nd0` and on the chosen uplink only,
   `rp_filter=0` on both;
4. policy routing: packets *from* the ND use table 140 (LAN route + router
   as default via the uplink), so they leave via Wi-Fi even if eth0 holds the
   main default route.

If both eth0 and wlan0 are on the same subnet, only one uplink should have
`proxy_arp=1`, otherwise both answer ARP for the ND. That is why
`ND_UPLINK` selects exactly one.

## 2. Set the ND's address in the disc image

nd100x must not be running. Use `ndtool` (see `docs/ETHERNET.md` section 5):

```sh
cp WD0-SINTRAN-M.IMG WD0-SINTRAN-M.IMG.orig
ndtool -x -p -F 'SYSTEM/AIP-*' -o work WD0-SINTRAN-M.IMG
# AIP-CONFIG.SYMB active line:   0  0  192.168.1.40  192.168.001.001  0
#   (IP gateway = your router; keep CR LF line ends)
# AIP-HOSTS.SYMB:  192.168.1.40  c3  C3   (and the host's address)
ndtool -p -f --put work/AIP-CONFIG.SYMB 'SYSTEM/AIP-CONFIG:SYMB' WD0-SINTRAN-M.IMG
ndtool -p -f --put work/AIP-HOSTS.SYMB  'SYSTEM/AIP-HOSTS:SYMB'  WD0-SINTRAN-M.IMG
ndtool --fsck WD0-SINTRAN-M.IMG
```

Pick an address that is free (not in the router's DHCP range, `ping` and
`ip neigh` show nothing). The image in this repo is set to 192.168.1.40.

## 3. Install

Create these files (paths as shown). Adjust `User=`, `WorkingDirectory=` and
`ExecStart=` in `nd100x.service` to where the repo and `build/bin/nd100x`
live (here `/home/ronny/repos/nd100x`).

**`/usr/local/sbin/nd100x-net.sh`** (`sudo chmod 755`):

```sh
#!/bin/sh
# nd100x-net.sh start|stop - put the ND (Ethernet II card on TAP nd0) on the
# host's LAN by proxy ARP + routing. Nothing on eth0/wlan0 is reconfigured
# (no bridge, no address change), so it is safe over ssh and works on Wi-Fi.
# Settings come from /etc/default/nd100x (see nd100x.default).
set -e
[ -r /etc/default/nd100x ] && . /etc/default/nd100x
ND_IP=${ND_IP:-192.168.1.40}
ND_UPLINK=${ND_UPLINK:-wlan0}          # wlan0 or eth0
ND_TAP=${ND_TAP:-nd0}
ND_USER=${ND_USER:-root}
TABLE=${ND_TABLE:-140}
ND_MODE=${ND_MODE:-proxyarp}           # proxyarp (wlan0 or eth0) | bridge (eth0 only)
ND_BRIDGE=${ND_BRIDGE:-br0}

if [ "$ND_MODE" = bridge ]; then
    # eth0 + nd0 in one bridge; dhcpcd must run on $ND_BRIDGE, not on eth0
    # (/etc/dhcpcd.conf: denyinterfaces eth0 nd0). Wi-Fi cannot be bridged.
    case "$1" in
    start)
        ip tuntap add dev "$ND_TAP" mode tap user "$ND_USER" 2>/dev/null || true
        ip link show "$ND_BRIDGE" >/dev/null 2>&1 || ip link add "$ND_BRIDGE" type bridge
        ip link set "$ND_BRIDGE" address "$(cat /sys/class/net/eth0/address)"
        ip link set eth0 master "$ND_BRIDGE"
        ip link set "$ND_TAP" master "$ND_BRIDGE"
        ip link set "$ND_TAP" up; ip link set eth0 up; ip link set "$ND_BRIDGE" up
        systemctl restart dhcpcd
        ;;
    stop)
        ip link set eth0 nomaster 2>/dev/null || true
        ip link del "$ND_BRIDGE" 2>/dev/null || true
        ip tuntap del dev "$ND_TAP" mode tap 2>/dev/null || true
        systemctl restart dhcpcd
        ;;
    esac
    exit 0
fi

case "$1" in
start)
    ip link show "$ND_UPLINK" >/dev/null
    ip tuntap add dev "$ND_TAP" mode tap user "$ND_USER" 2>/dev/null || true
    ip link set "$ND_TAP" up
    ip route replace "$ND_IP/32" dev "$ND_TAP"
    sysctl -q -w net.ipv4.ip_forward=1
    sysctl -q -w "net.ipv4.conf.$ND_TAP.proxy_arp=1" "net.ipv4.conf.$ND_UPLINK.proxy_arp=1"
    sysctl -q -w "net.ipv4.conf.$ND_TAP.rp_filter=0" "net.ipv4.conf.$ND_UPLINK.rp_filter=0"
    # Everything the ND sends leaves through the chosen uplink, even when
    # the other interface holds the main default route.
    # At boot the uplink may not have its DHCP lease yet: wait (max 90 s)
    i=0
    while [ $i -lt 90 ] && ! ip -4 route show default dev "$ND_UPLINK" | grep -q via; do
        sleep 1; i=$((i+1))
    done
    GW=$(ip -4 route show default dev "$ND_UPLINK" | awk '{print $3; exit}')
    NET=$(ip -4 route show dev "$ND_UPLINK" scope link | awk '{print $1; exit}')
    ip rule del from "$ND_IP" lookup "$TABLE" 2>/dev/null || true
    ip rule add from "$ND_IP" lookup "$TABLE"
    ip route flush table "$TABLE" 2>/dev/null || true
    if [ -n "$NET" ]; then ip route add "$NET" dev "$ND_UPLINK" table "$TABLE"; fi
    if [ -n "$GW" ]; then ip route add default via "$GW" dev "$ND_UPLINK" table "$TABLE"; fi
    ;;
stop)
    ip rule del from "$ND_IP" lookup "$TABLE" 2>/dev/null || true
    ip route flush table "$TABLE" 2>/dev/null || true
    ip route del "$ND_IP/32" dev "$ND_TAP" 2>/dev/null || true
    sysctl -q -w "net.ipv4.conf.$ND_UPLINK.proxy_arp=0" 2>/dev/null || true
    ip tuntap del dev "$ND_TAP" mode tap 2>/dev/null || true
    ;;
*) echo "usage: $0 start|stop" >&2; exit 2;;
esac
```

**`/etc/default/nd100x`**:

```sh
# /etc/default/nd100x - read by nd100x-net.sh
ND_IP=192.168.1.40        # the ND's address (must equal AIP-CONFIG in the disc image)
ND_UPLINK=eth0           # wlan0 or eth0
ND_TAP=nd0                # must equal net = tap:... in the INI
ND_USER=ronny             # owner of the TAP = the user nd100x runs as
ND_MODE=proxyarp         # proxyarp (Wi-Fi or eth0, default) | bridge (eth0 only)
```

**`/etc/systemd/system/nd100x-net.service`**:

```ini
[Unit]
Description=Network plumbing for nd100x (TAP + proxy ARP onto the LAN)
After=network-online.target dhcpcd.service wpa_supplicant.service
Wants=network-online.target

[Service]
Type=oneshot
RemainAfterExit=yes
ExecStart=/usr/local/sbin/nd100x-net.sh start
ExecStop=/usr/local/sbin/nd100x-net.sh stop
TimeoutStartSec=120

[Install]
WantedBy=multi-user.target
```

**`/etc/systemd/system/nd100x.service`**:

```ini
[Unit]
Description=ND-100 emulator with Ethernet II (SINTRAN M, TCP/IP)
Requires=nd100x-net.service
After=nd100x-net.service

[Service]
User=ronny
WorkingDirectory=/home/ronny/repos/nd100x
ExecStart=/home/ronny/repos/nd100x/build/bin/nd100x --config nd100-eth.ini
Restart=on-failure
RestartSec=5
StandardInput=null

[Install]
WantedBy=multi-user.target
```

**The INI** (`nd100-eth.ini`, the part that matters; the rest is the
existing file):

```ini
[controller.eth.0]
enabled = yes
net = tap:nd0          ; the service creates nd0, see ND_TAP

[runtime]
verbose = on
telnet = 9000          ; optional: the emulator's own terminal server
```

Then:

```sh
sudo chmod 755 /usr/local/sbin/nd100x-net.sh
sudo systemctl daemon-reload
sudo systemctl enable --now nd100x.service     # pulls in nd100x-net.service
```

**Pi (dhcpcd):** dhcpcd would try to run DHCP on the new `nd0`. Add

```
denyinterfaces nd0
```

to `/etc/dhcpcd.conf`. **Ubuntu (netplan/NetworkManager):** mark `nd0`
unmanaged, e.g. `/etc/NetworkManager/conf.d/nd0.conf` with
`[keyfile]` / `unmanaged-devices=interface-name:nd0`.

The service runs headless (`StandardInput=null`). Console output goes to the
journal: `journalctl -u nd100x -f`.

## 4. Settings: `/etc/default/nd100x`

| Variable | Meaning |
|---|---|
| `ND_IP` | the ND's address; must equal AIP-CONFIG in the image |
| `ND_UPLINK` | `wlan0` (preferred) or `eth0` |
| `ND_TAP` | TAP name; must equal `net = tap:...` in the INI |
| `ND_USER` | owner of the TAP = the user nd100x runs as |
| `ND_MODE` | `proxyarp` (default) or `bridge` |

After editing: `sudo systemctl restart nd100x-net nd100x`. **Always restart both**: restarting only `nd100x-net` recreates `nd0` and the running emulator keeps the dead TAP (the ND stops answering until nd100x is restarted).

### Waiting for Wi-Fi at boot

`nd100x-net.service` is ordered `After=dhcpcd.service wpa_supplicant.service`
(Pi; ordering only, harmless if a unit is absent), and the script itself
waits up to 90 s until the chosen `ND_UPLINK` has a default route, which for
Wi-Fi means associated and leased. That wait is the real dependency; unit
ordering alone cannot know the lease exists. On **Ubuntu** with
NetworkManager use `After=NetworkManager-wait-online.service` and enable it
(`systemctl enable NetworkManager-wait-online`); with netplan/networkd,
`systemd-networkd-wait-online.service`.

## 5. Bridge mode (eth0 only)

`ND_MODE=bridge` makes the service build `br0` at boot with eth0 and `nd0`
in it, so the ND is a normal station on the wire. Extra step on the **Pi**:
`/etc/dhcpcd.conf` needs `denyinterfaces eth0 nd0` so dhcpcd runs on `br0`
(the script gives `br0` eth0's MAC, so the router hands out the same lease).
A mistake here takes the host off the network, so keep proxy ARP unless you
need a bridge. **Bridge mode is untested.**

## 6. Which uplink? (measured: Wi-Fi did not work here, eth0 did)

On the test network the Windows PC is on the wired side. Its ARP broadcast
for the ND (`who-has 192.168.1.40`) arrived at the Pi on **eth0 only**,
never on wlan0, and the PC could ping the Pi's wlan0 address (.44) only
because unicast is routed. With `ND_UPLINK=wlan0` nobody answered the ARP
and the PC got no reply; with `ND_UPLINK=eth0` it worked at once. Reason: the
router/AP does not forward wired broadcasts to this Wi-Fi client. Wi-Fi PCs
on the same AP should work with `ND_UPLINK=wlan0`; wired PCs need `eth0`.
Check which side a PC is on with `sudo tcpdump -n -i any arp` while it
pings the ND. Setting `proxy_arp` on both interfaces would answer on both,
but the ND's replies leave through one uplink only (table 140).

## 6a. Checking the service: `journalctl -u nd100x -u nd100x-net -b`

```sh
journalctl -u nd100x -u nd100x-net -b      # both units, this boot
journalctl -u nd100x -u nd100x-net -b -f   # follow live
systemctl status nd100x nd100x-net         # current state only
```

`-u` selects a unit (twice = both, interleaved by time), `-b` means since
the last boot. The emulator's console output (SINTRAN messages) is in this
journal because the service has no terminal.

**Healthy boot** (real output, 12-OCT-2026, abridged; timestamps and PID vary):

```
15:31:29 systemd[1]: Starting Network plumbing for nd100x (TAP + proxy ARP onto the LAN)...
15:31:33 systemd[1]: Finished Network plumbing for nd100x (TAP + proxy ARP onto the LAN).
15:31:33 systemd[1]: Started ND-100 emulator with Ethernet II (SINTRAN M, TCP/IP).
15:31:33 nd100x[749]: [INFO] device: Ethernet II 0 Device object created. Address[140360-140363] ...
15:31:33 nd100x[749]: [INFO] net: Ethernet: attached to TAP interface nd0
15:31:33 nd100x[749]: [INFO] net: Ethernet II 0: network attached: tap:nd0 (active=1)
15:31:33 nd100x[749]: CPU: ND-100/CX   Memory: 4.000 Mbytes (2097152 words)
15:32:03 nd100x[749]:  SINTRAN III - VSX/500 M
15:32:05 nd100x[749]:  CPU NUMBER:    210
15:32:12 nd100x[749]: *** START COSMOS TCP/IP GATEWAY ***
15:32:15 nd100x[749]: Starting Ethernet II number 0
15:32:15 nd100x[749]: TELNET and TCP/IP in Ethernet II with Internet address 192.168.1.40 started
15:32:52 nd100x[749]: - FTP-SERVER ND-211185C07 February 7, 1990 -
```

What each part tells you:

| Line | Meaning |
|---|---|
| `Finished Network plumbing` a few seconds after `Starting` | the network unit waited for the uplink's lease and set up `nd0`, routes and proxy ARP. 4 s is normal; up to 90 s if Wi-Fi is slow |
| `attached to TAP interface nd0`, `(active=1)` | nd100x opened the TAP. Missing or `cannot attach` = the TAP does not exist or belongs to another user (`ND_USER`) |
| `SINTRAN III - VSX/500 M`, about 30-35 s after start | the OS booted from the disc image |
| `Internet address 192.168.1.40 started` | SINTRAN read AIP-CONFIG; this must show the address you set (section 2). It prints twice in some boots (the gateway restarts once) |
| `FTP-SERVER ND-211185C07` | the ND's FTP server is up; telnet (port 23) is up since the line above |

Lines that look bad but are **normal** here: `ERROR ... XROUT ... Product
unknown` and `WARNING ... ER-S3WD-LOG:DATA ... Not read, write and common
access` (SINTRAN watchdog and log-file messages on this image; no effect on
networking).

**Failure signatures:**

```
nd100x-net.service: Failed with result 'exit-code'.
Dependency failed for ND-100 emulator ...
nd100x.service: Job nd100x.service/start failed with result 'dependency'.
```
The network unit failed, so nd100x never started (the first reboot test,
section 7: uplink had no default route yet). Run
`sudo /usr/local/sbin/nd100x-net.sh start; echo $?` to see the error, and
check `ND_UPLINK` and that the interface got an address (`ip -br addr`).

If the journal is healthy but nothing answers on the LAN: check the uplink
(section 6), that the address in the `Internet address ... started` line is
the one you ping, and that both units were restarted together (section 4).

## 7. Verification, and what was and was not tested

Tested 12-OCT-2026 on the Pi (proxy ARP, uplink wlan0 for the first items, then eth0, kernel 6.1.21-v8+),
`nd100x 1.0.22 (git b528456)`:

- `nd100x-net.service` and `nd100x.service` both `active`, emulator runs with
  no tty, no ssh session needed;
- journal shows `TELNET and TCP/IP in Ethernet II with Internet address
  192.168.1.40 started`;
- policy rule `from 192.168.1.40 lookup 140`, table 140 = LAN + default via
  wlan0, `proxy_arp` 1 on wlan0 and 0 on eth0;
- from the Windows PC (wired), `ND_UPLINK=eth0`: ping answered (tcpdump shows
  request in on eth0, out on nd0, reply back out eth0). On `wlan0` it did not
  (section 6);
- from the Windows PC (reported by the user): `telnet 192.168.1.40` and FTP to
  the ND both work;
- from the Pi: `ping 192.168.1.40` 0 % loss (~5 ms), and port 23 gives
  `Telnet Server D02 on c3 (192.168.001.040) available.` and `ENTER`.

**First reboot test failed** (12-OCT-2026 15:11): `nd100x-net.service` ran
before eth0 had its DHCP lease, found no default route, and its last command
(`[ -n "$GW" ] && ...`) returned 1, so the unit failed and nd100x was never
started. Fixed: the script now waits up to 90 s for the uplink's default
route, ends with plain `if` statements, and the unit is ordered
`After=dhcpcd.service` with `TimeoutStartSec=120`. Second reboot
test (12-OCT-2026 15:31, nobody logged in): `nd100x-net.service` waited 4 s
for the lease and finished, `nd100x.service` started right after, SINTRAN
booted in ~35 s and printed `TELNET and TCP/IP in Ethernet II with Internet
address 192.168.1.40 started`, FTP server up (journal).

**Not tested** (do these before relying on it):

1. a PC that is on Wi-Fi (`ND_UPLINK=wlan0`);
4. `ND_MODE=bridge`.

## 7a. Testing outbound traffic from the ND

Log in on the ND (`telnet 192.168.1.40`, user `SYSTEM`, empty password) and
run the TCP/IP monitor (the `@` is the printed prompt; type only the command):

```
(TCP-IP)TCPIP-MONITOR
Slib: List-Version        <- tcpdev=0, MAC, AIP address, gateway, netmask
Slib: Ping                <- asks "Name of remote machine"
192.168.1.1               <- answers "Ping: ... OK"
```

Measured 12-OCT-2026 (uplink eth0): `Ping 192.168.1.1` and `Ping 8.8.8.8`
both answered `OK`; tcpdump on the Pi showed request `nd0` in, `eth0` out,
reply `eth0` in, `nd0` out. `telnet 192.168.1.45 22` from SINTRAN connected
and printed the `SSH-2.0-OpenSSH_8.4p1` banner; so the ND reaches the LAN and
the Internet. Other monitor commands: `Netstat`, `Arp`, `List-Hosts`.
`List-Domino-Configuration` fails with `Error code from subsystem NK is
-32235` (not needed for Ethernet II). On the Pi:
`sudo tcpdump -n -i any host 192.168.1.40 and not port 23`.

**Seen once, cause unknown:** `telnet` and `ftp-clien` typed at the SINTRAN
`@` prompt printed `SLinit : 047405B: Error when establishing contact with
packet level` (047405B octal = 20229 = `SLEconstart` in
`(TCP-IP)SLIB:DEFS`). Minutes later the same commands worked (confirmed by
the user, and from a SYSTEM telnet session). Not reproduced, so no cause is
documented. If it returns, run `List-Version` in the monitor first: it shows
whether the stack is up.

## 8. Undo

```sh
sudo systemctl disable --now nd100x.service nd100x-net.service
```

`stop` removes the TAP, the route, the policy rule and the uplink's
`proxy_arp`. `ip_forward` is left on.

## 9. Gotchas

- Do not use `pkill -f nd100x` from a shell whose command line contains
  "nd100x"; use `systemctl stop nd100x`.
- Do not run a second nd100x on the same image or the same TAP; the service
  and a hand-started copy fight over `nd0` and the disc image.
- `ndtool` writes only with the service stopped.

## 10. FAQ: how to test it

### Is the service up?
```sh
systemctl status nd100x nd100x-net          # both "active"
journalctl -u nd100x -u nd100x-net -b       # section 6a
```
Look for `Internet address 192.168.1.40 started`.

### Can I reach the ND from a PC? (inbound)
From any PC on the LAN (Windows, WSL, Linux):
```
ping 192.168.1.40
telnet 192.168.1.40          # "Telnet Server D02 on c3 ... ENTER"
ftp 192.168.1.40             # the ND's FTP server
```
Log in with user `SYSTEM`, empty password (just press Enter). Verified from a
wired Windows PC with `ND_UPLINK=eth0`.

If ping fails: on the Pi run `sudo tcpdump -n -i any arp or icmp` while the PC
pings. The ARP request `who-has 192.168.1.40` must arrive on the **uplink
interface** (section 6). Not arriving on it = wrong `ND_UPLINK` for that PC.
An ARP that arrives but gets no `is-at` reply = `proxy_arp` is off
(`sysctl net.ipv4.conf.eth0.proxy_arp`). A ping that reaches `nd0` but is
never answered = the emulator holds a stale TAP: `sudo systemctl restart
nd100x`.

### Can the ND reach the network? (outbound)
Log in on the ND (as above) and use the TCP/IP monitor:
```
(TCP-IP)TCPIP-MONITOR
Slib: Ping
192.168.1.1                  # router; then 8.8.8.8 for the Internet
Slib: List-Version           # shows tcpdev, MAC, IP, gateway, netmask
```
Or the telnet client: `telnet 192.168.1.45 22` should print an `SSH-2.0`
banner. Watch it on the Pi:
`sudo tcpdump -n -i any host 192.168.1.40 and not port 23`
(you should see each packet twice: `nd0` and the uplink).

### "SLinit : 047405B: Error when establishing contact with packet level"
Error 20229 (`SLEconstart`): the socket library could not reach the packet
level (the TCP/IP stack). It was seen once from `telnet` and `ftp-clien`
and went away; cause unknown, not reproduced. Things to check when it
happens: the TCP/IP monitor (`(TCP-IP)TCPIP-MONITOR`, then `List-Version`)
connects and shows the address; the journal has `Starting Ethernet II number
0` and `Internet address ... started` (the stack needs about 15 s after the
SINTRAN banner, and restarts once); `nd100x` was not restarted separately
from `nd100x-net` (section 4).

### Nothing is logged for my commands
Program output goes to your own telnet session, not to the journal. The
journal only shows the emulator's console (boot messages, errors).

### Ports 9000 gives no prompt
`telnet 192.168.1.45 9000` (the emulator's terminal server, `telnet = 9000`
in the INI) connects to terminals 48-50 but showed no SINTRAN prompt in the
test; use `telnet 192.168.1.40` (the ND's own server, port 23) instead.

### I changed something and the ND stopped answering
Restart **both** units: `sudo systemctl restart nd100x-net nd100x`
(section 4).

### Quick checklist on the Pi
```sh
systemctl is-active nd100x nd100x-net
ip rule | grep 140; ip route show table 140
sysctl net.ipv4.ip_forward net.ipv4.conf.eth0.proxy_arp net.ipv4.conf.wlan0.proxy_arp
ip -br link show nd0
ping -c3 192.168.1.40
```
