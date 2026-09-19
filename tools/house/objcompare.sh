#!/bin/sh
# objcompare.sh - gate G9: prove a step is behaviour-neutral.
#
#   tools/house/objcompare.sh BASE [RENAMES.tsv]
#
# Builds BASE (a commit, in a temporary git worktree) and the working tree as
# Release with -g0, then compares, for every project object file, the
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

build() {  # src dir, build dir
    mkdir -p "$2"
    (cd "$2" && cmake "$1" -DCMAKE_BUILD_TYPE=Release "-DCMAKE_C_FLAGS=-g0 -ffile-prefix-map=$1=." >/dev/null 2>&1) || return 1
    # mkptypes is built in the source tree; use the native one from REPO
    cmake --build "$2" -j"$(nproc)" >"$2.log" 2>&1
}

build "$TMP/base_src" "$TMP/base" || { echo "BASE build failed ($TMP/base.log)"; git worktree remove --force "$TMP/base_src"; exit 2; }
build "$REPO" "$TMP/work" || { echo "working-tree build failed ($TMP/work.log)"; git worktree remove --force "$TMP/base_src"; exit 2; }

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
    data = subprocess.run(["objdump", "-s", "-j", ".rodata", "-j", ".data", path],
                          capture_output=True, text=True).stdout
    txt = "\n".join(out.split("\n")[2:]) + "\n" + "\n".join(data.split("\n")[2:])
    if is_base and sym:
        txt = sym.sub(lambda m: mapping[m.group(1)], txt)
    return txt

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
[ $RC -eq 0 ] && echo "G9 PASS: identical" || echo "G9 FAIL"
exit $RC
