#!/usr/bin/env python3
"""Mutation spot check for the Ethernet II port (plan rule R8).

Usage:
  mutate.py --build-dir DIR --target TARGET --test CMD [--count N] [--seed S] FILE.c...

For each of N randomly chosen mutation sites in the given C files (seeded,
so a run can be repeated), one mutation is applied, TARGET is rebuilt in
DIR, and CMD is run. A mutant "survives" when the build succeeds and CMD
exits 0 - that means no test noticed the change, so a test is missing.
The original file is always restored, also on Ctrl-C or error.

Mutations (one per mutant):
  integer literal n -> n+1 (decimal and hex, not inside #include/#define lines'
  string parts), == <-> !=, < <-> >=, > <-> <=, && <-> ||, true <-> false

Comments and string literals are not mutated. A mutant that does not
compile is reported as "no-build" and does not count either way.

A test run that exceeds TEST_TIMEOUT seconds counts as killed (the
mutant made the tests hang); subprocess.run ends that test process.

Exit: 0 when no mutant survived, 1 otherwise.
"""
import argparse
import os
import random
import re
import shutil
import subprocess
import sys
import functools

print = functools.partial(print, flush=True)  # noqa: A001 - log lines appear as they happen

TOKEN = re.compile(r'(/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\')|'
                   r'(\b0[xX][0-9a-fA-F]+[uUlL]*\b|\b\d+[uUlL]*\b|==|!=|<=|>=|&&|\|\||\btrue\b|\bfalse\b|(?<![<>-])<(?![<=])|(?<![-<>])>(?![>=]))',
                   re.S)
SWAP = {"==": "!=", "!=": "==", "<=": ">", ">=": "<", "<": ">=", ">": "<=",
        "&&": "||", "||": "&&", "true": "false", "false": "true"}


def sites(text):
    out = []
    for m in TOKEN.finditer(text):
        if m.group(1):
            continue
        tok = m.group(2)
        line_start = text.rfind("\n", 0, m.start()) + 1
        line = text[line_start:text.find("\n", m.start())]
        if line.lstrip().startswith("#include"):
            continue
        out.append((m.start(2), m.end(2), tok))
    return out


def mutate(tok):
    if tok in SWAP:
        return SWAP[tok]
    m = re.match(r"(0[xX][0-9a-fA-F]+|\d+)([uUlL]*)$", tok)
    lit = m.group(1)
    if lit.lower().startswith("0x"):
        return hex(int(lit, 16) + 1) + m.group(2)
    if len(lit) > 1 and lit.startswith("0"):
        return "0" + format(int(lit, 8) + 1, "o") + m.group(2)
    return str(int(lit) + 1) + m.group(2)


TEST_TIMEOUT = 120  # seconds; a mutant that hangs the tests counts as killed


def run(cmd, cwd=None, timeout=None):
    try:
        return subprocess.run(cmd, shell=True, cwd=cwd, capture_output=True, text=True,
                              timeout=timeout).returncode
    except subprocess.TimeoutExpired:
        return -1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build-dir", required=True)
    ap.add_argument("--target", required=True)
    ap.add_argument("--test", required=True)
    ap.add_argument("--count", type=int, default=20)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("files", nargs="+")
    a = ap.parse_args()

    if run("cmake --build %s --target %s" % (a.build_dir, a.target)) != 0 or run(a.test) != 0:
        print("mutate: the unmutated build or test fails; fix that first")
        return 2

    pool = []
    for f in a.files:
        for s in sites(open(f, encoding="ascii").read()):
            pool.append((f,) + s)
    rnd = random.Random(a.seed)
    chosen = rnd.sample(pool, min(a.count, len(pool)))
    survived, killed, nobuild = [], 0, 0
    for f, start, end, tok in chosen:
        orig = open(f, encoding="ascii").read()
        new = mutate(tok)
        line = orig.count("\n", 0, start) + 1
        backup = f + ".mutate-orig"
        shutil.copyfile(f, backup)
        try:
            open(f, "w", encoding="ascii").write(orig[:start] + new + orig[end:])
            if run("cmake --build %s --target %s" % (a.build_dir, a.target)) != 0:
                nobuild += 1
                print("no-build  %s:%d %s -> %s" % (f, line, tok, new))
            elif run(a.test, timeout=TEST_TIMEOUT) == 0:
                survived.append("%s:%d %s -> %s" % (f, line, tok, new))
                print("SURVIVED  %s:%d %s -> %s" % (f, line, tok, new))
            else:
                killed += 1
                print("killed    %s:%d %s -> %s" % (f, line, tok, new))
        finally:
            shutil.move(backup, f)
            # The backup's timestamp predates the mutant's object file; without
            # this touch the rebuild below thinks it is up to date and the mutant
            # binary is left behind (seen 09-OCT-2026).
            os.utime(f, None)
    if run("cmake --build %s --target %s" % (a.build_dir, a.target)) != 0 or run(a.test) != 0:
        print("mutate: ERROR - after restoring the originals the build or test fails")
        return 2
    print("mutate: %d mutants, %d killed, %d survived, %d did not build (seed %d, %d sites)"
          % (len(chosen), killed, len(survived), nobuild, a.seed, len(pool)))
    return 1 if survived else 0


if __name__ == "__main__":
    sys.exit(main())
