"""The IMAGE-NO-COMPARE-FILE lines of (ND-OPERATIONS)BM-PARAMETER-B02:DATA, made to match
the pack they are on.

The file ND ships names (SYSTEM)SWAP-FILE-0:SWAP and (SYSTEM)SEGFIL0:DATA: the swap and
segment files an image compare must skip, because they change while the copy runs. ND's
installation notes say the system supervisor edits these lines for the files the machine
really has (ND-895055-3A "Information and Hints"; ND-895560-2 "Defining Extra Swap-Files and
Segment-Files", which does it with @PED). Backup Manager B02 looks every listed file up when
BM-OPERATOR starts and prints error 33626 with the name of one that is not there
(measured 2026-11-04: the packs the nd100x install scripts build have the ND-500 swap file
(SYSTEM)SWAP-FILE:DATA, which LIST-SWAP-FILE-INFO 0 shows, and no SWAP-FILE-0:SWAP).

The file has no parity and ends each line with CR LF; both are kept.
"""

import re

KEY = b"IMAGE-NO-COMPARE-FILE"
# ND-896058-4 item 27: at most 12 files of type IMAGE-NO-COMPARE (5 before that fix).
MAX_NO_COMPARE = 12

_LINE = re.compile(rb"^(?P<lead>\s*IMAGE-NO-COMPARE-FILE\s*=\s*)(?P<name>\S+)(?P<rest>.*)$")


def short_name(name):
    """'(PACK-ONE:SYSTEM)SWAP-FILE:DATA;1' -> '(SYSTEM)SWAP-FILE:DATA': no directory name,
    no version, upper case. The form ND's examples use in the parameter file."""
    n = name.strip().upper()
    m = re.match(r"^\((?:[^:)]*:)?([^)]*)\)(.*)$", n)
    if m:
        n = "(%s)%s" % (m.group(1), m.group(2))
    return n.split(";")[0]


def fix_no_compare(raw, swap_files, pack_files):
    """Return `raw` with the IMAGE-NO-COMPARE-FILE lines rewritten:

    - a line whose file is not in `pack_files` is removed;
    - each of `swap_files` that is not listed yet gets a line, in the layout of the
      first IMAGE-NO-COMPARE-FILE line, placed after the last one kept (or where the
      first one was, when none is kept).

    `swap_files` and `pack_files` are file names in any form short_name() takes.
    Raises ValueError when the file has no IMAGE-NO-COMPARE-FILE line to copy the layout
    from, or when the result would list more than MAX_NO_COMPARE files."""
    have = set(short_name(f) for f in pack_files)
    lines = raw.split(b"\r\n")
    out = []
    kept = []
    lead = None
    first_at = None
    last_kept_at = None
    for line in lines:
        m = _LINE.match(line)
        if not m:
            out.append(line)
            continue
        if lead is None:
            lead = m.group("lead")
            first_at = len(out)
        name = short_name(m.group("name").decode("ascii"))
        if name in have:
            out.append(line)
            kept.append(name)
            last_kept_at = len(out)
    if lead is None:
        raise ValueError("no IMAGE-NO-COMPARE-FILE line in the parameter file")
    new = []
    for f in swap_files:
        n = short_name(f)
        if n not in kept and n not in new:
            new.append(n)
    if len(kept) + len(new) > MAX_NO_COMPARE:
        raise ValueError("%d IMAGE-NO-COMPARE-FILE files, at most %d"
                         % (len(kept) + len(new), MAX_NO_COMPARE))
    at = last_kept_at if last_kept_at is not None else first_at
    out[at:at] = [lead + n.encode("ascii") for n in new]
    return b"\r\n".join(out)


def swap_file_names(screen):
    """The 'Swap file name:..... NAME' values in ND-500 monitor LIST-SWAP-FILE-INFO output."""
    return re.findall(r"Swap file name:\.*\s*(\S+)", screen)
