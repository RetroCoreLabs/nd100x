#!/bin/sh
# stage-deb.sh - lay out the files the nd100x .deb installs
#
# Usage: tools/stage-deb.sh <build-dir> <stage-dir>
#
# Run from the repository root after `make release`. Writes into <stage-dir>
# (removed first):
#   usr/bin/nd100x                                    the binary
#   usr/share/applications/nd100x.desktop             menu entry
#   usr/share/icons/hicolor/scalable/apps/nd100x.svg  icon, any size
#   usr/share/icons/hicolor/512x512/apps/nd100x.png   icon, fixed size
#
# An ELF binary has no place for an icon; on Linux the icon belongs to the
# .desktop entry, which names it as "nd100x" and the desktop finds it in the
# hicolor theme.
set -eu

BUILD="$1"
STAGE="$2"

rm -rf "$STAGE"
mkdir -p "$STAGE/usr/bin" \
         "$STAGE/usr/share/applications" \
         "$STAGE/usr/share/icons/hicolor/scalable/apps" \
         "$STAGE/usr/share/icons/hicolor/512x512/apps"

cp "$BUILD/bin/nd100x"                  "$STAGE/usr/bin/nd100x"
cp src/frontend/nd100x/nd100x.desktop   "$STAGE/usr/share/applications/nd100x.desktop"
cp assets/nd100x.svg                    "$STAGE/usr/share/icons/hicolor/scalable/apps/nd100x.svg"
cp assets/nd100x-512.png                "$STAGE/usr/share/icons/hicolor/512x512/apps/nd100x.png"
