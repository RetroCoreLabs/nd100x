#!/usr/bin/env python3
"""Make the ND-500/5000 monitor the reentrant subsystem ND-500-MONITOR (ND 211305, step 2.1).

    python3 tools/installs/nd500-monitor-reentrant/install.py SOURCE.IMG RESULT.IMG

ND's install description for the ND-500/5000 System Package (ND-895030-1A, version L;
ND-895030-2, version M), section "ND-500/5000 Monitor":

    @DUMP-PROGRAM-REENTRANT
    NAME: ND-500-MONITOR
    FILE NAME: (<user-name>)ND-500-MON-J<rev>:PROG,N5MON

Every ND product installer that defines a standard domain (Linkage-Loader, LINKER,
Backup Manager) starts the monitor as @ND-500-MONITOR. Without this step SINTRAN
answers "NO SUCH FILE NAME", the installer's ND-500 commands go to SINTRAN instead,
and the installer still says "INSTALLATION - FINISHED" with no domain copied.

SOURCE.IMG is never opened by the emulator; only a run that passes every check is
moved to RESULT.IMG. No floppy is needed: the monitor program is already on the pack.
"""

import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from install_common import (Regex, arguments, deliver, logged_in, machine,  # noqa: E402
                            prepare_pack)


def check(vm, what):
    """LIST-REENTRANT must show ND-500-MONITOR, and @ND-500-MONITOR must start it."""
    out = vm.command("LIST-REENTRANT", timeout=60)
    if not re.search(r"\bND-500-MONITOR\b", out):
        raise SystemExit("FAIL (%s): LIST-REENTRANT has no ND-500-MONITOR:\n%s" % (what, out))
    vm.sendline("ND-500-MONITOR")
    vm.expect(Regex(r"ND-5000?: ?$"), timeout=120, abort=Regex(r"NO SUCH|NOT FOUND"))
    m = re.search(r"ND-500/5000 MONITOR +Version +(\S+)", vm.before()[-400:])
    vm.command("EXIT", timeout=60)
    if not m:
        raise SystemExit("FAIL (%s): no monitor version line" % what)
    vm.note("%s: ND-500-MONITOR reentrant, monitor version %s" % (what, m.group(1)))
    return m.group(1)


def main():
    a = arguments(__doc__, lambda ap: ap.add_argument(
        "--monitor-file", default="(SYSTEM)ND-500-MON-J:PROG",
        help="the monitor program on the pack (default: %(default)s, as on nd100x ND5000.IMG)"))
    work, pack, ini = prepare_pack(a, "install-n5mon-")

    with machine(a.nd100x, ini, work, "install") as vm:
        logged_in(vm)
        out = vm.command("LIST-FILES %s,," % a.monitor_file, timeout=60)
        if "FILE " not in out:
            raise SystemExit("%s is not on the pack:\n%s" % (a.monitor_file, out))
        if re.search(r"\bND-500-MONITOR\b", vm.command("LIST-REENTRANT", timeout=60)):
            raise SystemExit("ND-500-MONITOR is already reentrant on this pack; nothing to do")
        vm.command("DUMP-PROGRAM-REENTRANT", until=Regex(r"NAME: ?$"))
        vm.command("ND-500-MONITOR", until=Regex(r"FILE NAME: ?$"))
        out = vm.command("%s,N5MON" % a.monitor_file, timeout=120)
        if re.search(r"NO SUCH|ILLEGAL|NOT ", out):
            raise SystemExit("DUMP-PROGRAM-REENTRANT failed:\n%s" % out)
        first = check(vm, "after install")

    with machine(a.nd100x, ini, work, "reboot") as vm:
        logged_in(vm)
        second = check(vm, "after a second boot")

    deliver(pack, a.result)
    print("PASS: ND-500-MONITOR is reentrant (monitor %s); still there after a second boot (%s)."
          % (first, second))
    print("Result: %s   (logs and transcripts in %s)" % (a.result, work))


if __name__ == "__main__":
    main()
