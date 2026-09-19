#!/usr/bin/env python3
"""protect_tables.py - house rule 2.8: wrap hand-aligned tables and register
maps in // clang-format off / // clang-format on, before a module is
formatted.

    tools/house/protect_tables.py [--dry-run] FILE...

A block is protected when all of these hold:
  - it is an enum / struct / union body, an array initializer, or a run of
    consecutive #define lines;
  - at least two of its lines are hand-aligned (two or more spaces between
    tokens after the indentation, not counting a trailing comment's own
    leading spaces only);
  - clang-format would change at least one of its lines.
Blocks already inside a clang-format off region are left alone.
Prints every wrapped block as file:first-last so each one can be reviewed.
"""

import argparse
import os
import re
import subprocess
import sys

CF = os.environ.get("CLANG_FORMAT", "clang-format")
ALIGNED = re.compile(r"\S {2,}\S")
BLOCK_OPEN = re.compile(r"^\s*(typedef\s+)?(enum|struct|union)\b[^;]*\{\s*(/[/*].*)?$"
                        r"|=\s*\{\s*(/[/*].*)?$")
DEFINE = re.compile(r"^\s*#\s*define\s")


def changed_lines(path, lines):
    """Original line numbers (1-based) that clang-format would change."""
    fmt = subprocess.run([CF, "--style=file", "--assume-filename=" + path],
                         input="\n".join(lines), text=True, capture_output=True,
                         cwd=os.path.dirname(os.path.abspath(path)) or ".").stdout
    import tempfile
    with tempfile.NamedTemporaryFile("w", delete=False, suffix=".c") as a, \
            tempfile.NamedTemporaryFile("w", delete=False, suffix=".c") as b:
        a.write("\n".join(lines))
        b.write(fmt)
    out = subprocess.run(["diff", "--unchanged-line-format=", "--old-line-format=%dn\n",
                          "--new-line-format=", a.name, b.name],
                         text=True, capture_output=True).stdout
    os.unlink(a.name)
    os.unlink(b.name)
    return {int(x) for x in out.split()}


def aligned(line):
    body = line.strip()
    code = re.split(r"/[/*]", body, 1)[0].rstrip()
    return bool(ALIGNED.search(code)) or (code != body and bool(re.search(r"\S {2,}/[/*]", body)))


def blocks(lines):
    """Yield (first, last) 0-based line ranges of candidate blocks."""
    i, n = 0, len(lines)
    while i < n:
        if BLOCK_OPEN.search(lines[i]):
            depth = 0
            j = i
            while j < n:
                depth += lines[j].count("{") - lines[j].count("}")
                if depth <= 0 and j > i:
                    break
                j += 1
            yield i, min(j, n - 1)
            i = j + 1
        elif DEFINE.match(lines[i]):
            j = i
            while j + 1 < n and (DEFINE.match(lines[j + 1]) or lines[j].rstrip().endswith("\\")):
                j += 1
            if j > i:
                yield i, j
            i = j + 1
        else:
            i += 1


def in_off_region(lines, idx):
    state = False
    for k in range(idx):
        if "clang-format off" in lines[k]:
            state = True
        elif "clang-format on" in lines[k]:
            state = False
    return state


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("files", nargs="+")
    a = ap.parse_args()
    total = 0
    for path in a.files:
        with open(path) as fh:
            lines = fh.read().split("\n")
        changed = changed_lines(path, lines)
        wrap = []
        for first, last in blocks(lines):
            span = range(first, last + 1)
            if sum(1 for k in span if aligned(lines[k])) < 2:
                continue
            if not any((k + 1) in changed for k in span):
                continue
            if in_off_region(lines, first):
                continue
            wrap.append((first, last))
        for first, last in reversed(wrap):
            indent = re.match(r"\s*", lines[first]).group(0) if not DEFINE.match(lines[first]) else ""
            lines.insert(last + 1, indent + "// clang-format on")
            lines.insert(first, indent + "// clang-format off")
        for first, last in wrap:
            print(f"{path}:{first + 1}-{last + 1}")
        total += len(wrap)
        if wrap and not a.dry_run:
            with open(path, "w") as fh:
                fh.write("\n".join(lines))
    print(f"protect_tables: {total} blocks", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
