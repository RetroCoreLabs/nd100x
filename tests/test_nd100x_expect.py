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


class FetchImage(unittest.TestCase):
    """fetch_image() against a local file:// URL - no network."""

    def setUp(self):
        import gzip, hashlib
        self.dir = tempfile.mkdtemp(prefix="ndx-fetch-test-")
        self.data = bytes(range(256)) * 40
        self.md5 = hashlib.md5(self.data).hexdigest()
        src = os.path.join(self.dir, "disk.img.gz")
        with open(src, "wb") as f:
            f.write(gzip.compress(self.data))
        self.url = "file://" + src
        self.out = os.path.join(self.dir, "out")

    def tearDown(self):
        shutil.rmtree(self.dir, ignore_errors=True)

    def read(self, p):
        with open(p, "rb") as f:
            return f.read()

    def test_downloads_unpacks_and_names_by_md5(self):
        p = ne.fetch_image(self.url, self.md5, self.out)
        self.assertEqual(os.path.basename(p), self.md5 + ".img")
        self.assertEqual(self.read(p), self.data)
        self.assertFalse(os.path.exists(p + ".part"))

    def test_wrong_md5_raises_and_keeps_nothing(self):
        with self.assertRaises(ValueError):
            ne.fetch_image(self.url, "0" * 32, self.out)
        self.assertEqual(os.listdir(self.out), [])

    def test_damaged_cached_file_is_fetched_again(self):
        os.makedirs(self.out)
        bad = os.path.join(self.out, self.md5 + ".img")
        with open(bad, "wb") as f:
            f.write(b"half a file")
        self.assertEqual(self.read(ne.fetch_image(self.url, self.md5, self.out)), self.data)


PROMAN_WARNING = (b"WARNING * 1050B:35B * 1988-12-16 09:34:12 * PROMAN.113127B\r\n"
                  b"          Processor Manager - PROMAN\r\n"
                  b"          No domino controllers found in system\r\n\r\n")
MTSERV_INFO = (b"INFO    * 1012B:0B * 1988-12-16 09:34:13 * MTSERV.30212B\r\n"
               b"          MTAD Server\r\n"
               b"          MTAD server initiated and started\r\n\r\n")


class LineText(unittest.TestCase):
    """line_text(): the text match="lines" patterns are searched in (SINTRAN ends a
    console line CR LF, measured on ND5000.IMG 2026-11-04)."""

    def test_cr_left_out_and_map_back(self):
        data = b"(Y/N): \r\n" + PROMAN_WARNING
        text, where = ne.line_text(data)
        self.assertNotIn(b"\r", text)
        self.assertEqual(len(text), len(where))
        i = text.index(b"PROMAN")
        self.assertEqual(data[where[i]:where[i] + 6], b"PROMAN")

    def test_question_line_ends_a_line_with_re_m(self):
        import re
        data = b">> User SYSTEM, UTILITY and BPUN will be checked (Y/N): \r\n" + PROMAN_WARNING + MTSERV_INFO
        text, _ = ne.line_text(data)
        self.assertIsNotNone(re.search(rb"\(Y/N\): ?$", text, re.M))
        self.assertIsNone(re.search(rb"\(Y/N\): ?$", data))   # without line mode: hidden

    def test_mode_is_checked(self):
        with self.assertRaises(ValueError):
            ne.Nd100x(exe=FAKE, match="anywhere")


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

    def test_settle_returns_output_once_quiet(self):
        with self.vm() as vm:
            vm.expect("RUNNING", timeout=10)
            vm.escape(timeout=10)
            vm.login("SYSTEM")
            vm.sendline("LIST-USERS")
            out = vm.settle(quiet=0.5, timeout=10)
            self.assertIn("OK LIST-USERS", out)
            self.assertTrue(out.rstrip().endswith("@"))
            self.assertEqual(vm.settle(quiet=0.3, timeout=10), "")  # nothing new since

    def test_question_found_past_console_messages(self):
        with self.vm(match="lines") as vm:
            vm.expect("RUNNING", timeout=10)
            vm.escape(timeout=10)
            vm.login("SYSTEM")
            vm.sendline("QUESTION")
            vm.wait_quiet(0.5)                  # the reports are on the screen too
            vm.expect(ne.Regex(r"\(Y/N\): ?$"), timeout=10)
            out = vm.command("Y", timeout=10)
            self.assertIn("OK Y", out)
            self.assertIn("MTAD server initiated", vm.before())

    def test_poll_returns_what_came_and_moves_on(self):
        with self.vm() as vm:
            vm.expect("RUNNING", timeout=10)
            vm.escape(timeout=10)
            vm.login("SYSTEM")
            vm.sendline("QUESTION")
            got = ""
            for _ in range(40):
                got += vm.poll(0.25)
                if "MTAD server initiated and started\r\n\r\n" in got:
                    break
            self.assertIn("Delete the previous version (Y/N): \r\nERROR   * 1B:12B", got)
            self.assertEqual(vm.poll(0.2), "")  # nothing new since

    def test_console_messages_hide_the_question_by_default(self):
        with self.vm() as vm:
            vm.expect("RUNNING", timeout=10)
            vm.escape(timeout=10)
            vm.login("SYSTEM")
            vm.sendline("QUESTION")
            with self.assertRaises(ne.ExpectTimeout):
                vm.expect(ne.Regex(r"\(Y/N\): ?$"), timeout=1)

    def test_settle_abort(self):
        with self.vm() as vm:
            vm.expect("RUNNING", timeout=10)
            vm.escape(timeout=10)
            vm.login("SYSTEM")
            vm.sendline("BAD THING")
            with self.assertRaises(ne.ExpectAbort):
                vm.settle(quiet=0.5, timeout=10, abort="BAD")

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
