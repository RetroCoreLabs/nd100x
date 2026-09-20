#!/usr/bin/env python3
"""rename_naming.py - apply house rules 3.2 and 3.4-3.7 with clang-tidy.

    tools/house/rename_naming.py MODULE_DIR [--apply]

MODULE_DIR is a directory under src/, e.g. src/devices/rtc. Without --apply
nothing is written: the renames clang-tidy wants to make are listed and
counted. With --apply they are written to the files.

Only replacements inside MODULE_DIR are ever applied. clang-tidy also reports
parameter names in shared headers such as src/devices/devices_types.h, which
belong to whichever module owns that header - those are dropped here and are
listed at the end as "skipped (outside module)".

Static function renames change symbol names, so they are written to
docs/house-audit/renames_naming.tsv (old<TAB>new) for gate G9:

    tools/house/objcompare.sh <base commit> docs/house-audit/renames_naming.tsv

Needs: clang-tidy and clang-apply-replacements (set CLANG_TIDY and
CLANG_APPLY_REPLACEMENTS to pick specific binaries), a compile database at
build/compile_commands.json, and python3 with PyYAML.
"""

import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

import yaml

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
COMPILE_DB = os.path.join(REPO, "build", "compile_commands.json")
TIDY_CONFIG = os.path.join(REPO, "tools", "house", "audit.clang-tidy")
RENAME_TSV = os.path.join(REPO, "docs", "house-audit", "renames_naming.tsv")

# "invalid case style for static function 'FooBar'" - the kind is everything
# between "for" and the quoted old name.
MSG = re.compile(r"invalid case style for (.+?) '([^']+)'")

# clang-tidy reports paths as it saw them, so a header reached through an
# include of "../devices_types.h" arrives as "src/devices/rtc/../devices_types.h".
# That string starts with the module directory although the file is outside it,
# which would let a module run edit its neighbours' headers. Every path is
# normalised before it is tested.
def inside_module(path, module_dir):
    """True if path really is a file under module_dir."""
    if not path:
        return False
    full = os.path.normpath(os.path.join(REPO, path))
    want = os.path.normpath(os.path.join(REPO, module_dir)) + os.sep
    return full.startswith(want)


# Files this project forbids editing (CLAUDE.md): mkptypes output, and the
# vendored NCR 5386 port. A module run must not touch them even though they
# sit inside src/.
PROTECTED = ("ncr5386.h", "ncr5386.c")


def generated(path):
    """True for a file that must never be edited (see CLAUDE.md)."""
    name = os.path.basename(os.path.normpath(path))
    return name.endswith("_protos.h") or name in PROTECTED


def is_static_decl(path, offset):
    """True if the declaration at this byte offset is a file-scope static.

    clang-tidy calls every file-scope variable a "global variable" in C, so a
    `static` one would otherwise be given the g_ prefix that house rule 3.3
    reserves for non-static globals. The declaration is taken to start after
    the previous ';', '}' or '{'.
    """
    try:
        with open(path, "rb") as handle:
            text = handle.read(offset)
    except OSError:
        return False
    start = max(text.rfind(b";"), text.rfind(b"}"), text.rfind(b"{"))
    return b"static" in text[start + 1:]


def tool(env_name, default):
    """Binary from the environment, else the first one on PATH."""
    path = os.environ.get(env_name) or shutil.which(default)
    if not path:
        sys.exit(f"rename_naming: {default} not found (set {env_name})")
    return path


def module_sources(module_dir):
    """The .c files of the compile database that live in module_dir."""
    if not os.path.exists(COMPILE_DB):
        sys.exit(
            "rename_naming: build/compile_commands.json missing - run\n"
            "  cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug "
            "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"
        )
    with open(COMPILE_DB, encoding="utf-8") as handle:
        entries = json.load(handle)
    want = os.path.join(REPO, module_dir) + os.sep
    files = sorted({e["file"] for e in entries if e["file"].startswith(want)})
    return [f for f in files if f.endswith(".c")]


def tidy_config(out_dir):
    """audit.clang-tidy plus the options needed to reach static functions.

    clang-tidy's StaticFunctionCase matches nothing for a file-scope static in
    C. FunctionCase reaches it, but also reaches non-static functions. Setting
    GlobalFunctionCase as well makes clang-tidy report the non-static ones as
    "global function" and the statics as "function", so rule 3.2 can be fixed
    here while rule 3.1 is left to the prefix-aware clang-rename pass.
    """
    with open(TIDY_CONFIG, encoding="utf-8") as handle:
        cfg = yaml.safe_load(handle)
    options = cfg.setdefault("CheckOptions", {})
    options["readability-identifier-naming.FunctionCase"] = "lower_case"
    options["readability-identifier-naming.GlobalFunctionCase"] = "lower_case"
    path = os.path.join(out_dir, "tidy.yaml")
    with open(path, "w", encoding="utf-8") as handle:
        yaml.safe_dump(cfg, handle)
    return path


def run_tidy(sources, module_dir, out_dir):
    """Run clang-tidy over sources, exporting fixes to out_dir. No writes."""
    tidy = tool("CLANG_TIDY", "clang-tidy")
    config = tidy_config(out_dir)
    fixes = []
    for index, source in enumerate(sources):
        out = os.path.join(out_dir, f"fixes{index}.yaml")
        cmd = [
            tidy,
            "-p", os.path.join(REPO, "build"),
            f"--config-file={config}",
            "--checks=-*,readability-identifier-naming",
            f"--header-filter=^{re.escape(os.path.join(REPO, module_dir))}/.*\\.h$",
            f"--export-fixes={out}",
            "--quiet",
            source,
        ]
        subprocess.run(cmd, cwd=REPO, capture_output=True, text=True, check=False)
        if os.path.exists(out) and os.path.getsize(out) > 0:
            fixes.append(out)
    return fixes


def load_renames(fix_files, module_dir):
    """Read the fix files. Returns (inside, outside) rename lists.

    Each rename is (kind, old, new, file). The same rename is reported once
    per translation unit that sees it, so duplicates are collapsed.
    """
    inside, outside, unspellable = {}, {}, []
    for path in fix_files:
        with open(path, encoding="utf-8") as handle:
            doc = yaml.safe_load(handle)
        if not doc:
            continue
        for diag in doc.get("Diagnostics", []):
            message = diag.get("DiagnosticMessage", {})
            found = MSG.search(message.get("Message", ""))
            if not found:
                continue
            kind, old = found.group(1), found.group(2)
            if kind == "global function":
                # Rule 3.1: needs the per-file prefix from prefixes.tsv, so it
                # is done by the clang-rename pass, not here.
                continue
            where = message.get("FilePath", "")
            if generated(where):
                continue
            new = fix_prefix(kind, message)
            if not new:
                continue  # non-static global: needs a whole-tree pass
            if suspect(old, new, kind):
                # A split acronym the table does not cover. Guessing produces
                # names like co_m5025_send_one_byte, so it is left alone and
                # reported for a person to spell.
                unspellable.append((kind, old, new, message.get("FilePath", "")))
                continue
            if new == old:
                # Stripping the g_ (see fix_prefix) can land back on the name
                # the file already uses: compliant, not a finding.
                continue
            target = inside if inside_module(where, module_dir) else outside
            target[(kind, old)] = (kind, old, new, where)
    return sorted(inside.values()), sorted(outside.values()), unspellable


# clang-tidy splits an identifier on every capital, so an acronym followed by
# digits or glued to a word comes apart: COM5025 -> co_m5025, IRQ12 -> ir_q12,
# CHStoLBA -> ch_sto_lba, TDBtoTSR -> td_bto_tsr. House style is a name that
# reads as words, so the acronym is spelled out rather than shortened.
SPELLINGS = {
    "co_m5025": "com5025",
    "ir_q": "interrupt_request_",
    "ch_sto_lba": "cylinder_head_sector_to_logical_block",
    "td_bto_tsr": "data_buffer_to_shift_register",
    "_to_tsr": "_to_shift_register",
    "_tsr_empty": "_shift_register_empty",
    "bits2_char_len": "bits_to_character_length",
}

# Split a name the way a reader would: an all-capitals run with any trailing
# digits is one word (COM5025, IRQ12, B1), otherwise a capital starts a word.
WORD = re.compile(r"[A-Z]+(?![a-z])[0-9]*|[A-Z][a-z0-9]*|[a-z][a-z0-9]*|[0-9]+")


def expected_snake(old):
    """What lower_case should look like, keeping acronyms whole."""
    words = []
    for part in old.split("_"):
        words += WORD.findall(part)
    return "_".join(w.lower() for w in words)


# Only these are spelled lower_case. A typedef or struct becomes CamelCase and
# an enum constant or macro UPPER_CASE, so comparing them with a snake_case
# split refuses every one of them.
SNAKE_KINDS = ("function", "local variable", "parameter", "static variable",
               "global variable")


def suspect(old, new, kind=None):
    """True if clang-tidy split the name somewhere a reader would not.

    COM5025_SendOneByte comes back as co_m5025_send_one_byte and IRQ12 as
    ir_q12, because clang-tidy breaks at every capital. firstB1 -> first_b1 is
    correct and must not be refused, so the test is whether the result matches
    an acronym-aware split rather than whether it merely looks odd.

    A lowercase word glued to an acronym - CHStoLBA - cannot be told apart
    this way and still needs a person; SPELLINGS carries those.
    """
    if kind is not None and kind not in SNAKE_KINDS:
        # CamelCase and UPPER_CASE names: the split is not snake_case, so this
        # test does not apply. A mangled one shows up as a build failure or in
        # the dry-run list, which is read before applying.
        return False
    return new != expected_snake(old)


def spell_out(new):
    """Repair a name clang-tidy split through an acronym."""
    for bad, good in SPELLINGS.items():
        if bad in new:
            new = new.replace(bad, good)
    return re.sub(r"__+", "_", new).strip("_")


def fix_prefix(kind, message):
    """Strip the g_ prefix from a file-scope static, in every replacement.

    The decision is made once, from the declaration that the diagnostic points
    at (DiagnosticMessage.FileOffset). It must not be made per replacement: a
    use site has no `static` in front of it, which would leave the declaration
    renamed one way and its uses another.

    Returns the replacement text after the correction.
    """
    repls = message.get("Replacements", [])
    if not repls:
        return ""
    # A non-static file-scope variable is used from any translation unit, and
    # clang-tidy reports each TU separately, so the atomic check below cannot
    # see the other uses. Renaming scsi_debug_enabled to g_scsi_debug_enabled
    # updated src/devices/scsi/device_scsi.{c,h} and left the use in the
    # vendored ncr5386.c, which then failed to compile. True globals need a
    # whole-tree pass; this tool only touches file statics.
    if kind == "global variable" and not is_static_decl(
            os.path.normpath(os.path.join(REPO, message.get("FilePath", ""))),
            message.get("FileOffset", 0)):
        return ""
    new = spell_out(repls[0].get("ReplacementText", ""))
    for repl in repls:
        repl["ReplacementText"] = spell_out(repl.get("ReplacementText", ""))
    if kind == "global variable" and new.startswith("g_"):
        decl = os.path.normpath(os.path.join(REPO, message.get("FilePath", "")))
        if is_static_decl(decl, message.get("FileOffset", 0)):
            for repl in repls:
                text = repl.get("ReplacementText", "")
                if text.startswith("g_"):
                    repl["ReplacementText"] = text[2:]
            new = repls[0].get("ReplacementText", "")
    return new


def filter_fixes(fix_files, module_dir, out_dir, escapes=()):
    """Copy the fix files to out_dir, dropping replacements outside module_dir."""
    kept = 0
    for index, path in enumerate(fix_files):
        with open(path, encoding="utf-8") as handle:
            doc = yaml.safe_load(handle)
        if not doc:
            continue
        diags = []
        for diag in doc.get("Diagnostics", []):
            message = diag.get("DiagnosticMessage", {})
            repls = message.get("Replacements", [])
            if not repls:
                continue
            # A rename is all-or-nothing. Applying only the parts that fall
            # inside the module splits it: the macro gPANS is defined in
            # src/cpu/cpu_types.h and used in src/devices/panel/panel.c, so
            # filtering replacement by replacement renamed the uses to G_PANS
            # and left the definition, and the build failed with "G_PANS
            # undeclared". A declaration owned by another module is that
            # module's rename to make.
            if any(not inside_module(r.get("FilePath", ""), module_dir)
                   or generated(r.get("FilePath", ""))
                   for r in repls):
                continue
            message["Replacements"] = repls
            found = MSG.search(message.get("Message", ""))
            if found:
                if found.group(1) == "global function":
                    continue  # rule 3.1, left to the clang-rename pass
                if found.group(2) in escapes:
                    continue  # used from another module; not ours to rewrite
                spelled = fix_prefix(found.group(1), message)
                if not spelled or spelled == found.group(2):
                    continue  # non-static global, or a no-op after the g_ strip
                if suspect(found.group(2), spelled, found.group(1)):
                    continue  # split acronym with no known spelling
            diag["DiagnosticMessage"] = message
            diags.append(diag)
            kept += len(repls)
        if not diags:
            continue
        doc["Diagnostics"] = diags
        with open(os.path.join(out_dir, f"apply{index}.yaml"), "w",
                  encoding="utf-8") as handle:
            yaml.safe_dump(doc, handle)
    return kept


def write_rename_tsv(inside):
    """Record the symbol-visible renames (static functions) for gate G9."""
    rows = {(old, new) for kind, old, new, _ in inside if "function" in kind}
    # Keep what is already there. A module often takes more than one pass -
    # src/ndlib needed a second after the acronym check was corrected - and
    # overwriting dropped the first pass's entries, so G9 compared a renamed
    # symbol against an empty map and reported a difference that did not
    # exist. Entries for names no longer present are harmless: the map is
    # applied to the base disassembly and simply does not match.
    if os.path.exists(RENAME_TSV):
        with open(RENAME_TSV, encoding="utf-8") as handle:
            for line in handle:
                if line.strip():
                    parts = line.rstrip("\n").split("\t")
                    if len(parts) >= 2:
                        rows.add((parts[0], parts[1]))
    os.makedirs(os.path.dirname(RENAME_TSV), exist_ok=True)
    with open(RENAME_TSV, "w", encoding="utf-8") as handle:
        for old, new in sorted(rows):
            handle.write(f"{old}\t{new}\n")
    return len(rows)


KINDS_TSV = os.path.join(REPO, "docs", "house-audit", "rename_kinds.tsv")


def used_outside(names, module_dir):
    """Of these names, the ones that also appear in files outside the module.

    clang-tidy works one translation unit at a time, so a name declared here
    and used from another module is reported only as this module's diagnostic:
    the atomic check cannot see the other uses. That is how renaming
    scsi_debug_enabled broke the vendored ncr5386.c, and how renaming the
    typedef BOOT_TYPE would break src/frontend/nd100x.

    A textual scan is the right tool here: it is looking for the name anywhere
    the compiler is not looking, including inactive #ifdef branches.
    """
    if not names:
        return set()
    pattern = re.compile(r"\b(" + "|".join(map(re.escape, names)) + r")\b")
    found = set()
    for top in ("src", "tests"):
        for base, dirs, files in os.walk(os.path.join(REPO, top)):
            dirs[:] = [d for d in dirs if d != "external"]
            for name in files:
                if not name.endswith((".c", ".h")):
                    continue
                path = os.path.join(base, name)
                if inside_module(path, module_dir):
                    continue
                try:
                    with open(path, encoding="utf-8", errors="replace") as handle:
                        for line in handle:
                            found.update(pattern.findall(line))
                except OSError:
                    continue
    return found


def record_kinds(inside, module_dir):
    """Append what was renamed, and what kind of thing it was.

    git shows that an identifier changed but not whether it was a parameter, a
    local or a static function. clang-tidy knows, so it is recorded here, and
    tools/house/rename_log.py joins it onto the trace it builds from history.
    Append-only: this is the record of every phase (see docs/TODO.md).
    """
    new_file = not os.path.exists(KINDS_TSV)
    with open(KINDS_TSV, "a", encoding="utf-8") as handle:
        if new_file:
            handle.write("module\tkind\tfile\told\tnew\n")
        for kind, old, new, where in inside:
            rel = os.path.relpath(
                os.path.normpath(os.path.join(REPO, where)), REPO)
            handle.write(f"{module_dir}\t{kind}\t{rel}\t{old}\t{new}\n")
    return len(inside)


def update_review(inside):
    """Follow the renames in the review tables.

    docs/house-audit/review/*.md has one row per function, keyed by name. The
    audit counts a function with no row, so a rename that leaves the table
    alone turns every renamed function into a fresh finding.
    """
    rdir = os.path.join(REPO, "docs", "house-audit", "review")
    if not os.path.isdir(rdir):
        return 0
    pairs = [(old, new) for _, old, new, _ in inside]
    changed = 0
    for name in sorted(os.listdir(rdir)):
        if not name.endswith(".md"):
            continue
        path = os.path.join(rdir, name)
        with open(path, encoding="utf-8") as handle:
            text = handle.read()
        before = text
        for old, new in pairs:
            text = re.sub(r"^\| " + re.escape(old) + r" \|",
                          "| " + new + " |", text, flags=re.M)
        if text != before:
            with open(path, "w", encoding="utf-8") as handle:
                handle.write(text)
            changed += 1
    return changed


def reformat(module_dir):
    """Re-format only the lines a rename left non-conforming.

    A new name is rarely the same length as the old one, so a rename can break
    clang-format compliance - renaming PANEL_STATUS_FUNCTIONS to
    PanelStatusFunctions shortened a line by one character and left the
    trailing comments of a bitfield misaligned, which the gate counts under
    rule 2.x.

    Only the offending lines are formatted. Several headers in src/devices
    hold hand-aligned register tables (see tools/house/protect_tables.py) that
    a whole-file format would disturb.
    """
    fmt = tool("CLANG_FORMAT", "clang-format")
    fixed = []
    root = os.path.join(REPO, module_dir)
    for base, _, files in os.walk(root):
        for name in sorted(files):
            if not name.endswith((".c", ".h")) or generated(name):
                continue
            path = os.path.join(base, name)
            result = subprocess.run([fmt, "--dry-run", "--style=file", path],
                                    capture_output=True, text=True, check=False)
            lines = sorted({int(m) for m in re.findall(
                r"^" + re.escape(path) + r":(\d+):", result.stderr, re.M)})
            if not lines:
                continue
            args = [fmt, "--style=file", "-i"]
            for no in lines:
                args.append(f"--lines={no}:{no}")
            subprocess.run(args + [path], capture_output=True, text=True,
                           check=False)
            fixed.append((os.path.relpath(path, REPO), len(lines)))
    return fixed


def stale_refs(inside):
    """Old names still written anywhere in the tree after a rename.

    An AST rename reaches declarations and uses, never text: a comment in
    another module, a log string or a document keeps the old name and quietly
    becomes wrong. Reported, not rewritten - a log string is a deliberate
    decision (changing one alters .rodata and costs the G9 proof).
    """
    # Only names that are visible outside their function can go stale
    # elsewhere. A local or a parameter called newCapacity recurs in unrelated
    # files, and reporting those is noise, not a finding.
    local = ("local variable", "parameter")
    names = [old for kind, old, _, _ in inside if kind not in local]
    if not names:
        return []
    pattern = re.compile(r"\b(" + "|".join(map(re.escape, names)) + r")\b")
    hits = []
    for top in ("src", "tests", "docs", "tools"):
        root = os.path.join(REPO, top)
        for base, dirs, files in os.walk(root):
            dirs[:] = [d for d in dirs if d not in ("__pycache__", "external")]
            for name in files:
                if not name.endswith((".c", ".h", ".md", ".py", ".sh")):
                    continue
                path = os.path.join(base, name)
                try:
                    with open(path, encoding="utf-8", errors="replace") as handle:
                        for no, line in enumerate(handle, 1):
                            if pattern.search(line):
                                hits.append((os.path.relpath(path, REPO), no,
                                             line.strip()))
                except OSError:
                    continue
    return hits


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    apply_it = "--apply" in sys.argv[1:]
    if len(args) != 1:
        sys.exit(__doc__)
    module_dir = args[0].rstrip("/")
    if not os.path.isdir(os.path.join(REPO, module_dir)):
        sys.exit(f"rename_naming: no such directory: {module_dir}")

    sources = module_sources(module_dir)
    if not sources:
        sys.exit(f"rename_naming: no compiled .c files under {module_dir}")
    print(f"{module_dir}: {len(sources)} source file(s)")

    work = tempfile.mkdtemp(prefix="rename_naming.")
    try:
        fix_files = run_tidy(sources, module_dir, work)
        inside, outside, unspellable = load_renames(fix_files, module_dir)
        # A local or a parameter cannot be referenced from another file, so
        # only the rest are checked; their names also recur harmlessly.
        local_kinds = ("local variable", "parameter")
        escapes = used_outside(
            [old for kind, old, _, _ in inside if kind not in local_kinds],
            module_dir)
        if escapes:
            inside = [r for r in inside if r[1] not in escapes]

        by_kind = {}
        for kind, old, new, _ in inside:
            by_kind.setdefault(kind, []).append((old, new))
        for kind in sorted(by_kind):
            print(f"\n{kind} ({len(by_kind[kind])}):")
            for old, new in sorted(by_kind[kind]):
                print(f"  {old} -> {new}")
        if escapes:
            print(f"\nNOT renamed - declared here but used from another "
                  f"module, which this per-module tool cannot rewrite "
                  f"({len(escapes)}):")
            for name in sorted(escapes):
                print(f"  {name}")
        if unspellable:
            print(f"\nNOT renamed - clang-tidy split an acronym and the "
                  f"spelling is not known ({len(unspellable)}):")
            for kind, o, n, where in unspellable:
                print(f"  {o} -> {n}   [{kind}, "
                      f"{os.path.relpath(os.path.normpath(os.path.join(REPO, where)), REPO)}]")
            print("  add the spelling to SPELLINGS in this script, then re-run")
        if outside:
            print(f"\nskipped (outside module) ({len(outside)}):")
            for kind, old, new, where in outside:
                rel = os.path.relpath(where, REPO)
                print(f"  {old} -> {new}   [{kind}, {rel}]")

        if not apply_it:
            print(f"\n{len(inside)} rename(s) inside {module_dir}. "
                  "Nothing written (no --apply).")
            return 0

        applied = os.path.join(work, "apply")
        os.makedirs(applied)
        count = filter_fixes(fix_files, module_dir, applied, escapes)
        if not count:
            print("\nnothing to apply")
            return 0
        applier = tool("CLANG_APPLY_REPLACEMENTS", "clang-apply-replacements")
        result = subprocess.run([applier, applied], cwd=REPO,
                                capture_output=True, text=True, check=False)
        if result.returncode != 0:
            sys.stderr.write(result.stderr)
            sys.exit("rename_naming: clang-apply-replacements failed")
        symbols = write_rename_tsv(inside)
        record_kinds(inside, module_dir)
        tables = update_review(inside)
        if tables:
            print(f"updated {tables} review table(s) in docs/house-audit/review")
        print(f"\napplied {count} replacement(s) in {module_dir}")
        print(f"wrote {symbols} symbol rename(s) to "
              f"{os.path.relpath(RENAME_TSV, REPO)}")
        for path, count in reformat(module_dir):
            print(f"re-formatted {count} line(s) in {path}")
        left = stale_refs(inside)
        if left:
            print(f"\nold names still written in {len(left)} place(s) - "
                  "comments, log strings or documents, decide each:")
            for path, no, line in left:
                print(f"  {path}:{no}: {line[:100]}")
        print("\nnow run the gate, then tools/house/objcompare.sh <base> "
              f"{os.path.relpath(RENAME_TSV, REPO)}")
    finally:
        shutil.rmtree(work, ignore_errors=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
