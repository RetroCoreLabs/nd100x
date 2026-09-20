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


def run_tidy(sources, module_dir, out_dir):
    """Run clang-tidy over sources, exporting fixes to out_dir. No writes."""
    tidy = tool("CLANG_TIDY", "clang-tidy")
    fixes = []
    for index, source in enumerate(sources):
        out = os.path.join(out_dir, f"fixes{index}.yaml")
        cmd = [
            tidy,
            "-p", os.path.join(REPO, "build"),
            f"--config-file={TIDY_CONFIG}",
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
        print(f"\napplied {count} replacement(s) in {module_dir}")
        print(f"wrote {symbols} symbol rename(s) to "
              f"{os.path.relpath(RENAME_TSV, REPO)}")
        print("now run the gate, then tools/house/objcompare.sh <base> "
              f"{os.path.relpath(RENAME_TSV, REPO)}")
    finally:
        shutil.rmtree(work, ignore_errors=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
