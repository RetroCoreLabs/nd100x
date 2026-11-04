#!/usr/bin/env python3
"""Install the ND LINKER B01 (ND 211224B01) with ND's own installer.

    python3 tools/installs/linker-b01/install.py SOURCE.IMG RESULT.IMG

Needs on the pack: BACKUP-SYSTEM and the reentrant ND-500-MONITOR
(tools/installs/nd500-monitor-reentrant): the installer defines the standard
domain LINKER in the ND-500 monitor.

SOURCE.IMG is never opened by the emulator; only a run that passes every check is
moved to RESULT.IMG. The floppy comes from the software archive, checked by md5.
"""

import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from install_common import (COMMAND_PROMPT, COPY_INST_Q, DELETE_CURRENT_Q,  # noqa: E402
                            DELETE_PREVIOUS_Q, DOMAIN_USER_Q, REMOVE_FLOPPY, UTILITY_Q,
                            Menu, Regex, State, When, arguments, check_standard_domain,
                            eject_floppy, floppy, install_product, standard_domains,
                            standard_start)

FLOPPY_MD5 = "1097cc3b230ef5b97582962d842e361d"     # archive entry 211224B01-XX-01D
INSTALLER = "(211224B01-XX-01D:FLOPPY-USER)IN-LINK-XX-B01:PROG"
NDL_PROMPT = r"NDL(\(\w+\))?:"


def dialogue():
    """The installer's dialogue, state by state, as on the screen of a real run
    (transcript of 2026-11-04). The same framework as the Linkage-Loader's, but the
    module 2 and 5 questions have no default and end in ' : ', and module 4 goes back
    to the menu straight after the floppy box."""
    menu = Menu({1: "m1-domain", 2: "m2-previous", 3: "menu", 4: "m4-floppy", 5: "m5-inst"})
    return [
        standard_start(menu),
        menu.state(),
        State("m1-domain", [When(DOMAIN_USER_Q, "", "m1-utility")]),
        State("m1-utility", [When(UTILITY_Q, "", "menu")]),
        State("m2-previous", [When(DELETE_PREVIOUS_Q, "N", "m2-current")]),
        State("m2-current", [When(DELETE_CURRENT_Q, "N", "menu")]),
        # Module 4 copies 15 files (about a minute), then asks for the floppy.
        State("m4-floppy", [When(REMOVE_FLOPPY, eject_floppy, "menu")]),
        # Module 5: the :INST question, a note on UE-ERMSG-EN-C:ERR and
        # DDBTABLES-G:VTM, then @.
        State("m5-inst", [When(COPY_INST_Q, "N", "end")]),
        State("end", [When(COMMAND_PROMPT, done=True)]),
    ]


def check(vm, what):
    """LINKER is a standard domain on (DOMAIN-USER)LINKER-B01:DOM, and @LINKER runs it:
    "- ND LINKER, Version B01 ..." and the NDL: prompt."""
    check_standard_domain(standard_domains(vm), "LINKER",
                          "(PACK-ONE:DOMAIN-USER)LINKER-B01:DOM", what)
    vm.sendline("LINKER")
    # The LINKER asks for the terminal type when the terminal has none set
    # ("Terminal type 000 is unknown." and a list, then "What is your terminal
    # type?"). The script's console is a plain text stream with no screen
    # control, which is ND's type 2, "Teletype ASR 33".
    # The prompt is "NDL:", or "NDL(ADV):" once the LINKER has run its start-up
    # command SET-ADVANCED-MODE, which it does by itself (seen 2026-11-02).
    got = vm.expect([Regex(NDL_PROMPT), Regex(r"What is your terminal type\?")],
                    timeout=300, abort=Regex(r"NO SUCH|error"))
    if "terminal type" in got:
        vm.sendline("2")
        vm.expect(Regex(NDL_PROMPT), timeout=300, abort=Regex(r"NO SUCH|error"))
    m = re.search(r"ND LINKER, Version (\S+)", vm.before()[-600:])
    vm.command("EXIT", timeout=60)
    if not m:
        raise SystemExit("FAIL (%s): @LINKER printed no version line" % what)
    vm.note("%s: LINKER standard domain, runs, version %s" % (what, m.group(1)))
    return m.group(1)


def main():
    a = arguments(__doc__)
    first, second = install_product(
        a, "install-linker-b01-", floppy(FLOPPY_MD5), INSTALLER, dialogue, check,
        needs=("BACKUP-SYSTEM", "ND-500-MONITOR"))
    print("PASS: LINKER %s installed and runs; after a second boot: %s." % (first, second))


if __name__ == "__main__":
    main()
