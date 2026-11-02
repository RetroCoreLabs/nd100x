#!/usr/bin/env python3
"""Unit tests for tools/nd100x_expect.py.

Run:  python3 tests/test_nd100x_expect.py        (standard library only)
CTest registers it as nd100x_expect_unit.

The byte framing of the --pipe control channel is tested byte for byte, and the
whole driver (typing, waiting, login, floppy swap, image copies, log filter,
transcript, shutdown) runs against tests/fake_nd100x_pipe.py, which answers like
nd100x --pipe without booting anything.
"""

import os
import shutil
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))

import nd100x_expect as ne  # noqa: E402

FAKE = [sys.executable, os.path.join(HERE, "fake_nd100x_pipe.py")]


class ControlFrame(unittest.TestCase):
    def test_bytes(self):
        self.assertEqual(ne.control_frame("mount 0 /tmp/a.img"), b"\xffmount 0 /tmp/a.img\n")

    def test_first_and_last_byte(self):
        f = ne.control_frame("eject 1")
        self.assertEqual(f[0], 0xFF)
        self.assertEqual(f[-1], 0x0A)
        self.assertEqual(len(f), len("eject 1") + 2)

    def test_refuses_what_would_break_the_frame(self):
        for bad in ("", "mount 0 a\nb", "mount 0 a\rb", "mount 0 \xff", "mount 0 æ"):
            with self.subTest(bad=bad):
                with self.assertRaises(ValueError):
                    ne.control_frame(bad)

    def test_floppy_unit(self):
        for good in (0, 1, 2):
            self.assertEqual(ne._floppy_unit(good), good)
        for bad in (-1, 3, "0", None, True):
            with self.subTest(bad=bad):
                with self.assertRaises(ValueError):
                    ne._floppy_unit(bad)

    def test_reply_pattern_matches_nd100x_wording(self):
        # Wording of pipe_handle_control() in src/frontend/nd100x/nd100x.c.
        m = ne.CONTROL_REPLY.search("[pipe-control] mount 0 '/a b/c.img' -> ok")
        self.assertEqual(m.group(1), "ok")
        m = ne.CONTROL_REPLY.search("[pipe-control] mount 0 '/x.img' -> FILE NOT FOUND")
        self.assertEqual(m.group(1), "FILE NOT FOUND")
        m = ne.CONTROL_REPLY.search("[pipe-control] eject 2 -> ok")
        self.assertEqual(m.group(1), "ok")


class Session(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp(prefix="ndx-expect-test-")
        self.work = os.path.join(self.dir, "work")

    def tearDown(self):
        shutil.rmtree(self.dir, ignore_errors=True)
        os.environ.pop("FAKE_IGNORE_ESC", None)

    def file(self, name, data=b"x"):
        p = os.path.join(self.dir, name)
        with open(p, "wb") as f:
            f.write(data)
        return p

    def read(self, path, mode="r"):
        with open(path, mode, **({} if "b" in mode else {"encoding": "latin-1"})) as f:
            return f.read()

    def vm(self, **kw):
        kw.setdefault("echo", False)
        return ne.Nd100x(exe=FAKE, **kw)

    def test_full_session_with_copies_log_and_transcript(self):
        disk = self.file("disk.img", b"D" * 64)
        tape = self.file("tape.img", b"T" * 64)
        fl = self.file("two.img")
        log = os.path.join(self.dir, "emulator.log")
        tx = os.path.join(self.dir, "console.log")
        with self.vm(boot="smd", smd0=disk, scsi={1: "tape:" + tape}, work_dir=self.work,
                     log_file=log, log_keep=r"TAPE", transcript=tx) as vm:
            self.assertTrue(vm.pid)
            # nd100x is given the copies, never the originals.
            self.assertIn("--smd0=" + vm.copies["smd0"], vm.args)
            self.assertIn("--scsi1=tape:" + vm.copies["scsi1"], vm.args)
            vm.expect("RUNNING", timeout=10)
            vm.escape(timeout=10)
            vm.login("SYSTEM")
            out = vm.command("LIST-USERS", timeout=10)
            self.assertIn("OK LIST-USERS", out)
            self.assertTrue(out.rstrip().endswith("@"))
            mounted = vm.mount(1, fl, copy=True)
            self.assertNotEqual(mounted, fl)
            vm.eject(1)
            copies = dict(vm.copies)
        self.assertEqual(self.read(disk, "rb"), b"D" * 64)       # original untouched
        for p in copies.values():
            self.assertFalse(os.path.exists(p), p)                  # copies removed
        text = self.read(log)
        self.assertIn("TAPE1", text)                                # kept by log_keep
        self.assertIn("[pipe-control] mount 1", text)               # control replies always kept
        self.assertNotIn("register noise", text)                    # filtered out
        t = self.read(tx)
        self.assertIn("control mount 1", t)
        self.assertIn("OK LIST-USERS", t)

    def test_control_bytes_never_reach_the_keyboard(self):
        fl = self.file("f.img")
        with self.vm(log_file=os.path.join(self.dir, "l.log")) as vm:
            vm.expect("RUNNING", timeout=10)
            vm.mount(0, fl)
            vm.escape(timeout=10)
            # The fake echoes every keystroke; a broken frame would show up as text.
            self.assertNotIn("mount", vm.before())

    def test_mount_reply_when_log_is_mixed_into_the_screen(self):
        fl = self.file("f.img")
        with self.vm() as vm:                                       # old behaviour, no log_file
            vm.expect("RUNNING", timeout=10)
            vm.mount(2, fl)

    def test_mount_missing_file_raises(self):
        with self.vm(log_file=os.path.join(self.dir, "l.log")) as vm:
            vm.expect("RUNNING", timeout=10)
            with self.assertRaises(ne.ExpectAbort):
                vm.mount(0, os.path.join(self.dir, "missing.img"))

    def test_escape_repeats_until_enter(self):
        os.environ["FAKE_IGNORE_ESC"] = "2"
        with self.vm() as vm:
            vm.expect("RUNNING", timeout=10)
            vm.escape(every=0.3, timeout=10)

    def test_answer_waits_for_a_question(self):
        with self.vm() as vm:
            vm.expect("RUNNING", timeout=10)
            vm.escape(timeout=10)
            out = vm.answer("SYSTEM", timeout=10)
            self.assertIn("PASSWORD:", out)

    def test_send_refuses_0xff(self):
        with self.vm() as vm:
            with self.assertRaises(ValueError):
                vm.send(b"A\xffB")

    def test_missing_image_refused_before_start(self):
        with self.assertRaises(FileNotFoundError):
            self.vm(smd0=os.path.join(self.dir, "missing.img"), work_dir=self.work)

    def test_bad_scsi_spec(self):
        with self.assertRaises(ValueError):
            self.vm(scsi={1: "/no/media/prefix.img"})

    def test_keep_copies(self):
        disk = self.file("disk.img")
        with self.vm(smd0=disk, work_dir=self.work, keep_copies=True) as vm:
            vm.expect("RUNNING", timeout=10)
            copy = vm.copies["smd0"]
        self.assertTrue(os.path.exists(copy))

    def test_timeout_shows_last_output(self):
        with self.vm() as vm:
            vm.expect("RUNNING", timeout=10)
            with self.assertRaises(ne.ExpectTimeout) as e:
                vm.expect("NEVER SHOWN", timeout=0.5)
            self.assertIn("timed out", str(e.exception))

    def test_process_stopped_on_close(self):
        vm = self.vm().start()
        vm.expect("RUNNING", timeout=10)
        vm.close()
        self.assertIsNotNone(vm._proc.poll())

    def test_close_twice_is_harmless(self):
        vm = self.vm().start()
        vm.close()
        vm.close()


if __name__ == "__main__":
    unittest.main(verbosity=1)
