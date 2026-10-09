#!/bin/sh
# Extract the in-scope RetroCore C# files from the pinned commit into
# build/ethport-cs-snapshot/, so every port tool reads the exact source the
# port follows - not RetroCore's working tree, which carries the trace hooks
# (and any later edits). Needs RETROCORE_DIR.
#
# On WSL with RetroCore on a Windows drive, set RETROCORE_GIT=git.exe and
# RETROCORE_GIT_DIR to the Windows path, because WSL git is very slow there.
set -e
cd "$(dirname "$0")/../.."
: "${RETROCORE_DIR:?set RETROCORE_DIR to the RetroCore checkout}"
COMMIT=935163feb549a7edd3f3595589efa66d70a6e172
GIT=${RETROCORE_GIT:-git}
GITDIR=${RETROCORE_GIT_DIR:-$RETROCORE_DIR}
OUT=build/ethport-cs-snapshot
rm -rf "$OUT"
mkdir -p "$OUT"
for rel in $(grep -v '^#' tools/ethport/snapshot_files.txt); do
    mkdir -p "$OUT/$(dirname "$rel")"
    # </dev/null: git.exe would otherwise read stdin
    "$GIT" -C "$GITDIR" show "$COMMIT:$rel" </dev/null > "$OUT/$rel.tmp"
    tr -d '\r' < "$OUT/$rel.tmp" > "$OUT/$rel"
    rm "$OUT/$rel.tmp"
    if [ ! -s "$OUT/$rel" ]; then
        echo "snapshot: $rel is empty at $COMMIT" >&2
        exit 1
    fi
done
echo "$COMMIT" > "$OUT/COMMIT"
echo "snapshot of RetroCore $COMMIT in $OUT ($(grep -vc '^#' tools/ethport/snapshot_files.txt) files)"
