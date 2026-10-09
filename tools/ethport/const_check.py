#!/usr/bin/env python3
"""Check that numeric constants in the ported Ethernet II C code were copied
from the C# source, not retyped (plan rule R4).

Every `#define NAME <number>` and every enum constant `NAME = <number>` in
src/devices/ethernet/**/*.{c,h} (generated Musashi tables excluded) must
carry a source marker on the same line or the line above:

    /* cs: <path under the RetroCore root>:<line> */

The script opens that C# line and fails unless the same numeric value
appears in it (C# literals: decimal, 0x hex, 0b binary, with optional _
separators and u/U/l/L suffixes; C literals: decimal, 0x hex, leading-0
octal, with optional u/U/l/L suffixes).

A constant that has no C# counterpart (pure C plumbing) carries
    /* cs: none - <reason> */
and is listed in the output so Ronny can review the list.

Reads the pinned RetroCore snapshot in build/ethport-cs-snapshot
(tools/ethport/snapshot_retrocore.sh), never the RetroCore working tree.
"""
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SRC = os.path.join(ROOT, "src", "devices", "ethernet")
SKIP = os.path.join(SRC, "m68k", "generated")

DEFINE = re.compile(r"^\s*#\s*define\s+([A-Z_][A-Z0-9_]*)\s+\(?\s*(-?(?:0[xX][0-9a-fA-F]+|\d+))[uUlL]*\s*\)?")
ENUMC = re.compile(r"^\s*([A-Z_][A-Z0-9_]*)\s*=\s*\(?\s*(-?(?:0[xX][0-9a-fA-F]+|\d+))[uUlL]*\s*\)?\s*,?")
MARK = re.compile(r"/\*\s*cs:\s*(\S+?\.cs):(\d+)\s*\*/")
NONE = re.compile(r"/\*\s*cs:\s*none\s*-\s*(.+?)\s*\*/")
CS_NUM = re.compile(r"(?<![\w.])(0[xX][0-9a-fA-F_]+|0[bB][01_]+|\d[\d_]*)[uUlL]*(?![\w.])")


def c_value(tok):
    neg = tok.startswith("-")
    t = tok.lstrip("-")
    if t.lower().startswith("0x"):
        v = int(t, 16)
    elif len(t) > 1 and t.startswith("0"):
        v = int(t, 8)
    else:
        v = int(t, 10)
    return -v if neg else v


def cs_values(line):
    vals = set()
    for m in CS_NUM.finditer(line):
        t = m.group(1).replace("_", "")
        if t.lower().startswith("0x"):
            vals.add(int(t, 16))
        elif t.lower().startswith("0b"):
            vals.add(int(t[2:], 2))
        else:
            vals.add(int(t, 10))
    return vals


def main():
    rc_dir = os.path.join(ROOT, "build", "ethport-cs-snapshot")
    if not os.path.exists(os.path.join(rc_dir, "COMMIT")):
        print("const_check: run tools/ethport/snapshot_retrocore.sh first")
        return 2
    errors, nones, checked = [], [], 0
    cs_cache = {}
    for dp, _, fns in os.walk(SRC):
        if dp.startswith(SKIP):
            continue
        for fn in sorted(fns):
            if not fn.endswith((".c", ".h")):
                continue
            path = os.path.join(dp, fn)
            rel = os.path.relpath(path, ROOT)
            lines = open(path, encoding="ascii").read().split("\n")
            for i, line in enumerate(lines):
                m = DEFINE.match(line) or ENUMC.match(line)
                if not m:
                    continue
                name, tok = m.group(1), m.group(2)
                ctx = line + ((" " + lines[i - 1]) if i > 0 else "")
                nm = NONE.search(ctx)
                if nm:
                    nones.append("%s:%d %s - %s" % (rel, i + 1, name, nm.group(1)))
                    continue
                mk = MARK.search(ctx)
                if not mk:
                    errors.append("%s:%d: %s has no /* cs: file:line */ marker" % (rel, i + 1, name))
                    continue
                cs_path = os.path.join(rc_dir, mk.group(1))
                if cs_path not in cs_cache:
                    if not os.path.exists(cs_path):
                        errors.append("%s:%d: %s marker file not found: %s" % (rel, i + 1, name, mk.group(1)))
                        continue
                    with open(cs_path, encoding="utf-8-sig", errors="replace") as f:
                        cs_cache[cs_path] = f.read().split("\n")
                cs_lines = cs_cache[cs_path]
                ln = int(mk.group(2))
                if ln < 1 or ln > len(cs_lines):
                    errors.append("%s:%d: %s marker line out of range" % (rel, i + 1, name))
                    continue
                want = c_value(tok)
                if want not in cs_values(cs_lines[ln - 1]) and -want not in cs_values(cs_lines[ln - 1]):
                    errors.append("%s:%d: %s = %d not found on %s:%d: %s"
                                  % (rel, i + 1, name, want, mk.group(1), ln, cs_lines[ln - 1].strip()))
                    continue
                checked += 1
    for n in nones:
        print("NO C# SOURCE:", n)
    for e in errors:
        print("FAIL:", e)
    print("const_check: %d checked, %d without C# source, %d errors" % (checked, len(nones), len(errors)))
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
