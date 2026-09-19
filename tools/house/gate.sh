#!/bin/sh
# gate.sh - the per-step gate of docs/HOUSE_STANDARD_FULL_CLEANUP_PLAN.md.
#
#   tools/house/gate.sh [--golden] [--extra=cpu,devices,floppy,wasm,dap] [--skip-wasm]
#
# Runs G1-G8 always (G9 is tools/house/objcompare.sh, run by hand on
# behaviour-neutral steps) and the extra gates named with --extra. Stops at
# the first failure and exits non-zero. Prints GATE PASS at the end.
#
# --golden   write the golden files instead of comparing against them
#            (phase 0.5 only).
#
# Builds are CLEAN builds in their own directories (build_gate_debug,
# build_gate_release, build_gate_wasm): an incremental build does not
# recompile unchanged files and so would hide their warnings.
#
# Needs: cmake, gcc, python3. WASM: emcmake on PATH (source emsdk_env.sh).
# Scratch files go to $GATE_TMP (default: a mktemp directory).

cd "$(dirname "$0")/../.." || exit 2
REPO=$(pwd)

GOLDEN_DIR=docs/house-audit/golden
WRITE_GOLDEN=0
EXTRA=""
SKIP_WASM=0
for arg in "$@"; do
    case "$arg" in
    --golden) WRITE_GOLDEN=1 ;;
    --extra=*) EXTRA="${arg#--extra=}" ;;
    --skip-wasm) SKIP_WASM=1 ;;
    *) echo "gate: unknown argument $arg"; exit 2 ;;
    esac
done

TMP="${GATE_TMP:-$(mktemp -d)}"
mkdir -p "$TMP"
LOG="$TMP/gate.log"
: > "$LOG"

fail() {
    echo "GATE FAIL: $*"
    echo "  details: $LOG"
    exit 1
}
step() { echo "== $*"; }

has_extra() {
    case ",$EXTRA," in *",$1,"*) return 0 ;; esac
    return 1
}

# Warnings from project files only: vendored and nd500x lines are dropped.
# The builds run with make -Oline so parallel jobs cannot interleave lines;
# a warning line that still has no file name fails the gate (it cannot be
# attributed, so it cannot be excused).
project_warnings() {
    grep -E 'warning:' "$1" |
        grep -v -e '/external/' -e '/tools/mkptypes/' -e 'nd500x/' \
                -e 'ncr5386\.' -e '^emcc: warning' |
        sort -u
}

build_clean() {  # dir, extra cmake args...
    dir=$1
    shift
    rm -rf "$dir"
    mkdir -p "$dir"
    (cd "$dir" && cmake .. "$@" >>"$LOG" 2>&1) || fail "cmake configure $dir"
    cmake --build "$dir" -j"$(nproc)" -- -Oline >"$TMP/$(basename "$dir").build" 2>&1 ||
        fail "build $dir (see $TMP/$(basename "$dir").build)"
    W=$(project_warnings "$TMP/$(basename "$dir").build")
    if [ -n "$W" ]; then
        echo "$W"
        fail "warnings in $dir"
    fi
}

# ---- G1 / G2: native Debug and Release, 0 project warnings --------------
step "G1 native Debug build"
build_clean build_gate_debug -DCMAKE_BUILD_TYPE=Debug
step "G2 native Release build"
build_clean build_gate_release -DCMAKE_BUILD_TYPE=Release

# ---- G3: WASM glass build ------------------------------------------------
if [ "$SKIP_WASM" -eq 0 ]; then
    step "G3 WASM glass build"
    command -v emcmake >/dev/null 2>&1 || fail "emcmake not on PATH (source emsdk_env.sh)"
    rm -rf build_gate_wasm
    mkdir -p build_gate_wasm
    (cd build_gate_wasm && emcmake cmake .. -DBUILD_WASM=ON -DDEBUGGER_ENABLED=ON >>"$LOG" 2>&1) ||
        fail "WASM configure"
    cmake --build build_gate_wasm -j"$(nproc)" -- -Oline >"$TMP/build_gate_wasm.build" 2>&1 ||
        fail "WASM build"
    # emcmake exports CC=emcc and the configure step rebuilds the native
    # mkptypes tool with it (known build bug, see the nd100x profile).
    # Rebuild it with gcc so later native builds keep working.
    make -B -C tools/mkptypes CC=gcc >>"$LOG" 2>&1 || fail "mkptypes repair"
    W=$(project_warnings "$TMP/build_gate_wasm.build")
    if [ -n "$W" ]; then
        echo "$W"
        fail "warnings in WASM build"
    fi
else
    echo "== G3 skipped (--skip-wasm)"
fi

BIN="$REPO/build_gate_debug/bin/nd100x"

# ---- G4: binary freshness ------------------------------------------------
step "G4 binary freshness"
HEAD=$(git rev-parse --short HEAD)
VER=$("$BIN" --version)
echo "$VER"
case "$VER" in
*"git $HEAD"*) ;;
*) fail "binary hash does not match HEAD $HEAD" ;;
esac

# ---- G5: unit tests ------------------------------------------------------
step "G5 unit tests"
(cd build_gate_debug && ctest --output-on-failure >"$TMP/ctest.log" 2>&1) ||
    fail "ctest (see $TMP/ctest.log)"
grep 'tests passed' "$TMP/ctest.log"

# ---- G6 / G7: SMD boot and golden ----------------------------------------
# Fixed instruction count and stdin from /dev/null: the run is deterministic
# (ticks RTC). Phase 0.4 showed two runs differ only in the host timing
# lines, which are removed before the compare.
boot_smd() {  # binary, tag
    cp SMD0.IMG "$TMP/$2.img" || fail "copy SMD0.IMG"
    timeout 600 "$1" --boot=smd --smd0="$TMP/$2.img" --pipe --max-instr=100000000 \
        </dev/null >"$TMP/$2.console" 2>"$TMP/$2.stderr"
    RC=$?
    [ "$RC" -eq 0 ] || fail "SMD boot exit code $RC"
    grep -q 'SINTRAN III RUNNING' "$TMP/$2.console" || fail "no SINTRAN III RUNNING"
    if grep -a -q -e 'WARNING' -e 'ERROR' "$TMP/$2.console"; then
        grep -a -e 'WARNING' -e 'ERROR' "$TMP/$2.console"
        fail "WARNING/ERROR on the SMD console"
    fi
    if grep -a -q -e '^\[ERROR\]' -e '^\[WARN\]' "$TMP/$2.stderr"; then
        grep -a -e '^\[ERROR\]' -e '^\[WARN\]' "$TMP/$2.stderr"
        fail "ERROR/WARN in the SMD log"
    fi
    grep -a -v -e '^Number of instructions run:' -e '^usertime:' \
        -e '^Current cpu cycle time is:' "$TMP/$2.console" >"$TMP/$2.console.norm"
    sha256sum <"$TMP/$2.img" | cut -d' ' -f1 >"$TMP/$2.img.sha256"
}

step "G6 SMD boot"
boot_smd "$BIN" smd
echo "SINTRAN III RUNNING, no WARNING/ERROR"

step "G7 SMD boot golden"
if [ "$WRITE_GOLDEN" -eq 1 ]; then
    mkdir -p "$GOLDEN_DIR"
    cp "$TMP/smd.console.norm" "$GOLDEN_DIR/smd_boot.console"
    cp "$TMP/smd.img.sha256" "$GOLDEN_DIR/smd_boot.img.sha256"
    echo "golden written to $GOLDEN_DIR"
else
    cmp "$TMP/smd.console.norm" "$GOLDEN_DIR/smd_boot.console" ||
        fail "SMD console differs from golden"
    cmp "$TMP/smd.img.sha256" "$GOLDEN_DIR/smd_boot.img.sha256" ||
        fail "SMD image after boot differs from golden"
    echo "console and disk image identical to golden"
fi

# ---- G8: rule counts -----------------------------------------------------
step "G8 rule counts"
python3 tools/house/audit.py --compare docs/house-audit/counts.json >"$TMP/audit.txt" 2>&1 ||
    { cat "$TMP/audit.txt"; fail "a rule count went up (tools/house/audit.py)"; }
tail -1 "$TMP/audit.txt"

# ---- extra gates ---------------------------------------------------------
if has_extra cpu; then
    step "G10 TPE INSTRUCTION-C03 and PAGING-C02"
    cp FLOPPY.IMG "$TMP/tpe.img" || fail "copy FLOPPY.IMG"
    python3 tools/tpe_autorun.py --image "$TMP/tpe.img" --outdir "$TMP/tpe" \
        --only INSTRUCTION-C03,PAGING-C02 >"$TMP/tpe.txt" 2>&1 ||
        { cat "$TMP/tpe.txt"; fail "TPE"; }
    grep -E 'PASS|FAIL' "$TMP/tpe.txt" | tail -2
fi

if has_extra devices; then
    step "G11 Winchester boot"
    cp WD0.IMG "$TMP/wd.img" || fail "copy WD0.IMG"
    timeout 600 "$BIN" --boot=wd --wd0="$TMP/wd.img" --pipe --max-instr=100000000 \
        </dev/null >"$TMP/wd.console" 2>&1 || fail "WD boot exit code"
    grep -q 'SINTRAN III RUNNING' "$TMP/wd.console" || fail "WD: no SINTRAN III RUNNING"
    echo "SINTRAN III RUNNING"
fi

if has_extra floppy; then
    step "G12 floppy boot"
    cp FLOPPY.IMG "$TMP/fl.img" || fail "copy FLOPPY.IMG"
    timeout 300 "$BIN" --boot=floppy --image="$TMP/fl.img" --pipe --max-instr=20000000 \
        </dev/null >"$TMP/fl.console" 2>&1
    grep -q 'TPE Monitor' "$TMP/fl.console" || fail "floppy: no TPE Monitor"
    echo "TPE Monitor"
fi

if has_extra wasm; then
    step "G13 puppeteer"
    [ -d build_wasm_glass/bin ] || fail "run make wasm-glass first (puppeteer serves build_wasm_glass/bin)"
    for t in verify-disasm-worker.js test-gateway-browser.js test-hdd-manager-browser.js; do
        timeout 300 node "$t" >"$TMP/$t.log" 2>&1 || { tail -20 "$TMP/$t.log"; fail "$t"; }
        echo "$t: pass"
    done
fi

if has_extra dap; then
    step "G14 DAP attach"
    python3 tools/house/dap_check.py --binary "$BIN" --image SMD0.IMG --tmp "$TMP" ||
        fail "DAP attach/pause"
fi

echo "GATE PASS ($HEAD)"
