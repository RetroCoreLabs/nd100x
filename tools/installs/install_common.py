"""Shared parts of the product install scripts in tools/installs/<product>/install.py.

Every install script here works the same way (README.md in this folder):
copy the source pack into a work folder, boot the copy, log in, run ND's own
installer and answer its questions from a table, check the result, boot it
again and check again, and only then move the copy to the result name.
"""

import argparse
import os
import re
import shutil
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(REPO, "tools"))
from nd100x_expect import (Nd100x, Regex, ExpectTimeout, fetch_catalog_image,  # noqa: E402,F401
                           _md5_of_file)
from dialogue import (State, When, run_dialogue, DialogueError, DialogueFailed,  # noqa: E402,F401
                      OutOfOrder, DialogueTimeout)

FLOPPY_CACHE = os.path.expanduser("~/.cache/nd100x/images")

# config/ND5000.ini (ND-100 with MFbus memory, octobus and one ND-5000 at station
# 070B) without the telnet server, so two runs cannot fight over a port.
# ND120CX/32-bit as there: NUCLEUS (NKSERV) ends at once on a CPU type below 5.
ND5000_INI = """# Written by a tools/installs script from config/ND5000.ini.
[machine]
cpu = ND120CX
fpp = 32
[controller.smd.0]
enabled = yes
disk0 = {pack}
[controller.floppy.0]
enabled = yes
{floppy_line}
[controller.scsi.0]
enabled = yes
[terminals]
enabled = 5, 6, 7, 8
[boot]
device = smd.0.0
[mfbus]
size      = 8
base_page = 04100B
[mfbus.part.0]
pages   = 4096
[controller.octobus.0]
enabled = yes
[nd5000.1]
enabled = yes
station = 070B
"""


# --- ND's standard installer (IN-<product>-XX-<ver><rev>:PROG) ------------------
# The Linkage-Loader, LINKER and Backup Manager installers are one framework: three
# MESSAGE boxes ending in a "Press CR" line, then a main menu of five modules ("These
# modules must be executed in the right order from 1 to 5"), each module going back
# to the menu when it is done. The wording varies a little between products; every
# pattern below was seen on the screen (transcripts of 2026-11-02 and 2026-11-04).
# Each product's table is in its own install.py, built from these pieces.
#
# A prompt is matched by its own text and never has to end its line ($). A server's
# console report can be cut by the installer's output and go on right after the
# question, on the question's own line. Seen on ND5000.IMG with its boot batch,
# 2026-11-04 (CR left out):
#             MTAD server initiated and s
#   >> Please specify which user you want the files BACKUP-SYS-I:PROG and
#   >> BACKUP-SERV-I:PROG copied to (default: UTILITY) : tarted
# Only the bare @ prompt keeps its $: the closing notes hold lines such as
# "@MODE (BACKUP-MANAGER)BM-START-B02:MODE,,,,,,".

# '>> Press "CR" to continue' (Backup Manager: '>> Press CR to continue')
PRESS_CR = r'>> Press "?CR"? to continue'
# ">>Which module do you want to execute? (default:'1'): " (Backup Manager: no '?').
# The default is the module the installer expects next.
MENU = r">>Which module do you want to execute\?? \(default:'(\d)'\):"
# Module 4: "Remove floppy 210319H--01D from station 1 unit 0." in a MESSAGE box,
# then the Press CR line.
REMOVE_FLOPPY = r'Remove floppy \S+ from station 1 unit 0\.[\s:]*' + PRESS_CR
# Module 1 of the Linkage-Loader and LINKER: the installer's own defaults are taken
# (Return), DOMAIN-USER for the domain and UTILITY for the utility files.
DOMAIN_USER_Q = r">>On which user do you want to place the domain\? \(default:'DOMAIN-USER'\):"
UTILITY_Q = r">>On which user do you want to place the utility files\? \(default:'UTILITY'\):"
# Module 2. A first install has no earlier version to delete, so N:
# ">>Do you want to delete the previous version of the product? (default:'Y'): "
# ">>Do you want to delete the current version of the product (default:'Y'): "
# (LINKER: no default, ends in ' : '; Backup Manager: no "the", no '?')
DELETE_PREVIOUS_Q = (r">>Do you want to delete (the )?previous version of the product\??"
                     r"( \(default:'\w'\))? ?:")
DELETE_CURRENT_Q = (r">>Do you want to delete (the )?current version of the product\??"
                    r"( \(default:'\w'\))? ?:")
# Module 5: ">>Do you want to copy IN-NLL-XX-H02:INST to floppy number 1? (default:'N'): "
# N: the distribution floppy stays as it came from the archive.
COPY_INST_Q = r">>Do you want to copy \S+:INST to floppy number 1\?( \(default:'\w'\))? ?:"
# The SINTRAN prompt the installer leaves at when it has finished.
COMMAND_PROMPT = r"^@ ?$"


class Menu:
    """The main menu: modules 1 to 5 in turn.

    first    the state each module's first question is in, by module number;
             "menu" for a module that asks nothing (module 3 of the Linkage-Loader
             and LINKER checks and goes back)
    The installer's own default ("(default:'3')") names the module it expects next.
    A different default means the module before did not finish (the installer keeps
    the old default), and the run stops; so does the menu coming back after 5."""

    def __init__(self, first):
        self.first = first
        self.next = 1

    def reply(self, vm, m):
        if self.next > 5:
            raise DialogueFailed("the installer's menu came back after module 5")
        if m.group(1) != str(self.next):
            raise DialogueFailed("the menu offers module %s, but module %d is next: module %d "
                                 "did not finish" % (m.group(1), self.next, self.next - 1))
        vm.note("menu: module %d" % self.next)
        return str(self.next)

    def goto(self, m):
        state = self.first[self.next]
        self.next += 1
        return state

    def when(self):
        return When(MENU, self.reply, self.goto)

    def state(self, timeout=300):
        return State("menu", [self.when()], timeout)


def standard_start(menu):
    """The three opening MESSAGE boxes, each ending in Press CR, then the menu."""
    return State("start", [When(PRESS_CR, "", "start"), menu.when()])


def eject_floppy(vm, m):
    vm.eject(0)
    return ""


# Lines that mean a step of the installer failed. The installers go on after them
# and still end "INSTALLATION - FINISHED", so every piece of output is searched for
# them as it arrives and the run stops at the first one.
# Each one was seen on the screen of a failed run (2026-11-02):
#   "NO MORE PAGES FOR THIS USER"    a file copy to a user without room
#   "Filename syntax error"          Backup Manager joining BM-OPERATOR-B02:DO02
#                                    to its :DOM when the :DOM was already there
#   "NOT READ ACCESS"                "Error when loading Control Store."
#   "Error when loading"
INSTALL_FAILED = r"NO MORE PAGES|Filename syntax error|NOT READ ACCESS|Error when loading"


def machine(exe, ini, work, name):
    """The machine, not started yet (the `with` block starts and stops it).
    boot=None: the boot device comes from [boot] in the INI.
    match="lines": prompts are found anywhere in the new output, not only at its end
    (nd100x_expect.line_text); a server's console report printed after a prompt does
    not hide it."""
    return Nd100x(exe=exe, config=ini, boot=None, echo=False, match="lines",
                  log_file=os.path.join(work, name + "-emulator.log"),
                  log_keep=r"ERROR|FATAL|pipe",
                  transcript=os.path.join(work, name + "-console.log"))


def logged_in(vm, user="SYSTEM"):
    """Wait for SINTRAN, get ENTER with ESC and log in (empty password)."""
    vm.expect("RUNNING", timeout=600)
    vm.escape()
    vm.login(user)


def enter_floppy(vm, unit=0):
    """@ENTER-DIRECTORY for the floppy in FLOPPY-DISC-1 `unit` (directory name left empty:
    SINTRAN takes the name from the floppy)."""
    vm.command("ENTER-DIRECTORY", until=Regex(r"DIRECTORY NAME:"))
    vm.answer("")
    vm.answer("FLOPPY-DISC-1")
    vm.answer(str(unit))


def floppy(md5, local=None):
    """Path of a distribution floppy: `local` if given (its md5 is checked), else the
    archive copy, downloaded and checked."""
    if local:
        if _md5_of_file(local) != md5:
            raise SystemExit("%s is not the expected floppy (md5 should be %s)" % (local, md5))
        return local
    return fetch_catalog_image(md5, FLOPPY_CACHE)


def arguments(doc, extra=None):
    """The command line every install script takes: SOURCE RESULT and the options."""
    ap = argparse.ArgumentParser(description=doc.split("\n\n")[0])
    ap.add_argument("source", help="SINTRAN pack to install on (only a copy is used)")
    ap.add_argument("result", help="where the installed pack goes; must not exist")
    ap.add_argument("--nd100x", default=os.path.join(REPO, "build", "bin", "nd100x"))
    ap.add_argument("--work", help="folder for the copies and logs (default: a new temp folder)")
    ap.add_argument("--silence", type=float, default=600,
                    help="stop when the machine prints nothing for this many seconds")
    ap.add_argument("--total", type=float, default=7200,
                    help="stop when the installer dialogue takes longer than this (seconds)")
    if extra:
        extra(ap)
    a = ap.parse_args()
    if os.path.exists(a.result):
        raise SystemExit("%s exists; give a new file name" % a.result)
    if os.path.abspath(a.source) == os.path.abspath(a.result):
        raise SystemExit("source and result must differ")
    return a


def write_ini(path, pack, floppy=None):
    """The machine INI: the pack in SMD unit 0, `floppy` (or nothing) in floppy unit 0."""
    with open(path, "w") as f:
        f.write(ND5000_INI.format(pack=pack,
                                  floppy_line=("disk0 = %s" % floppy) if floppy else ""))


def prepare(a, prefix, floppy_path):
    """Copy the pack and the first floppy into the work folder and write the INI.
    Returns (work, pack, ini). SINTRAN may write to a floppy, so it is a copy too."""
    work = a.work or tempfile.mkdtemp(prefix=prefix)
    os.makedirs(work, exist_ok=True)
    pack = os.path.join(work, "pack.img")
    fl0 = os.path.join(work, "floppy0.img")
    shutil.copyfile(a.source, pack)
    shutil.copyfile(floppy_path, fl0)
    ini = os.path.join(work, "machine.ini")
    write_ini(ini, pack, fl0)
    print("work folder: %s" % work)
    return work, pack, ini


def prepare_pack(a, prefix):
    """prepare() for an install that needs no floppy: the floppy drive stays empty."""
    work = a.work or tempfile.mkdtemp(prefix=prefix)
    os.makedirs(work, exist_ok=True)
    pack = os.path.join(work, "pack.img")
    shutil.copyfile(a.source, pack)
    ini = os.path.join(work, "machine.ini")
    write_ini(ini, pack)
    print("work folder: %s" % work)
    return work, pack, ini


def deliver(pack, result):
    """Move the checked pack to the result name."""
    os.makedirs(os.path.dirname(os.path.abspath(result)), exist_ok=True)
    shutil.move(pack, result)


def require_reentrant(vm, names):
    """Stop unless every name in `names` is in @LIST-REENTRANT (subsystems and ND-500
    standard domains both show there). Says which install script provides it."""
    out = vm.command("LIST-REENTRANT", timeout=60)
    missing = [n for n in names if not re.search(r"\b%s\b" % re.escape(n), out)]
    if missing:
        raise SystemExit("this pack needs %s first (see tools/installs/README.md):\n%s"
                         % (", ".join(missing), out))


ND5000_PROMPT = Regex(r"ND-5000?:")
# The monitor's own error lines, e.g. "ND-500(0) error:      ND-500(0) reserved for
# special use". When it refuses entry it prints one and goes back to @.
ND5000_ERROR = r"ND-500\(\d+\) error:[^\n]*"
ND5000_REFUSED = Regex(ND5000_ERROR + r"\n\s*@")
# The errors that mean another job holds the ND-500 just now; measured 2026-11-04 on
# ND5000.IMG while its boot batch loaded the control store: entry refused with
# "reserved for special use", LOAD-CONTROL-STORE and START-SWAPPER refused with
# "Other user(s) already logged on ND-500(0)" (error 2034; the ND-500 Loader Monitor
# manual: some system supervisor commands need the ND-500 to themselves).
ND5000_BUSY = r"already logged on|reserved for special use"


def nd5000_loaded(version):
    """(swapper, microprogram) from the monitor's VERSION output when both are loaded,
    else None. Before the start it prints "Swapper.......: " with nothing after it and
    "Micro program.:     0"; after it "Swapper.......: REV.-K01" and
    "Micro program.: 11930" (ND-500 Loader Monitor manual 8.9.7; measured 2026-11-04)."""
    swapper = re.search(r"^Swapper\.+: *(\S[^\r\n]*)", version, re.M)
    micro = re.search(r"^Micro program\.+: *(\d+)", version, re.M)
    if swapper and micro and int(micro.group(1)) != 0:
        return swapper.group(1).strip(), micro.group(1)
    return None


def start_nd5000(vm, timeout=300):
    """Make sure the ND-5000 runs, started the way ND's (SYSTEM)LOAD-MODE:MODE starts
    it at every boot (ND-896058-2, section 14):

        @ND-500-MONITOR
        LOAD-CONTROL-STORE,,,,
        START-SWAPPER
        EXIT

    Without it, the first program to use the ND-5000 loads the control store itself,
    as whoever runs it. The installers switch to DOMAIN-USER before they run the
    Linkage-Loader, and DOMAIN-USER has no read access to (SYSTEM)CONTROL-STORE:DATA:
    the monitor prints "Error when loading Control Store. NOT READ ACCESS", the
    domain is not copied, and the installer still ends "INSTALLATION - FINISHED".
    Measured 2026-11-02: the same domain copy as DOMAIN-USER works after this start.

    A pack may start the ND-5000 in its own boot batch (ND5000.IMG does, about 20 s
    after log-in). So VERSION decides: already loaded, nothing to do. Otherwise the
    two commands, each up to its ND-5000: prompt, with every "ND-500(0) error:" line
    read. A busy error (ND5000_BUSY: the batch holds the ND-500 just now) means leave
    the monitor, wait 5 s and look again; any other error stops the run. VERSION
    after the start must show both loaded. While the script sits in the monitor it
    holds the ND-500 too, so it only stays there for these commands."""
    deadline = time.time() + timeout
    while True:
        vm.sendline("ND-500-MONITOR")
        got = vm.expect([ND5000_PROMPT, ND5000_REFUSED], timeout=120,
                        abort=Regex(r"NO SUCH FILE NAME"))
        said = got
        if not re.search(ND5000_ERROR, got):
            said = vm.command("VERSION", until=ND5000_PROMPT, timeout=60)
            loaded = nd5000_loaded(said)
            if not loaded:
                said = vm.command("LOAD-CONTROL-STORE,,,,", until=ND5000_PROMPT, timeout=300)
                said += vm.command("START-SWAPPER", until=ND5000_PROMPT, timeout=300)
                errors = re.findall(ND5000_ERROR + r"|Error when loading[^\n]*|NOT READ ACCESS",
                                    said.replace("\r", ""))
                if errors and not all(re.search(ND5000_BUSY, e) for e in errors):
                    vm.command("EXIT", timeout=60)
                    raise SystemExit("could not start the ND-5000:\n%s" % said)
                version = vm.command("VERSION", until=ND5000_PROMPT, timeout=60)
                loaded = nd5000_loaded(version)
                if loaded:
                    vm.note("ND-5000 started as SYSTEM (swapper %s, microprogram %s)" % loaded)
            else:
                vm.note("ND-5000 already running (swapper %s, microprogram %s)" % loaded)
            vm.command("EXIT", timeout=60)
            if loaded:
                return
        elif not re.search(ND5000_BUSY, got):
            raise SystemExit("the ND-500 monitor refused: %s" % got)
        if time.time() > deadline:
            raise SystemExit("the ND-5000 was not running within %d s; the monitor last said:\n%s"
                             % (timeout, said))
        vm.note("ND-500 busy (the pack's boot batch?); again in 5 s")
        vm.wait_quiet(5)


def user_stat(vm, user):
    """@USER-STAT for `user`: returns (pages used, pages given), or None when the
    user does not exist."""
    vm.command("USER-STAT", until=Regex(r"USER NAME:"))
    out = vm.command(user, until=Regex(r"OUTPUT FILE:|\n@ ?$"))
    if "OUTPUT FILE" in out:
        out = vm.command("", timeout=60)
    m = re.search(r"(\d+) PAGES USED OUT OF (\d+) PAGES", out)
    if not m:
        # For a user that does not exist, SINTRAN L still asks OUTPUT FILE: and then
        # prints nothing at all before the @ (seen 2026-11-02, BACKUP-MANAGER before
        # the Backup Manager install).
        if re.fullmatch(r"\s*@?\s*", out):
            return None
        raise SystemExit("USER-STAT %s printed no page count:\n%s" % (user, out))
    return int(m.group(1)), int(m.group(2))


def make_room(vm, user, pages, directory="PACK-ONE"):
    """Make sure `user` exists and has at least `pages` free, with the commands ND's
    installer framework uses itself (CREATE-USER <dir>:<user>, then
    GIVE-USER-SPACE <dir>:<user> <pages>; GIVE-USER-SPACE adds to what the user has).
    Prints USER-STAT before and after."""
    before = user_stat(vm, user)
    if before is None:
        out = vm.command("CREATE-USER %s:%s" % (directory, user), timeout=60)
        if re.search(r"ILLEGAL|ALREADY|NO MORE|NOT ", out):
            raise SystemExit("CREATE-USER %s failed:\n%s" % (user, out))
        before = user_stat(vm, user)
    used, given = before
    vm.note("USER-STAT %s before: %d pages used out of %d" % (user, used, given))
    short = pages - (given - used)
    if short > 0:
        out = vm.command("GIVE-USER-SPACE %s:%s %d" % (directory, user, short), timeout=60)
        if re.search(r"ILLEGAL|NO MORE|NOT ", out):
            raise SystemExit("GIVE-USER-SPACE %s %d failed:\n%s" % (user, short, out))
        used, given = user_stat(vm, user)
        vm.note("USER-STAT %s after: %d pages used out of %d" % (user, used, given))
    if given - used < pages:
        raise SystemExit("%s still has only %d free pages, needs %d" % (user, given - used, pages))


def standard_domains(vm):
    """@ND-500-MONITOR, LIST-STANDARD-DOMAINS, EXIT. Returns the listing."""
    vm.sendline("ND-500-MONITOR")
    vm.expect(Regex(r"ND-5000?:"), timeout=120, abort=Regex(r"NO SUCH FILE NAME"))
    out = vm.command("LIST-STANDARD-DOMAINS", until=Regex(r"ND-5000?:"), timeout=60)
    vm.command("EXIT", timeout=60)
    return out


def check_standard_domain(listing, name, segment_file, what):
    """`name` must be listed as initialized, with `segment_file` as one of its segments."""
    m = re.search(r"^%s +\(Standard domain is initialized\)\s*\n((?:\s+\S.*\n?)*)"
                  % re.escape(name), listing, re.M)
    if not m or segment_file not in m.group(1):
        raise SystemExit("FAIL (%s): standard domain %s on %s is not there:\n%s"
                         % (what, name, segment_file, listing))


def install_product(a, prefix, floppy_path, installer, dialogue, check, needs=(), before=None,
                    between=None):
    """The whole run for a product: copy, boot, log in, check `needs` in
    LIST-REENTRANT, call before(vm, work) if given, enter the floppy in unit 0, run
    `installer` and answer it with run_dialogue(dialogue()) (a function, so the menu's
    module counter starts afresh), then check(vm, what) after the install and again
    after a second boot, then deliver.
    between(pack), if given, runs while no emulator has the pack open: after the
    install run has stopped and before the second boot, so the second boot and its
    check run on what it changed.
    When `needs` has ND-500-MONITOR, the ND-5000 must run before the installer:
    start_nd5000() after each boot (it also copes with a pack whose boot batch
    starts it).
    Returns what the two checks returned."""
    work, pack, ini = prepare(a, prefix, floppy_path)
    nd5000 = "ND-500-MONITOR" in needs
    with machine(a.nd100x, ini, work, "install") as vm:
        logged_in(vm)
        require_reentrant(vm, needs)
        if nd5000:
            start_nd5000(vm)
        if before:
            before(vm, work)
        enter_floppy(vm, 0)
        vm.sendline(installer)
        run_dialogue(vm, dialogue(), fail=INSTALL_FAILED, silence=a.silence, total=a.total)
        first = check(vm, "after install")
    if between:
        between(pack)
    with machine(a.nd100x, ini, work, "reboot") as vm:
        logged_in(vm)
        if nd5000:
            start_nd5000(vm)
        second = check(vm, "after a second boot")
    deliver(pack, a.result)
    print("Result: %s   (logs and transcripts in %s)" % (a.result, work))
    return first, second
