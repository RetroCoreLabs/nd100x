# ND-100 Gateway

A Node.js bridge between the browser-based WASM emulator and the things a
browser cannot touch: files on the local disk, TCP listeners, and a wire that
other machines can join.

A browser tab cannot open `SMD0.IMG`, cannot accept a telnet connection, and
cannot put an ethernet frame on the host's network. The gateway does those
things on its behalf and carries the results over **one** WebSocket.

```
Browser                                  Gateway (Node.js)
+---------------------------+            +----------------------------+
| Main Worker               |            |                            |
|   WebSocket -- terminals ------------> | TCP :5001  PuTTY / telnet  |
|            -- HDLC frames -----------> | TCP :5010  HDLC client     |
|            -- ETH frames ------------> | TCP :3094  RETH members    |
|            -- control JSON ----------> |                            |
| Disk Sub-Worker           |            |                            |
|   WebSocket -- block R/W ------------> | fs.readSync / writeSync    |
|   SharedArrayBuffer       |            | (SMD0.IMG, FLOPPY.IMG ...) |
+---------------------------+            +----------------------------+
```

**The wire protocol is not described here.** It lives in
`docs/GATEWAY-PROTOCOL.md`, which is the specification; this file is about
running the thing.

## Install and run

```sh
make gateway-install       # npm install (the ws library), once
make gateway               # start it with gateway.conf.json
```

Or directly, from this directory:

```sh
npm install
node gateway.js --static ../../build_wasm_glass/bin --verbose
```

The usual whole-system command builds the Glass UI and serves it from the same
port as the WebSocket, so there is only one server and no cross-origin problem:

```sh
make wasm-glass-gateway    # then open http://localhost:8765/?worker=1
```

`?worker=1` matters -- the gateway is only used in Worker mode.

## CLI options

| Option | Description |
|--------|-------------|
| `--config path` | Config file (default: `gateway.conf.json` beside `gateway.js`) |
| `--static dir` | Serve static files from this directory; overrides `staticDir` |
| `--verbose`, `-v` | Log every frame, not just connections |
| `--help`, `-h` | Usage and exit |

`--static` is what makes `SharedArrayBuffer` work. The gateway sends
`Cross-Origin-Opener-Policy: same-origin` and
`Cross-Origin-Embedder-Policy: credentialless` with the static files, and
without those two headers the browser refuses to create a SharedArrayBuffer --
which is how the disk sub-worker hands blocks to the blocked main Worker. A
plain `python3 -m http.server` does not send them, so disk I/O will not work
behind one.

A path given to `--static` resolves against the current directory; a
`staticDir` in the config resolves against the config file.

## Configuration

`gateway.conf.json`:

| Key | Default | Meaning |
|-----|---------|---------|
| `websocket.port` | `8765` | HTTP + WebSocket listen port |
| `staticDir` | `""` | Serve static files from here (with COOP/COEP headers) |
| `terminals.port` | `5001` | TCP port for remote terminal clients |
| `terminals.welcome` | `"ND-100/CX Terminal Server"` | Banner shown to TCP clients |
| `hdlc[].name` | -- | Label, used in log lines only |
| `hdlc[].channel` | -- | HDLC channel number (0-based) |
| `hdlc[].port` | -- | TCP listen port for that channel |
| `hdlc[].enabled` | `false` | Enable this HDLC server |
| `ethernet[].name` | -- | Label, used in log lines only |
| `ethernet[].segment` | `0` | Segment number, as used in frame types `0x30`-`0x32` |
| `ethernet[].port` | -- | TCP listen port for RETH members (3094 by convention) |
| `ethernet[].enabled` | `false` | Enable this ethernet segment |
| `smd.images` | `[]` | SMD images, array index = unit number |
| `floppy.images` | `[]` | Floppy images, same format |
| `scsi.images` | `[]` | SCSI images, array index = SCSI ID (0-6) |

Each image entry is either a bare path string or
`{ "path": ..., "name": ..., "description": ... }`. Relative paths resolve
against the config file, so the shipped config reaches the repository root with
`../../SMD0.IMG`.

### Ports at a glance

| Port | What connects |
|------|---------------|
| 8765 | the browser: WebSocket, and static files when `--static` is used |
| 5001 | PuTTY, telnet, any TCP terminal client |
| 5010 | an HDLC peer |
| 3094 | ethernet segment members -- see below |

## The four services

**Terminal I/O.** TCP clients land on a menu, pick a released terminal, and are
then bridged to that emulated terminal by ident code. Which terminals may be
taken is decided in the emulator, not here.

**Disk block I/O.** The disk sub-worker opens a *second* WebSocket and asks for
blocks. It is separate because the C code calls `machine_block_read()`
synchronously while the main Worker is stopped inside `Atomics.wait()`; a
worker with its own event loop is the only thing that can still answer. Whole
images are never transferred -- always single blocks.

**HDLC.** One TCP socket per channel. An HDLC line is point-to-point, so a
second client for the same channel replaces the first.

**Ethernet segments.** A segment is **multipoint**: a set of members, with each
frame repeated to every member except the one that sent it. Anything speaking
RETH may join -- a native `nd500x`
(`ND500X_ETH_UPLINK=tcp:127.0.0.1:3094`), a RetroCore machine, another
gateway, or `tools/reth-tap` to put the host's own network stack on the wire.
This is the one place the ethernet code must not copy the HDLC code: built with
one socket per segment it would work perfectly with two machines and silently
drop the third.

## Tests

```sh
make gateway-test          # test-gateway.js + test-ethernet.js, 27 checks
make gateway-test-wasm     # test-eth-wasm.js, 17 checks, needs a wasm build
```

| Script | Checks | Needs | What it guards |
|--------|--------|-------|----------------|
| `test-gateway.js` | 14 | node only | server start, WebSocket connect and reject (2 allowed, 3rd refused), TCP banner, terminal registration and menu selection, byte forwarding both ways, disconnect, reconnection |
| `test-ethernet.js` | 13 | node only | a segment with **three** members, frames repeated to all others, never echoed to the sender, a member leaving not disturbing the rest, a client that does not speak RETH being dropped |
| `test-eth-wasm.js` | 17 | a built wasm module | the `Nd500_Eth_*` exports exist, are reachable through `Module._name`, and refuse politely before a machine exists rather than faulting |

The first two start their own `gateway.js` as a child process and talk to it
over ordinary TCP -- no emulator, no browser, no disk image.
`test-ethernet.js` deliberately uses ports 13094 and 18765 instead of 3094 and
8765 so a real gateway can keep running while it does.

`test-eth-wasm.js` takes an optional path to `nd100wasm.js`; with none it looks
in `build_wasm/bin/`. It does **not** prove frames reach NDIX -- that needs a
booted guest and belongs in an end-to-end run.

A fourth suite, `tests/browser/test-gateway-browser.js` (Puppeteer, 10 checks), drives the
whole stack through a real page; it lives with the Glass UI tests, not here.

## Known limits

**One gateway serves one emulator.** `emulatorWs` is a single connection.
That is enough for the case it was built for -- an ND-100 and an ND-500 share
one WebAssembly module because they share MPM5 memory -- but two browser tabs
need either a gateway each or `emulatorWs` becoming a set.

**Disk images are opened, never created.** A path in the config that does not
exist logs `WARNING: Failed to open ...` at startup and that unit stays
unmounted -- the gateway does not stop.

**Images are opened read/write** (`fs.openSync(resolved, 'r+')`), because a
guest may write to them. A file with no write permission fails to open and its
unit is skipped with the same warning, which reads as "missing" when it is
really "not writable".

## Files

| File | |
|------|--|
| `gateway.js` | the server: HTTP, WebSocket, TCP terminals, HDLC, ethernet, disk I/O |
| `gateway.conf.json` | default configuration |
| `package.json` | one dependency, `ws` |
| `test-gateway.js`, `test-ethernet.js`, `test-eth-wasm.js` | see Tests |

## See also

- `docs/GATEWAY-PROTOCOL.md` -- the wire protocol: frame types, JSON control messages, startup sequence, ident code map
- `tools/reth-tap/README.md` -- putting the host's network stack on an ethernet segment
- `GLASS.md` -- the browser side that connects to this
