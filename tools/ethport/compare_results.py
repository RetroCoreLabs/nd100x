#!/usr/bin/env python3
"""Compare the C port's test outcomes with RetroCore's recorded outcomes.

Usage: compare_results.py <test_ethernet binary>

RetroCore outcomes: docs/ethernet-port/RETROCORE-RESULTS.csv (class, method,
test_case, outcome). A method counts as Passed there when all its test cases
passed, Failed when any failed, NotExecuted when none ran.
C outcomes: the runner's PASS/FAIL/IGNORED lines (Class__Method).

For every C test whose Class__Method exists in RetroCore's list:
  RetroCore Passed      -> C must PASS
  RetroCore Failed      -> C must be XFAIL (marked ETH_TEST_EXPECT_FAIL and
                           failing: a faithful port fails the same way)
  RetroCore NotExecuted -> C must be IGNORED
Any other combination is reported and the exit status is 1.
Tests in class Port (no RetroCore counterpart) are not compared.
"""
import csv
import os
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
RES = os.path.join(ROOT, "docs", "ethernet-port", "RETROCORE-RESULTS.csv")


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    rc = {}
    for r in csv.DictReader(open(RES, encoding="utf-8")):
        key = "%s__%s" % (r["class"], r["method"])
        prev = rc.get(key)
        out = r["outcome"]
        if prev is None or out == "Failed" or (prev == "NotExecuted" and out == "Passed"):
            rc[key] = out
    run = subprocess.run([sys.argv[1]], capture_output=True, text=True)
    c = {}
    for line in run.stdout.splitlines():
        parts = line.split()
        if len(parts) >= 2 and parts[0] in ("PASS", "FAIL", "IGNORED", "XFAIL", "UNEXPECTED-PASS"):
            c[parts[1]] = parts[0]
    # RetroCore Failed -> the C test must be marked ETH_TEST_EXPECT_FAIL and fail (XFAIL)
    want = {"Passed": "PASS", "Failed": "XFAIL", "NotExecuted": "IGNORED"}
    bad, compared = 0, 0
    for key, outcome in sorted(c.items()):
        if key.startswith("Port__"):
            continue
        if key not in rc:
            print("NO RETROCORE RESULT: %s (C %s)" % (key, outcome))
            bad += 1
            continue
        compared += 1
        if want.get(rc[key]) != outcome:
            print("MISMATCH: %s RetroCore %s, C %s" % (key, rc[key], outcome))
            bad += 1
    print("compare_results: %d compared, %d mismatches" % (compared, bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
