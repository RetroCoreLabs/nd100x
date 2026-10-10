# Ethernet II controller

nd100x emulates the Norsk Data Ethernet II controller (PCB 3094, ND-110063):
an ND-100 bus card with its own MC68000, an MFP 68901 and an Am7990 LANCE.
The 68000 runs the card's own firmware, loaded by SINTRAN; the emulator
connects the LANCE's wire side to a host network.

This document covers:

1. [The card on the ND-100 bus](#1-the-card-on-the-nd-100-bus)
2. [Configuring the card (command line and INI)](#2-configuring-the-card)
3. [Host network backends](#3-host-network-backends)
4. [Linux: reaching the ND directly with TAP](#4-linux-reaching-the-nd-directly-with-tap)
5. [SINTRAN with COSMOS TCP/IP: what to edit in the disc image](#5-sintran-with-cosmos-tcpip-what-to-edit-in-the-disc-image)
6. [Checking that it works](#6-checking-that-it-works)
7. [SINTRAN without TCP/IP: COSMOS over Ethernet](#7-sintran-without-tcpip-cosmos-over-ethernet)
8. [Limits of this version](#8-limits-of-this-version)
9. [Windows: Npcap and the loopback adapter (not yet in nd100x)](#9-windows-npcap-and-the-loopback-adapter-not-yet-in-nd100x)
10. [Browser (WebAssembly): network through the gateway](#10-browser-webassembly-network-through-the-gateway)
11. [Several native nd100x on one subnet without the gateway](#11-several-native-nd100x-on-one-subnet-without-the-gateway)

Every fact below that was measured says so and gives the date. Port details
and the evidence for each part of the card are in
`docs/ETHERNET-II-PORT-PLAN.md` and `docs/ethernet-port/PHASE-LOG.md`.

---

## 1. The card on the ND-100 bus

The thumbwheel on the card (12J) selects the IOX block and IDENT code. Source:
`eth_create_device_strap` in `src/devices/ethernet/device_ethernet.c`.

| Thumbwheel | IOX (octal)     | IDENT (octal) | Level |
|-----------:|-----------------|--------------:|------:|
| 0          | 140360 - 140363 | 140034        | 12    |
| 1          | 140364 - 140367 | 140035        | 12    |
| 2          | 140370 - 140373 | 140036        | 12    |
| 3          | 140374 - 140377 | 140037        | 12    |

The card has 512 KB of DRAM that the ND-100 also sees, through a window in
ND-100 physical memory. The window's bank is set by a strap (7J/9J):

- default bank = 16 + 4 x thumbwheel, so thumbwheel 0 is bank 16 =
  ND-100 byte address 0x200000;
- the window REPLACES local memory at that address: on a 4 MB machine TPE
  CONFIGURATION shows banks 020-023 as "Ether" and local memory as
  3.512 MB (measured 10-OCT-2026).

---

## 2. Configuring the card

Every option exists as a command-line flag and as an INI key; the command
line wins.

| Command line        | INI key in `[controller.eth.N]` | Meaning |
|---------------------|---------------------------------|---------|
| `--eth0=SPEC`       | `net = SPEC`                    | Adds the card (thumbwheel 0) and names its host network, see section 3 |
| `--eth0-bank=N`     | `bank = N`                      | DRAM window bank, 0-252, multiple of 4; omitted = 16 + 4 x thumbwheel |
| `--eth0-trace=FILE` | `trace = FILE`                  | Differential trace of the card (for comparing with RetroCore) |

Naming the section adds the card; `enabled = no` leaves it out.

INI example (`nd100-eth.ini` in the repository root is a complete machine):

```ini
[controller.eth.0]
enabled = yes
net = tap:nd0
# bank = 16
# trace = eth-trace.txt
```

`nd100x --config nd100-eth.ini --show-config` prints the card as it was
understood, for example:

```
eth     wheel 0  IOX 0140360-0140363  enabled
    net tap:nd0, bank 16 (default)
```

---

## 3. Host network backends

| SPEC | What it does | Reaches |
|------|--------------|---------|
| `none` | Card runs, sent frames are dropped, nothing is received | nothing |
| `tap:IFNAME` | Linux TAP interface (section 4) | the Linux host's own IP stack: ping, telnet, ftp from the host |
| `udp` | UDP multicast segment, group 239.3.9.4, port 3094 | other nd100x and RetroCore cards on the same group |
| `udp:PORT`, `udp:GROUP`, `udp:GROUP:PORT` | same, other group or port | same |
| `listen`, `listen:PORT` | TCP link, waits for one peer (default port 3094; 0 = the OS picks) | one peer emulator |
| `tcp:HOST`, `tcp:HOST:PORT`, `HOST:PORT` | TCP link, connects to a peer, redials if it drops | one peer emulator |

The `udp` and TCP forms use RetroCore's wire formats, so an nd100x card and a
RetroCore card can be on the same segment:

- UDP: one Ethernet frame (no FCS) per datagram; multicast loopback is on,
  so several emulators on one host see each other;
- TCP: each side first sends the five bytes `R E T H 01`, then every frame as
  a 2-byte big-endian length followed by the frame.

What the emulator does to frames between the host and the card (same as
RetroCore):

- a received frame whose source MAC is the card's own is dropped (the echo of
  its own transmission);
- IPv4 / TCP / UDP checksums that do not verify are recomputed (a host with
  checksum offload sends them unfinished); frames that verify are not touched;
- frames shorter than 60 bytes are padded to 60, as a real segment would
  deliver them (SINTRAN ignores shorter ARP replies - measured by RetroCore).

Not available yet: `pcap:` (real host adapter through libpcap / Npcap),
TAP on Windows, and the browser build's network.

---

## 4. Linux: reaching the ND directly with TAP

A TAP interface is a virtual Ethernet adapter on the Linux host. The card
sends its frames into it and the host answers as if the ND were on a cable
plugged into it.

### Create the interface once (needs root)

```sh
sudo ip tuntap add dev nd0 mode tap user $USER
sudo ip addr add 192.168.210.1/24 dev nd0
sudo ip link set nd0 up
```

`user $USER` makes the interface yours, so nd100x itself runs without root.
The interface stays until reboot (or `sudo ip tuntap del dev nd0 mode tap`).
Until nd100x opens it, `ip link` shows it as `NO-CARRIER ... DOWN`; that is
normal.

### Choose a subnet that is not used anywhere else

The host side (`192.168.210.1` above) and the ND (`192.168.210.40` in
section 5) must be on a subnet no other interface of the host, or of Windows
when running under WSL, already uses. Check with `ip -4 addr` (Linux) and
`ipconfig` (Windows). 192.168.210.0/24 was free on the machine this was
written on; 192.168.199.0/24 there belongs to the Windows "ND-Loopback"
adapter RetroCore uses.

### Under WSL2: reaching the ND from Windows

With WSL's default NAT networking, `nd0` lives inside WSL. Linux programs
in WSL reach the ND directly. Windows has no route to 192.168.210.0/24
until two things are set. Measured 10-OCT-2026: after both steps, Windows
telnets to the ND in the browser (gateway with TAP `nd0`).

1. WSL forwards packets between Windows and `nd0`:
   ```sh
   sudo sysctl -w net.ipv4.ip_forward=1
   ```
2. Windows sends 192.168.210.x to WSL. Find WSL's address with
   `ip -4 -br addr show eth0` in WSL (for example `172.24.50.23`), then in
   an administrator `cmd`:
   ```
   route add 192.168.210.0 mask 255.255.255.0 172.24.50.23
   ```

Replies need nothing extra: the ND's IP gateway is 192.168.210.1 (the
AIP-CONFIG line in section 5), which is WSL, and WSL's default route goes
back to Windows.

### Keeping it working after a restart

None of the steps above survive a WSL restart or a Windows reboot (`wsl
--shutdown`, Windows update, ...):

| Lost on | What | Restore |
|---------|------|---------|
| WSL restart | `nd0` and its address | the three `ip` commands in "Create the interface once" |
| WSL restart | forwarding | `sudo sysctl -w net.ipv4.ip_forward=1`, or once for good: `echo net.ipv4.ip_forward=1 \| sudo tee /etc/sysctl.d/99-nd100x.conf` |
| WSL restart | WSL's address (NAT gives a new one) | the Windows route, with the new address |
| Windows reboot | the Windows route | the Windows route |

The Windows route cannot be made permanent with `route -p`, because the
WSL address it points at changes. Instead re-add it with the current
address, from an administrator PowerShell, after WSL has started (these
three lines have not been run yet):

```powershell
$wsl = (wsl -e sh -c "ip -4 -br addr show eth0").Split(' ',[StringSplitOptions]::RemoveEmptyEntries)[2].Split('/')[0]
route delete 192.168.210.0 2>$null
route add 192.168.210.0 mask 255.255.255.0 $wsl
```

Run order after a restart: WSL commands first (`nd0`, forwarding), then the
PowerShell lines on Windows, then the gateway or nd100x.

---

## 5. SINTRAN with COSMOS TCP/IP: what to edit in the disc image

The COSMOS TCP/IP gateway (ND-211185) takes the ND's own address from two
text files on user SYSTEM. Both must agree; the comment at the top of
AIP-CONFIG says so itself ("The Internet address must be updated in the
AIP-HOSTS:SYMB too"). The controller must be restarted - in practice: boot
again - before a change takes effect.

### (SYSTEM)AIP-CONFIG:SYMB

One line per TCP/IP controller:

```
# TCP number    E-II number    IP address    IP gateway    Subnet bits
0              0              192.168.210.40 192.168.210.001  0
```

- TCP number: the TCP/IP controller number, 0 for the first;
- E-II number: the Ethernet II card number, 0 for the first;
- IP address: the ND's address;
- IP gateway: where packets for other networks go; on a TAP setup, the host
  side of the interface;
- Subnet bits: 0 on the image used here.

### (SYSTEM)AIP-HOSTS:SYMB

Name table, one host per line: address, then names:

```
192.168.210.40     c3          C3
192.168.210.1      host        HOST
```

The line for the ND's own address must match AIP-CONFIG.

### Other AIP files

`AIP-NETWORKS`, `AIP-PROTOCOL` and `AIP-SERVICES` are the usual name tables
and hold no address of this machine. `AIP-RESOLVER` names the DNS server
(`NAMESERVER`); change it only if the ND should resolve names through DNS.

To find every file holding an address before changing anything, extract all
text files and search them:

```sh
mkdir -p ndtext
ndtool -x -p -d -F '*:SYMB' -o ndtext DISC.IMG
ndtool -x -p -d -F '*:MODE' -o ndtext DISC.IMG
grep -rl '192\.168' ndtext
```

### Editing with ndtool (emulator stopped)

nd100x must NOT be running on the image while ndtool writes to it.

```sh
cp DISC.IMG DISC.IMG.orig                                  # keep the original
ndtool -x -p -F 'SYSTEM/AIP-*' -o work DISC.IMG             # extract (-p: strip parity)
#   edit work/AIP-CONFIG.SYMB and work/AIP-HOSTS.SYMB; keep the CR LF line ends
ndtool -n -p -f --put work/AIP-CONFIG.SYMB 'SYSTEM/AIP-CONFIG:SYMB' DISC.IMG   # dry run
ndtool    -p -f --put work/AIP-CONFIG.SYMB 'SYSTEM/AIP-CONFIG:SYMB' DISC.IMG
ndtool    -p -f --put work/AIP-HOSTS.SYMB  'SYSTEM/AIP-HOSTS:SYMB'  DISC.IMG
ndtool --fsck DISC.IMG                                       # compare with the original's fsck
```

Done that way on WD0-SINTRAN-M.IMG on 10-OCT-2026: the two files read back
identical to the edited copies, kept their access rights (public read), and
`--fsck` gave 0 errors and the same 6 warnings the original already had.
The edit can also be made inside SINTRAN with its own editor.

---

## 6. Checking that it works

Measured 10-OCT-2026 with `nd100x --config nd100-eth.ini` (SINTRAN III
VSX/500 M on WD0-SINTRAN-M.IMG, `net = tap:nd0`, host 192.168.210.1/24):

1. The SINTRAN console prints during start-up:
   ```
   COSMOS TCP/IP Gateway for Ethernet II ND-211185D02 January 20, 1992
   Starting Ethernet II number 0
   TELNET and TCP/IP in Ethernet II with Internet address 192.168.210.40 started
   ```
   The address shown is the one SINTRAN read from AIP-CONFIG.
2. `ip link show nd0` turns to `UP,LOWER_UP` once nd100x has opened it.
3. `ping 192.168.210.40` answers (4 of 4, 0.8-1.9 ms); `ip neigh` shows the
   card's MAC address, here `08:00:26:d2:00:00`.
4. `telnet 192.168.210.40` gives
   `Telnet Server D02 on c3 (192.168.210.040) available.` and the SINTRAN
   `ENTER` / `PASSWORD:` prompts.

If the console says "Internet address" with the old address, AIP-CONFIG was
not changed (or the image is not the one you edited: check the `disk0` line
of the INI). If nd100x prints `Ethernet tap:nd0: cannot attach`, the
interface does not exist or does not belong to you (section 4).

---

### The F12 status page and packet view

F12, then `7` (Ethernet Status), shows each card live, refreshed every
second: IOX, IDENT, level, DRAM window, the ND-100 side (interrupt enable,
INT12 pending, halt, reset, window reads/writes), the 68000 (running / STOP /
held / HALTED, PC, SR), the LANCE (MAC, CSR0 with bit names, receive queue),
frame counters and the host network's counters.

`P` on that page opens the packet view: the card's last 64 frames, both
directions, newest at the bottom, one decoded line each. Keys: `SPACE`
pause/resume (the card keeps recording), `Up`/`Down` or `k`/`j` select while
paused, `ENTER` hex dump of the selected frame, `C` clear, `ESC` back. On
Windows the arrow keys are not passed to the menu yet; use `k`/`j`.

Measured 10-OCT-2026 during a telnet session from Linux:

```
=== Ethernet II card 0 packets  (tap:nd0)  TX 17  RX 20  live ===
    #  time         dir  len  src > dst  what
    12 02:34:45.499 TX   60  nd > 4a:32:91:f9:e4:69  TCP 192.168.210.40:23 > 192.168.210.1:47220 [S.] len 0
    13 02:34:45.499 RX   60  4a:32:91:f9:e4:69 > nd  TCP 192.168.210.1:47220 > 192.168.210.40:23 [.] len 0
    14 02:34:45.527 TX  120  nd > 4a:32:91:f9:e4:69  TCP 192.168.210.40:23 > 192.168.210.1:47220 [P.] len 66
```

How a frame is decoded depends on the field after the two MAC addresses,
and that depends on the firmware the card runs:

- **TCP/IP firmware** (COSMOS TCP/IP gateway, ND-211185) sends **Ethernet II
  (DIX)** frames: the field is an EtherType, 0x0600 or higher. The view
  decodes 0x0806 as ARP and 0x0800 as IPv4 (ICMP, TCP, UDP); other types are
  shown as `type XXXX`.
- **COSMOS firmware** sends **IEEE 802.3** frames: the field is a length,
  1500 or less, and an 802.2 LLC header follows. The view shows
  `802.3 len N LLC dsap XX ssap XX ctl XX`; the COSMOS payload itself is not
  decoded yet.

## 7. SINTRAN without TCP/IP: COSMOS over Ethernet

To be written. This section will describe running a SINTRAN that has no
TCP/IP gateway and uses COSMOS (XMSG) over the Ethernet II card instead,
including how two machines - two nd100x, or nd100x and RetroCore - are put on
the same segment with the `udp` or TCP backends.

---

## 8. Limits of this version

- **One card.** The hardware allows four (thumbwheels 0-3) and the INI file
  accepts `[controller.eth.0]` to `[controller.eth.3]`, but nd100x refuses a
  second card at start-up:
  `only one Ethernet II card is supported in this version`. Reason: the 68000
  emulator (Musashi) keeps one CPU in global state, so two cards would run
  on the same CPU. Several cards need a saved CPU context per card.
- **Only `--eth0` on the command line.** Thumbwheels 1-3 are INI only, which
  with the one-card limit means: the card is always thumbwheel 0 unless set
  through the INI.
- **TPE ETHERNET-TWO**: tests 12, 24, 25 and 26 fail (in RetroCore too);
  see section 4 of `docs/ETHERNET-II-PORT-PLAN.md`.
- **No pcap, no Windows host network, no browser network** yet.

---

## 9. Windows: Npcap and the loopback adapter (not yet in nd100x)

nd100x has no `pcap:` backend yet, so this section describes the setup
RetroCore uses on Windows and that nd100x will follow. The facts are from
RetroCore's `DOCS/ND_EthernetII_Network_Configuration_2026-09-27.md`, measured
there on 27-SEP-2026; they have not been repeated with nd100x.

1. Install Npcap from <https://npcap.com/> (RetroCore verified 1.10.4).
   Wireshark installs it too.
2. Npcap never hands a frame the card sends to the Windows host's own IP
   stack, so with the card on a real adapter the ND is reachable from every
   OTHER machine on the LAN but not from the PC itself
   (<https://github.com/nmap/npcap/issues/544>).
3. To reach the ND from the PC itself, put the card on the **Microsoft
   KM-TEST Loopback Adapter**, which ships with Windows:
   - add it with `hdwwiz.exe`: Network adapters, Microsoft,
     "Microsoft KM-TEST Loopback Adapter";
   - give it a static address and no gateway, for example
     192.168.199.1 / 255.255.255.0 (RetroCore names the adapter
     "ND-Loopback");
   - put the ND on the same /24 in AIP-CONFIG and AIP-HOSTS (section 5),
     with the adapter's address as its gateway;
   - bridge the card to the adapter (in RetroCore:
     `--net=pcap:<adapter GUID>` or `--net=pcap:KM-TEST`).
   Frames the card sends then come up into Windows' IP stack, and Windows'
   frames are captured and reach the card. The ND is off the LAN unless
   Internet Connection Sharing is added on top.
4. The loopback adapter delivers short frames (42-byte ARP); the 60-byte
   padding in section 3 is what makes SINTRAN accept them.

With that, RetroCore measured ping, telnet (port 23) and FTP (port 21) from
the PC to SINTRAN. Use a different subnet for nd100x's Linux TAP setup than
for this adapter if both are used on one machine (section 4).

---

## 10. Browser (WebAssembly): network through the gateway

The browser build has two built-in machines with the card (Machine Setup):

| Machine | Disc image | Card |
|---------|------------|------|
| TCP/IP  | `WD0-SINTRAN-M.IMG` on the Winchester (SINTRAN M, AIP files as in section 5) | `net = gateway:0` |
| COSMOS  | `BIGDISK0-K-100.IMG` on SMD; also HDLC thumbwheel 2 (SINTRAN device 1362, gateway HDLC channel 1) | `net = gateway:0` |

The images are shipped as `images/*.IMG.bz2` and unpacked next to the page
by `make wasm-glass`.

`net = gateway[:SEGMENT]` (segment 0 when omitted) exists only in the
browser build. The card's frames go over the page's gateway WebSocket
(message 0x31 out, 0x30 in, 0x32 link state, see `docs/GATEWAY-PROTOCOL.md`)
to `tools/nd100-gateway/gateway.js`, which repeats them to every member of
that Ethernet segment's RETH port (`ethernet[]` in `gateway.conf.json`,
segment 0 = port 3094). Anything that speaks RETH can be a member: a native
nd100x with `--eth0=tcp:GATEWAYHOST:3094`, RetroCore, or the host itself
through `tools/reth-tap`. A native nd100x refuses `gateway`; it joins with
`tcp:HOST:3094`.

### Reaching the browser machine from the Linux host

1. Create `nd0` once (section 4), host address 192.168.210.1/24.
2. Start the gateway: `node tools/nd100-gateway/gateway.js`. By default
   it puts every Ethernet segment on TAP `nd0` when that device exists: it
   runs `tools/reth-tap/reth-tap` itself (building it with `make` if
   needed) and stops it on exit. All frames pass, TCP/IP (DIX) and COSMOS
   (IEEE 802.3). `--tap=NAME` picks another TAP device, `--no-tap` keeps the
   segments off the host network; per segment, `"tap": "NAME"` or
   `"tap": false` in `gateway.conf.json`. `nd0` can be held by one program
   only: stop any native nd100x using `tap:nd0` first.
3. In the page: Worker mode, connect to the gateway, select the TCP/IP
   machine, power on.
4. When the console says `... Internet address 192.168.210.40 started`,
   `ping 192.168.210.40` and `telnet 192.168.210.40` work from WSL as in
   section 6, and from Windows with the route in section 4.

Port 3094 is not used by the browser: the page's frames reach the gateway
over the WebSocket. 3094 is where programs OUTSIDE the gateway join the
same segment - reth-tap, a native nd100x, RetroCore.

Measured 10-OCT-2026 with `node test-eth-nd100-browser.js --tap=nd0`
(headless, Worker mode, test gateway on segment port 19094): host ping
answered, port 23 returns `Telnet Server D02 on c3 (192.168.210.040)
available.` and the SINTRAN `ENTER` prompt. `--machine=COSMOS` boots the
COSMOS machine: the console shows `XROUT: Network server ENNS0 started,
sysid 9800` with no input, and IEEE 802.3 frames (length field 14, LLC
A8 A8 03) appear on the segment.

### One MAC address per IP address

Measured 10-OCT-2026: once SINTRAN has answered an ARP request from an IP
address at one MAC address, it ignores ARP requests from the same IP
address at any other MAC address (for at least the minutes the test ran;
how long SINTRAN keeps the entry is unknown). Two hosts on the segment
that both use 192.168.210.1 - say a test tool and the real host through
reth-tap - lock out whichever came second. Give every member of the
segment its own address.

### Native nd100x machines on the browser's segment

A native nd100x joins the gateway's segment instead of opening a TAP
itself:

```
nd100x --config nd100-eth.ini --eth0=tcp:127.0.0.1:3094
```

or `net = tcp:127.0.0.1:3094` under `[controller.eth.0]`. Any number of
machines can join. Through the gateway's TAP they are on `nd0`'s subnet, so
WSL and (with the section 4 route) Windows reach each of them like the
browser machine. Not tested with a native machine yet.

| Machine | Card setting |
|---------|--------------|
| Browser | `net = gateway:0` |
| Native nd100x on the gateway's segment | `net = tcp:127.0.0.1:3094` |
| Native nd100x straight on a TAP | `net = tap:ndN` (section 11) |

Every machine on one subnet needs its own IP address and its own MAC
address:

- IP address: a disc image copy per machine with its own address in
  AIP-CONFIG (section 5), e.g. .41, .42, ...
- MAC address: the card's MAC is set at start-up from the ND machine's CPU
  number, which comes from the SINTRAN on the disc image - nd100x has no
  setting for it. Seen: the COSMOS image prints `CPU NUMBER: 100` and its
  MAC is 08:00:26:64:00:00 (0x64 = 100); the TCP/IP image's MAC is
  08:00:26:d2:00:00 (0xD2 = 210, its CPU number not checked on the
  console). Copies of one image therefore share a MAC; each machine needs
  an image whose SINTRAN has its own CPU number.

---

## 11. Several native nd100x on one subnet without the gateway

One TAP per machine, all attached to a Linux bridge that holds the host
address. After each WSL start (not tested yet):

```sh
sudo ip link add br-nd type bridge
sudo ip addr add 192.168.210.1/24 dev br-nd
sudo ip link set br-nd up
for i in 1 2 3 4 5 6 7 8 9 10; do
  sudo ip tuntap add dev nd$i mode tap user $USER
  sudo ip link set nd$i master br-nd up
done
```

- Machine 1 uses `net = tap:nd1`, machine 2 `net = tap:nd2`, and so on.
- Only the bridge has 192.168.210.1: remove it from `nd0` first
  (`sudo ip addr del 192.168.210.1/24 dev nd0`) and add `nd0` to the bridge
  too (`sudo ip link set nd0 master br-nd`) if the gateway should share
  the subnet.
- The Windows route of section 4 is unchanged.
- IP and MAC rules as above: one IP and one CPU number per machine.
