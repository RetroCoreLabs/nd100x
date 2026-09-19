#!/usr/bin/env python3
"""review_skeleton.py - create / update the per-function review files for the
REVIEW rules of the house C standard (plan section 3, phase 0.8).

    tools/house/review_skeleton.py [--build DIR]

Writes docs/house-audit/review/<module>.md: one table per source file, one
row per function (static and non-static, taken from the object files), one
column per REVIEW rule. A cell is empty until a reviewer writes "ok" or
"n/a" (with a reason after it if needed). Existing cells are kept when the
file is regenerated; rows of functions that no longer exist are dropped and
new functions are added with empty cells.

tools/house/audit.py counts every empty cell as a finding of rule "REVIEW".
"""

import argparse
import collections
import os
import re
import sys

sys.path.insert(0, os.path.dirname(__file__))
import audit  # noqa: E402

REVIEW_RULES = ["2.8", "3.10", "5.1", "5.4", "5.7", "5.8", "5.10", "6.3", "6.8",
                "7.2", "7.5", "7.6", "8.1", "8.2", "8.5", "9.5", "10.1", "10.2",
                "11.3", "12.3"]

OUT = os.path.join(audit.REPO, "docs", "house-audit", "review")


def module_of(rel):
    parts = rel.split("/")
    if parts[0] == "src":
        return parts[1]
    return parts[0] if parts[0] != "tools" else "tools-" + parts[1]


def read_existing(path):
    cells = {}
    if not os.path.exists(path):
        return cells
    cur = None
    with open(path) as fh:
        for ln in fh:
            m = re.match(r"^## (\S+)", ln)
            if m:
                cur = m.group(1)
                continue
            if ln.startswith("| ") and cur and not ln.startswith("| Function") and not ln.startswith("|---"):
                cols = [c.strip() for c in ln.strip().strip("|").split("|")]
                cells[(cur, cols[0])] = cols[2:]
    return cells


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build", default="build_gate_audit")
    a = ap.parse_args()
    files = audit.project_files()
    bdir, entries = audit.ensure_build(a.build)
    objmap = audit.object_map(bdir, entries, files)
    per_module = collections.defaultdict(dict)
    header_funcs = {}
    for rel, obj in sorted(objmap.items()):
        funcs = []
        for t, name in audit.nm(obj):
            if t in ("T", "t") and not name.startswith((".", "_")) and "." not in name:
                line = audit.find_def_line(rel, name, strict=True)
                if line is None:
                    # static inline from a header: listed under that header
                    h = audit.header_defining(files, name)
                    if h:
                        header_funcs.setdefault(h, set()).add(
                            (audit.find_def_line(h, name, strict=True), name))
                    continue
                funcs.append((line, name))
        per_module[module_of(rel)][rel] = sorted(funcs)
    for h, funcs in header_funcs.items():
        per_module[module_of(h)][h] = sorted(funcs)
    os.makedirs(OUT, exist_ok=True)
    total = 0
    for mod, files_funcs in sorted(per_module.items()):
        path = os.path.join(OUT, mod + ".md")
        old = read_existing(path)
        lines = [f"# Review: {mod}", "",
                 "One row per function. A cell is empty until reviewed; write `ok`, or",
                 "`n/a`, or `fixed <commit>`. Rules: see the plan, section 3 (REVIEW).",
                 ""]
        head = "| Function | Line | " + " | ".join(REVIEW_RULES) + " |"
        sep = "|---|---|" + "---|" * len(REVIEW_RULES)
        for rel, funcs in sorted(files_funcs.items()):
            lines += [f"## {rel}", "", head, sep]
            for line, name in funcs:
                cells = old.get((rel, name), [""] * len(REVIEW_RULES))
                cells = (cells + [""] * len(REVIEW_RULES))[:len(REVIEW_RULES)]
                lines.append(f"| {name} | {line} | " + " | ".join(cells) + " |")
                total += 1
            lines.append("")
        with open(path, "w") as fh:
            fh.write("\n".join(lines))
    print(f"review files: {len(per_module)} modules, {total} functions, in {os.path.relpath(OUT, audit.REPO)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
