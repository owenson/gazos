#!/bin/sh
# Build GazOS, boot it in QEMU and run the end-to-end shell tests
# (tools/test.py) for each firmware (UEFI, legacy BIOS) and keyboard
# (USB-only, PS/2-only). The disk is opened read-only (-snapshot).
#
#   ./test.sh                  test every combination, in a visible QEMU window
#   ./test.sh --firmware uefi  test one firmware only (uefi or bios)
#   ./test.sh --keyboard usb   test one keyboard only (usb or ps2)
#   ./test.sh --headless       no window (CI)
#   ./test.sh --keep           keep the serial logs afterwards
set -e
cd "$(dirname "$0")"
./build.sh
exec python3 tools/test.py "$@"
