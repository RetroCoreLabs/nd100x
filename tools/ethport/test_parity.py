#!/usr/bin/env python3
"""Compare the RetroCore Ethernet II test list with the ported C tests
(plan rule R5).

C# side: every method marked [Test] or [TestCase(...)] in the files listed
in docs/ethernet-port/TEST-SCOPE.txt (one path per line, relative to
the RetroCore root; '#' starts a comment).

C side: every ETH_TEST(CsClass, CsMethod) / ETH_TEST_FLAGS(...) in
tests/ethernet/**/*.c (tests/ethernet/eth_test.h). With --binary=<path to
test_ethernet> the runner's --list output must name exactly the same tests,
which proves every defined test is registered and run.
A C# method with N [TestCase] rows must be ported with all N cases and
carry /* cs-cases: N */ on its ETH_TEST line. cs-ignore:/cs-explicit
markers must agree with the ETH_TEST_IGNORE/ETH_TEST_EXPLICIT flags.

C# tests are listed by Roslyn (tools/ethport/cs_inventory --tests), so
multi-line attributes are handled.

Also reported:
  - C# tests marked [Ignore] or [Explicit] (the C port must keep the same
    marker: a C comment /* cs-ignore: <reason> */ or /* cs-explicit */ in
    the function's first line);
  - C tests with no C# counterpart (allowed, listed for review).

A C# test may be left out only through a NOT_PORTED row in
docs/ethernet-port/TEST-NOT-PORTED.csv (columns: test,reason,approved) with
approved=Ronny.

Reads the pinned RetroCore snapshot in build/ethport-cs-snapshot
(tools/ethport/snapshot_retrocore.sh), never the RetroCore working tree. Exit 1 on any missing test or marker mismatch.
"""
import csv
import io
import os
import subprocess
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SCOPE = os.path.join(ROOT, "docs", "ethernet-port", "TEST-SCOPE.txt")
NOTP = os.path.join(ROOT, "docs", "ethernet-port", "TEST-NOT-PORTED.csv")
CTESTS = os.path.join(ROOT, "tests", "ethernet")

CFUNC = re.compile(r"^\s*ETH_TEST(?:_FLAGS)?\(\s*(\w+)\s*,\s*(\w+)\s*(?:,\s*([^)]*))?\)(.*)$")


def cs_tests(rc_dir):
    """List C# tests with Roslyn (tools/ethport/cs_inventory --tests)."""
    files = []
    for raw in open(SCOPE, encoding="ascii"):
        rel = raw.split("#")[0].strip()
        if rel:
            files.append(os.path.join(rc_dir, rel))
    dll = os.path.join(ROOT, "tools", "ethport", "cs_inventory", "bin", "Release", "net10.0",
                       "cs_inventory.dll")
    out = subprocess.run(["dotnet", dll, "--tests", rc_dir] + files, check=True,
                         capture_output=True, text=True).stdout
    tests = {}
    for r in csv.DictReader(io.StringIO(out)):
        key = "%s__%s" % (r["class"], r["method"])
        if key in tests:
            raise SystemExit("duplicate C# test key %s" % key)
        tests[key] = {
            "where": "%s:%s" % (r["file"], r["line"]),
            "ignore": r["ignore"] == "1",
            "explicit": r["explicit"] == "1",
            "cases": int(r["cases"]),
        }
    return tests


def c_tests(errors):
    tests = {}
    for dp, _, fns in os.walk(CTESTS):
        for fn in fns:
            if not fn.endswith(".c"):
                continue
            path = os.path.join(dp, fn)
            for n, line in enumerate(open(path, encoding="ascii"), start=1):
                m = CFUNC.match(line)
                if not m:
                    continue
                key = "%s__%s" % (m.group(1), m.group(2))
                where = "%s:%d" % (os.path.relpath(path, ROOT), n)
                flags = m.group(3) or ""
                tail = m.group(4)
                ign_flag = "ETH_TEST_IGNORE" in flags
                exp_flag = "ETH_TEST_EXPLICIT" in flags
                xf_flag = "ETH_TEST_EXPECT_FAIL" in flags
                ign_mark = "cs-ignore:" in tail
                exp_mark = "cs-explicit" in tail
                xf_mark = "cs-fails-in-retrocore" in tail
                if ign_flag != ign_mark or exp_flag != exp_mark or xf_flag != xf_mark:
                    errors.append("%s: ETH_TEST flags and cs- markers disagree for %s" % (where, key))
                if key in tests:
                    errors.append("%s: duplicate C test %s (first at %s)" % (where, key, tests[key]["where"]))
                cm = re.search(r"cs-cases: (\d+)", tail)
                tests[key] = {
                    "where": where,
                    "ignore": ign_mark,
                    "explicit": exp_mark,
                    "cases": int(cm.group(1)) if cm else 0,
                }
    return tests


def runtime_list(binary):
    out = subprocess.run([binary, "--list"], check=True, capture_output=True, text=True).stdout
    return {line.split(" ")[0] for line in out.splitlines() if line.strip()}


def main():
    rc_dir = os.path.join(ROOT, "build", "ethport-cs-snapshot")
    if not os.path.exists(os.path.join(rc_dir, "COMMIT")):
        print("test_parity: run tools/ethport/snapshot_retrocore.sh first")
        return 2
    cs = cs_tests(rc_dir)
    errors = []
    c = c_tests(errors)
    binary = next((a[len("--binary="):] for a in sys.argv[1:] if a.startswith("--binary=")), None)
    if binary:
        reg = runtime_list(binary)
        for k in sorted(set(c) - reg):
            errors.append("defined in source but not registered in %s: %s" % (binary, k))
        for k in sorted(reg - set(c)):
            errors.append("registered in %s but not found in source: %s" % (binary, k))
    notp = {}
    if os.path.exists(NOTP):
        for r in csv.DictReader(open(NOTP, encoding="ascii")):
            notp[r["test"]] = r
    for k, v in sorted(cs.items()):
        if k in notp:
            if notp[k]["approved"].strip() != "Ronny" or not notp[k]["reason"].strip():
                errors.append("not-ported row for %s lacks reason/approval" % k)
            continue
        if k not in c:
            errors.append("missing C test for %s (%s)" % (k, v["where"]))
            continue
        for flag in ("ignore", "explicit", "cases"):
            want = v[flag] if flag != "cases" or v[flag] > 1 else c[k][flag]
            if want != c[k][flag]:
                errors.append("%s marker differs for %s: C# %s, C %s"
                              % (flag, k, v[flag], c[k][flag]))
    extra = sorted(set(c) - set(cs))
    for k in extra:
        print("C-only test (review):", k, c[k]["where"])
    for e in errors:
        print("FAIL:", e)
    print("test_parity: C# tests %d, C tests %d, not-ported %d, errors %d"
          % (len(cs), len(c), len(notp), len(errors)))
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
