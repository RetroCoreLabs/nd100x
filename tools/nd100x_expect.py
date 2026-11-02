#!/usr/bin/env python3
r"""
nd100x_expect.py - drive nd100x over pipes (expect / send automation).

nd100x with --pipe reads its keyboard from stdin and writes the emulated terminal
output to stdout (unbuffered). This module spawns that as a subprocess and gives
you a small pexpect-style API so you can automate TPE / SINTRAN / CONFIGURE runs
from Python: wait for text, send keystrokes, with per-step timeouts and abort
patterns that fail fast.

    from nd100x_expect import Nd100x

    with Nd100x(cputype="ND120CX", boot="smd", smd0="SMD0.IMG",
                image=r"...\Nd-210523I01-XX-01D.img") as vm:   # or boot="floppy"
        vm.expect("TPE>", timeout=30, abort=["HALT", "malfunction"])
        vm.send("INSTRUCTION\r")
        vm.expect("Version", timeout=10)
        vm.send("run\r")
        vm.expect("looping", timeout=90, abort="*** ERROR ***")   # TPE errors -> fail fast

Only the Python standard library is used. Windows and Linux both work (on Linux
--pipe is a no-op because nd100x already reads a redirected stdin, but passing it
is harmless and keeps the command identical across platforms).

A SINTRAN session on COPIES of the images, with a filtered log and a transcript:

    with Nd100x(config="config/ND5000.ini", boot="smd", smd0="ND5000.IMG",
                scsi={1: "tape:blank-tape.img"}, work_dir="/tmp/run1",
                log_file="/tmp/run1/emulator.log", log_keep=r"TAPE|ERROR",
                transcript="/tmp/run1/console.log", echo=False) as vm:
        vm.expect("RUNNING", timeout=300)
        vm.escape()                          # ESC until SINTRAN answers ENTER
        vm.login("SYSTEM")                   # name + password in one write
        out = vm.command("LIST-USERS")       # type it, wait for the @ prompt
        vm.mount(0, "/tmp/run1/disk2.img")   # change floppy while it runs

Never write the 0xFF control byte by hand: control_frame() builds and checks it.
"""

import os
import re
import sys
import time
import shutil
import threading
import subprocess

# Default location of the built binary, relative to this file (tools/ -> ../build/bin/).
# $ND100X_EXE overrides it; CTest sets it to the binary of the build it runs in,
# which is not build/ for every build directory (CI builds into build_test/).
_HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_EXE = os.environ.get("ND100X_EXE") or os.path.join(
    _HERE, "..", "build", "bin", "nd100x.exe" if os.name == "nt" else "nd100x")

# Bytes we drop from the output stream before matching: NUL, and CSI/ESC escape
# sequences the emulated terminal may emit (cursor moves etc.). \r and \n are kept.
_ANSI = re.compile(rb"\x1b\[[0-9;?]*[ -/]*[@-~]|\x1b[@-Z\\-_]|[\x00\x07]")


class ExpectTimeout(Exception):
    """Raised when an expect() pattern is not seen within its timeout."""


class ExpectAbort(Exception):
    """Raised when an expect() sees one of its abort patterns first."""


class Regex:
    """Wrap a pattern to have expect()/abort treat it as a REGEX instead of a literal.

    Plain str/bytes passed to expect()/abort are matched LITERALLY (re.escape'd), so
    "*** ERROR ***" or "CPU type" just work. Use Regex(r"CPU type\.+: ND-120/CX") when
    you actually want regex metacharacters.
    """
    def __init__(self, pat):
        self.pat = pat


def _compile_pattern(p):
    if isinstance(p, Regex):
        pat = p.pat
        return re.compile(pat.encode() if isinstance(pat, str) else pat)
    if isinstance(p, re.Pattern):
        return p if isinstance(p.pattern, bytes) else re.compile(p.pattern.encode())
    b = p.encode() if isinstance(p, str) else p        # plain str/bytes -> LITERAL match
    return re.compile(re.escape(b))


# --- --pipe control channel -----------------------------------------------------
# A stdin line framed as 0xFF <command> 0x0A is a control command for nd100x, not a
# keystroke (pipe_handle_control() in src/frontend/nd100x/nd100x.c). nd100x answers
# on stderr: "[pipe-control] mount <unit> '<path>' -> ok" (or FILE NOT FOUND /
# BAD UNIT), "[pipe-control] eject <unit> -> ok".
CONTROL_START = 0xFF
CONTROL_END = 0x0A
CONTROL_REPLY = re.compile(r"\[pipe-control\] .* -> (.+)$")


def control_frame(command):
    """Return the exact bytes of one control command: 0xFF, the command, 0x0A.

    Refuses a command that would break the frame: a line break would end it early
    and the rest would be TYPED at the console; 0xFF would start a new frame; a
    non-ASCII character has no single-byte form nd100x would read back the same.
    """
    if not command:
        raise ValueError("empty control command")
    if "\n" in command or "\r" in command:
        raise ValueError("control command contains a line break: %r" % command)
    try:
        body = command.encode("ascii")
    except UnicodeEncodeError:
        raise ValueError("control command must be ASCII: %r" % command) from None
    return bytes([CONTROL_START]) + body + bytes([CONTROL_END])


def _floppy_unit(unit):
    # machine_floppy_swap() accepts floppy units 0-2.
    if isinstance(unit, bool) or not isinstance(unit, int) or not 0 <= unit <= 2:
        raise ValueError("floppy unit must be 0, 1 or 2, not %r" % (unit,))
    return unit


# SINTRAN prompts, matched at the END of the new output: "@" at command level,
# "ENTER" after ESC, and questions ending in ':' (PASSWORD:, DEVICE NAME: ...).
SINTRAN_COMMAND_PROMPT = rb"@ ?$"
SINTRAN_ANY_PROMPT = rb"(@|ENTER|:) ?$"
SINTRAN_ENTER = rb"ENTER ?$"


# --- online floppy/disk catalog -------------------------------------------------
# The SAME catalog the F12 browser and the C floppydb use. Fetching over HTTP with
# urllib works on every platform (including Windows w64devkit, where the C build has
# no libcurl), so the Python driver can search the catalog, download an image, and
# mount it via the ordinary hot-swap control channel - fully self-contained.
# The catalog and the images live in the RetroCoreLabs/norskdata-software-archive
# GitHub repository. Every image is a gzip file, images/<md5>/<name>.img.gz; the
# path of each one is in the catalog record (storage.git.imagePath).
CATALOG_ARCHIVE_BASE = "https://raw.githubusercontent.com/RetroCoreLabs/norskdata-software-archive/main/"
CATALOG_JSON_URL = CATALOG_ARCHIVE_BASE + "catalog/floppies.json"
# Same User-Agent the C libcurl download uses. Set it on every request.
CATALOG_USER_AGENT = "nd100x/1.0"


def _catalog_cache_path():
    home = os.environ.get("HOME") or os.environ.get("USERPROFILE") or _HERE
    return os.path.join(home, ".cache", "nd100x", "floppies-archive.json")


class FloppyCatalog:
    """Search the online floppy/disk catalog by md5 (unique) or SINTRAN directory name.

    Mirrors the C floppydb: only records that name an image in the archive
    (storage.git.imagePath) are kept; the directory name is the record's volumeName;
    the size is imageSizeBytes / 2048 pages and > 1000 pages => an SMD image. A
    directory name MAY match several images - use the md5 to pin a specific one.
    """

    def __init__(self, entries):
        self.entries = entries

    @classmethod
    def load(cls, force=False, max_age=24 * 3600, timeout=30):
        """Load the catalog: fresh cache -> use it, else download and cache, else
        fall back to a stale cache. Returns a FloppyCatalog. Raises on total failure."""
        import json
        cache = _catalog_cache_path()
        text = None
        if not force and os.path.isfile(cache) and (time.time() - os.path.getmtime(cache)) < max_age:
            with open(cache, "r", encoding="utf-8", errors="replace") as f:
                text = f.read()
        if text is None:
            try:
                import urllib.request
                _req = urllib.request.Request(CATALOG_JSON_URL,
                                              headers={"User-Agent": CATALOG_USER_AGENT})
                with urllib.request.urlopen(_req, timeout=timeout) as r:
                    text = r.read().decode("utf-8", "replace")
                try:
                    os.makedirs(os.path.dirname(cache), exist_ok=True)
                    with open(cache, "w", encoding="utf-8") as f:
                        f.write(text)
                except OSError:
                    pass                                   # cache write is best-effort
            except Exception:
                if os.path.isfile(cache):                  # offline: use whatever cache we have
                    with open(cache, "r", encoding="utf-8", errors="replace") as f:
                        text = f.read()
                else:
                    raise
        return cls.from_json(text)

    @classmethod
    def from_json(cls, text):
        import json
        raw = json.loads(text)
        entries = []
        for item in raw:
            if not isinstance(item, dict):
                continue
            md5 = (item.get("md5") or "").strip()
            image_path = ((item.get("storage") or {}).get("git") or {}).get("imagePath")
            if not md5 or not image_path:                  # no image in the archive
                continue
            volume = item.get("volumeName") or ""
            set_name = (item.get("backupSet") or {}).get("name") or ""
            stem = os.path.basename(image_path)
            if stem.endswith(".img.gz"):
                stem = stem[:-len(".img.gz")]
            pages = int((item.get("imageSizeBytes") or 0) // 2048)
            entries.append({
                "id": len(entries) + 1,
                "name": volume or set_name or stem,
                "md5": md5,
                "directory_name": volume,
                "pages": pages if pages > 0 else -1,
                "is_smd": pages > 1000,
                "image_path": image_path,
            })
        return cls(entries)

    def find_md5(self, md5):
        md5 = (md5 or "").lower()
        for e in self.entries:
            if e["md5"].lower() == md5:
                return e
        return None

    def find_directory(self, directory_name):
        want = (directory_name or "").lower()
        return [e for e in self.entries if e["directory_name"].lower() == want]

    def image_url(self, entry):
        import urllib.parse
        return CATALOG_ARCHIVE_BASE + urllib.parse.quote(entry["image_path"])


class Nd100x:
    """A running nd100x subprocess you can expect()/send() against."""

    def __init__(self, exe=None, boot="smd", image=None, smd0=None, cputype=None,
                 cpu_number=None, extra_args=None, max_instr=None, echo=True,
                 config=None, scsi=None, work_dir=None, keep_copies=False,
                 log_file=None, log_keep=None, transcript=None):
        """
        exe          binary path, or a list (a command prefix, e.g. [python, script])
        config       machine INI (--config)
        scsi         {scsi_id: "hdd:PATH" | "tape:PATH"} -> --scsi<id>=...
        work_dir     when given, image, smd0 and every scsi PATH are COPIED into this
                     folder first and nd100x only ever opens the copies. The copies
                     are deleted by close() unless keep_copies=True.
        log_file     when given, the emulator log (stderr) is NOT mixed into the
                     screen: it is read on its own and only the lines matching
                     log_keep (a regex; None keeps all) are written to this file.
                     Use it with --scsi-debug: one backup run logged 5.1 GB.
                     pipe-control replies are always kept, mount()/eject() need them.
        transcript   file that receives the screen output and one line per step
        """
        exe = exe or DEFAULT_EXE
        self.exe = exe
        self.work_dir = work_dir
        self.keep_copies = keep_copies
        self.copies = {}                     # name -> path of each image copy made
        if work_dir:
            os.makedirs(work_dir, exist_ok=True)
        args = (list(exe) if isinstance(exe, (list, tuple)) else [exe]) + ["--pipe"]
        if config:     args.append("--config=%s" % config)
        if boot:       args.append("--boot=%s" % boot)
        if image:      args.append("--image=%s" % self._copy("image", image))
        if smd0:       args.append("--smd0=%s" % self._copy("smd0", smd0))
        for sid in sorted(scsi or {}):
            media, sep, path = str(scsi[sid]).partition(":")
            if not sep or media not in ("hdd", "tape", "cdrom", "floppy"):
                raise ValueError("scsi[%r] must be 'hdd:PATH' or 'tape:PATH', not %r"
                                 % (sid, scsi[sid]))
            args.append("--scsi%d=%s:%s" % (int(sid), media, self._copy("scsi%d" % int(sid), path)))
        if cputype:    args.append("--cputype=%s" % cputype)
        if cpu_number is not None: args.append("--cpu-number=%s" % cpu_number)
        if max_instr:  args.append("--max-instr=%d" % max_instr)
        if extra_args: args.extend(extra_args)
        self.args = args
        self.echo = echo                     # mirror emulated output to our stdout as it arrives
        self.log_file = log_file
        self._log_keep = re.compile(log_keep) if log_keep else None
        self._log = []                       # kept stderr lines (log_file mode)
        self._log_seen = 0                   # log lines already used by a control reply
        self._buf = bytearray()              # cleaned output accumulated so far
        self._raw_since_match = 0            # index into _buf where the current expect starts scanning
        self._lock = threading.Lock()
        self._proc = None
        self._reader = None
        self._log_reader = None
        self._closed = False
        self._t0 = None
        self._tx = open(transcript, "w", encoding="latin-1") if transcript else None

    def _copy(self, name, src):
        """Copy an image into work_dir (if set) and return the path nd100x gets."""
        if not self.work_dir:
            return src
        src = os.path.expanduser(src)
        if not os.path.isfile(src):
            raise FileNotFoundError(src)
        dst = os.path.join(self.work_dir, "%s-%s" % (name, os.path.basename(src)))
        if os.path.abspath(dst) == os.path.abspath(src):
            raise ValueError("the copy would overwrite the original: %s" % src)
        shutil.copyfile(src, dst)
        self.copies[name] = dst
        return dst

    @property
    def pid(self):
        """Process id of the running emulator (None before start())."""
        return self._proc.pid if self._proc else None

    def note(self, text):
        """Write a step line into the transcript."""
        if self._tx:
            t = time.time() - self._t0 if self._t0 else 0.0
            self._tx.write("\n>>> [%7.1f s] %s\n" % (t, text))
            self._tx.flush()

    # -- lifecycle ---------------------------------------------------------
    def start(self):
        separate = self.log_file is not None
        self._proc = subprocess.Popen(
            self.args, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE if separate else subprocess.STDOUT, bufsize=0)
        self._t0 = time.time()
        self.note("start pid %d: %s" % (self._proc.pid, " ".join(self.args)))
        self._reader = threading.Thread(target=self._pump, daemon=True)
        self._reader.start()
        if separate:
            self._log_reader = threading.Thread(target=self._pump_log, daemon=True)
            self._log_reader.start()
        return self

    def _pump(self):
        """Reader thread: drain stdout, strip escapes, append to _buf, optionally echo."""
        while True:
            chunk = self._proc.stdout.read(256)
            if not chunk:
                break
            clean = _ANSI.sub(b"", chunk)
            with self._lock:
                self._buf.extend(clean)
            if self.echo and clean:
                sys.stdout.buffer.write(clean)
                sys.stdout.buffer.flush()
            if self._tx and clean:
                self._tx.write(clean.decode("latin-1").replace("\r", ""))
                self._tx.flush()

    def _pump_log(self):
        """Log reader thread (log_file mode): read ALL of stderr so nd100x never blocks
        on a full pipe, but write only the kept lines to disc."""
        with open(self.log_file, "w", encoding="latin-1") as f:
            for raw in self._proc.stderr:
                line = raw.decode("latin-1").rstrip("\r\n")
                control = line.startswith("[pipe-control]")
                if control or self._log_keep is None or self._log_keep.search(line):
                    f.write(line + "\n")
                    f.flush()
                    with self._lock:
                        self._log.append(line)

    def log_lines(self):
        """The emulator log lines kept so far (log_file mode)."""
        with self._lock:
            return list(self._log)

    def __enter__(self):
        return self.start()

    def __exit__(self, *exc):
        self.close()

    def close(self):
        if self._closed:
            return
        self._closed = True
        if self._proc and self._proc.poll() is None:
            self.note("stop pid %d" % self._proc.pid)
            try:
                self._proc.terminate()
                self._proc.wait(timeout=5)
            except Exception:
                try: self._proc.kill()
                except Exception: pass
        for t in (self._reader, self._log_reader):
            if t is not None:
                t.join(timeout=5)
        if self._proc:
            for pipe in (self._proc.stdin, self._proc.stdout, self._proc.stderr):
                if pipe is not None:
                    try:
                        pipe.close()
                    except OSError:
                        pass
        if not self.keep_copies:
            for path in self.copies.values():
                try:
                    os.remove(path)
                except OSError:
                    pass
        if self._tx:
            self.note("closed")
            self._tx.close()
            self._tx = None

    # -- I/O ---------------------------------------------------------------
    def send(self, text):
        """Send keystrokes. Use \\r for ENTER (the ND terminal wants CR)."""
        data = text.encode("latin-1", "replace") if isinstance(text, str) else text
        if CONTROL_START in data:
            # 0xFF would start a control frame and swallow the rest of the line.
            raise ValueError("keystrokes contain 0xFF; use mount()/eject() for control commands")
        self.note("type %r" % data)
        self._proc.stdin.write(data)
        self._proc.stdin.flush()

    def sendline(self, text=""):
        self.send(text + "\r")

    # -- SINTRAN helpers ---------------------------------------------------
    def escape(self, answer=Regex(SINTRAN_ENTER), every=15, timeout=300):
        """Send ESC every `every` seconds until `answer` shows (default SINTRAN's
        ENTER prompt). SINTRAN ignores ESC while it is still starting, so a single
        ESC is not enough. Returns the matched text."""
        deadline = time.time() + timeout
        while True:
            self.send(b"\x1b")
            try:
                return self.expect(answer, timeout=min(every, max(0.1, deadline - time.time())))
            except ExpectTimeout:
                if time.time() >= deadline:
                    raise

    def login(self, user="SYSTEM", password="", timeout=60):
        """Log in at the ENTER prompt and wait for the @ prompt. Name, CR, password,
        CR go in ONE write: split sends bounce back to a fresh ENTER prompt."""
        self.send("%s\r%s\r" % (user, password))
        return self.expect(Regex(SINTRAN_COMMAND_PROMPT), timeout=timeout)

    def command(self, text, until=Regex(SINTRAN_COMMAND_PROMPT), timeout=120, abort=None):
        """Type a command line and wait until `until` (default the @ prompt).
        Returns all output the command produced, prompt included."""
        with self._lock:
            mark = len(self._buf)
        self.sendline(text)
        self.expect(until, timeout=timeout, abort=abort)
        with self._lock:
            return bytes(self._buf[mark:self._raw_since_match]).decode("latin-1")

    def answer(self, text, timeout=60, abort=None):
        """Type an answer and wait for the next SINTRAN prompt (@, ENTER or a
        question ending in ':'). Returns the output."""
        return self.command(text, until=Regex(SINTRAN_ANY_PROMPT), timeout=timeout, abort=abort)

    def wait_quiet(self, seconds):
        """Let the machine run `seconds` without typing (output is still collected)."""
        self.note("wait %g s" % seconds)
        time.sleep(seconds)

    # -- floppy hot-swap (--pipe control channel) --------------------------
    def _control(self, cmd):
        """Send a 0xFF-framed control line (intercepted by nd100x, not the terminal)."""
        frame = control_frame(cmd)
        self.note("control %s" % cmd)
        self._proc.stdin.write(frame)
        self._proc.stdin.flush()

    def _control_reply(self, timeout, aborts):
        """Wait for nd100x's [pipe-control] reply. In log_file mode it is on the
        separate log; otherwise it is mixed into the screen output."""
        if self.log_file is None:
            self.expect("-> ok", timeout=timeout, abort=aborts)
            return
        deadline = time.time() + timeout
        while time.time() < deadline:
            with self._lock:
                new = self._log[self._log_seen:]
                self._log_seen = len(self._log)
            for line in new:
                m = CONTROL_REPLY.search(line)
                if m:
                    self.note("control reply: %s" % m.group(1))
                    if m.group(1).strip() != "ok":
                        raise ExpectAbort(line)
                    return
            if self._proc.poll() is not None:
                raise ExpectTimeout("nd100x exited before answering the control command")
            time.sleep(0.02)
        raise ExpectTimeout("no [pipe-control] reply within %ss (is nd100x in --pipe mode?)"
                            % timeout)

    def mount(self, unit, path, confirm=True, timeout=5, copy=False):
        """Hot-swap: eject floppy `unit` (0-2) and mount `path` while the machine runs -
        the "operator inserts the next disk" step. Use an ABSOLUTE path. With confirm=True,
        wait for the emulator's ack and raise if the file was not found / the unit is bad.
        copy=True mounts a copy made in work_dir instead of the file itself.
        Returns the path that was mounted."""
        _floppy_unit(unit)
        if copy:
            path = self._copy("floppy%d" % unit, path)
        self._control("mount %d %s" % (unit, path))
        if confirm:
            self._control_reply(timeout, ["FILE NOT FOUND", "BAD UNIT"])
        return path

    def eject(self, unit, confirm=True, timeout=5):
        """Eject floppy `unit` while the machine runs."""
        _floppy_unit(unit)
        self._control("eject %d" % unit)
        if confirm:
            self._control_reply(timeout, ["BAD UNIT"])

    # -- online catalog (Python-side fetch; works on Windows without libcurl) ----
    def _catalog(self, force=False):
        """Lazily load + cache the shared FloppyCatalog on this instance."""
        if getattr(self, "_cat", None) is None or force:
            self._cat = FloppyCatalog.load(force=force)
        return self._cat

    def catalog_matches(self, directory=None, md5=None):
        """Resolve a catalog selector WITHOUT mounting, for disambiguation.

        Pass md5= (unique) or directory= (the SINTRAN "Directory name", which may
        match several images). Returns a list of entry dicts {id, name, md5,
        directory_name, pages, is_smd}. When more than one is returned, pick the one
        you want and pass its md5 to mount_catalog()."""
        cat = self._catalog()
        if md5:
            e = cat.find_md5(md5)
            return [e] if e else []
        if directory:
            return list(cat.find_directory(directory))
        return []

    def mount_catalog(self, unit, md5=None, directory=None, dest_dir=None,
                      confirm=True, timeout=120):
        """Download a catalog image (.img.gz, unpacked here) and hot-swap it onto floppy `unit`.

        Identify the disk by md5= (unique) or directory= (SINTRAN "Directory name").
        If a directory name matches several images, the matches are logged and the
        FIRST is used - pass an md5 to pin a specific one. The image is fetched with
        urllib (so this works on Windows where the C build has no libcurl) into
        dest_dir (default: the OS temp dir) and mounted via the ordinary hot-swap
        control channel. Returns the local image path.

        Raises LookupError if nothing matches, and the usual ExpectAbort/ExpectTimeout
        from the mount confirmation."""
        import urllib.request, tempfile
        matches = self.catalog_matches(md5=md5, directory=directory)
        if not matches:
            raise LookupError("catalog: no disk for md5=%r directory=%r" % (md5, directory))
        if len(matches) > 1:
            sys.stderr.write("[catalog] %r is ambiguous - %d images (using the first):\n"
                             % (directory, len(matches)))
            for e in matches:
                sys.stderr.write("    - name=%r dir=%r pages=%d md5=%s\n"
                                 % (e["name"], e["directory_name"], e["pages"], e["md5"]))
            sys.stderr.write("    (pin one with md5=...)\n")
        entry = matches[0]
        url = self._catalog().image_url(entry)
        dest_dir = dest_dir or tempfile.gettempdir()
        dest = os.path.join(dest_dir, entry["md5"] + ".img")
        if not os.path.isfile(dest):                       # simple content-addressed cache
            sys.stderr.write("[catalog] downloading %s -> %s\n" % (url, dest))
            _req = urllib.request.Request(url, headers={"User-Agent": CATALOG_USER_AGENT})
            with urllib.request.urlopen(_req, timeout=180) as _r:
                _data = _r.read()
            if url.endswith(".gz"):                        # the archive stores .img.gz
                import gzip
                _data = gzip.decompress(_data)
            with open(dest, "wb") as _f:
                _f.write(_data)
        self.mount(unit, os.path.abspath(dest), confirm=confirm, timeout=timeout)
        return dest

    def dbmount(self, unit, selector, confirm=True, timeout=120):
        """Mount from the catalog using nd100x's OWN C resolver+downloader (WinHTTP
        on Windows, libcurl on Linux/BSD/macOS). `selector` is "md5:<hash>",
        "dir:<name>" or a bare token. On WASM/RISC-V builds this fails; prefer
        mount_catalog() there."""
        self._control("dbmount %d %s" % (int(unit), selector))
        if confirm:
            self._control_reply(timeout, ["NOT FOUND", "NO CATALOG", "MOUNT FAILED", "BAD UNIT"])

    def dblist(self, selector):
        """Ask nd100x's C catalog to list matches for a selector.
        Results are printed to the emulator's stderr; returns nothing."""
        self._control("dblist %s" % selector)

    def expect(self, pattern, timeout=30, abort=None, poll=0.02):
        """
        Wait until `pattern` (str/regex or list of them) appears in new output.
        `abort` is a str/regex or list; if one of those appears first, raise
        ExpectAbort. Returns the matched text. Raises ExpectTimeout on timeout.
        Matching is on output produced AFTER the previous expect() (so a marker
        from an earlier step is never re-matched).
        """
        wants = [pattern] if isinstance(pattern, (str, bytes, Regex)) else list(pattern)
        aborts = [] if abort is None else ([abort] if isinstance(abort, (str, bytes, Regex)) else list(abort))
        wants_re = [_compile_pattern(p) for p in wants]
        abort_re = [_compile_pattern(p) for p in aborts]

        deadline = time.time() + timeout
        start = self._raw_since_match
        while True:
            with self._lock:
                window = bytes(self._buf[start:])
            for rx in abort_re:
                m = rx.search(window)
                if m:
                    self._raw_since_match = start + m.end()
                    raise ExpectAbort("abort pattern %r matched: ...%s" %
                                      (rx.pattern.decode(errors="replace"),
                                       window[max(0, m.start()-40):m.end()+10].decode(errors="replace")))
            for rx in wants_re:
                m = rx.search(window)
                if m:
                    self._raw_since_match = start + m.end()
                    return m.group(0).decode(errors="replace")
            if self._proc.poll() is not None and not window:
                raise ExpectTimeout("nd100x exited before %r appeared" % wants)
            if time.time() > deadline:
                tail = window[-200:].decode(errors="replace")
                raise ExpectTimeout("timed out after %ss waiting for %r; last output:\n%s"
                                    % (timeout, wants, tail))
            time.sleep(poll)

    def before(self):
        """All cleaned output captured so far (for logging / assertions)."""
        with self._lock:
            return bytes(self._buf).decode(errors="replace")
