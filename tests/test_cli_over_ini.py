#!/usr/bin/env python3
"""The machine flags on the command line win over a --config INI, on a real boot.

BUGS.md L1 (measured 2026-11-08): with --config, a --scsi1=tape:FILE given on the same
command line was dropped without a word - SINTRAN's selection of SCSI ID 1 timed out -
while --show-config printed the drive. The boot path now resolves the machine the same
way as --show-config (resolve_machine_config in src/frontend/nd100x/nd100x.c).

Each boot runs a few hundred thousand instructions with no disc to boot from, which is
enough for the device set-up lines; the emulator is ND100X_EXE (set by CTest) or the
build/bin/nd100x next to this repository."""

import os
import subprocess
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
EXE = os.environ.get("ND100X_EXE") or os.path.join(HERE, "..", "build", "bin", "nd100x")

# An ND-100 with an SCSI controller and nothing attached to it.
INI = """[machine]
cpu = 100
[controller.scsi.0]
enabled = yes
[boot]
device = none
"""


class CliOverIni(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.ini = os.path.join(self.dir.name, "machine.ini")
        with open(self.ini, "w") as f:
            f.write(INI)
        self.tape = os.path.join(self.dir.name, "tape.img")
        with open(self.tape, "wb") as f:
            f.write(b"\xff" * 65536)            # blank tape: FFFFFFFF is end of data

    def tearDown(self):
        self.dir.cleanup()

    def run_nd100x(self, *args):
        r = subprocess.run([EXE, "--config=" + self.ini] + list(args),
                           stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, timeout=60, cwd=self.dir.name)
        return r.stdout.decode("latin-1")

    def boot(self, *args):
        return self.run_nd100x("--scsi-debug", "--max-instr=200000", "-v", *args)

    def test_show_config_lists_the_tape(self):
        out = self.run_nd100x("--scsi1=tape:" + self.tape, "--show-config")
        self.assertIn("disk1 = tape:" + self.tape, out)

    def test_boot_attaches_the_tape_from_the_command_line(self):
        out = self.boot("--scsi1=tape:" + self.tape)
        self.assertIn("unit 1 attached as tape", out)

    def test_boot_without_the_flag_attaches_nothing(self):
        out = self.boot()
        self.assertNotIn("attached as tape", out)

    def test_boot_takes_the_cpu_type_from_the_command_line(self):
        out = self.boot("--cputype=ND120CX")
        self.assertIn("CPU: ND-120/CX", out)

    def test_boot_adds_a_command_line_hdlc_line_once(self):
        # Before the fix the command-line HDLC lines were added on their own, after
        # the INI devices; now they come through the machine config, and the old loop
        # must not add them a second time.
        out = self.boot("--hdlc=1:%d" % (20000 + os.getpid() % 20000), "--log=hdlc:info")
        self.assertEqual(out.count("HDLC 1 added"), 1, out)

    def test_boot_takes_the_cpu_type_from_the_ini_without_the_flag(self):
        out = self.boot()
        self.assertIn("CPU: ND-100", out)
        self.assertNotIn("ND-120", out)


if __name__ == "__main__":
    unittest.main()
