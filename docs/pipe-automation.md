# nd100x pipe automation (`--pipe` + Python `expect`)

Drive nd100x programmatically: wait for terminal output, send keystrokes, with
per-step timeouts and fail-fast abort patterns. This is how you script TPE
diagnostics, SINTRAN logins, `CONFIGURE`, regression checks, etc. without a human
at the console.

## The `--pipe` flag

```
nd100x --pipe --boot=smd --smd0=SMD0.IMG
```

`--pipe` changes two things:

- **Keyboard comes from stdin** instead of the interactive console. On Windows the
  normal build reads the console with `ReadConsoleInputW`, which cannot be fed from
  a pipe; `--pipe` switches it to a non-blocking read of the stdin pipe/file. (On
  Linux nd100x already reads a redirected stdin, so `--pipe` is a harmless no-op
  there and the same command works on both.)
- **stdout is unbuffered**, so a driver sees the emulated terminal output as it is
  produced (essential for `expect`).

One byte is consumed per emulation step, so input is paced by the machine, not
dumped all at once.

**Platform support:** desktop (Windows / Linux) only. It is compiled out of the
**WASM** build (the browser front end handles I/O via xterm.js) and the **RISC-V**
target (no host console/pipe). Do not rely on it there.

You can drive it from *any* language over the two pipes (write keystrokes to stdin,
read terminal output from stdout). The Python helper below is just the batteries-
included option.

## Python driver: `tools/nd100x_expect.py`

A small, standard-library-only, `pexpect`-style API.

```python
from nd100x_expect import Nd100x, Regex, ExpectTimeout, ExpectAbort

with Nd100x(cputype="ND120CX", boot="smd", smd0="SMD0.IMG") as vm:
    vm.expect("RUNNING", timeout=90, abort=["malfunction", "HALT"])
    ...
```

### `Nd100x(...)` constructor

| arg | meaning |
|---|---|
| `exe` | path to the binary (default `../build/bin/nd100x[.exe]`) |
| `boot` | `smd` \| `floppy` \| `bpun` \| `aout` \| `bp` |
| `image` | image file (floppy/aout/bpun) |
| `smd0` | SMD unit-0 disk image |
| `cputype` | `ND100CX` … `ND110CX` … `ND120CX` |
| `cpu_number` | SYSNO override (banner CPU NUMBER) |
| `max_instr` | stop after N instructions (safety cap) |
| `extra_args` | list of extra CLI args passed through |
| `echo` | mirror the emulated output to your own stdout (default `True`) |

Any CPU-identity flag works via `extra_args`, e.g.
`extra_args=["--microcode-version=L", "--system-type=100"]`.

### Methods

- **`vm.expect(pattern, timeout=30, abort=None)`** — block until `pattern` appears
  in output produced *after the previous* `expect` (so an old marker is never
  re-matched). `pattern` and `abort` are **literal substrings by default**
  (so `"*** ERROR ***"` just works); wrap in `Regex(r"...")` for regex.
  Both accept a list. Raises `ExpectTimeout` on timeout, `ExpectAbort` if an
  `abort` pattern appears first. Returns the matched text.
- **`vm.send(text)`** — send keystrokes. Use `\r` for ENTER (the ND terminal wants CR).
- **`vm.sendline(text="")`** — `send(text + "\r")`.
- **`vm.mount(unit, path, confirm=True)`** — **hot-swap** a floppy while the machine runs: eject
  floppy `unit` (0-2) and mount `path`. This is the "operator inserts the next install disk" step.
  Use an **absolute path**. `confirm=True` waits for the emulator ack and raises if the file was not
  found / the unit is bad. The guest sees the new disk on its next floppy read.
- **`vm.eject(unit)`** — eject a floppy while running.
- **`vm.before()`** — everything captured so far (for logging / assertions).

### Floppy hot-swap control channel

Under `--pipe`, a line on stdin framed as `0xFF <command> \n` is a **control command**, not a
keystroke (`0xFF` never appears in ND keyboard input). `vm.mount()`/`vm.eject()` use it; the emulator
acks on stderr with a `[pipe-control] ...` line. You can drive it from any language:

```
printf '\377mount 1 /images/DISK2.IMG\n'   # 0xFF = \377 octal, then the command + newline
```

This is what makes multi-disk **automated installs** possible: sit in the installer's "insert next
disk" prompt, `vm.mount(0, next_disk)`, press RETURN, continue. A worked install-orchestration example
(with a disk catalog + SHA-256 verify) is the next layer on top of this.

Sketch:

```python
with Nd100x(boot="floppy", image="INSTALL-1.IMG", cputype="ND120CX") as vm:
    vm.expect("Insert distribution floppy 2", timeout=300, abort="ERROR")
    vm.mount(0, r"D:\dist\INSTALL-2.IMG")     # swap disk 1 -> disk 2 in the drive
    vm.sendline()                             # press RETURN to continue
    vm.expect("Insert distribution floppy 3", timeout=300, abort="ERROR")
    vm.mount(0, r"D:\dist\INSTALL-3.IMG")
    vm.sendline()
    vm.expect("Installation complete", timeout=600, abort="ERROR")
```

Always use it as a context manager (`with`) so the subprocess is terminated on exit.

### Mounting straight from the online catalog

Instead of pointing at local image files, you can mount disks from the **online floppy/disk
catalog** (`https://ndlib.hackercorp.no/floppies.json` — the same catalog the F12 browser uses).
A disk is identified two ways:

- **`md5`** — the image's content hash. **Unique**: always resolves to exactly one image.
- **`directory`** — the SINTRAN *"Directory name"* from the disk's listing. Human-friendly but
  **not unique**: in the live catalog **106 directory names map to more than one image** (e.g.
  `N-10-102-I` has 11). When a directory name is ambiguous the driver logs every match
  (`name`, `directory_name`, `pages`, `md5`) and uses the **first**; pin a specific version by
  passing its `md5`.

**Python side (`vm.mount_catalog` / `vm.catalog_matches`) — works everywhere, incl. Windows.**
The catalog JSON and the image are fetched with `urllib` (Python stdlib), so this does **not**
need libcurl. The image is downloaded to the OS temp dir (content-addressed by md5, so it is
cached) and mounted through the same hot-swap channel:

```python
with Nd100x(boot="smd", smd0="SMD0.IMG", cputype="ND120CX") as vm:
    vm.expect("Insert distribution floppy 2", timeout=300, abort="ERROR")

    # Unique — mount by md5:
    vm.mount_catalog(0, md5="2cb9ffb6b37d367bc9d0426cb56fdfda")

    # Or by directory name; disambiguate first if needed:
    hits = vm.catalog_matches(directory="N-10-102-I")
    if len(hits) > 1:
        for h in hits:
            print(h["md5"], h["pages"], h["name"])
        vm.mount_catalog(0, md5=hits[0]["md5"])   # pin the one you want
    else:
        vm.mount_catalog(0, directory="N-10-102-I")

    vm.sendline()                                 # press RETURN to continue
```

A ready-to-run example is `tools/examples/mount_catalog_demo.py` (boots TPE, mounts a
catalog floppy, and proves the guest reads it via `LOAD PAGI`). Validated end-to-end:
it downloads a real 154-page floppy over HTTPS and the guest's directory view follows
the mounted disk.

> **User-Agent:** the ndlib server rejects the default Python-urllib UA with HTTP 403.
> The driver sends `User-Agent: nd100x/1.0` (the same UA the C libcurl path uses) on the
> catalog JSON and image requests, which the server serves. If you script the catalog
> download yourself, set that header too.

**C side (`dbmount` / `dblist` control commands).** nd100x can also resolve
and download the image itself via its bundled catalog API (`src/ndlib/floppydb.c` +
`machine_floppy_mount_catalog`). The download goes through `src/ndlib/download.c`: WinHTTP on
Windows (every Windows build, no libcurl needed), the system libcurl on Linux / BSD / macOS.
Only WASM and RISC-V builds lack it (the download stub returns nothing). The
control verbs are `dbmount <unit> <md5:hash|dir:name|token>` and `dblist <selector>`:

```python
vm.dblist("dir:N-10-102-I")        # emulator logs all matches to its stderr
vm.dbmount(0, "md5:2cb9ffb6...")   # C-side resolve + download + mount
```

On Windows, prefer `mount_catalog()` (Python fetch) over `dbmount()` (C fetch). Both end at the
same `mount_drive()` inside the emulator; they differ only in **who downloads the image**.

## Example 1 — TPE INSTRUCTION under ND-120 (`tools/examples/tpe_instruction_nd120.py`)

Boots the TPE floppy as an ND-120/CX, runs `INSTRUCTION`, and asserts the identity
header. Fails fast on any TPE `*** ERROR ***`.

```python
with Nd100x(boot="floppy", image=tpe_floppy, cputype="ND120CX") as vm:
    vm.expect("TPE>", timeout=60, abort=["HALT", "malfunction"])
    vm.send("INSTRUCTION\r")
    vm.expect("Version", timeout=20)
    vm.send("run\r")
    vm.expect("ND-120/CX", timeout=60, abort="*** ERROR ***")
    vm.expect("3202", timeout=10)
    vm.expect("100014B", timeout=10)
```

Run it:
```
python tools/examples/tpe_instruction_nd120.py [path-to-TPE-floppy.img]
```
Prints the live TPE identity block and exits 0 on success. Verified output:
```
CPU type.............: ND-120/CX
Print number.........: 3202
Print release version: D
Microprogram version.: 100014B
```

## Example 2 — boot SINTRAN and run a command

```python
from nd100x_expect import Nd100x

with Nd100x(boot="smd", smd0="SMD0.IMG", cputype="ND120CX") as vm:
    vm.expect("RUNNING", timeout=120, abort="malfunction")
    vm.expect("ENTER", timeout=30)          # "ENTER SYSTEM"
    vm.sendline("SYSTEM")                    # user
    vm.expect("PASSWORD", timeout=10)
    vm.sendline("")                          # (blank password on this image)
    vm.expect("@", timeout=30)               # SINTRAN command prompt
    vm.sendline("WHO")
    vm.expect("SYSTEM", timeout=10)
    print("\nSINTRAN reachable, WHO answered.")
```

## Example 3 — CONFIGURE

```python
with Nd100x(boot="floppy", image=tpe_floppy, cputype="ND120CX") as vm:
    vm.expect("TPE>", timeout=60)
    vm.send("CONFIGURE\r")
    vm.expect("Version", timeout=20)
    vm.send("run\r")
    vm.expect(Regex(r"[Mm]emory"), timeout=60, abort="*** ERROR ***")
    print(vm.before())     # dump the full CONFIGURE report
```

## Driving it without Python

Any process that owns the two pipes works. From a shell, a one-shot feed:

```
printf 'INSTRUCTION\rrun\r' | nd100x --pipe --boot=floppy --image=TPE.img --max-instr=80000000
```

(Timing-blind — the Python `expect` loop is what makes it robust. But this is handy
for a quick smoke test.)

## Notes

- Escape sequences (cursor moves) the emulated terminal emits are stripped before
  matching, and `echo=True` mirrors the cleaned stream to your own stdout so you see
  what the driver sees.
- Set `max_instr` as a safety cap so a hung boot can't run forever in CI.
- `expect` only scans output produced since the previous `expect`, so reusing a
  common word (e.g. `"Version"`) across steps is safe.
