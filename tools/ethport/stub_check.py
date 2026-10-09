#!/usr/bin/env python3
"""Fail on stubs and placeholders in the ported Ethernet II C code (plan rule R3).

Scans src/devices/ethernet/**/*.c,*.h and tests/ethernet/**/*.c,*.h, except the
generated Musashi tables in src/devices/ethernet/m68k/generated/.

Fails on:
  - the words TODO, FIXME, XXX, stub, "not implemented", "placeholder"
  - a function whose body is empty
  - a function whose whole body is one trivial return (return; return 0;
    return false; return NULL; return -1;)
  - any non-ASCII byte

A trivial body is allowed only when the line just above the function, or
the function's first line, carries the marker
    /* trivial: <C# file>:<line> */
naming the C# line that does the same thing; the marker is checked to name
a .cs file and a line number.
"""
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
DIRS = [os.path.join(ROOT, "src", "devices", "ethernet"), os.path.join(ROOT, "tests", "ethernet")]
SKIP = os.path.join(ROOT, "src", "devices", "ethernet", "m68k", "generated")

WORDS = re.compile(r"\bTODO\b|\bFIXME\b|\bXXX\b|\bstub\b|not implemented|placeholder", re.I)
ETHTEST = re.compile(r"^\s*ETH_TEST(?:_FLAGS)?\(\s*\w+\s*,\s*(\w+)[^)]*\)[^\n{]*\n?\s*\{", re.M)
# Files allowed to contain empty test bodies, with the reason.
EXEMPT = {
    "selftest_must_fail.c": "runner self-test: the empty test is the point (CTest WILL_FAIL)",
}
FUNC = re.compile(r"^[A-Za-z_][\w \*]*?\b([A-Za-z_]\w*)\s*\(([^;{}]*)\)\s*\n?\{", re.M)
TRIVIAL = re.compile(r"^\s*(return\s*(0|false|NULL|-1|0u)?\s*;)?\s*$")
MARK = re.compile(r"/\* trivial: \S+\.cs:\d+ \*/")


def body_of(text, open_idx):
    depth = 0
    for i in range(open_idx, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[open_idx + 1:i]
    return None


def strip_comments(s):
    s = re.sub(r"/\*.*?\*/", "", s, flags=re.S)
    return re.sub(r"//[^\n]*", "", s)


def check_file(path, errors):
    raw = open(path, "rb").read()
    for n, line in enumerate(raw.split(b"\n"), start=1):
        if any(b > 0x7F for b in line):
            errors.append("%s:%d: non-ASCII byte" % (path, n))
    text = raw.decode("ascii", errors="replace")
    for n, line in enumerate(text.split("\n"), start=1):
        if WORDS.search(line):
            errors.append("%s:%d: banned word: %s" % (path, n, line.strip()))
    if not path.endswith(".c"):
        return
    if os.path.basename(path) in EXEMPT:
        return
    for m in ETHTEST.finditer(text):
        body = body_of(text, m.end() - 1)
        if body is not None and TRIVIAL.match(strip_comments(body)):
            line = text.count("\n", 0, m.start()) + 1
            errors.append("%s:%d: test %s has an empty/trivial body" % (path, line, m.group(1)))
    for m in FUNC.finditer(text):
        name = m.group(1)
        if name in ("if", "for", "while", "switch", "return", "sizeof"):
            continue
        body = body_of(text, m.end() - 1)
        if body is None:
            continue
        if TRIVIAL.match(strip_comments(body)):
            start = text.rfind("\n", 0, m.start() - 1)
            prev = text[text.rfind("\n", 0, max(start, 0)) + 1:m.end()]
            if not MARK.search(prev + body):
                line = text.count("\n", 0, m.start()) + 1
                errors.append("%s:%d: trivial body in %s() without /* trivial: X.cs:N */"
                              % (path, line, name))


def main():
    errors = []
    files = 0
    dirs = sys.argv[1:] or DIRS
    for d in dirs:
        if not os.path.isdir(d):
            continue
        for dp, _, fns in os.walk(d):
            if dp.startswith(SKIP):
                continue
            for fn in fns:
                if fn.endswith((".c", ".h")):
                    files += 1
                    check_file(os.path.join(dp, fn), errors)
    for e in errors:
        print("FAIL:", os.path.relpath(e.split(":")[0], ROOT) + e[len(e.split(":")[0]):])
    print("stub_check: %d files, %d errors" % (files, len(errors)))
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
