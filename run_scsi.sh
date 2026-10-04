#!/bin/bash
#
# run_scsi.sh - test the ND-3201/3204 SCSI controller
#
# Two ways to exercise the SCSI disk:
#
#   ./run_scsi.sh          Boot SINTRAN from SMD0.IMG with the SCSI card
#                          attached, then mount the SCSI disk by hand.
#   ./run_scsi.sh boot     Boot directly from the SCSI disk.
#   ./run_scsi.sh debug    As default, plus the --scsi-debug protocol log.
#
# Any extra arguments are passed through to nd100x, e.g.
#   ./run_scsi.sh debug --trace
#
# ---------------------------------------------------------------------------
# Mounting the SCSI disk from a running SINTRAN (the default mode)
# ---------------------------------------------------------------------------
#   1. Wait for "SINTRAN III RUNNING" (takes ~25s at full speed).
#   2. Press ESC             -> "ENTER"
#   3. Type:  system         -> "PASSWORD:"
#   4. Press Enter           (no password)  -> "OK"
#   5. @ENTER-DIR,,DISC-SCSI-1,0
#   6. @LIST-DIRECTORIES-ENTERED,,   then Enter for OUTPUT FILE
#      -> DIR INDEX 0 : DISC-75MB-1 UNIT 0 : PACK-ONE
#         DIR INDEX 1 : DISC-SCSI-1 UNIT 0 ** 125 MB ** : SCSI-PACK
#   7. @DIR   -> DIRECTORY NAME: SCSI-PACK
#
# ---------------------------------------------------------------------------
# NOTE on SCSI-K.image
# ---------------------------------------------------------------------------
# The repo copy of SCSI-K.image has been modified in two places so it can be
# mounted alongside SMD0.IMG. The original at D:\ND\HDD\SCSI-K.image is
# untouched. Both edits are in the master block (image bytes 2000-2047):
#
#   * directory name       PACK-ONE -> SCSI-PACK
#     SMD0.IMG is also called PACK-ONE, and SINTRAN cannot hold two
#     directories with the same name - ENTER-DIRECTORY reported success but
#     silently did not mount it.
#
#   * ext_last_system_number  21238 -> 102
#     The disk was still marked as entered by ND-110 Satellite system 21238
#     and was never released, so SINTRAN (CPU NUMBER 102) refused it with
#     "DIRECTORY ENTERED BY ANOTHER SYSTEM".
#
# Both values were rewritten with the norskdata-ndfs Python API so the master
# block checksum stays valid.
#
# ---------------------------------------------------------------------------
# Timing
# ---------------------------------------------------------------------------
# The SCSI bootstrap waits ~1 second of emulated time after resetting the SCSI
# bus (50 RTC ticks x 20ms), which is ~530,000 CPU instructions. If you cap
# --max-instr below that the run looks like a hang - it is not.

set -e

cd "$(dirname "$0")"

NDX=./build/bin/nd100x
SCSI_IMAGE=SCSI-K.image

if [ ! -x "$NDX" ]; then
    echo "Error: $NDX not built. Run 'make debug' first." >&2
    exit 1
fi

if [ ! -f "$SCSI_IMAGE" ]; then
    echo "Error: $SCSI_IMAGE not found in $(pwd)." >&2
    echo "Copy it in with:  cp /mnt/d/ND/HDD/SCSI-K.image ." >&2
    exit 1
fi

MODE="${1:-smd}"
[ $# -gt 0 ] && shift

case "$MODE" in
    boot|scsi)
        # Boot straight off the SCSI disk.
        echo ">>> Booting from SCSI (${SCSI_IMAGE})"
        exec "$NDX" --boot=scsi --scsi0=hdd:"$SCSI_IMAGE" "$@"
        ;;
    debug)
        # SMD boot + SCSI card, with the protocol log on stderr.
        echo ">>> Booting SINTRAN from SMD0.IMG, SCSI card attached (--scsi-debug)"
        echo ">>> Tip: 2>scsi.log  to keep the trace out of the console"
        exec "$NDX" --boot=smd --scsi0=hdd:"$SCSI_IMAGE" --scsi-debug "$@"
        ;;
    smd|*)
        # Default: boot the known-good SMD system, SCSI disk available to mount.
        echo ">>> Booting SINTRAN from SMD0.IMG, SCSI card attached"
        echo ">>> ESC -> system -> <Enter> -> @ENTER-DIR,,DISC-SCSI-1,0"
        exec "$NDX" --boot=smd --scsi0=hdd:"$SCSI_IMAGE" "$@"
        ;;
esac
