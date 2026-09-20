#!/usr/bin/env python3
"""rename_log.py - the full trace of every identifier renamed by the cleanup.

    tools/house/rename_log.py [--since COMMIT] [--out FILE]

Writes one row per renamed identifier to docs/house-audit/rename_log.tsv:

    commit  date  rules  kind  file  line  old  new  count

so any name can be traced back to the commit that changed it, and any commit
can be checked for what it renamed. --since defaults to the commit where the
cleanup began.

How the rows are found: for every commit in the range, each changed .c/.h file
is compared with its parent. Lines that were replaced are split into
identifiers and the two sides are lined up; where a pair differs and both
sides are identifiers, that is a rename. A line that only moved or was
re-indented yields nothing, so a pure clang-format commit logs no renames.

This reconstructs history. Renames applied from now on are logged directly by
tools/house/rename_naming.py, which knows the kind (static function,
parameter, local variable) from clang-tidy instead of inferring it.
"""

import argparse
import collections
import difflib
import os
import re
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(REPO, "docs", "house-audit", "rename_log.tsv")
FIRST = "7a295c2"  # the commit that set house rule 3.1 to module_verb

IDENT = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
RULES = re.compile(r"\b(\d+\.\d+(?:-\d+\.\d+)?)\b")
KEYWORDS = {
    "if", "else", "for", "while", "do", "switch", "case", "default", "break",
    "continue", "return", "goto", "sizeof", "static", "const", "void", "int",
    "char", "long", "short", "unsigned", "signed", "float", "double", "struct",
    "union", "enum", "typedef", "extern", "register", "volatile", "inline",
    "bool", "true", "false", "NULL",
}


def git(*args):
    """Run git in the repo and return stdout, or "" if the command failed."""
    result = subprocess.run(["git", "-C", REPO] + list(args),
                            capture_output=True, text=True, check=False)
    return result.stdout if result.returncode == 0 else ""


def commits(since):
    """The commits from since (exclusive) to HEAD, oldest first."""
    out = git("log", "--reverse", "--format=%H%x09%ad%x09%s", "--date=short",
              f"{since}..HEAD")
    rows = []
    for line in out.splitlines():
        parts = line.split("\t")
        if len(parts) == 3:
            rows.append(parts)
    return rows


def changed_files(sha):
    """The .c and .h files a commit modified, excluding generated ones."""
    out = git("diff-tree", "--no-commit-id", "--name-only", "-r", sha)
    return [f for f in out.splitlines()
            if f.endswith((".c", ".h")) and not f.endswith("_protos.h")]


def renames_in_file(sha, path):
    """Identifier pairs that differ between this commit and its parent.

    Yields (line_in_new_file, old_name, new_name).
    """
    before = git("show", f"{sha}^:{path}")
    after = git("show", f"{sha}:{path}")
    if not before or not after:
        return
    # Compare the identifiers of the whole file, not line by line. A long new
    # name makes clang-format re-wrap the declaration, so the old and new lines
    # hold different numbers of identifiers and a line-based compare drops the
    # rename entirely - convert_cylinder_head_sector_to_logical_block in
    # device_smd.c went missing exactly that way.
    def tokens(text):
        out = []
        for no, line in enumerate(text.splitlines(), 1):
            for match in IDENT.finditer(line):
                out.append((match.group(0), no))
        return out

    old_tokens, new_tokens = tokens(before), tokens(after)
    matcher = difflib.SequenceMatcher(
        None, [t[0] for t in old_tokens], [t[0] for t in new_tokens],
        autojunk=False)
    for tag, i1, i2, j1, j2 in matcher.get_opcodes():
        if tag != "replace":
            continue
        for offset in range(min(i2 - i1, j2 - j1)):
            old = old_tokens[i1 + offset][0]
            new, line = new_tokens[j1 + offset]
            if old == new or old in KEYWORDS or new in KEYWORDS:
                continue
            yield line, old, new


KINDS_TSV = os.path.join(REPO, "docs", "house-audit", "rename_kinds.tsv")


def load_kinds():
    """(file, old, new) -> kind, as recorded by rename_naming.py when applying.

    git shows that an identifier changed, not whether it was a parameter, a
    local or a static function. Where the runner recorded the kind, it is used;
    anything older or renamed by hand stays "identifier".
    """
    kinds = {}
    if not os.path.exists(KINDS_TSV):
        return kinds
    with open(KINDS_TSV, encoding="utf-8") as handle:
        for no, line in enumerate(handle):
            if no == 0 or not line.strip():
                continue
            parts = line.rstrip("\n").split("\t")
            if len(parts) == 5:
                _, kind, path, old, new = parts
                kinds[(path, old, new)] = kind
    return kinds


FUNC_DEF = r"^[A-Za-z_][\w \*]*\b{name}\s*\("
STATIC_FUNC = r"^\s*static\b[\w \*]*\b{name}\s*\("
STATIC_VAR = r"^\s*static\b[\w \*]*\b{name}\s*(\[|=|;)"


def classify(path, new):
    """Infer what a renamed identifier is, for rows with no recorded kind.

    Used for renames applied before rename_naming.py recorded the kind, and
    for anything renamed by hand. The answer is read from the file as it
    stands now, so it is an inference, not clang-tidy's own classification.
    """
    full = os.path.join(REPO, path)
    try:
        with open(full, encoding="utf-8", errors="replace") as handle:
            text = handle.read()
    except OSError:
        return "identifier"
    name = re.escape(new)
    if re.search(STATIC_FUNC.format(name=name), text, re.M):
        return "static function"
    if re.search(FUNC_DEF.format(name=name), text, re.M):
        return "function"
    if re.search(STATIC_VAR.format(name=name), text, re.M):
        return "static variable"
    # A name inside the parentheses of a definition is a parameter.
    for line in text.splitlines():
        if re.search(r"\b" + name + r"\b", line) and "(" in line and \
                re.search(r"^[A-Za-z_][\w \*]*\w\s*\(", line):
            head = line[line.index("("):]
            if re.search(r"\b" + name + r"\b", head):
                return "parameter"
    return "local variable"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--since", default=FIRST)
    ap.add_argument("--out", default=OUT)
    args = ap.parse_args()

    kinds = load_kinds()
    rows = []
    for sha, date, subject in commits(args.since):
        rules = ",".join(sorted(set(RULES.findall(subject)))) or "-"
        for path in changed_files(sha):
            # (old, new, file) -> [lines]
            found = collections.defaultdict(list)
            for line, old, new in renames_in_file(sha, path):
                found[(old, new)].append(line)
            for (old, new), lines in sorted(found.items()):
                kind = kinds.get((path, old, new)) or classify(path, new)
                rows.append((sha[:7], date, rules, kind, path,
                             str(min(lines)), old, new, str(len(lines))))

    with open(args.out, "w", encoding="utf-8") as handle:
        handle.write("commit\tdate\trules\tkind\tfile\tline\told\tnew\tcount\n")
        for row in rows:
            handle.write("\t".join(row) + "\n")
    print(f"{len(rows)} rename(s) written to {os.path.relpath(args.out, REPO)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
