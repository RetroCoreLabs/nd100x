#!/usr/bin/env python3
"""audit.py - count every measurable rule of the house C standard, per file.

    tools/house/audit.py [--write FILE] [--compare FILE] [--rule R] [--list R]
                         [--build DIR] [--no-tidy] [--no-cc] [--jobs N]

Counts rule violations in every project C file (vendored and generated code
excluded) and prints a table "rule  count  what". The rule numbers are the
ones in the c-coding-standard skill; see docs/HOUSE_STANDARD_FULL_CLEANUP_PLAN.md
section 3 for which rule is measured how.

  --write FILE    store the counts as JSON (baseline / after a step)
  --compare FILE  exit 1 if any rule has MORE findings than in FILE, in total
                  or in any single file
  --rule R        only print rule R (e.g. 3.1); with --list, list the findings
  --list R        print every finding of rule R as file:line: text
  --build DIR     build tree with compile_commands.json and object files
                  (default build_gate_audit; configured and built if missing)
  --no-tidy       skip the clang-tidy checks (fast mode, text checks only)
  --no-cc         skip the extra-warning compile

Tools: clang-format and clang-tidy are taken from $CLANG_FORMAT /
$CLANG_TIDY, else from PATH. A missing tool is an error, never a silent 0.
"""

import argparse
import collections
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))

EXCLUDE = (
    re.compile(r"^external/"),
    re.compile(r"^template-glass/external/"),
    re.compile(r"^tools/mkptypes/"),
    re.compile(r"^src/devices/scsi/ncr5386\.[ch]$"),
    re.compile(r"_protos\.h$"),
)

# Names the profile keeps (nd100x profile A.5, A.6): WASM exports and main.
KEEP_NAMES = {"main", "Init", "Boot", "SendKeyToTerminal", "scsi_debug_enabled"}

# Decisions already taken, which the rule text alone would re-litigate.
# rename_naming.py carries the same list; keep the two in step.
#
#   opcode_*   the instruction handlers are opcode_<mnemonic>_<what it does>
#              by Ronny's choice (21-SEP-2026); the file prefix does not
#              apply to them.
#   g[A-Z]*    the register access macros gPC, gA, gD, gB, gT, gX, gL, which
#              the repo CLAUDE.md documents as the convention.
#   _A.._X     the register file indices and their enum constants. One family
#              declared together in cpu_types.h; renaming half of it is worse
#              than renaming none.
#   _DEGRADE_  a behavioural flag, documented in docs/ND-DOMAIN-GOTCHAS.md.
DECIDED_PREFIX = ("opcode_",)
DECIDED_NAMES = {
    "_A", "_B", "_D", "_L", "_P", "_T", "_X", "_STS",
    "_U0", "_U1", "_U2", "_U3", "_U4", "_U5", "_U6", "_U7",
    "Four", "Sixteen", "_DEGRADE_", "_removed_MOVB_AND_MOVBF_",
}
DECIDED_MACRO = re.compile(r"^g[A-Z]")


def decided(name):
    """True for a name whose spelling has already been settled."""
    return (name in DECIDED_NAMES or name.startswith(DECIDED_PREFIX)
            or DECIDED_MACRO.match(name) is not None)

# Platform macros: profile A.2 chose the compiler macros.
RETIRED_PLATFORM = re.compile(r"\bPLATFORM_(WASM|WINDOWS|LINUX|RISCV)\b")
PLATFORM_TEST = re.compile(
    r"^\s*#\s*(if|ifdef|ifndef|elif)\b.*\b(__EMSCRIPTEN__|_WIN32|__riscv|__linux__|__APPLE__)\b"
)

BANNED = re.compile(
    r"\b(gets|strcpy|strcat|sprintf|vsprintf|strncpy|atoi|atol|atof|alloca|system)\s*\("
)
PRINT_DIAG = re.compile(r"\b(printf|puts|perror)\s*\(|\bfprintf\s*\(\s*(stdout|stderr)\b")
EXIT_CALL = re.compile(r"\b(exit|abort|_Exit)\s*\(")
GETENV = re.compile(r"\bgetenv\s*\(")
DEBUG_GATE = re.compile(r"^\s*#\s*(if|ifdef|ifndef|elif)\b.*\bDEBUG\w*")
IF0 = re.compile(r"^\s*#\s*if\s+0\b")
GOTO = re.compile(r"\bgoto\s+(\w+)\s*;")
REALLOC_SELF = re.compile(r"\b(\w+(?:->\w+|\.\w+)*)\s*=\s*\(?[^;=]*\)?\s*realloc\s*\(\s*\1\s*,")
FIXME = re.compile(r"\bFIXME\b")
TODO_BAD = re.compile(r"\bTODO\b(?!\(\w+\))")
PRAGMA_ONCE = re.compile(r"^\s*#\s*pragma\s+once\b")
THREADS_H = re.compile(r"#\s*include\s*<threads\.h>|\b\w+_s\s*\(")
INCLUDE = re.compile(r'^\s*#\s*include\s*([<"])([^>"]+)[>"]')
BAD_TYPES = re.compile(r"\b(short|long|unsigned\s+char)\b")
UCHAR_CAST = re.compile(r"\(\s*unsigned\s+char\s*\)")
VOLATILE = re.compile(r"\bvolatile\b")
SETJMP = re.compile(r"\b(setjmp|longjmp|sigsetjmp|siglongjmp)\s*\(")
SETJMP_FILES = {"src/cpu/cpu.c"}

# Library code for rules 7.4 / 9.4: everything except the frontends, tests
# and tools (rule 7.4 exempts main() and the frontend).
def is_library(path):
    return path.startswith("src/") and not path.startswith("src/frontend/")


def run(cmd, cwd=REPO, check=True, **kw):
    return subprocess.run(cmd, cwd=cwd, check=check, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, **kw)


def project_files():
    out = run(["git", "ls-files", "*.c", "*.h"]).stdout.split()
    return sorted(p for p in out if not any(r.search(p) for r in EXCLUDE))


def tool(env, name):
    path = os.environ.get(env) or shutil.which(name)
    if not path:
        sys.exit(f"audit: {name} not found (set ${env} or put it on PATH)")
    return path


class Findings:
    def __init__(self):
        self.items = collections.defaultdict(list)   # rule -> [(file, line, text)]
        self.what = {}

    def add(self, rule, path, line, text):
        self.items[rule].append((path, line, text.strip()[:160]))

    def counts(self):
        res = {}
        for rule, items in self.items.items():
            per = collections.Counter(p for p, _, _ in items)
            res[rule] = {"total": len(items), "files": dict(sorted(per.items()))}
        return res


WHAT = {
    "1.2": "threads.h / Annex K _s functions",
    "1.4": "#pragma once",
    "1.5": "non-ASCII / CR / tab / trailing ws / no final newline",
    "2.x": "clang-format changes needed (2.1-2.6)",
    "2.2": "missing braces (clang-tidy)",
    "2.4": "more than one declaration per statement",
    "2.7": "switch without default / unmarked fall-through",
    "3.1": "non-static function not module_verb (lower snake) / mixed prefix in file",
    "3.2": "static function not snake_case",
    "3.3": "non-static global not g_snake_case, or extern in a .c file",
    "3.4-3.7": "identifier naming (locals, statics, macros, enums, types)",
    "3.8": "file name not snake_case",
    "3.9": "include guard missing / wrong / reserved",
    "4.1": "banner (purpose line, SPDX, copyright) missing",
    "4.3": "definition in a header",
    "4.4": "include order (own header first, <std>, <sys>, \"project\")",
    "4.5": "#include with \"../\"",
    "4.6": "non-static symbol not used outside its file",
    "5.2": "short / long / unsigned char used",
    "5.3": "shift of a negative value / shift count out of range (compiler)",
    "5.5": "signed/unsigned mixing (-Wsign-compare/-Wsign-conversion)",
    "5.6": "char widened without unsigned char cast",
    "5.7": "type punning / cast-align",
    "5.9": "pointer parameter could be const",
    "5.10": "volatile (each needs a review row)",
    "6.1": "assignment in condition",
    "6.2": "mixed operators without parentheses",
    "6.4": "goto that is not a forward jump to a cleanup label",
    "6.5": "alloca / VLA",
    "6.6": "recursion",
    "6.7": "#if 0 / commented-out code",
    "6.8": "macro hygiene",
    "7.1": "function over 100 lines or 6 parameters (no exemption)",
    "7.3": "return value not checked",
    "7.4": "exit/abort in library code",
    "7.6": "else after return",
    "8.3": "p = realloc(p, ...)",
    "8.4": "banned function",
    "8.5/8.7": "sizeof expression",
    "8.6": "non-literal format string",
    "9.4": "printf/puts/perror/fprintf(stdout|stderr) in library code",
    "9.7": "getenv switch / #ifdef DEBUG gate",
    "10.3": "setjmp/longjmp outside src/cpu/cpu.c",
    "11.2": "non-static function without Doxygen in a hand-written header",
    "11.4": "FIXME / TODO without (name)",
    "12.1": "retired PLATFORM_* macro",
    "12.2": "platform #if in src/cpu or src/devices",
    "REVIEW": "empty cells in docs/house-audit/review (manual rules)",
}

TIDY_RULES = {
    "readability-braces-around-statements": "2.2",
    "readability-isolate-declaration": "2.4",
    "readability-identifier-naming": "3.4-3.7",
    # hicpp-signed-bitwise removed 21-SEP-2026. It flags every signed
    # operand, which on this codebase is 3,102 correct lines: int is 32
    # bits, the emulated machine is 16, so a register value in an int has
    # 15 bits of headroom and the operations are well defined. Clearing it
    # would mean retyping ~1,124 variables for no behavioural gain, and an
    # index that legitimately reaches -1 would become four billion.
    # Rule 5.3 now targets the real hazards, which the compiler catches:
    # -Wshift-negative-value, -Wshift-overflow, -Wshift-count-overflow.
    "bugprone-signed-char-misuse": "5.6",
    "bugprone-casting-through-void": "5.7",
    "readability-non-const-parameter": "5.9",
    "bugprone-assignment-in-if-condition": "6.1",
    "readability-math-missing-parentheses": "6.2",
    "misc-no-recursion": "6.6",
    "bugprone-macro-parentheses": "6.8",
    "bugprone-macro-repeated-side-effects": "6.8",
    "readability-function-size": "7.1",
    "bugprone-unused-return-value": "7.3",
    "cert-err33-c": "7.3",
    "readability-else-after-return": "7.6",
    "bugprone-sizeof-expression": "8.5/8.7",
    "misc-definitions-in-headers": "4.3",
}

CC_FLAGS = {
    # Rule 5.3, narrowed 21-SEP-2026: these catch the cases that are actually
    # wrong - shifting a negative value, shifting past the sign bit, a shift
    # count at or beyond the width. The old blanket check reported every
    # signed operand, which here meant 3,102 correct lines.
    "-Wshift-negative-value": "5.3",
    "-Wshift-overflow": "5.3",
    "-Wshift-count-overflow": "5.3",
    "-Wshift-count-negative": "5.3",
    "-Wsign-compare": "5.5",
    "-Wsign-conversion": "5.5",
    "-Wswitch-default": "2.7",
    "-Wimplicit-fallthrough=": "2.7",
    "-Wcast-align=strict": "5.7",
    "-Wformat-nonliteral": "8.6",
    "-Wvla": "6.5",
}


# ---------------------------------------------------------------- text checks

def strip_comments_and_strings(src):
    """Return src with comments and string/char literals blanked (same line
    count), plus the list of (line, comment_text)."""
    out = []
    comments = []
    i, n, line = 0, len(src), 1
    while i < n:
        c = src[i]
        if src.startswith("//", i):
            j = src.find("\n", i)
            j = n if j < 0 else j
            comments.append((line, src[i:j]))
            out.append(" " * (j - i))
            i = j
        elif src.startswith("/*", i):
            j = src.find("*/", i + 2)
            j = n if j < 0 else j + 2
            text = src[i:j]
            comments.append((line, text))
            out.append("".join(ch if ch == "\n" else " " for ch in text))
            line += text.count("\n")
            i = j
        elif c in "\"'":
            j = i + 1
            while j < n and src[j] != c and src[j] != "\n":
                j += 2 if src[j] == "\\" else 1
            j = min(j + 1, n)
            out.append(c + " " * (j - i - 2) + (src[j - 1] if j - 1 > i else ""))
            i = j
        else:
            if c == "\n":
                line += 1
            out.append(c)
            i += 1
    return "".join(out), comments


CODE_LIKE = re.compile(
    r"^\s*(//|/?\*)?\s*("
    r"(if|for|while|switch|return)\s*\(.*"      # control statements
    r"|[A-Za-z_][\w\[\]\.\->]*\s*(=|\+=|-=|\|=|&=)\s*[^=].*;"  # assignment;
    r"|[A-Za-z_]\w*\s*\([^;]*\)\s*;"            # call;
    r"|#\s*(include|define|if|endif)\b.*"
    r"|[{}]\s*"
    r")\s*(\*/)?\s*$")


def check_text(path, f):
    with open(os.path.join(REPO, path), "rb") as fh:
        raw = fh.read()
    lines_raw = raw.split(b"\n")
    for no, ln in enumerate(lines_raw, 1):
        if any(b > 0x7F for b in ln):
            f.add("1.5", path, no, "non-ASCII byte")
        if ln.endswith(b"\r"):
            f.add("1.5", path, no, "CR line ending")
        if b"\t" in ln:
            f.add("1.5", path, no, "tab")
        if ln.rstrip(b"\r") != ln.rstrip(b"\r").rstrip(b" \t"):
            f.add("1.5", path, no, "trailing whitespace")
    if raw and not raw.endswith(b"\n"):
        f.add("1.5", path, len(lines_raw), "no newline at end of file")

    src = raw.decode("ascii", errors="replace")
    code, comments = strip_comments_and_strings(src)
    lines = src.split("\n")
    code_lines = code.split("\n")
    base = os.path.basename(path)
    is_header = path.endswith(".h")

    # 3.8 file names
    if not re.fullmatch(r"[a-z0-9_]+\.[ch]", base):
        f.add("3.8", path, 1, base)

    # 4.1 banner in the first 25 lines
    head = "\n".join(lines[:25])
    if "SPDX-License-Identifier:" not in head:
        f.add("4.1", path, 1, "no SPDX-License-Identifier")
    if not re.search(r"Copyright", head):
        f.add("4.1", path, 1, "no copyright line")
    if not re.search(r"\b" + re.escape(base) + r"\s*-\s*\S", head):
        f.add("4.1", path, 1, "no '<file> - <purpose>' line")

    # 3.9 include guard
    if is_header:
        guard = re.sub(r"[^A-Z0-9]", "_", base.upper())
        m = re.search(r"^\s*#\s*ifndef\s+(\w+)\s*\n\s*#\s*define\s+(\w+)", code, re.M)
        if not m or m.group(1) != m.group(2):
            f.add("3.9", path, 1, "no #ifndef/#define guard")
        elif m.group(1) != guard:
            f.add("3.9", path, code[:m.start()].count("\n") + 1,
                  f"guard {m.group(1)} should be {guard}")

    # 4.4 include order and 4.5
    incs = []
    for no, ln in enumerate(code_lines, 1):
        m = INCLUDE.match(lines[no - 1])
        # the raw line matches, and the line is not inside a comment
        if m and ln.lstrip().startswith("#"):
            incs.append((no, m.group(1), m.group(2)))
            if m.group(2).startswith("../"):
                f.add("4.5", path, no, lines[no - 1])
    if incs and path.endswith(".c"):
        own = base[:-2] + ".h"
        own_exists = os.path.exists(os.path.join(REPO, os.path.dirname(path), own))
        if own_exists and os.path.basename(incs[0][2]) != own:
            f.add("4.4", path, incs[0][0], f"first include is not {own}")
        # group rank: C standard <..> (no '/'), other system <..>, project "..."
        c_std = {"assert.h", "complex.h", "ctype.h", "errno.h", "fenv.h", "float.h",
                 "inttypes.h", "iso646.h", "limits.h", "locale.h", "math.h",
                 "setjmp.h", "signal.h", "stdalign.h", "stdarg.h", "stdatomic.h",
                 "stdbool.h", "stddef.h", "stdint.h", "stdio.h", "stdlib.h",
                 "stdnoreturn.h", "string.h", "tgmath.h", "time.h", "uchar.h",
                 "wchar.h", "wctype.h"}
        ranks = []
        for no, kind, name in incs:
            if own_exists and os.path.basename(name) == own:
                continue
            if kind == "<":
                ranks.append((no, 1 if name in c_std else 2, name))
            else:
                ranks.append((no, 3, name))
        top = 0
        for no, r, name in ranks:
            if r < top:
                f.add("4.4", path, no, f"{name} after a later group")
            top = max(top, r)

    # per-line code checks
    fn_start = None
    depth = 0
    labels_seen_later = set(re.findall(r"^\s*(\w+)\s*:", code, re.M))
    for no, ln in enumerate(code_lines, 1):
        full = lines[no - 1]
        if PRAGMA_ONCE.search(ln):
            f.add("1.4", path, no, full)
        if THREADS_H.search(full if full.lstrip().startswith("#") else ln):
            f.add("1.2", path, no, full)
        for _ in BANNED.finditer(ln):
            f.add("8.4", path, no, full)
        if REALLOC_SELF.search(ln):
            f.add("8.3", path, no, full)
        for _ in (PRINT_DIAG.finditer(ln) if is_library(path) else ()):
            f.add("9.4", path, no, full)
        for _ in (EXIT_CALL.finditer(ln) if is_library(path) else ()):
            f.add("7.4", path, no, full)
        for _ in (GETENV.finditer(ln) if not path.startswith("tests/") else ()):
            f.add("9.7", path, no, full)
        if DEBUG_GATE.search(ln):
            f.add("9.7", path, no, full)
        if IF0.search(ln):
            f.add("6.7", path, no, full)
        for _ in RETIRED_PLATFORM.finditer(ln):
            f.add("12.1", path, no, full)
        if PLATFORM_TEST.search(ln) and re.match(r"src/(cpu|devices)/", path):
            f.add("12.2", path, no, full)
        if re.search(r"\balloca\s*\(", ln):
            f.add("6.5", path, no, full)
        if not ln.lstrip().startswith("#"):
            # one finding per occurrence (a line split by clang-format must
            # not change the count); a cast to unsigned char is not a data
            # type and is what rule 5.6 asks for
            for _ in BAD_TYPES.finditer(UCHAR_CAST.sub(" ", ln)):
                f.add("5.2", path, no, full)
        for _ in VOLATILE.finditer(ln):
            f.add("5.10", path, no, full)
        if SETJMP.search(ln) and path not in SETJMP_FILES:
            f.add("10.3", path, no, full)
        if path.endswith(".c") and re.match(r"\s*extern\b", ln):
            f.add("3.3", path, no, full)
        m = GOTO.search(ln)
        if m:
            label = m.group(1)
            later = re.search(r"^\s*" + re.escape(label) + r"\s*:", "\n".join(code_lines[no:]), re.M)
            if not later:
                f.add("6.4", path, no, "goto " + label + " jumps backwards")
            elif not re.match(r"(fail|error|err|out|cleanup|done|exit)\w*$", label):
                f.add("6.4", path, no, "goto " + label + " is not a cleanup label")
    for no, text in comments:
        if FIXME.search(text):
            f.add("11.4", path, no, text.splitlines()[0])
        for k, tl in enumerate(text.splitlines()):
            if TODO_BAD.search(tl):
                f.add("11.4", path, no + k, tl)
            if CODE_LIKE.match(tl) and not re.search(r"\b(e\.g\.|i\.e\.|example|Example)\b", tl):
                f.add("6.7", path, no + k, tl)


# ---------------------------------------------------------------- clang-format

def check_format(files, f, jobs):
    cf = tool("CLANG_FORMAT", "clang-format")

    def one(path):
        r = run([cf, "--dry-run", "--style=file", path], check=False)
        return path, r.stderr

    with ThreadPoolExecutor(jobs) as ex:
        for path, err in ex.map(one, files):
            for m in re.finditer(rf"^{re.escape(path)}:(\d+):\d+: warning: (.*)$", err, re.M):
                f.add("2.x", path, int(m.group(1)), m.group(2))


# ---------------------------------------------------------------- build tree

def ensure_build(build):
    bdir = os.path.join(REPO, build)
    cc = os.path.join(bdir, "compile_commands.json")
    if not os.path.exists(cc):
        os.makedirs(bdir, exist_ok=True)
        run(["cmake", "..", "-DCMAKE_BUILD_TYPE=Debug",
             "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"], cwd=bdir)
    run(["cmake", "--build", ".", "-j", str(os.cpu_count() or 4)], cwd=bdir)
    with open(cc) as fh:
        entries = json.load(fh)
    return bdir, entries + extra_entries(bdir)


# Sources outside the CMake build (tools/reth-tap has its own Makefile). They
# are compiled here with the house language level so the symbol, clang-tidy
# and warning checks see them too. The reth-tap tests #include reth-tap.c;
# symbols they get that way are not defined in the test file and are skipped
# by the strict definition lookup.
EXTRA_SOURCES = ["tools/reth-tap/reth-tap.c", "tools/reth-tap/test-framing.c",
                 "tools/reth-tap/test-gateway.c"]


def extra_entries(bdir):
    out = os.path.join(bdir, "house_extra")
    os.makedirs(out, exist_ok=True)
    res = []
    for rel in EXTRA_SOURCES:
        src = os.path.join(REPO, rel)
        if not os.path.exists(src):
            continue
        obj = os.path.join(out, os.path.basename(rel) + ".o")
        cmd = ["cc", "-std=gnu11", "-D_GNU_SOURCE", "-c", src, "-o", obj]
        subprocess.run(cmd, cwd=os.path.dirname(src), stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL)
        res.append({"directory": os.path.dirname(src), "file": src, "arguments": cmd})
    return res


def project_entries(entries, files):
    fs = set(files)
    res = []
    for e in entries:
        rel = os.path.relpath(os.path.join(e["directory"], e["file"]), REPO)
        if rel in fs:
            res.append((rel, e))
    return res


def check_tidy(bdir, entries, files, f, jobs):
    ct = tool("CLANG_TIDY", "clang-tidy")
    cfg = os.path.join(REPO, "tools", "house", "audit.clang-tidy")
    fs = set(files)
    seen = set()

    def one(item):
        rel, e = item
        r = run([ct, "-p", bdir, "--config-file=" + cfg, "--quiet",
                 os.path.join(REPO, rel)], check=False)
        return r.stdout

    with ThreadPoolExecutor(jobs) as ex:
        for out in ex.map(one, project_entries(entries, files)):
            for m in re.finditer(r"^(/[^:\n]+):(\d+):(\d+): warning: (.*) \[([\w\-.,]+)\]$", out, re.M):
                rel = os.path.relpath(m.group(1), REPO)
                if rel not in fs:
                    continue
                for check in m.group(5).split(","):
                    rule = TIDY_RULES.get(check)
                    if not rule:
                        continue
                    key = (rule, rel, m.group(2), m.group(3), check)
                    if key in seen:
                        continue
                    seen.add(key)
                    if rule == "7.1" and size_exempt(rel, int(m.group(2))):
                        continue
                    if rule == "3.4-3.7" and static_ok(rel, int(m.group(2)), m.group(4)):
                        continue
                    named = ANY_CASE_MSG.match(m.group(4))
                    if rule == "3.4-3.7" and named and decided(named.group(1)):
                        continue
                    f.add(rule, rel, int(m.group(2)), m.group(4))


# Rule 3.4 allows a static file-scope variable to be s_snake_case OR plain
# snake_case. clang-tidy cannot express that: in C it calls every file-scope
# variable a "global variable" (StaticVariableCase only reaches function-local
# statics), so audit.clang-tidy's GlobalVariablePrefix g_ makes it demand a g_
# on compliant statics such as rtc_wall_clock_mode and s_wrtc_bits. Those are
# not findings and are dropped here.
CASE_MSG = re.compile(r"invalid case style for global variable '([^']+)'")
# Any kind, not just global variable: the settled names include macros
# (gPC), enum constants (Four, Sixteen) and the register indices.
ANY_CASE_MSG = re.compile(r"invalid case style for .+? '([^']+)'")
STATIC_NAME = re.compile(r"^(s_)?[a-z][a-z0-9_]*$")


def static_ok(rel, line, message):
    """True for a file-scope static whose name rule 3.4 already allows."""
    found = CASE_MSG.match(message)
    if not found or not STATIC_NAME.match(found.group(1)):
        return False
    try:
        with open(os.path.join(REPO, rel), encoding="utf-8", errors="replace") as h:
            lines = h.readlines()
    except OSError:
        return False
    if line > len(lines):
        return False
    # The declaration may be wrapped; look back to the end of the previous one.
    text = ""
    for no in range(line - 1, max(-1, line - 4), -1):
        text = lines[no] + text
        if re.search(r"\bstatic\b", text):
            return True
        if re.search(r"[;{}]", lines[no]) and no != line - 1:
            break
    return False


def size_exempt(rel, line):
    with open(os.path.join(REPO, rel), errors="replace") as fh:
        lines = fh.read().split("\n")
    above = "\n".join(lines[max(0, line - 6):line])
    return "size exemption: opcode table" in above


def check_cc(entries, files, f, jobs):
    fs = set(files)
    seen = set()
    flags = [k if not k.endswith("=") else k + "5" for k in CC_FLAGS]

    def one(item):
        rel, e = item
        args = e.get("arguments") or shlex.split(e["command"])
        args = [a for a in args if a not in ("-o",)]
        # drop "-o <obj>" and "-c"
        clean = []
        skip = False
        for a in args:
            if skip:
                skip = False
                continue
            if a == "-o":
                skip = True
                continue
            clean.append(a)
        cmd = clean + ["-fsyntax-only"] + flags
        r = subprocess.run(cmd, cwd=e["directory"], text=True,
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        return r.stderr

    with ThreadPoolExecutor(jobs) as ex:
        for err in ex.map(one, project_entries(entries, files)):
            for m in re.finditer(r"^([^:\n]+):(\d+):(\d+): warning: (.*) \[(-W[\w\-=]+)\]$", err, re.M):
                path = m.group(1)
                rel = os.path.relpath(os.path.abspath(path), REPO) if not os.path.isabs(path) \
                    else os.path.relpath(path, REPO)
                if rel not in fs:
                    continue
                flag = m.group(5)
                rule = None
                for k, v in CC_FLAGS.items():
                    if flag == k or (k.endswith("=") and flag.startswith(k)):
                        rule = v
                if not rule:
                    continue
                key = (rule, rel, m.group(2), m.group(3), flag)
                if key in seen:
                    continue
                seen.add(key)
                f.add(rule, rel, int(m.group(2)), m.group(4))


# ---------------------------------------------------------------- symbols

def object_map(bdir, entries, files):
    """rel source path -> object file, from compile_commands."""
    res = {}
    for rel, e in project_entries(entries, files):
        args = e.get("arguments") or shlex.split(e["command"])
        if "-o" in args:
            obj = args[args.index("-o") + 1]
            res[rel] = os.path.join(e["directory"], obj)
    return res


def nm(obj):
    r = run(["nm", obj], check=False)
    syms = []
    for ln in r.stdout.splitlines():
        parts = ln.split()
        if len(parts) == 3:
            syms.append((parts[1], parts[2]))
        elif len(parts) == 2:
            syms.append((parts[0], parts[1]))
    return syms


# Rule 3.1 (changed by Ronny 20-SEP-2026): non-static functions are lower
# snake_case with the module/device prefix - smd_read, cpu_do_op.
MODULE_VERB = re.compile(r"[a-z][a-z0-9]*(_[a-z0-9]+)+")
# Exempt: names JavaScript calls by name (profile A.5).
JS_EXPORT = re.compile(r"^(Dbg_|Nd500_)")
SNAKE = re.compile(r"[a-z][a-z0-9_]*")
G_SNAKE = re.compile(r"g_[a-z0-9_]+")


def find_def_line(rel, name, strict=False):
    """Line of the first mention of name in rel that is not a comment. With
    strict=True: the line of a DEFINITION ("name(" on a line that does not
    end the statement with ';'), or None if rel does not define name."""
    with open(os.path.join(REPO, rel), errors="replace") as fh:
        for no, ln in enumerate(fh, 1):
            if ln.lstrip().startswith(("//", "*", "/*")):
                continue
            if not re.search(r"\b" + re.escape(name) + r"\b", ln):
                continue
            if not strict:
                return no
            if re.search(r"\b" + re.escape(name) + r"\s*\(", ln) and not ln.rstrip().endswith(";") \
                    and not re.search(r"[=,(]\s*" + re.escape(name) + r"\s*\(", ln) \
                    and not re.match(r"\s*(return|if|while|for|switch)\b", ln):
                return no
    return None if strict else 1


def header_defining(files, name):
    for h in files:
        if h.endswith(".h") and find_def_line(h, name, strict=True):
            return h
    return None


def check_symbols(objmap, files, f):
    defined = {}                     # symbol -> rel
    undefined = collections.Counter()
    per_file = {}
    inline_seen = set()
    for rel, obj in objmap.items():
        if not os.path.exists(obj):
            continue
        syms = nm(obj)
        per_file[rel] = syms
        for t, name in syms:
            if t == "U":
                undefined[name] += 1
    for rel, syms in per_file.items():
        prefixes = collections.Counter()
        for t, name in syms:
            if t == "T" and name not in KEEP_NAMES and not JS_EXPORT.match(name) \
                    and not decided(name):
                if not MODULE_VERB.fullmatch(name):
                    f.add("3.1", rel, find_def_line(rel, name), name)
                else:
                    prefixes[name.split("_", 1)[0]] += 1
                if undefined[name] == 0 and name not in KEEP_NAMES:
                    f.add("4.6", rel, find_def_line(rel, name), name + " (function)")
            elif t == "t" and not name.startswith((".", "_")) and "." not in name:
                where = rel
                line = find_def_line(rel, name, strict=True)
                if line is None:
                    # a static inline from a header: count it once, at the header
                    where = header_defining(files, name)
                    if where is None or (where, name) in inline_seen:
                        continue
                    inline_seen.add((where, name))
                    line = find_def_line(where, name, strict=True)
                if not SNAKE.fullmatch(name):
                    f.add("3.2", where, line, name)
            elif t in ("D", "B", "R", "C"):
                if not G_SNAKE.fullmatch(name) and name not in KEEP_NAMES:
                    f.add("3.3", rel, find_def_line(rel, name), name)
                if undefined[name] == 0:
                    f.add("4.6", rel, find_def_line(rel, name), name + " (variable)")
        if len(prefixes) > 1:
            main_prefix = prefixes.most_common(1)[0][0]
            for t, name in syms:
                if t == "T" and MODULE_VERB.fullmatch(name) and not name.startswith(main_prefix + "_") \
                        and not JS_EXPORT.match(name) and not decided(name) \
                        and not rel.startswith("tests/"):
                    f.add("3.1", rel, find_def_line(rel, name), name + " (prefix differs from " + main_prefix + "_)")
    return per_file


def count_params(params):
    """Number of parameters in a declaration's parameter list: commas inside
    parentheses (a function-pointer parameter's own arguments) do not count."""
    depth = 0
    n = 1
    for c in params:
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
        elif c == "," and depth == 0:
            n += 1
    return n


def check_doxygen(per_file, files, f):
    """11.2: every exported function has a Doxygen block (@brief, one @param
    per parameter, @return unless void) at a declaration in a hand-written
    project header."""
    # One linear pass per header: every top-level statement that ends in ';'
    # and has the shape "... name ( params ) ... ;" is a declaration. Its doc
    # block is the comment that ends on the line just above it (blank lines
    # allowed in between).
    decls = collections.defaultdict(list)   # name -> [(header, doc, decl, params)]
    for h in (p for p in files if p.endswith(".h")):
        with open(os.path.join(REPO, h), errors="replace") as fh:
            src = fh.read()
        code, comments = strip_comments_and_strings(src)
        # extern "C" { ... } wraps declarations without nesting them: blank the
        # braces so the scan below still sees them at file scope.
        m = re.search(r'extern\s+"?\s*"?\s*\{', code)
        if m:
            # blank the whole "extern "C" {" so the next declaration does not
            # look like a continuation of it
            blank = "".join("\n" if c == "\n" else " " for c in code[m.start():m.end()])
            code = code[:m.start()] + blank + code[m.end():]
            depth = 1
            for i in range(m.end(), len(code)):
                if code[i] == "{":
                    depth += 1
                elif code[i] == "}":
                    depth -= 1
                    if depth == 0:
                        code = code[:i] + " " + code[i + 1:]
                        break
        comment_end = {}                     # last line of comment -> text
        for line, text in comments:
            comment_end[line + text.count("\n")] = text
        src_lines = src.split("\n")
        start = 0
        depth = 0
        for i, ch in enumerate(code):
            if ch in "{(":
                depth += 1
            elif ch in "})":
                depth -= 1
            if depth == 0 and ch in ";}" or (ch == "\n" and code[start:i].lstrip().startswith("#")):
                stmt = code[start:i]
                stmt_start = start
                start = i + 1
                if ch != ";" or "(" not in stmt or stmt.lstrip().startswith(("#", "typedef")):
                    continue
                m = re.search(r"\b(\w+)\s*\(([^()]*(?:\([^()]*\)[^()]*)*)\)\s*(__attribute__.*)?$",
                              stmt.strip(), re.S)
                if not m:
                    continue
                lead = len(stmt) - len(stmt.lstrip())
                first_line = code[:stmt_start + lead].count("\n") + 1
                k = first_line - 1
                while k > 0 and src_lines[k - 1].strip() == "":
                    k -= 1
                doc = comment_end.get(k, "")
                decls[m.group(1)].append((h, doc, stmt.strip(), m.group(2).strip()))

    for rel, syms in per_file.items():
        # Test files define stand-ins for real functions (Device_Init, ...);
        # their documentation belongs at the real declaration, counted there.
        if rel.startswith(("tests/", "tools/")):
            continue
        for t, name in syms:
            if t != "T" or name == "main":
                continue
            ok = False
            reason = "no declaration in a hand-written header"
            for h, doc, decl, params in decls.get(name, []):
                nparams = 0 if params in ("", "void") else count_params(params)
                if not doc:
                    reason = "declared in " + h + " without a Doxygen block"
                    continue
                if "@brief" not in doc:
                    reason = "no @brief"
                    continue
                if doc.count("@param") != nparams:
                    reason = f"{doc.count('@param')} @param for {nparams} parameters"
                    continue
                if not re.match(r"(extern\s+)?(static\s+)?(inline\s+)?void\s", decl) \
                        and "@return" not in doc and "@retval" not in doc:
                    reason = "no @return"
                    continue
                ok = True
                break
            if not ok:
                f.add("11.2", rel, find_def_line(rel, name), name + ": " + reason)


def check_review(f, per_file, files):
    """REVIEW: every empty cell in docs/house-audit/review/*.md is one
    finding, and every function that has no row at all is one finding (run
    tools/house/review_skeleton.py to add the rows). A missing review
    directory is itself a finding, never a 0."""
    rdir = os.path.join(REPO, "docs", "house-audit", "review")
    if not os.path.isdir(rdir):
        f.add("REVIEW", "docs/house-audit/review", 1, "review files missing")
        return
    rows = set()
    for name in os.listdir(rdir):
        cur = None
        with open(os.path.join(rdir, name)) as fh:
            for ln in fh:
                if ln.startswith("## "):
                    cur = ln[3:].strip()
                elif ln.startswith("| ") and cur and not ln.startswith(("| Function", "|---")):
                    rows.add((cur, ln.strip().strip("|").split("|")[0].strip()))
    for rel, syms in per_file.items():
        for t, name in syms:
            if t not in ("T", "t") or name.startswith((".", "_")) or "." in name:
                continue
            line = find_def_line(rel, name, strict=True)
            if line is None:
                continue
            if (rel, name) not in rows:
                f.add("REVIEW", rel, line, f"{name}: no row in the review file")
    for name in sorted(os.listdir(rdir)):
        rel = os.path.join("docs", "house-audit", "review", name)
        cur = None
        header = []
        with open(os.path.join(rdir, name)) as fh:
            for no, ln in enumerate(fh, 1):
                if ln.startswith("## "):
                    cur = ln[3:].strip()
                elif ln.startswith("| Function"):
                    header = [c.strip() for c in ln.strip().strip("|").split("|")]
                elif ln.startswith("| ") and not ln.startswith("|---") and cur:
                    cols = [c.strip() for c in ln.strip().strip("|").split("|")]
                    for rule, cell in zip(header[2:], cols[2:]):
                        if not cell:
                            f.add("REVIEW", rel, no, f"{cur} {cols[0]}: rule {rule}")


# ---------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--write")
    ap.add_argument("--compare")
    ap.add_argument("--rule")
    ap.add_argument("--list")
    ap.add_argument("--build", default="build_gate_audit")
    ap.add_argument("--no-tidy", action="store_true")
    ap.add_argument("--no-cc", action="store_true")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    a = ap.parse_args()

    files = project_files()
    f = Findings()
    for p in files:
        check_text(p, f)
    check_format(files, f, a.jobs)

    bdir, entries = ensure_build(a.build)
    objmap = object_map(bdir, entries, files)
    per_file = check_symbols(objmap, files, f)
    check_doxygen(per_file, files, f)
    if not a.no_tidy:
        check_tidy(bdir, entries, files, f, a.jobs)
    if not a.no_cc:
        check_cc(entries, files, f, a.jobs)

    check_review(f, per_file, files)
    counts = f.counts()
    for rule in WHAT:
        counts.setdefault(rule, {"total": 0, "files": {}})
    skipped = []
    if a.no_tidy:
        skipped += sorted(set(TIDY_RULES.values()))
    if a.no_cc:
        skipped += sorted(set(CC_FLAGS.values()))

    def key(r):
        return [int(x) if x.isdigit() else 99 for x in re.split(r"[.\-/]", r)]

    if a.list:
        for p, ln, text in sorted(f.items.get(a.list, [])):
            print(f"{p}:{ln}: {text}")
        return 0

    total = 0
    for rule in sorted(counts, key=key):
        if a.rule and rule != a.rule:
            continue
        n = counts[rule]["total"]
        total += n
        mark = " (not measured this run)" if rule in skipped else ""
        print(f"{rule:8} {n:7}  {WHAT.get(rule, '')}{mark}")
    print(f"{'TOTAL':8} {total:7}  in {len(files)} files")

    if a.write:
        with open(a.write, "w") as fh:
            json.dump({"files": len(files), "skipped": skipped, "counts": counts}, fh,
                      indent=1, sort_keys=True)
            fh.write("\n")

    if a.compare:
        with open(a.compare) as fh:
            old = json.load(fh)["counts"]
        worse = []
        for rule, c in counts.items():
            if rule in skipped:
                continue
            o = old.get(rule, {"total": 0, "files": {}})
            if c["total"] > o["total"]:
                worse.append(f"{rule}: total {o['total']} -> {c['total']}")
            for p, n in c["files"].items():
                if n > o["files"].get(p, 0):
                    worse.append(f"{rule}: {p} {o['files'].get(p, 0)} -> {n}")
        if worse:
            print("RULE COUNTS WENT UP:")
            for w in worse:
                print("  " + w)
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
