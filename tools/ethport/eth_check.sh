#!/bin/sh
# Run every Ethernet II port check (plan section 0, rules R2-R5) in one go.
# Usage: tools/ethport/eth_check.sh [--final]
#   --final  also fail on matrix rows still PLANNED (end of the port)
# Needs build/ethport-cs-snapshot (tools/ethport/snapshot_retrocore.sh) and
# the .NET SDK (C# test listing). Builds the C tests in build_eth.
# Exit status: number of failed checks (0 = all passed).
cd "$(dirname "$0")/../.."
FINAL=$1
fails=0
step()
{
    name=$1
    shift
    echo "=== $name"
    if "$@"; then
        echo "=== $name: OK"
    else
        echo "=== $name: FAILED"
        fails=$((fails + 1))
    fi
}
[ -f build/ethport-cs-snapshot/COMMIT ] || { echo "run tools/ethport/snapshot_retrocore.sh first"; exit 1; }
[ -d build_eth ] || cmake -S . -B build_eth -DCMAKE_BUILD_TYPE=Debug >/dev/null
step "build C tests" cmake --build build_eth --target test_eth_runner_pass test_eth_runner_fail
if [ -f build_eth/bin/test_ethernet ] || ls tests/ethernet/test_eth_*.c >/dev/null 2>&1; then
    step "build test_ethernet" cmake --build build_eth --target test_ethernet
    step "run C tests" sh -c "cd build_eth && ctest -R 'eth' --output-on-failure"
    BIN="--binary=build_eth/bin/test_ethernet"
else
    step "run eth CTest (runner self-tests, Musashi)" sh -c "cd build_eth && ctest -R eth --output-on-failure"
    BIN=""
fi
step "stub_check (R3)" python3 tools/ethport/stub_check.py
step "const_check (R4)" python3 tools/ethport/const_check.py
step "matrix_check (R2)" python3 tools/ethport/matrix_check.py $FINAL
step "test_parity (R5)" python3 tools/ethport/test_parity.py $BIN
echo "eth-check: $fails failed step(s)"
exit $fails
