#!/usr/bin/env python3
"""Unit tests for tools/installs/backup-manager-b02/bm_parameter.py (no emulator needed)."""

import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..",
                                "tools", "installs", "backup-manager-b02"))
from bm_parameter import fix_no_compare, short_name, swap_file_names  # noqa: E402

# The lines round the IMAGE-NO-COMPARE-FILE block as ND ships them in BM-PARAMETER-B02:DATA.
SHIPPED = (b"   IMAGE-HIGH-MEMORY-ADDRESS= 177777B % Highest memory address\r\n"
           b"\r\n"
           b"   IMAGE-NO-COMPARE-FILE    = (SYSTEM)SWAP-FILE-0:SWAP\r\n"
           b"   IMAGE-NO-COMPARE-FILE    = (SYSTEM)SEGFIL0:DATA\r\n"
           b"\r\n"
           b"   DOMINO-MODE              = ON      % Search for DOMINO\r\n")

# LIST-SWAP-FILE-INFO 0 on the ND-500 monitor, as the pack the install scripts build shows it.
SCREEN = ("ND-5000: LIST-SWAP-FILE-INFO\r\nSwap file number: 0\r\n"
          "Swap file number:............ 0\r\n"
          "Swap file name:.............. (PACK-ONE:SYSTEM)SWAP-FILE:DATA;1\r\n"
          "Mass storage addr:........... 104044B\r\n")


class ShortName(unittest.TestCase):
    def test_directory_and_version_go(self):
        self.assertEqual(short_name("(PACK-ONE:SYSTEM)SWAP-FILE:DATA;1"), "(SYSTEM)SWAP-FILE:DATA")

    def test_plain_name_stays(self):
        self.assertEqual(short_name("(system)segfil0:data"), "(SYSTEM)SEGFIL0:DATA")


class SwapFileNames(unittest.TestCase):
    def test_name_from_screen(self):
        self.assertEqual(swap_file_names(SCREEN), ["(PACK-ONE:SYSTEM)SWAP-FILE:DATA;1"])

    def test_no_swap_file(self):
        self.assertEqual(swap_file_names("ND-5000: LIST-SWAP-FILE-INFO\r\n1\r\nND-5000: "), [])


class FixNoCompare(unittest.TestCase):
    def test_missing_swap_file_replaced_by_real_one(self):
        got = fix_no_compare(SHIPPED, ["(PACK-ONE:SYSTEM)SWAP-FILE:DATA;1"],
                             ["(SYSTEM)SEGFIL0:DATA;1", "(SYSTEM)SWAP-FILE:DATA;1"])
        self.assertEqual(got, SHIPPED.replace(
            b"   IMAGE-NO-COMPARE-FILE    = (SYSTEM)SWAP-FILE-0:SWAP\r\n"
            b"   IMAGE-NO-COMPARE-FILE    = (SYSTEM)SEGFIL0:DATA\r\n",
            b"   IMAGE-NO-COMPARE-FILE    = (SYSTEM)SEGFIL0:DATA\r\n"
            b"   IMAGE-NO-COMPARE-FILE    = (SYSTEM)SWAP-FILE:DATA\r\n"))

    def test_file_that_matches_is_unchanged(self):
        got = fix_no_compare(SHIPPED, ["(SYSTEM)SWAP-FILE-0:SWAP"],
                             ["(SYSTEM)SWAP-FILE-0:SWAP;1", "(SYSTEM)SEGFIL0:DATA;1"])
        self.assertEqual(got, SHIPPED)

    def test_no_line_kept_new_one_goes_where_the_first_was(self):
        got = fix_no_compare(SHIPPED, ["(SYSTEM)SWAP-FILE:DATA"], ["(SYSTEM)SWAP-FILE:DATA;1"])
        self.assertIn(b"\r\n\r\n   IMAGE-NO-COMPARE-FILE    = (SYSTEM)SWAP-FILE:DATA\r\n\r\n"
                      b"   DOMINO-MODE", got)
        self.assertNotIn(b"SEGFIL0", got)

    def test_crlf_and_length_of_other_lines_kept(self):
        got = fix_no_compare(SHIPPED, ["(SYSTEM)SWAP-FILE:DATA"],
                             ["(SYSTEM)SEGFIL0:DATA", "(SYSTEM)SWAP-FILE:DATA"])
        self.assertTrue(got.endswith(b"% Search for DOMINO\r\n"))
        self.assertNotIn(b"\n", got.replace(b"\r\n", b""))

    def test_no_line_at_all_is_an_error(self):
        with self.assertRaises(ValueError):
            fix_no_compare(b"   DOMINO-MODE = ON\r\n", ["(SYSTEM)SWAP-FILE:DATA"], [])

    def test_more_than_twelve_is_an_error(self):
        swaps = ["(SYSTEM)SWAP-%d:DATA" % i for i in range(12)]
        with self.assertRaises(ValueError):
            fix_no_compare(SHIPPED, swaps, ["(SYSTEM)SEGFIL0:DATA"] + swaps)


if __name__ == "__main__":
    unittest.main()
