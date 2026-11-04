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

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(REPO, "tools"))
from nd100x_expect import (Nd100x, Regex, ExpectTimeout, fetch_catalog_image,  # noqa: E402,F401
                           _md5_of_file)

FLOPPY_CACHE = os.path.expanduser("~/.cache/nd100x/images")

# config/ND5000.ini (ND-100 with MFbus memory, octobus and one ND-5000 at station
# 070B) without the telnet server, so two runs cannot fight over a port.
ND5000_INI = """# Written by a tools/installs script from config/ND5000.ini.
[machine]
cpu = 100
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


class Answer:
    """One installer question and what to do about it.

    pattern  regex matched against the end of the screen once the output is quiet
    reply    the text to type (Return is added), or a function (vm, match) that
             does what is needed (mount a floppy, eject) and returns the text
    last     True for the installer's final question: after it has been answered,
             the @ prompt means the installer has finished. Before it, a bare @
             (a mode job between two commands) is not taken as the end.
    """

    def __init__(self, pattern, reply, last=False):
        self.pattern = pattern
        self.reply = reply
        self.last = last


COMMAND_PROMPT = r"\n@ ?$"


# --- ND's standard installer (IN-<product>-XX-<ver><rev>:PROG) ------------------
# The Linkage-Loader, LINKER and Backup Manager installers are one framework: a
# main menu of five modules ("These modules must be executed in ascending order
# from 1 to 5"), MESSAGE boxes ending in a "Press CR" line, and the same delete,
# floppy and parameter-file questions. The wording varies a little between
# products; every variant below was seen on the screen (2026-11-02).

class Modules:
    """Answers the main menu with 1, 2, 3, 4, 5 in turn. The menu coming back once
    more after 5 means a module failed; the run stops."""

    def __init__(self):
        self.left = ["1", "2", "3", "4", "5"]

    def __call__(self, vm, m):
        if not self.left:
            raise SystemExit("the installer's menu came back after module 5")
        module = self.left.pop(0)
        vm.note("menu: module %s" % module)
        return module


def eject_floppy(vm, m):
    vm.eject(0)
    return ""


def standard_answers(extra=()):
    """The answers every ND standard installer needs, with the product's own
    answers (`extra`) checked first. A fresh Modules() per call."""
    return list(extra) + [
        # "Remove floppy 210319H--01D from station 1 unit 0." in a MESSAGE box,
        # then '>> Press "CR" to continue' (Backup Manager: '>> Press CR ...').
        Answer(r'Remove floppy \S+ from station 1 unit 0\.[\s:]*>> Press "?CR"? to continue ?$',
               eject_floppy),
        Answer(r'>> Press "?CR"? to continue ?$', ""),
        # ">>Which module do you want to execute? (default:'1'): "
        # (Backup Manager: no question mark)
        Answer(r">>Which module do you want to execute\?? \(default:'\d'\): ?$", Modules()),
        # Module 1 of the Linkage-Loader and LINKER: the installer's own defaults,
        # DOMAIN-USER for the domain, UTILITY for the utility files.
        Answer(r">>On which user do you want to place the domain\? \(default:'DOMAIN-USER'\): ?$", ""),
        Answer(r">>On which user do you want to place the utility files\? \(default:'UTILITY'\): ?$", ""),
        # Module 2. A first install has no earlier version to delete:
        # ">>Do you want to delete the previous version of the product? (default:'Y'): "
        # ">>Do you want to delete the current version of the product (default:'Y'): "
        # (LINKER: no default, ends in ' : '; Backup Manager: no "the", no '?')
        Answer(r">>Do you want to delete (the )?previous version of the product\??"
               r"( \(default:'\w'\))? ?: ?$", "N"),
        Answer(r">>Do you want to delete (the )?current version of the product\??"
               r"( \(default:'\w'\))? ?: ?$", "N"),
        # Module 5: ">>Do you want to copy IN-NLL-XX-H02:INST to floppy number 1? (default:'N'): "
        # N: the distribution floppy stays as it came from the archive.
        Answer(r">>Do you want to copy \S+:INST to floppy number 1\?( \(default:'\w'\))? ?: ?$",
               "N", last=True),
    ]


# Lines that mean a step of the installer failed. The installers go on after them
# and still end "INSTALLATION - FINISHED", so the whole output is searched at the end.
# Each one was seen on the screen of a failed run (2026-11-02):
#   "NO MORE PAGES FOR THIS USER"    a file copy to a user without room
#   "Filename syntax error"          Backup Manager joining BM-OPERATOR-B02:DO02
#                                    to its :DOM when the :DOM was already there
#   "NOT READ ACCESS"                "Error when loading Control Store."
#   "Error when loading"
INSTALL_FAILED = r"NO MORE PAGES|Filename syntax error|NOT READ ACCESS|Error when loading"


def run_installer(vm, answers, step_timeout=900):
    """Answer the installer from `answers` until it is back at @ after the last one.
    A quiet screen that matches nothing is a pause ("Wait....") until step_timeout;
    then the run stops and shows the screen. Nothing is ever answered by a guess.
    At the end, any INSTALL_FAILED line in the output stops the run."""
    seen = ""
    everything = []
    quiet_for = 0.0
    finished = False
    while True:
        out = vm.settle(quiet=3, timeout=step_timeout)
        everything.append(out)
        seen = (seen + out)[-3000:]
        tail = seen.rstrip("\r\n")
        for a in answers:
            m = re.search(a.pattern, tail, re.S | re.I)
            if m:
                reply = a.reply(vm, m) if callable(a.reply) else a.reply
                vm.sendline(reply)
                finished = finished or a.last
                seen, quiet_for = "", 0.0
                break
        else:
            if finished and re.search(COMMAND_PROMPT, "\n" + tail):
                bad = [line.strip() for line in "".join(everything).splitlines()
                       if re.search(INSTALL_FAILED, line, re.I)]
                if bad:
                    raise SystemExit("the installer finished, but a step failed:\n  "
                                     + "\n  ".join(bad))
                return
            quiet_for += 3 if not out else 0
            if quiet_for > step_timeout:
                raise ExpectTimeout("no known question or @ prompt for %ss; the screen ends:\n%s"
                                    % (step_timeout, seen[-800:]))


def machine(exe, ini, work, name):
    """The machine, not started yet (the `with` block starts and stops it).
    boot=None: the boot device comes from [boot] in the INI."""
    return Nd100x(exe=exe, config=ini, boot=None, echo=False,
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
    vm.command("ENTER-DIRECTORY", until=Regex(r"DIRECTORY NAME: ?$"))
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
    ap.add_argument("--step-timeout", type=float, default=900)
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


def start_nd5000(vm):
    """Start the ND-5000 as SYSTEM, the way ND's (SYSTEM)LOAD-MODE:MODE does at every
    boot (ND-896058-2, section 14):

        @ND-500-MONITOR
        LOAD-CONTROL-STORE,,,,
        START-SWAPPER
        EXIT

    The packs these scripts run on have no LOAD-MODE, so nothing starts the ND-5000
    at boot, and the first program to use it loads the control store itself, as
    whoever runs it. The installers switch to DOMAIN-USER before they run the
    Linkage-Loader, and DOMAIN-USER has no read access to (SYSTEM)CONTROL-STORE:DATA:
    the monitor prints "Error when loading Control Store. NOT READ ACCESS", the
    domain is not copied, and the installer still ends "INSTALLATION - FINISHED".
    Measured 2026-11-02: the same domain copy as DOMAIN-USER works after this start."""
    vm.sendline("ND-500-MONITOR")
    vm.expect(Regex(r"ND-5000?: ?$"), timeout=120, abort=Regex(r"NO SUCH FILE NAME"))
    out = vm.command("LOAD-CONTROL-STORE,,,,", until=Regex(r"ND-5000?: ?$"), timeout=300)
    out += vm.command("START-SWAPPER", until=Regex(r"ND-5000?: ?$"), timeout=300)
    vm.command("EXIT", timeout=60)
    if re.search(r"error|NOT READ|NO SUCH", out, re.I) or "Loading Swapper" not in out:
        raise SystemExit("could not start the ND-5000:\n%s" % out)
    vm.note("ND-5000 started as SYSTEM (control store loaded, swapper started)")


def user_stat(vm, user):
    """@USER-STAT for `user`: returns (pages used, pages given), or None when the
    user does not exist."""
    vm.command("USER-STAT", until=Regex(r"USER NAME: ?$"))
    out = vm.command(user, until=Regex(r"OUTPUT FILE: ?$|\n@ ?$"))
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
    vm.expect(Regex(r"ND-5000?: ?$"), timeout=120, abort=Regex(r"NO SUCH FILE NAME"))
    out = vm.command("LIST-STANDARD-DOMAINS", until=Regex(r"ND-5000?: ?$"), timeout=60)
    vm.command("EXIT", timeout=60)
    return out


def check_standard_domain(listing, name, segment_file, what):
    """`name` must be listed as initialized, with `segment_file` as one of its segments."""
    m = re.search(r"^%s +\(Standard domain is initialized\)\s*\n((?:\s+\S.*\n?)*)"
                  % re.escape(name), listing, re.M)
    if not m or segment_file not in m.group(1):
        raise SystemExit("FAIL (%s): standard domain %s on %s is not there:\n%s"
                         % (what, name, segment_file, listing))


def install_product(a, prefix, floppy_path, installer, answers, check, needs=(), before=None,
                    between=None):
    """The whole run for a product: copy, boot, log in, check `needs` in
    LIST-REENTRANT, call before(vm, work) if given, enter the floppy in unit 0, run
    `installer` with `answers`, then check(vm, what) after the install and again
    after a second boot, then deliver.
    between(pack), if given, runs while no emulator has the pack open: after the
    install run has stopped and before the second boot, so the second boot and its
    check run on what it changed.
    When `needs` has ND-500-MONITOR, the ND-5000 is started as SYSTEM after each
    boot first (start_nd5000), as LOAD-MODE would.
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
        run_installer(vm, answers, a.step_timeout)
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
