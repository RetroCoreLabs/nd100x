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
    inside, outside = {}, {}
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
            if new == old:
                # Stripping the g_ (see fix_prefix) can land back on the name
                # the file already uses: compliant, not a finding.
                continue
            target = inside if inside_module(where, module_dir) else outside
            target[(kind, old)] = (kind, old, new, where)
    return sorted(inside.values()), sorted(outside.values())


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
    new = repls[0].get("ReplacementText", "")
    if kind == "global variable" and new.startswith("g_"):
        decl = os.path.normpath(os.path.join(REPO, message.get("FilePath", "")))
        if is_static_decl(decl, message.get("FileOffset", 0)):
            for repl in repls:
                text = repl.get("ReplacementText", "")
                if text.startswith("g_"):
                    repl["ReplacementText"] = text[2:]
            new = repls[0].get("ReplacementText", "")
    return new


def filter_fixes(fix_files, module_dir, out_dir):
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
            repls = [
                r for r in message.get("Replacements", [])
                if inside_module(r.get("FilePath", ""), module_dir)
                and not generated(r.get("FilePath", ""))
            ]
            if not repls:
                continue
            message["Replacements"] = repls
            found = MSG.search(message.get("Message", ""))
            if found:
                if found.group(1) == "global function":
                    continue  # rule 3.1, left to the clang-rename pass
                if fix_prefix(found.group(1), message) == found.group(2):
                    continue  # no-op after the g_ strip; nothing to write
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
    rows = [(old, new) for kind, old, new, _ in inside if "function" in kind]
    os.makedirs(os.path.dirname(RENAME_TSV), exist_ok=True)
    with open(RENAME_TSV, "w", encoding="utf-8") as handle:
        for old, new in sorted(rows):
            handle.write(f"{old}\t{new}\n")
    return len(rows)


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


def stale_refs(inside):
    """Old names still written anywhere in the tree after a rename.

    An AST rename reaches declarations and uses, never text: a comment in
    another module, a log string or a document keeps the old name and quietly
    becomes wrong. Reported, not rewritten - a log string is a deliberate
    decision (changing one alters .rodata and costs the G9 proof).
    """
    names = [old for _, old, _, _ in inside]
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
        inside, outside = load_renames(fix_files, module_dir)

        by_kind = {}
        for kind, old, new, _ in inside:
            by_kind.setdefault(kind, []).append((old, new))
        for kind in sorted(by_kind):
            print(f"\n{kind} ({len(by_kind[kind])}):")
            for old, new in sorted(by_kind[kind]):
                print(f"  {old} -> {new}")
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
        count = filter_fixes(fix_files, module_dir, applied)
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
        tables = update_review(inside)
        if tables:
            print(f"updated {tables} review table(s) in docs/house-audit/review")
        print(f"\napplied {count} replacement(s) in {module_dir}")
        print(f"wrote {symbols} symbol rename(s) to "
              f"{os.path.relpath(RENAME_TSV, REPO)}")
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
