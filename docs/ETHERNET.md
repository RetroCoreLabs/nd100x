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
10. [Browser (WebAssembly): network through the gateway (planned)](#10-browser-webassembly-network-through-the-gateway-planned)

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

### Under WSL2

With WSL's default NAT networking the TAP interface lives inside WSL: Linux
programs in WSL reach the ND; Windows programs do not, unless a route to the
WSL address is added on Windows. Not tested.

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
2. Start the gateway: `node tools/nd100-gateway/gateway.js` (segment 0 on
   port 3094 is in the shipped `gateway.conf.json`).
3. Put the host on the segment:
   `tools/reth-tap/reth-tap --dev nd0 --host 127.0.0.1 --port 3094`
   (`make -C tools/reth-tap` builds it).
4. In the page: Worker mode, connect to the gateway, select the TCP/IP
   machine, power on.
5. When the console says `... Internet address 192.168.210.40 started`,
   `ping 192.168.210.40` and `telnet 192.168.210.40` work as in section 6.

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
