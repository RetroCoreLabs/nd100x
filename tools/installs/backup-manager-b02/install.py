#!/usr/bin/env python3
"""Install the Backup Manager B02 (ND 211226B02) on a SINTRAN pack with ND's own installer.

    python3 tools/installs/backup-manager-b02/install.py SOURCE.IMG RESULT.IMG

Needs on the pack, in this order (tools/installs/README.md): BACKUP-SYSTEM I05, the
reentrant ND-500-MONITOR, the Linkage-Loader and the LINKER. The installer checks
for BACKUP-SYSTEM, LINKER and LINKAGE-LOADER itself, defines four ND-500 standard
domains with the monitor, and links BM-Operator with the LINKER.

SOURCE.IMG is never opened by the emulator: the run works on a copy, and only a
run that passes every check is moved to RESULT.IMG. The six floppies are downloaded
from the RetroCoreLabs software archive and checked by md5.

Any question the script does not know stops the run with the screen shown.
"""

import os
import re
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bm_parameter import MAX_NO_COMPARE, fix_no_compare, swap_file_names  # noqa: E402
from install_common import (Answer, Regex, arguments, check_standard_domain,  # noqa: E402
                            enter_floppy, floppy, install_product, make_room,
                            standard_answers, standard_domains, user_stat)

# The six floppies of the B02 set, by the directory name SINTRAN shows for each
# (the label inside the image) and the md5 of the archive copy.
FLOPPIES = {
    "211226B02-XX-01D": "d91616fbc2dfb42c4cacd61141c2b529",   # installer, job database, server
    "211226B02-XX-02D": "b5b2c0e6c5265a212ff520dcae6b2a51",   # BM-FILERE-B02:DOM
    "211226B02-XX-03D": "58bc43dc1c8e70664f6ddacbc80772a1",   # BM-OPERATOR-B02:DOM
    "211226B02-XX-04D": "4e2d509bf12ca1fdcebe24acfc3f5679",   # BM-DEFINI, BM-SCHED, BM-OPERATOR part 2
    "211226B02-EN-01D": "9ceed9f8b02aaa1ad44df78f0ec39d48",   # English screen texts (:SEG)
    "211226B02-EN-02D": "8216d1a692387f1cbd3371ca845007ca",   # English screen texts (:DATA)
}
FIRST = "211226B02-XX-01D"
INSTALLER = "(%s:FLOPPY-USER)IN-BCKMAN-XX-B02:PROG" % FIRST

# ND-OPERATIONS gets the job database BM-JOBDB-B02:DATA and BM-PARAMETER-B02:DATA.
# The installer creates the user with 20 pages (PAGES_NEED_2 in its own script
# IN-BCKMAN-XX-B02:INIT) and then cannot fit the files: the job database takes 34
# pages, BM-PARAMETER-B02:DATA fails with "NO MORE PAGES FOR THIS USER", and the
# installer still ends "INSTALLATION - FINISHED". Measured on a run with room:
# "37 PAGES USED" (2026-11-02). When the user exists, the installer does not
# create it, so the script makes the room first.
ND_OPERATIONS_PAGES = 40

WORK_FLOPPIES = {}      # directory name -> copy in the work folder (filled by copy_floppies)


def copy_floppies(vm, work):
    """Copies of the floppies in the work folder: SINTRAN may write to a floppy."""
    for name, md5 in FLOPPIES.items():
        WORK_FLOPPIES[name] = os.path.join(work, name + ".img")
        shutil.copyfile(floppy(md5), WORK_FLOPPIES[name])
    for user in ("SYSTEM", "BACKUP-MANAGER"):
        s = user_stat(vm, user)
        vm.note("USER-STAT %s before: %s" % (user, "%d pages used out of %d" % s if s else "no such user"))
    make_room(vm, "ND-OPERATIONS", ND_OPERATIONS_PAGES)
    copy_message_file(vm, work)


# B02 needs a User Environment message file (SYSTEM)UE-ERMSG-EN-D..:ERR, but neither
# brings one nor checks for it. Without it the first COPY in BM-OPERATOR stops with
# "An internal error has occurred ... Library UE-ERMSG-EN-D' not found" (measured
# 2026-11-04 on a pack built by these scripts; the C05 file BACKUP-SYSTEM I05 brings is
# not taken). ND fixed it in a later version: "The installation program did not check
# the precence of correct UE-ERRMSG-file. This is now delivered along with the product"
# (NDInsight Installation/Installation-Description/ND-895055-1A-EN.md, the B04 note).
# The file comes from the File Manager C03 floppy in the archive; a pack with the D05
# file copied from it ran a COPY (2026-11-04).
MESSAGE_FLOPPY = "211075C03-EN-01D"
MESSAGE_FLOPPY_MD5 = "be8441322f2c65076c48ecc3b4468eda"
MESSAGE_FILE = "UE-ERMSG-EN-D05:ERR"


def copy_message_file(vm, work):
    """Copy MESSAGE_FILE to SYSTEM from the File Manager floppy, the way an operator
    would: mount it in unit 0, @ENTER-DIRECTORY, @COPY-FILE, @RELEASE-DIRECTORY, then
    put the installer floppy back in unit 0. Nothing is done when SYSTEM has a
    UE-ERMSG-EN-D file already."""
    have = vm.command("LIST-FILES (SYSTEM)UE-ERMSG-EN-D,,", timeout=60)
    if re.search(r"UE-ERMSG-EN-D\d\d:ERR", have):
        vm.note("SYSTEM already has a UE-ERMSG-EN-D message file")
        return
    copy = os.path.join(work, MESSAGE_FLOPPY + ".img")
    shutil.copyfile(floppy(MESSAGE_FLOPPY_MD5), copy)
    vm.mount(0, copy)
    enter_floppy(vm, 0)
    out = vm.command('COPY-FILE "(SYSTEM)%s",(%s:FLOPPY-USER)%s'
                     % (MESSAGE_FILE, MESSAGE_FLOPPY, MESSAGE_FILE), timeout=300)
    vm.command("RELEASE-DIRECTORY %s" % MESSAGE_FLOPPY, timeout=60)
    vm.mount(0, os.path.join(work, "floppy0.img"))
    listed = vm.command("LIST-FILES (SYSTEM)UE-ERMSG-EN-D,,", timeout=60)
    if re.search(r"NO |ILLEGAL|ERROR", out) or MESSAGE_FILE not in listed:
        raise SystemExit("FAIL: could not copy %s to SYSTEM:\n%s\n%s" % (MESSAGE_FILE, out, listed))
    vm.note("%s copied to SYSTEM from %s" % (MESSAGE_FILE, MESSAGE_FLOPPY))


def change_floppy(vm, m):
    """The installer asked for floppy m.group('name'): put that one in unit 0."""
    name = m.group("name").upper()
    if name not in WORK_FLOPPIES:
        raise SystemExit("the installer asks for floppy %r, which is not in the B02 set" % name)
    vm.mount(0, WORK_FLOPPIES[name])
    return ""


# The Backup Manager's own questions, as seen on the screen (2026-11-02). The
# menu, the "Press CR" boxes, the delete questions and the last eject come from
# standard_answers(); these are checked first.
EXTRA = [
    # Module 4, between floppies, in a MESSAGE box:
    #   "Remove floppy 211226B02-XX-01D and insert
    #    floppy 211226B02-XX-02D into the floppy station 1 unit 0."
    #   ">> Press CR to continue"
    # The order is 01D, 02D, 03D, 04D, EN-01D. EN-02D is never asked for.
    Answer(r"Remove floppy \S+ and insert\s+floppy (?P<name>\S+) into the floppy station 1 unit 0\."
           r"[\s:]*>> Press CR to continue ?$", change_floppy),
    # Module 1: ">>Backup Manager can be installed for SIBAS/R backup
    #            >> (with SIBAS (R),without SIBAS (N)) (default:'R'): "
    # N: no SIBAS/R-B on the pack, so BM-Operator is linked with the stub
    # BM-SIBDUM-B02:NRF (mode file BM-DNOSIB-B02).
    Answer(r"\(with SIBAS \(R\),without SIBAS \(N\)\) \(default:'\w'\): ?$", "N"),
    # ">>Which language version do you want to install (default:'EN'): "
    # EN is the only language set there is (floppies EN-01D and EN-02D).
    Answer(r">>Which language version do you want to install \(default:'\w+'\): ?$", "EN"),
    # Module 3, for a user that does not exist yet: "Choose between the following
    # directories for the user BACKUP-MANAGER:" then ">>Give directory name : "
    # (a second time with "(default:'PACK-ONE')").
    Answer(r"for the user (ND-OPERATIONS|BACKUP-MANAGER):.*>>Give directory name"
           r"( \(default:'[\w-]+'\))? : ?$", "PACK-ONE"),
    # Module 5: ">>What will you do with the parameter file? : "
    # 3 = delete it on SYSTEM and do not copy it to floppy 1 (the floppy stays
    # as it came from the archive).
    Answer(r">>What will you do with the parameter file\? : ?$", "3", last=True),
]

# The four standard domains the installer defines, each with its domain file.
DOMAINS = {
    "BM-OPERATOR": "(PACK-ONE:BACKUP-MANAGER)BM-OPERATOR-B02:DOM",
    "BM-DEFINITION": "(PACK-ONE:BACKUP-MANAGER)BM-DEFINI-B02:DOM",
    "BM-SCHEDULER": "(PACK-ONE:BACKUP-MANAGER)BM-SCHED-B02:DOM",
    "BM-FILERESTORE": "(PACK-ONE:BACKUP-MANAGER)BM-FILERE-B02:DOM",
}
ND_OPERATIONS_FILES = ("BM-JOBDB-B02:DATA", "BM-PARAMETER-B02:DATA")


def check(vm, what):
    """The four standard domains, the two ND-OPERATIONS files, the message file, and
    USER-STAT."""
    listing = standard_domains(vm)
    for name, segment_file in DOMAINS.items():
        check_standard_domain(listing, name, segment_file, what)
    messages = vm.command("LIST-FILES (SYSTEM)UE-ERMSG-EN-D,,", timeout=60)
    if not re.search(r"UE-ERMSG-EN-D\d\d:ERR", messages):
        raise SystemExit("FAIL (%s): no (SYSTEM)UE-ERMSG-EN-D..:ERR\n%s" % (what, messages))
    files = vm.command("LIST-FILES (ND-OPERATIONS),,", timeout=60)
    missing = [f for f in ND_OPERATIONS_FILES if not re.search(re.escape(f), files)]
    if missing:
        raise SystemExit("FAIL (%s): not on ND-OPERATIONS: %s\n%s" % (what, ", ".join(missing), files))
    stats = []
    for user in ("SYSTEM", "ND-OPERATIONS", "BACKUP-MANAGER"):
        used, given = user_stat(vm, user)
        stats.append("%s %d/%d" % (user, used, given))
    vm.note("%s: 4 standard domains, ND-OPERATIONS files there; pages used: %s"
            % (what, ", ".join(stats)))
    if what == "after install":
        SWAP_FILES[:] = swap_files(vm)
        vm.note("ND-500 swap files: %s" % ", ".join(SWAP_FILES))
    return ", ".join(stats)


SWAP_FILES = []         # filled by check() after the install, used by fix_parameter_file()
PARAMETER_FILE = "ND-OPERATIONS/BM-PARAMETER-B02:DATA"


def swap_files(vm):
    """Ask the ND-500 monitor for every swap file: LIST-SWAP-FILE-INFO 0, 1, ... until a
    number has none. The answer has 'Swap file name:..... (PACK-ONE:SYSTEM)NAME:TYPE;1'."""
    nd5 = Regex(r"ND-5000?: ?$")
    vm.sendline("ND-500-MONITOR")
    vm.expect(nd5, timeout=120)
    names = []
    for n in range(MAX_NO_COMPARE):
        vm.command("LIST-SWAP-FILE-INFO", until=Regex(r"number: ?$"), timeout=60)
        found = swap_file_names(vm.command(str(n), until=nd5, timeout=120))
        if not found:
            break
        names += found
    vm.command("EXIT", timeout=60)
    if not names:
        raise SystemExit("FAIL: LIST-SWAP-FILE-INFO 0 shows no swap file")
    return names


def fix_parameter_file(pack):
    """Make the IMAGE-NO-COMPARE-FILE lines of BM-PARAMETER-B02:DATA name the swap files
    this pack has (bm_parameter.py says why), then read the file back from the pack."""
    work = tempfile.mkdtemp(prefix="bm-parameter-")
    try:
        subprocess.run(["ndtool", "-x", "-o", work, "-F", "*/BM-PARAMETER-B02:DATA", pack],
                       check=True, capture_output=True)
        path = os.path.join(work, "BM-PARAMETER-B02.DATA")
        raw = open(path, "rb").read()
        listing = subprocess.run(["ndtool", "-t", pack], check=True, capture_output=True,
                                 text=True).stdout
        pack_files = re.findall(r"\([^)\s]+\)[^\s;]+;\d+", listing)
        new = fix_no_compare(raw, SWAP_FILES, pack_files)
        if new == raw:
            print("BM-PARAMETER-B02:DATA: the IMAGE-NO-COMPARE-FILE lines already match the pack")
            return
        open(path, "wb").write(new)
        subprocess.run(["ndtool", "-f", "--put", path, PARAMETER_FILE, pack], check=True,
                       capture_output=True)
        os.remove(path)
        subprocess.run(["ndtool", "-x", "-o", work, "-F", "*/BM-PARAMETER-B02:DATA", pack],
                       check=True, capture_output=True)
        if open(path, "rb").read() != new:
            raise SystemExit("FAIL: BM-PARAMETER-B02:DATA did not land on the pack")
        print("BM-PARAMETER-B02:DATA: IMAGE-NO-COMPARE-FILE lines now: %s" % ", ".join(
            line.split(b"=", 1)[1].strip().decode() for line in new.split(b"\r\n")
            if line.strip().startswith(b"IMAGE-NO-COMPARE-FILE")))
    finally:
        shutil.rmtree(work, ignore_errors=True)


def main():
    a = arguments(__doc__)
    first, second = install_product(
        a, "install-bm-b02-", floppy(FLOPPIES[FIRST]), INSTALLER, standard_answers(EXTRA), check,
        needs=("BACKUP-SYSTEM", "ND-500-MONITOR", "LINKAGE-LOADER", "LINKER"),
        before=copy_floppies, between=fix_parameter_file)
    print("PASS: Backup Manager B02 installed (pages used: %s); after a second boot: %s."
          % (first, second))


if __name__ == "__main__":
    main()
