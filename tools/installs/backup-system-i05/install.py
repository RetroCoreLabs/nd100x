#!/usr/bin/env python3
"""Install BACKUP-SYSTEM I05 (ND 210337I05) on a SINTRAN pack with ND's own installer.

    python3 tools/installs/backup-system-i05/install.py SOURCE.IMG RESULT.IMG

SOURCE.IMG is never opened by the emulator: the run works on a copy, and only a
run that passes every check is moved to RESULT.IMG. RESULT.IMG must not exist.
The I05 floppy is downloaded from the RetroCoreLabs software archive and checked
by md5, unless --floppy names a local copy (which is checked the same way).

What it does, in order (README.md beside this file has the details):
  1. boots SOURCE's copy as an ND-100 with one ND-5000 on the octobus,
  2. logs in as SYSTEM, enters the floppy as a directory and runs
     (210337I05-XX-01D:FLOPPY-USER)INST-BASY-I05:PROG, answering its questions
     the way ND's install notes say,
  3. checks LIST-REENTRANT and the @BACKUP-SYSTEM banner (must say I05),
  4. boots the result a second time and checks the same two things again, so
     a pack that only works until the next boot is never handed out.
Any question the script does not know stops the run with the screen shown.
"""

import argparse
import hashlib
import os
import re
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
sys.path.insert(0, os.path.join(REPO, "tools"))
from nd100x_expect import (Nd100x, Regex, ExpectTimeout, fetch_catalog_image,  # noqa: E402
                           _md5_of_file)

# The floppy: archive entry "210337I05-XX-01D", label "PART 1 OF 1",
# product/release date 881206. The archive holds a second, different image under
# the same directory name, so this one is pinned by md5.
FLOPPY_MD5 = "395982991324b9425ecceaf71552f266"
FLOPPY_DIR = "210337I05-XX-01D"
INSTALLER = "(%s:FLOPPY-USER)INST-BASY-I05:PROG" % FLOPPY_DIR

# The machine: config/ND5000.ini (ND-100, MFbus, octobus, one ND-5000 at station
# 070B) without telnet, so two runs cannot fight over a port.
MACHINE_INI = """# Written by tools/installs/backup-system-i05/install.py from config/ND5000.ini.
[machine]
cpu = 100
[controller.smd.0]
enabled = yes
disk0 = {pack}
[controller.floppy.0]
enabled = yes
disk0 = {floppy}
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

# The installer's questions, as seen on the screen of a real run (2026-11-02),
# and the answer ND's install notes give. Matched against the end of the output
# once it has gone quiet. None = take the floppy out (eject) and then press CR.
ANSWERS = [
    # "Do you want to delete previous version of the BACKUP-SYSTEM if it exist ?
    #  User SYSTEM, UTILITY and BPUN will be checked (Y/N): "
    (r"delete previous version.*\(Y/N\) ?: ?$", "Y"),
    # "...BACKUP-SYS-I:PROG and BACKUP-SERV-I:PROG copied to (default: UTILITY) : "
    (r"\(default: UTILITY\) ?: ?$", ""),
    # "Should the BACKUP-SYSTEM be dumped reentrant (Y/N) ? "
    (r"dumped reentrant \(Y/N\) ?\? ?$", "Y"),
    # 'Remove floppy 210337I05-XX-01D from station 1 unit 0 and type "CR"'
    (r'Remove floppy \S+ from station 1 unit 0 and type "CR" ?$', None),
    # After the note that termination handling was reset to blanks:
    # '>> Press "RETURN" to continue.'
    (r'Press "RETURN" to continue\. ?$', ""),
]
COMMAND_PROMPT = r"\n@ ?$"


def run_installer(vm, step_timeout):
    """Answer the installer until it has released the floppy and is back at @.
    The installer's last question is the floppy removal, so a bare @ seen before
    that (a mode job between two commands) is not taken as the end."""
    seen = ""
    waited = 0.0
    floppy_out = False
    while True:
        out = vm.settle(quiet=3, timeout=step_timeout)
        seen = (seen + out)[-2000:]
        tail = seen.rstrip("\r\n")
        for pat, reply in ANSWERS:
            if re.search(pat, tail, re.S | re.I):
                if reply is None:
                    vm.eject(0)
                    floppy_out = True
                    reply = ""
                vm.sendline(reply)
                seen, waited = "", 0.0
                break
        else:
            if floppy_out and re.search(COMMAND_PROMPT, "\n" + tail):
                return
            # Quiet but not a known question: a "Wait...." pause, or a question
            # this script has never seen. Keep waiting until step_timeout.
            waited += 3 if not out else 0
            if waited > step_timeout:
                raise ExpectTimeout("no known question or @ prompt for %ss; the screen ends:\n%s"
                                    % (step_timeout, seen[-600:]))


def check_installed(vm, what):
    """LIST-REENTRANT must hold BACKUP-SYSTEM-I and @BACKUP-SYSTEM must say I05."""
    out = vm.command("LIST-REENTRANT", timeout=60)
    if "BACKUP-SYSTEM-I" not in out:
        raise SystemExit("FAIL (%s): LIST-REENTRANT has no BACKUP-SYSTEM-I:\n%s" % (what, out))
    vm.sendline("BACKUP-SYSTEM")
    vm.expect(Regex(r"Ba-sy: ?$"), timeout=120, abort=Regex(r"NO SUCH|NOT FOUND|ERROR"))
    banner = vm.before()[-400:]
    # I05 prints "BACKUP-SYSTEM ver I05 881107"; I02 printed "BACKUP-SYSTEM / I02  870827".
    m = re.search(r"BACKUP-SYSTEM +(?:ver|/) *(\S+) +(\d+)", banner)
    if not m or not m.group(1).startswith("I05"):
        raise SystemExit("FAIL (%s): @BACKUP-SYSTEM does not say I05:\n%s" % (what, banner))
    vm.command("EXIT", timeout=60)
    vm.note("%s: BACKUP-SYSTEM / %s %s, reentrant entry present" % (what, m.group(1), m.group(2)))
    return "%s %s" % (m.group(1), m.group(2))


def machine(exe, ini, work, name):
    """The machine, not started yet (the `with` block starts and stops it).
    boot=None: the boot device comes from [boot] in the INI."""
    return Nd100x(exe=exe, config=ini, boot=None, echo=False,
                  log_file=os.path.join(work, name + "-emulator.log"),
                  log_keep=r"ERROR|FATAL|pipe",
                  transcript=os.path.join(work, name + "-console.log"))


def logged_in(vm):
    """Wait for SINTRAN, get ENTER with ESC and log in as SYSTEM (empty password)."""
    vm.expect("RUNNING", timeout=600)
    vm.escape()
    vm.login("SYSTEM")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("source", help="SINTRAN pack to install on (only a copy is used)")
    ap.add_argument("result", help="where the installed pack goes; must not exist")
    ap.add_argument("--floppy", help="local copy of the I05 floppy instead of downloading it")
    ap.add_argument("--nd100x", default=os.path.join(REPO, "build", "bin", "nd100x"))
    ap.add_argument("--work", help="folder for the copies and logs (default: a new temp folder)")
    ap.add_argument("--step-timeout", type=float, default=900)
    a = ap.parse_args()

    if os.path.exists(a.result):
        raise SystemExit("%s exists; give a new file name" % a.result)
    if os.path.abspath(a.source) == os.path.abspath(a.result):
        raise SystemExit("source and result must differ")
    if a.floppy:
        if _md5_of_file(a.floppy) != FLOPPY_MD5:
            raise SystemExit("%s is not the I05 floppy (md5 should be %s)" % (a.floppy, FLOPPY_MD5))
        floppy = a.floppy
    else:
        floppy = fetch_catalog_image(FLOPPY_MD5, os.path.expanduser("~/.cache/nd100x/images"))

    work = a.work or tempfile.mkdtemp(prefix="install-basy-i05-")
    os.makedirs(work, exist_ok=True)
    pack = os.path.join(work, "pack.img")
    fl0 = os.path.join(work, "floppy.img")
    shutil.copyfile(a.source, pack)
    shutil.copyfile(floppy, fl0)          # SINTRAN may write to the floppy
    ini = os.path.join(work, "machine.ini")
    with open(ini, "w") as f:
        f.write(MACHINE_INI.format(pack=pack, floppy=fl0))
    print("work folder: %s" % work)

    with machine(a.nd100x, ini, work, "install") as vm:
        logged_in(vm)
        vm.command("ENTER-DIRECTORY", until=Regex(r"DIRECTORY NAME: ?$"))
        vm.answer("")
        vm.answer("FLOPPY-DISC-1")
        vm.answer("0")
        vm.sendline(INSTALLER)
        run_installer(vm, a.step_timeout)
        first = check_installed(vm, "after install")

    with machine(a.nd100x, ini, work, "reboot") as vm:
        logged_in(vm)
        second = check_installed(vm, "after a second boot")

    os.makedirs(os.path.dirname(os.path.abspath(a.result)), exist_ok=True)
    shutil.move(pack, a.result)
    print("PASS: BACKUP-SYSTEM / %s installed; still there after a second boot (%s)." % (first, second))
    print("Result: %s   (logs and transcripts in %s)" % (a.result, work))


if __name__ == "__main__":
    main()
