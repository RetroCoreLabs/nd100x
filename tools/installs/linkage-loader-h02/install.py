#!/usr/bin/env python3
"""Install the ND Linkage-Loader H02 (ND 210319H02) with ND's own installer.

    python3 tools/installs/linkage-loader-h02/install.py SOURCE.IMG RESULT.IMG

Needs on the pack: BACKUP-SYSTEM (the installer copies with it) and the reentrant
ND-500-MONITOR (tools/installs/nd500-monitor-reentrant): the installer copies the
domain by running the Linkage-Loader itself in the ND-500 monitor.

SOURCE.IMG is never opened by the emulator; only a run that passes every check is
moved to RESULT.IMG. The floppy comes from the software archive, checked by md5.
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from install_common import (Regex, arguments, check_standard_domain, floppy,  # noqa: E402
                            install_product, standard_answers, standard_domains)

FLOPPY_MD5 = "016307c989e20c2997e68723f0298637"     # archive entry 210319H02-XX-01D
INSTALLER = "(210319H02-XX-01D:FLOPPY-USER)IN-NLL-XX-H02:PROG"


def check(vm, what):
    """LINKAGE-LOADER is a standard domain on (DOMAIN-USER)LINKAGE-LOAD-H02, and it runs:
    the ND-5000 loads it and it prints its version and the Nll: prompt."""
    check_standard_domain(standard_domains(vm), "LINKAGE-LOADER",
                          "(PACK-ONE:DOMAIN-USER)LINKAGE-LOAD-H02:PSEG", what)
    vm.sendline("ND-500-MONITOR")
    vm.expect(Regex(r"ND-5000?: ?$"), timeout=120)
    vm.sendline("LINKAGE-LOADER")
    vm.expect(Regex(r"Nll: ?$"), timeout=300, abort=Regex(r"error|No such", ))
    banner = vm.before()[-600:]
    version = banner.split("ND-Linkage-Loader -")[-1].split()[0] if "ND-Linkage-Loader -" in banner else "?"
    vm.command("EXIT", until=Regex(r"ND-5000?: ?$"), timeout=60)
    vm.command("EXIT", timeout=60)
    vm.note("%s: LINKAGE-LOADER standard domain, runs, version %s" % (what, version))
    return version


def main():
    a = arguments(__doc__)
    first, second = install_product(
        a, "install-nll-h02-", floppy(FLOPPY_MD5), INSTALLER, standard_answers(), check,
        needs=("BACKUP-SYSTEM", "ND-500-MONITOR"))
    print("PASS: Linkage-Loader %s installed and runs; after a second boot: %s." % (first, second))


if __name__ == "__main__":
    main()
