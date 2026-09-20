#!/bin/sh
# objcompare.sh - gate G9: prove a step is behaviour-neutral.
#
#   tools/house/objcompare.sh BASE [RENAMES.tsv]
#
# Builds BASE (a commit, in a temporary git worktree) and the working tree as
# Release with -g0 and __LINE__ fixed to 0 (formatting moves lines, and test
# macros print __LINE__), then compares, for every project object file, the
# disassembly of the code and the contents of the read-only and data
# sections. With RENAMES.tsv (lines "old<TAB>new"), symbol names in the BASE
# disassembly are mapped old -> new before the compare, so a pure rename
# step compares equal.
#
# Exit 0 = identical. Exit 1 = differences (listed). Vendored objects are
# skipped. Needs cmake, gcc, objdump, python3.

cd "$(dirname "$0")/../.." || exit 2
REPO=$(pwd)
BASE=$1
RENAMES=$2
WORK_REV=${WORK_REV:-}   # set to a commit to compare BASE with it instead of the working tree
[ -n "$BASE" ] || { echo "usage: $0 BASE [RENAMES.tsv]"; exit 2; }

TMP="${GATE_TMP:-$(mktemp -d)}/objcompare"
rm -rf "$TMP"
mkdir -p "$TMP"

git worktree add --detach "$TMP/base_src" "$BASE" >/dev/null 2>&1 || { echo "worktree add failed"; exit 2; }
# The base worktree has no submodule checkouts; point it at this repo's.
for sub in $(git config --file .gitmodules --get-regexp path | awk '{print $2}'); do
    rm -rf "$TMP/base_src/$sub"
    mkdir -p "$(dirname "$TMP/base_src/$sub")"
    ln -s "$REPO/$sub" "$TMP/base_src/$sub"
done

# The build stamp (git hash, build time) differs between any two builds.
# Pre-including this header defines the generated header's guard and fixed
# values, so nd100x_version.h adds nothing and both builds embed the same text.
STAMP="$TMP/fixed_stamp.h"
cat >"$STAMP" <<'EOS'
#define ND100X_VERSION_H
#define ND100X_VERSION    "objcompare"
#define ND100X_GIT_HASH   "objcompare"
#define ND100X_BUILD_TIME "objcompare"
EOS

build() {  # src dir, build dir
    mkdir -p "$2"
    (cd "$2" && cmake "$1" -DCMAKE_BUILD_TYPE=Release "-DCMAKE_C_FLAGS=-g0 -ffile-prefix-map=$1=. -Wno-builtin-macro-redefined -D__LINE__=0 -include $STAMP" >/dev/null 2>&1) || return 1
    # mkptypes is built in the source tree; use the native one from REPO
    cmake --build "$2" -j"$(nproc)" >"$2.log" 2>&1
}

build "$TMP/base_src" "$TMP/base" || { echo "BASE build failed ($TMP/base.log)"; git worktree remove --force "$TMP/base_src"; exit 2; }
WORK_SRC="$REPO"
if [ -n "$WORK_REV" ]; then
    git worktree add --detach "$TMP/work_src" "$WORK_REV" >/dev/null 2>&1 || { echo "worktree add failed"; exit 2; }
    for sub in $(git config --file .gitmodules --get-regexp path | awk '{print $2}'); do
        rm -rf "$TMP/work_src/$sub"
        mkdir -p "$(dirname "$TMP/work_src/$sub")"
        ln -s "$REPO/$sub" "$TMP/work_src/$sub"
    done
    WORK_SRC="$TMP/work_src"
fi
build "$WORK_SRC" "$TMP/work" || { echo "working build failed ($TMP/work.log)"; git worktree remove --force "$TMP/base_src"; exit 2; }

python3 - "$TMP" "$RENAMES" <<'EOF'
import os, re, subprocess, sys
tmp, renames = sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else ""
skip = re.compile(r"(external|mkptypes|ncr5386|nd500x)")
mapping = {}
if renames:
    for ln in open(renames):
        if ln.strip() and not ln.startswith("#"):
            old, new = ln.rstrip("\n").split("\t")[:2]
            mapping[old] = new
sym = re.compile(r"\b(" + "|".join(map(re.escape, sorted(mapping, key=len, reverse=True))) + r")\b") if mapping else None

def objs(root):
    res = {}
    for d, _, fs in os.walk(root):
        for f in fs:
            if f.endswith(".o"):
                p = os.path.join(d, f)
                rel = os.path.relpath(p, root)
                if not skip.search(rel):
                    res[rel] = p
    return res

def dump(path, is_base):
    out = subprocess.run(["objdump", "-d", "-z", "--no-show-raw-insn", path],
                         capture_output=True, text=True).stdout
    # every section's contents (code, .rodata*, .data*, relocation-free
    # parts), except debug info and notes, which carry no behaviour
    heads = subprocess.run(["objdump", "-h", path], capture_output=True, text=True).stdout
    secs = [ln.split()[1] for ln in heads.splitlines()
            if ln.strip()[:1].isdigit() and len(ln.split()) > 1
            and not ln.split()[1].startswith((".debug", ".note", ".comment"))]
    args = ["objdump", "-s"]
    for sec in secs:
        args += ["-j", sec]
    data = subprocess.run(args + [path], capture_output=True, text=True).stdout
    code = "\n".join(out.split("\n")[2:])
    # The rename map may only rewrite symbol names, which live in the
    # disassembly. It must NOT touch the section dump: objdump -s prints an
    # ASCII rendering beside the hex, so renaming RTC_Ident -> rtc_ident there
    # rewrites the .rodata string "RTC_Ident: %d" in the base only and reports
    # a difference the bytes do not have. The hex is the same either way.
    if is_base and sym:
        code = sym.sub(lambda m: mapping[m.group(1)], code)
    return code + "\n" + "\n".join(data.split("\n")[2:])

base, work = objs(os.path.join(tmp, "base")), objs(os.path.join(tmp, "work"))
bad = []
for rel in sorted(set(base) | set(work)):
    if rel not in base or rel not in work:
        bad.append(f"{rel}: only in {'base' if rel in base else 'working tree'}")
        continue
    if dump(base[rel], True) != dump(work[rel], False):
        bad.append(f"{rel}: code or data differs")
print(f"objcompare: {len(set(base) & set(work))} objects compared")
for b in bad:
    print("  " + b)
sys.exit(1 if bad else 0)
EOF
RC=$?
git worktree remove --force "$TMP/base_src" >/dev/null 2>&1
[ -n "$WORK_REV" ] && git worktree remove --force "$TMP/work_src" >/dev/null 2>&1
[ $RC -eq 0 ] && echo "G9 PASS: identical" || echo "G9 FAIL"
exit $RC
