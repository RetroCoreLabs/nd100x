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

import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from install_common import (INSTALL_FAILED, Regex, State, When, arguments,  # noqa: E402
                            deliver, eject_floppy, enter_floppy, floppy, logged_in, machine,
                            prepare, run_dialogue)

# The floppy: archive entry "210337I05-XX-01D", label "PART 1 OF 1",
# product/release date 881206. The archive holds a second, different image under
# the same directory name, so this one is pinned by md5.
FLOPPY_MD5 = "395982991324b9425ecceaf71552f266"
FLOPPY_DIR = "210337I05-XX-01D"
INSTALLER = "(%s:FLOPPY-USER)INST-BASY-I05:PROG" % FLOPPY_DIR


# The installer's dialogue, state by state, as on the screen of a real run
# (transcript of 2026-11-04), with the answers ND's install notes give. It asks the
# five questions in this order and has no branches. "Batch input error / No such
# file name" before the first question is SINTRAN's (no LOAD-MODE batch file) and
# the installer's own "The message: NO SUCH FILE NAME is ok !" note; both are
# ignored like any other text.
DIALOGUE = [
    # "Do you want to delete previous version of the BACKUP-SYSTEM if it exist ?
    #  User SYSTEM, UTILITY and BPUN will be checked (Y/N): "
    State("delete-old", [When(r"delete previous version.*\(Y/N\) ?:", "Y", "user")]),
    # "...BACKUP-SYS-I:PROG and BACKUP-SERV-I:PROG copied to (default: UTILITY) : "
    # after "--> Wait approx. 1 - 2 min." while it deletes.
    State("user", [When(r"\(default: UTILITY\) ?:", "", "reentrant")]),
    # "Should the BACKUP-SYSTEM be dumped reentrant (Y/N) ? "
    State("reentrant", [When(r"dumped reentrant \(Y/N\) ?\?", "Y", "floppy")]),
    # 'Remove floppy 210337I05-XX-01D from station 1 unit 0 and type "CR"', after the
    # BACKUP-LOAD-I:MODE job that loads the DMA server.
    State("floppy", [When(r'Remove floppy \S+ from station 1 unit 0 and type "CR"',
                          eject_floppy, "return")]),
    # After the note that termination handling was reset to blanks:
    # '>> Press "RETURN" to continue.'
    State("return", [When(r'Press "RETURN" to continue\.', "", "end")]),
    # The two IMPORTANT notes, "<< Installation finished >>" and @.
    State("end", [When(r"<< Installation finished >>\s*@ ?", done=True)]),
]


def check_installed(vm, what):
    """LIST-REENTRANT must hold BACKUP-SYSTEM-I and @BACKUP-SYSTEM must say I05."""
    out = vm.command("LIST-REENTRANT", timeout=60)
    if "BACKUP-SYSTEM-I" not in out:
        raise SystemExit("FAIL (%s): LIST-REENTRANT has no BACKUP-SYSTEM-I:\n%s" % (what, out))
    vm.sendline("BACKUP-SYSTEM")
    vm.expect(Regex(r"Ba-sy:"), timeout=120, abort=Regex(r"NO SUCH|NOT FOUND|ERROR"))
    banner = vm.before()[-400:]
    # I05 prints "BACKUP-SYSTEM ver I05 881107"; I02 printed "BACKUP-SYSTEM / I02  870827".
    m = re.search(r"BACKUP-SYSTEM +(?:ver|/) *(\S+) +(\d+)", banner)
    if not m or not m.group(1).startswith("I05"):
        raise SystemExit("FAIL (%s): @BACKUP-SYSTEM does not say I05:\n%s" % (what, banner))
    vm.command("EXIT", timeout=60)
    vm.note("%s: BACKUP-SYSTEM %s %s, reentrant entry present" % (what, m.group(1), m.group(2)))
    return "%s %s" % (m.group(1), m.group(2))


def main():
    a = arguments(__doc__, lambda ap: ap.add_argument(
        "--floppy", help="local copy of the I05 floppy instead of downloading it"))
    work, pack, ini = prepare(a, "install-basy-i05-", floppy(FLOPPY_MD5, a.floppy))

    with machine(a.nd100x, ini, work, "install") as vm:
        logged_in(vm)
        enter_floppy(vm, 0)
        vm.sendline(INSTALLER)
        run_dialogue(vm, DIALOGUE, fail=INSTALL_FAILED, silence=a.silence, total=a.total)
        first = check_installed(vm, "after install")

    with machine(a.nd100x, ini, work, "reboot") as vm:
        logged_in(vm)
        second = check_installed(vm, "after a second boot")

    deliver(pack, a.result)
    print("PASS: BACKUP-SYSTEM %s installed; still there after a second boot (%s)." % (first, second))
    print("Result: %s   (logs and transcripts in %s)" % (a.result, work))


if __name__ == "__main__":
    main()
