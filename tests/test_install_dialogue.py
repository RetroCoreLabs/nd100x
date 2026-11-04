#!/usr/bin/env python3
"""Unit tests for tools/installs/dialogue.py (no emulator needed).

A stand-in machine plays a script: what it prints before the first answer, and what
it prints after each answer. A fake clock moves 0.5 s on every poll that brings
nothing, so the timeout tests take no real time.
"""

import importlib.util
import os
import sys
import unittest

INSTALLS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools", "installs")
sys.path.insert(0, INSTALLS)
from dialogue import (State, When, run_dialogue, DialogueFailed, OutOfOrder,  # noqa: E402
                      DialogueTimeout)

# Console reports as SINTRAN prints them (CR LF), seen on ND5000.IMG 2026-11-04.
PROMAN_WARNING = ("WARNING * 1050B:35B * 1988-12-16 09:34:12 * PROMAN.113127B\r\n"
                  "          Processor Manager - PROMAN\r\n"
                  "          No domino controllers found in system\r\n\r\n")


class Clock:
    def __init__(self):
        self.now = 0.0

    def __call__(self):
        return self.now


class Machine:
    """Prints script[0] at once and script[i] after the i-th answer. A piece may be a
    list of chunks: one chunk per poll. `forever`: printed on every poll once the
    script has run out (a machine that keeps printing)."""

    def __init__(self, script, clock, forever=None):
        self.pending = []
        self.script = list(script)
        self.clock = clock
        self.forever = forever
        self.typed = []
        self.notes = []
        self.ejected = []
        self.mounted = []
        self._next()

    def _next(self):
        if self.script:
            piece = self.script.pop(0)
            self.pending.extend(piece if isinstance(piece, list) else [piece])

    def poll(self, wait=0.5):
        if self.pending:
            self.clock.now += 0.01
            return self.pending.pop(0)
        self.clock.now += wait
        return self.forever or ""

    def sendline(self, text=""):
        self.typed.append(text)
        self._next()

    def note(self, text):
        self.notes.append(text)

    def eject(self, unit):
        self.ejected.append(unit)

    def mount(self, unit, path):
        self.mounted.append((unit, path))


def install_script(product):
    """tools/installs/<product>/install.py as a module (its tables, not its main())."""
    spec = importlib.util.spec_from_file_location(
        product.replace("-", "_"), os.path.join(INSTALLS, product, "install.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def run(script, states, forever=None, **kw):
    clock = Clock()
    vm = Machine(script, clock, forever)
    out = run_dialogue(vm, states, clock=clock, **kw)
    return vm, out


# The real BACKUP-SYSTEM I05 install, shortened (transcript of 2026-11-04).
BASY = [
    "<< Installation of Backup-System  version I >>\r\nWait.....\r\n"
    "Checking resources on user SYSTEM....\r\nOK!\r\n"
    ">> Do you want to delete previous version of the BACKUP-SYSTEM if it exist ?\r\n"
    ">> User SYSTEM, UTILITY and BPUN will be checked (Y/N): ",
    "Y\r\n\r\n--> Wait approx. 1 - 2 min.\r\n\r\n"
    ">> Please specify which user you want the files BACKUP-SYS-I:PROG and\r\n"
    ">> BACKUP-SERV-I:PROG copied to (default: UTILITY) : ",
    "\r\n>> Copying BACKUP-SYS-I05:PROG to user UTILITY\r\nWait....\r\n"
    ">> Should the BACKUP-SYSTEM be dumped reentrant (Y/N) ? ",
    "Y\r\n@DUMP-PROGRAM-REENTRANT BACKUP-SYSTEM-I (UTILITY)BACKUP-SYS-I\r\n"
    "Remove floppy 210337I05-XX-01D from station 1 unit 0 and type \"CR\"",
    "\r\n>> Press \"RETURN\" to continue.",
    "\r\n= NB: Append the file (SYSTEM)BACKUP-LOAD-I:MODE to the HENT-MODE file  =\r\n"
    "<< Installation finished >>\r\n\r\n@",
]


def basy_states():
    return install_script("backup-system-i05").DIALOGUE


class RealInstall(unittest.TestCase):
    def test_backup_system_i05(self):
        vm, out = run(BASY, basy_states())
        self.assertEqual(vm.typed, ["Y", "", "Y", "", ""])
        self.assertEqual(vm.ejected, [0])
        self.assertIn("<< Installation finished >>", out)
        self.assertNotIn("\r", out)
        self.assertIn("[delete-old] answer 'Y' to", vm.notes[0])
        self.assertTrue(vm.notes[0].endswith("-> user"))
        self.assertTrue(vm.notes[-1].endswith("-> done"))

    def test_question_followed_by_a_console_report(self):
        script = list(BASY)
        script[0] = [script[0], "\r\n" + PROMAN_WARNING]
        vm, _ = run(script, basy_states())
        self.assertEqual(vm.typed, ["Y", "", "Y", "", ""])

    def test_question_inside_a_cut_console_report(self):
        # ND5000.IMG with its boot batch, 2026-11-04: MTSERV's report was cut by the
        # installer's question and went on after it, on the question's own line.
        script = list(BASY)
        script[1] = ["Y\r\n\r\n--> Wait approx. 1 - 2 min.\r\n\r\n\r\n\r\n"
                     "INFO    * 1012B:0B * 1988-12-16 09:34:13 * MTSERV.30212B\r\n"
                     "          MTAD Server\r\n"
                     "          MTAD server initiated and s",
                     "\r\n>> Please specify which user you want the files BACKUP-SYS-I:PROG and\r\n"
                     ">> BACKUP-SERV-I:PROG copied to (default: UTILITY) : ",
                     "tarted\r\n\r\n" + PROMAN_WARNING]
        vm, _ = run(script, basy_states())
        self.assertEqual(vm.typed, ["Y", "", "Y", "", ""])


# ND's standard installer, as the transcripts of 2026-11-02 (Backup Manager) and
# 2026-11-04 (Linkage-Loader, LINKER) show it: the question lines exactly as printed,
# the MESSAGE boxes shortened.
BOX = ("\r\n::::::::::::::::::::::::::::::::::: MESSAGE :::::::::::::::::::::::::::::::::::\r\n"
       "%s\r\n:::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::\r\n")
CR_Q = '>> Press "CR" to continue'
BM_CR_Q = ">> Press CR to continue"


def menu(n, question_mark="?"):
    return ("\r\nII   5  Exit program                                                         II\r\n"
            "\r\n>>Which module do you want to execute%s (default:'%d'): " % (question_mark, n))


NLL = [
    BOX % "IN-NLL-XX-H02:XCOM will be copied to user SYSTEM." + CR_Q,
    BOX % "IN-NLL-XX-H02:INST will be created on user SYSTEM" + CR_Q,
    BOX % "The termination handling is defined on (SYSTEM)IN-NLL-XX-H02:PROG." + CR_Q,
    "\r\n@enable-term-handl,b,,,\r\n" + menu(1),
    "1\r\n>>On which user do you want to place the domain? (default:'DOMAIN-USER'): ",
    "\r\n>>On which user do you want to place the utility files? (default:'UTILITY'): ",
    menu(2),
    "2\r\n>>Do you want to delete the previous version of the product? (default:'Y'): ",
    "N\r\n>>Do you want to delete the current version of the product (default:'Y'): ",
    "N\r\n" + BOX % "No files deleted!" + menu(3),
    "3\r\n" + BOX % "The environment is OK." + menu(4),
    "4\r\n" + BOX % "Copying the domain: LINKAGE-LOAD-H02 to user DOMAIN-USER"
    + BOX % "Remove floppy 210319H--01D from station 1 unit 0." + CR_Q,
    BOX % "Remember to append these lines to the ND500-HENT file on user SYSTEM:" + CR_Q,
    menu(5),
    "5\r\nI  N  S  T  A  L  L  A  T  I  O  N     -     F  I  N  I  S  H  E  D\r\n"
    ">>Do you want to copy IN-NLL-XX-H02:INST to floppy number 1? (default:'N'): ",
    "N\r\n\r\n@",
]

LINKER = [
    BOX % "IN-LINK-XX-B01:XCOM will be copied to user SYSTEM." + CR_Q,
    BOX % "IN-LINK-XX-B01:INST will be created on user SYSTEM" + CR_Q,
    BOX % "The termination handling is defined on (SYSTEM)IN-LINK-XX-B01:PROG." + CR_Q,
    menu(1),
    "1\r\n>>On which user do you want to place the domain? (default:'DOMAIN-USER'): ",
    "\r\n>>On which user do you want to place the utility files? (default:'UTILITY'): ",
    menu(2),
    "2\r\n>>Do you want to delete the previous version of the product? : ",
    "N\r\n>>Do you want to delete the current version of the product : ",
    "N\r\n" + BOX % "No files deleted!" + menu(3),
    "3\r\n" + BOX % "The environment is OK." + menu(4),
    "4\r\n" + BOX % "Copying the file: LINKER-AUTO-BASC:JOB to user UTILITY"
    + BOX % "Remove floppy 211224B--01D from station 1 unit 0." + CR_Q,
    BOX % "Defining the domain: LINKER as a standard-domain." + menu(5),
    "5\r\n>>Do you want to copy IN-LINK-XX-B01:INST to floppy number 1? : ",
    "N\r\n" + BOX % "File: DDBTABLES-G:VTM or a newer version on user SYSTEM." + "\r\n@",
]


def bm_swap(old, new):
    return (BOX % ("Copying from floppy %s is FINISHED!" % old)
            + BOX % ("Remove floppy %s and insert\r\nfloppy %s into the floppy station 1 unit 0."
                     % (old, new)) + BM_CR_Q)


BM = [
    BOX % "IN-BCKMAN-XX-B02:INST will, if not already existing on either user SYSTEM or" + BM_CR_Q,
    BOX % "The program IN-BCKMAN-XX-B02:PROG will be copied to user SYSTEM." + BM_CR_Q,
    BOX % "@DEFINE-ESCAPE-CHARACTER,1D,,\r\nThe file must be deleted manually." + BM_CR_Q,
    menu(1, ""),
    "1\r\n>>Backup Manager can be installed for SIBAS/R backup\r\n"
    ">> (with SIBAS (R),without SIBAS (N)) (default:'R'): ",
    "N\r\n>>Which language version do you want to install (default:'EN'): ",
    "EN\r\n" + menu(2, ""),
    "2\r\n>>Do you want to delete previous version of the product : ",
    "N\r\n>>Do you want to delete current version of the product : ",
    "N\r\n" + BOX % "No files deleted!" + BOX % "The Delete module is finished." + BM_CR_Q,
    menu(3, ""),
    "3\r\n" + BOX % ("Choose between the following directories for the user BACKUP-MANAGER:\r\n\r\n"
                     "DIR INDEX 0 : DISC-75MB-1 UNIT 0 : PACK-ONE\r\n"
                     "DIR INDEX 40 : FLOPPY-DISC-1 UNIT 0 : 211226B02-XX-01D")
    + "\r\n>>Give directory name : ",
    "PACK-ONE\r\n" + BOX % "There are enough free segments." + BOX % "The environment is adequate."
    + BM_CR_Q,
    menu(4, ""),
    "4\r\n" + bm_swap("211226B02-XX-01D", "211226B02-XX-02D"),
    bm_swap("211226B02-XX-02D", "211226B02-XX-03D"),
    bm_swap("211226B02-XX-03D", "211226B02-XX-04D"),
    bm_swap("211226B02-XX-04D", "211226B02-EN-01D"),
    BOX % "Copying from floppy 211226B02-EN-01D is FINISHED!"
    + BOX % "Remove floppy 211226B02-EN-01D from station 1 unit 0." + BM_CR_Q,
    BOX % "The mode file: BM-SERVER-B02:MODE will now be executed"
    + BOX % ("Remember to append these lines to the HENT-MODE file on user SYSTEM:\r\n\r\n"
             "@MODE (BACKUP-MANAGER)BM-DUMP-:MODE,,,,\r\n"
             "These lines are written to the logg file IN-BCKMAN-XX-B02:LOGG too.") + BM_CR_Q,
    menu(5, ""),
    "5\r\n" + BOX % "3 : The parameter file will be deleted on user SYSTEM and will NOT be"
    + "\r\n>>What will you do with the parameter file? : ",
    "3\r\nInclude the next line in the LOAD-MODE:MODE (or corresponding) file:\r\n"
    "@MODE (BACKUP-MANAGER)BM-START-B02:MODE,,,,,,\r\n\r\n@",
]


class StandardInstallers(unittest.TestCase):
    def test_linkage_loader_h02(self):
        vm, _ = run(NLL, install_script("linkage-loader-h02").dialogue())
        self.assertEqual(vm.typed, ["", "", "", "1", "", "", "2", "N", "N", "3", "4", "", "",
                                    "5", "N"])
        self.assertEqual(vm.ejected, [0])

    def test_linker_b01(self):
        vm, _ = run(LINKER, install_script("linker-b01").dialogue())
        self.assertEqual(vm.typed, ["", "", "", "1", "", "", "2", "N", "N", "3", "4", "", "5", "N"])
        self.assertEqual(vm.ejected, [0])

    def test_backup_manager_b02(self):
        bm = install_script("backup-manager-b02")
        bm.WORK_FLOPPIES.update({name: "/work/%s.img" % name for name in bm.FLOPPIES})
        vm, _ = run(BM, bm.dialogue())
        self.assertEqual(vm.typed, ["", "", "", "1", "N", "EN", "2", "N", "N", "", "3",
                                    "PACK-ONE", "", "4", "", "", "", "", "", "", "5", "3"])
        self.assertEqual(vm.mounted, [(0, "/work/211226B02-XX-02D.img"),
                                      (0, "/work/211226B02-XX-03D.img"),
                                      (0, "/work/211226B02-XX-04D.img"),
                                      (0, "/work/211226B02-EN-01D.img")])
        self.assertEqual(vm.ejected, [0])

    def test_menu_default_must_be_the_next_module(self):
        # Module 1 did not finish: the installer offers module 1 again.
        script = NLL[:6] + [menu(1)]
        with self.assertRaises(DialogueFailed) as e:
            run(script, install_script("linkage-loader-h02").dialogue())
        self.assertIn("module 1 did not finish", str(e.exception))

    def test_menu_after_module_5_stops(self):
        script = NLL[:-1] + ["N\r\n" + menu(5)]
        with self.assertRaises(OutOfOrder):
            run(script, install_script("linkage-loader-h02").dialogue())


class Choosing(unittest.TestCase):
    def test_earliest_match_wins_not_list_order(self):
        # "b\?$" is listed first but matches at 5; "^a.*\?$" matches at 0 and wins.
        states = [State("s", [When(r"b\?$", "2", "end"), When(r"^a.*\?$", "1", "end")]),
                  State("end", [When(r"^@$", done=True)])]
        vm, _ = run(["a or b?", "\r\n@"], states)
        self.assertEqual(vm.typed, ["1"])

    def test_leftover_question_after_an_answer_is_out_of_order(self):
        # Two questions on the screen before one answer: the second is not answered.
        states = [State("s", [When(r"first\?$", "1", "end"), When(r"second\?$", "2", "end")]),
                  State("end", [When(r"^@$", done=True)])]
        with self.assertRaises(OutOfOrder):
            run(["first?\r\nsecond?", "\r\n@"], states)

    def test_tie_goes_to_the_first_listed(self):
        states = [State("s", [When(r"^Delete.*\?$", "A", "end"), When(r"^Delete", "B", "end")]),
                  State("end", [When(r"^@$", done=True)])]
        vm, _ = run(["Delete it?", "\r\n@"], states)
        self.assertEqual(vm.typed, ["A"])

    def test_branch_picks_its_own_next_state(self):
        states = [State("ask", [When(r"exists, overwrite\?$", "Y", "over"),
                                When(r"copied\.$", None, "end")]),
                  State("over", [When(r"overwritten\.$", None, "end")]),
                  State("end", [When(r"^@$", done=True)])]
        vm, _ = run(["file copied.\r\n@"], states)
        self.assertEqual(vm.typed, [])
        vm, _ = run(["file exists, overwrite?", "\r\noverwritten.\r\n@"], states)
        self.assertEqual(vm.typed, ["Y"])

    def test_state_jumps_to_itself(self):
        states = [State("msg", [When(r'Press "CR" to continue$', "", "msg"),
                                When(r"^Which module\? $", "1", "end")]),
                  State("end", [When(r"^@$", done=True)])]
        script = ['>> Press "CR" to continue'] * 3 + ["\r\nWhich module? ", "\r\n@"]
        vm, _ = run(script, states)
        self.assertEqual(vm.typed, ["", "", "", "1"])

    def test_goto_by_function_counts_modules(self):
        left = ["1", "2"]

        def module(vm, m):
            return left[0]

        def after(m):
            return "m" + left.pop(0)
        states = [State("menu", [When(r"^Module\? $", module, after)]),
                  State("m1", [When(r"^one done$", None, "menu")]),
                  State("m2", [When(r"^two done$", None, "end")]),
                  State("end", [When(r"^@$", done=True)])]
        vm, _ = run(["Module? ", "\r\none done\r\nModule? ", "\r\ntwo done\r\n@"], states)
        self.assertEqual(vm.typed, ["1", "2"])

    def test_question_split_over_writes_waits_until_complete(self):
        states = [State("q", [When(r"\(Y/N\) ?: ?$", "Y", "end")]),
                  State("end", [When(r"^@$", done=True)])]
        vm, _ = run([[">> Delete it (Y", "/N): "], "\r\n@"], states)
        self.assertEqual(vm.typed, ["Y"])


class Stopping(unittest.TestCase):
    def test_fail_line_stops_before_anything_is_typed(self):
        states = basy_states()
        script = ["Error when loading Control Store.\r\nNOT READ ACCESS\r\n" + BASY[0]]
        clock = Clock()
        vm = Machine(script, clock)
        with self.assertRaises(DialogueFailed) as e:
            run_dialogue(vm, states, fail=r"NOT READ ACCESS|Error when loading", clock=clock)
        self.assertEqual(vm.typed, [])
        self.assertIn("state delete-old", str(e.exception))
        self.assertIn("NOT READ ACCESS", str(e.exception))

    def test_question_of_another_state_is_not_answered(self):
        states = basy_states()
        script = [">> Should the BACKUP-SYSTEM be dumped reentrant (Y/N) ? "]
        clock = Clock()
        vm = Machine(script, clock)
        with self.assertRaises(OutOfOrder) as e:
            run_dialogue(vm, states, clock=clock)
        self.assertEqual(vm.typed, [])
        self.assertIn("state reentrant", str(e.exception))

    def test_unknown_text_is_ignored(self):
        states = basy_states()
        script = list(BASY)
        script[1] = "Batch input error\r\nNo such file name\r\n" + script[1]
        vm, _ = run(script, states)
        self.assertEqual(vm.typed, ["Y", "", "Y", "", ""])


class Timeouts(unittest.TestCase):
    def states(self, timeout=300):
        return [State("q", [When(r"\(Y/N\): $", "Y", "end")], timeout=timeout),
                State("end", [When(r"^@$", done=True)])]

    def test_state_timeout_while_the_machine_keeps_printing(self):
        clock = Clock()
        vm = Machine(["Wait"], clock, forever=".")
        with self.assertRaises(DialogueTimeout) as e:
            run_dialogue(vm, self.states(timeout=60), clock=clock)
        msg = str(e.exception)
        self.assertIn("state q", msg)
        self.assertIn("within 60 s", msg)
        self.assertIn(r"\(Y/N\): $", msg)
        self.assertLess(clock.now, 70)

    def test_silence_timeout(self):
        clock = Clock()
        vm = Machine(["Wait...."], clock)
        with self.assertRaises(DialogueTimeout) as e:
            run_dialogue(vm, self.states(timeout=3600), silence=120, clock=clock)
        self.assertIn("no output at all for 120 s", str(e.exception))
        self.assertLess(clock.now, 125)

    def test_whole_run_timeout(self):
        clock = Clock()
        vm = Machine(["Wait"], clock, forever=".")
        with self.assertRaises(DialogueTimeout) as e:
            run_dialogue(vm, self.states(timeout=3600), total=200, clock=clock)
        self.assertIn("more than 200 s", str(e.exception))


class Nd5000Version(unittest.TestCase):
    """install_common.nd5000_loaded() on the monitor's VERSION output (2026-11-04)."""

    def setUp(self):
        import install_common
        self.loaded = install_common.nd5000_loaded

    def test_before_the_start(self):
        out = ("VERSION\r\n\r\nSubsystem part: 88. 6.16 REV.-J04\r\nSystem part...: 88. 8.17\r\n"
               "Swapper.......: \r\nMicro program.:     0\r\nCpu type......:  5000\r\n")
        self.assertIsNone(self.loaded(out))

    def test_after_the_start(self):
        out = ("Subsystem part: 88. 6.16 REV.-J04\r\nSystem part...: 88. 8.17\r\n"
               "Swapper.......: REV.-K01\r\nMicro program.: 11930\r\nCpu type......:  5000\r\n")
        self.assertEqual(self.loaded(out), ("REV.-K01", "11930"))


class Table(unittest.TestCase):
    def test_goto_to_a_missing_state(self):
        with self.assertRaises(ValueError):
            run_dialogue(Machine([], Clock()), [State("a", [When("x", "", "nowhere")])])

    def test_state_twice(self):
        s = State("a", [When("x", done=True)])
        with self.assertRaises(ValueError):
            run_dialogue(Machine([], Clock()), [s, s])

    def test_when_needs_goto_or_done(self):
        with self.assertRaises(ValueError):
            When("x", "Y")


if __name__ == "__main__":
    unittest.main()
