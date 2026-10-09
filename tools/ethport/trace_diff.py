#!/usr/bin/env python3
"""Compare two Ethernet II traces (docs/ethernet-port/TRACE-FORMAT.md).

Usage: trace_diff.py [--insn] <expected-trace> <actual-trace>

Each stream (ND, M68K, NET) is compared separately in seq order; the tick
field is ignored. INSN lines are skipped unless --insn is given. The first
difference per stream is printed with 20 lines of context from both sides.
Exit 0 when every stream is identical, 1 otherwise, 2 on a malformed line.

There are no tolerance options. Accepted differences are named filters
listed in TRACE-FORMAT.md; none exist yet.
"""
import sys

CONTEXT = 20


def parse(path, insn):
    streams = {}
    for n, line in enumerate(open(path, encoding="ascii"), start=1):
        line = line.rstrip("\n")
        if not line:
            continue
        parts = line.split(" ")
        if len(parts) < 4:
            print("%s:%d: malformed line: %r" % (path, n, line))
            sys.exit(2)
        stream, seq, tick, event = parts[0], parts[1], parts[2], parts[3]
        if event == "INSN" and not insn:
            continue
        lst = streams.setdefault(stream, [])
        try:
            int(seq)
            int(tick)
        except ValueError:
            print("%s:%d: bad seq/tick: %r" % (path, n, line))
            sys.exit(2)
        lst.append((" ".join([event] + parts[4:]), tick, n))
    return streams


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    insn = "--insn" in sys.argv
    if len(args) != 2:
        print(__doc__)
        return 2
    exp, act = parse(args[0], insn), parse(args[1], insn)
    failed = False
    for stream in sorted(set(exp) | set(act)):
        e, a = exp.get(stream, []), act.get(stream, [])
        first = None
        for i in range(min(len(e), len(a))):
            if e[i][0] != a[i][0]:
                first = i
                break
        if first is None and len(e) != len(a):
            first = min(len(e), len(a))
        if first is None:
            print("%s: identical (%d events)" % (stream, len(e)))
            continue
        failed = True
        print("%s: FIRST DIFFERENCE at event %d (expected %d events, actual %d)"
              % (stream, first, len(e), len(a)))
        lo = max(0, first - CONTEXT)
        for side, lst, name in (("expected", e, args[0]), ("actual", a, args[1])):
            print("  --- %s (%s)" % (side, name))
            for j in range(lo, min(len(lst), first + CONTEXT + 1)):
                mark = ">>" if j == first else "  "
                print("  %s %6d tick=%s line=%d  %s" % (mark, j, lst[j][1], lst[j][2], lst[j][0]))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
