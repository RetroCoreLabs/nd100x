#!/bin/sh
# check_changes.sh - house C standard checks on the lines a branch changes.
#
#   tools/house/check_changes.sh [BASE]      (default BASE: origin/main)
#
# Checks only lines ADDED relative to BASE in *.c / *.h, never whole files,
# so existing code is not flagged until someone touches it. Vendored and
# generated code (external/, tools/mkptypes/, *_protos.h, the NCR port) is
# skipped. Exits non-zero if any check fails.
#
# Checks:
#   1. banned functions (house rule 8.4)
#   2. non-ASCII bytes, CR line endings, trailing whitespace (rule 1.5)
#   3. clang-format on the changed lines (skipped with a note if
#      git-clang-format is not installed)

cd "$(dirname "$0")/../.." || exit 2

BASE="${1:-origin/main}"
FAIL=0

paths() {
    git diff --name-only --diff-filter=AM "$BASE" -- '*.c' '*.h' |
        grep -v -e '^external/' -e '^tools/mkptypes/' -e '_protos\.h$' \
                -e '^src/devices/scsi/ncr5386\.[ch]$'
}

FILES=$(paths)
if [ -z "$FILES" ]; then
    echo "house: no changed C files relative to $BASE"
    exit 0
fi

# Added lines only, prefixed with file:line so a hit can be found.
added_lines() {
    # shellcheck disable=SC2086
    git diff -U0 "$BASE" -- $FILES | awk '
        /^\+\+\+ / { file = substr($0, 7); next }
        /^@@ /     { split($3, a, ","); line = substr(a[1], 2) + 0; next }
        /^\+/      { print file ":" line ": " substr($0, 2); line++ }
    '
}

ADDED=$(added_lines)

BANNED='\b(gets|strcpy|strcat|sprintf|vsprintf|strncpy|atoi|atol|atof|alloca|system)[[:space:]]*\('
HITS=$(printf '%s\n' "$ADDED" | grep -E "$BANNED")
if [ -n "$HITS" ]; then
    echo "house: banned function in changed lines (rule 8.4):"
    printf '%s\n' "$HITS"
    FAIL=1
fi

HITS=$(printf '%s\n' "$ADDED" | LC_ALL=C grep -n "$(printf '[\200-\377]')")
if [ -n "$HITS" ]; then
    echo "house: non-ASCII byte in changed lines (rule 1.5):"
    printf '%s\n' "$HITS"
    FAIL=1
fi

HITS=$(printf '%s\n' "$ADDED" | grep "$(printf '\r')")
if [ -n "$HITS" ]; then
    echo "house: CR line ending in changed lines (rule 1.5):"
    printf '%s\n' "$HITS"
    FAIL=1
fi

# Tested on the line content only: the "file:line: " prefix ends in a blank.
# shellcheck disable=SC2086
HITS=$(git diff -U0 "$BASE" -- $FILES | awk '
    /^\+\+\+ / { file = substr($0, 7); next }
    /^@@ /     { split($3, a, ","); line = substr(a[1], 2) + 0; next }
    /^\+/      { if (substr($0, 2) ~ /[ \t]+$/) print file ":" line; line++ }
')
if [ -n "$HITS" ]; then
    echo "house: trailing whitespace in changed lines (rule 1.5):"
    printf '%s\n' "$HITS"
    FAIL=1
fi

if git clang-format -h >/dev/null 2>&1; then
    # shellcheck disable=SC2086
    FMT=$(git clang-format --diff "$BASE" -- $FILES 2>&1)
    case "$FMT" in
        ""|*"no modified files to format"*|*"did not modify any files"*) ;;
        *)
            echo "house: clang-format would change these lines:"
            printf '%s\n' "$FMT"
            FAIL=1
            ;;
    esac
else
    echo "house: git-clang-format not installed - format check skipped"
fi

if [ "$FAIL" -eq 0 ]; then
    echo "house: changed lines pass"
fi
exit "$FAIL"
