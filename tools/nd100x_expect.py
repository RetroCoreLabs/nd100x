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
"""

import os
import re
import sys
import time
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


# --- online floppy/disk catalog -------------------------------------------------
# The SAME catalog the F12 browser and the C floppydb use. Fetching over HTTP with
# urllib works on every platform (including Windows w64devkit, where the C build has
# no libcurl), so the Python driver can search the catalog, download an image, and
# mount it via the ordinary hot-swap control channel - fully self-contained.
CATALOG_JSON_URL = "https://ndlib.hackercorp.no/floppies.json"
CATALOG_IMAGES_BASE = "https://ndlib.hackercorp.no/images/"
# The ndlib server rejects the default Python-urllib User-Agent (403); it serves
# nd100x/1.0 (the same UA the C libcurl download uses). Set it on every request.
CATALOG_USER_AGENT = "nd100x/1.0"


def _catalog_cache_path():
    home = os.environ.get("HOME") or os.environ.get("USERPROFILE") or _HERE
    return os.path.join(home, ".cache", "nd100x", "floppies.json")


class FloppyCatalog:
    """Search the online floppy/disk catalog by md5 (unique) or SINTRAN directory name.

    Mirrors the C floppydb: only Status == 0 records are kept; the "Directory name"
    and "Filesystem image size" (octal pages) are pulled out of DirectoryContent;
    > 1000 pages => an SMD image. A directory name MAY match several images - use the
    md5 to pin a specific one.
    """

    _DIR_RE = re.compile(r"Directory name\s*:\s*([^\r\n]*)")
    _SIZE_RE = re.compile(r"Filesystem image size\s*:\s*([0-7]+)")

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
            if item.get("Status", 0) != 0:                 # keep only Status 0
                continue
            content = item.get("DirectoryContent") or ""
            dm = cls._DIR_RE.search(content)
            sm = cls._SIZE_RE.search(content)
            pages = int(sm.group(1), 8) if sm else -1      # octal, like the catalog
            entries.append({
                "id": item.get("Id", 0),
                "name": item.get("Name", "") or "",
                "md5": (item.get("Md5", "") or "").strip(),
                "directory_name": (dm.group(1).strip() if dm else ""),
                "pages": pages,
                "is_smd": pages > 1000,
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
        return CATALOG_IMAGES_BASE + entry["md5"] + ".img"


class Nd100x:
    """A running nd100x subprocess you can expect()/send() against."""

    def __init__(self, exe=None, boot="smd", image=None, smd0=None, cputype=None,
                 cpu_number=None, extra_args=None, max_instr=None, echo=True):
        self.exe = exe or DEFAULT_EXE
        args = [self.exe, "--pipe", "--boot=%s" % boot]
        if image:      args.append("--image=%s" % image)
        if smd0:       args.append("--smd0=%s" % smd0)
        if cputype:    args.append("--cputype=%s" % cputype)
        if cpu_number is not None: args.append("--cpu-number=%s" % cpu_number)
        if max_instr:  args.append("--max-instr=%d" % max_instr)
        if extra_args: args.extend(extra_args)
        self.args = args
        self.echo = echo                     # mirror emulated output to our stdout as it arrives
        self._buf = bytearray()              # cleaned output accumulated so far
        self._raw_since_match = 0            # index into _buf where the current expect starts scanning
        self._lock = threading.Lock()
        self._proc = None
        self._reader = None
        self._closed = False

    # -- lifecycle ---------------------------------------------------------
    def start(self):
        self._proc = subprocess.Popen(
            self.args, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, bufsize=0)
        self._reader = threading.Thread(target=self._pump, daemon=True)
        self._reader.start()
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

    def __enter__(self):
        return self.start()

    def __exit__(self, *exc):
        self.close()

    def close(self):
        if self._closed:
            return
        self._closed = True
        if self._proc and self._proc.poll() is None:
            try:
                self._proc.terminate()
                self._proc.wait(timeout=5)
            except Exception:
                try: self._proc.kill()
                except Exception: pass

    # -- I/O ---------------------------------------------------------------
    def send(self, text):
        """Send keystrokes. Use \\r for ENTER (the ND terminal wants CR)."""
        data = text.encode("latin-1", "replace") if isinstance(text, str) else text
        self._proc.stdin.write(data)
        self._proc.stdin.flush()

    def sendline(self, text=""):
        self.send(text + "\r")

    # -- floppy hot-swap (--pipe control channel) --------------------------
    def _control(self, cmd):
        """Send a 0xFF-framed control line (intercepted by nd100x, not the terminal)."""
        self._proc.stdin.write(b"\xff" + (cmd + "\n").encode("latin-1", "replace"))
        self._proc.stdin.flush()

    def mount(self, unit, path, confirm=True, timeout=5):
        """Hot-swap: eject floppy `unit` (0-2) and mount `path` while the machine runs -
        the "operator inserts the next disk" step. Use an ABSOLUTE path. With confirm=True,
        wait for the emulator's ack and raise if the file was not found / the unit is bad."""
        self._control("mount %d %s" % (int(unit), path))
        if confirm:
            self.expect("-> ok", timeout=timeout, abort=["FILE NOT FOUND", "BAD UNIT"])

    def eject(self, unit, confirm=True, timeout=5):
        """Eject floppy `unit` while the machine runs."""
        self._control("eject %d" % int(unit))
        if confirm:
            self.expect("-> ok", timeout=timeout, abort=["BAD UNIT"])

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
        """Download a catalog image and hot-swap it onto floppy `unit`.

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
            with urllib.request.urlopen(_req, timeout=180) as _r, open(dest, "wb") as _f:
                _f.write(_r.read())
        self.mount(unit, os.path.abspath(dest), confirm=confirm, timeout=timeout)
        return dest

    def dbmount(self, unit, selector, confirm=True, timeout=120):
        """Mount from the catalog using nd100x's OWN C resolver+downloader (libcurl
        builds only). `selector` is "md5:<hash>", "dir:<name>" or a bare token. On a
        no-libcurl build this fails; prefer mount_catalog() there."""
        self._control("dbmount %d %s" % (int(unit), selector))
        if confirm:
            self.expect("-> ok", timeout=timeout,
                        abort=["NOT FOUND", "NO CATALOG", "MOUNT FAILED", "BAD UNIT"])

    def dblist(self, selector):
        """Ask nd100x's C catalog to list matches for a selector (libcurl builds).
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
