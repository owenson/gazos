#!/bin/sh
# Build GazOS, boot it in QEMU and run the end-to-end shell tests
# (tools/test.py), via UEFI and via legacy BIOS. The disk is opened
# read-only (-snapshot).
#
#   ./test.sh                  test both boot modes, in a visible QEMU window
#   ./test.sh --firmware uefi  test one mode only (uefi or bios)
#   ./test.sh --headless       no window (CI)
#   ./test.sh --keep           keep the serial logs afterwards
set -e
cd "$(dirname "$0")"
./build.sh
exec python3 tools/test.py "$@"
