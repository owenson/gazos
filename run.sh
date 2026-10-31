#!/bin/sh
# Build GazOS and boot it in QEMU. The disk sits on the q35 machine's
# AHCI/SATA controller.
#
#   ./run.sh              boot via UEFI (OVMF firmware) - the default
#   ./run.sh --bios       boot via legacy BIOS (SeaBIOS) instead
#   ./run.sh --snapshot   don't write any changes back to disk.img
#   ./run.sh --serial     also print the kernel's serial console here
#   ./run.sh --gdb        wait for gdb on :1234 before starting the CPU
#                         (then: gdb kernel.elf -ex 'target remote :1234')
#   ./run.sh --headless   no window (use with --serial)
# Extra arguments after "--" are passed to QEMU.
set -e
cd "$(dirname "$0")"
./build.sh

MODE=uefi
EXTRA=""
while [ $# -gt 0 ]; do
	case "$1" in
	--bios)     MODE=bios ;;
	--uefi)     MODE=uefi ;;
	--snapshot) EXTRA="$EXTRA -snapshot" ;;
	--serial)   EXTRA="$EXTRA -serial stdio" ;;
	--gdb)      EXTRA="$EXTRA -s -S"
	            echo "QEMU is waiting for gdb: gdb kernel.elf -ex 'target remote :1234'" ;;
	--headless) EXTRA="$EXTRA -display none" ;;
	--)         shift; break ;;
	*)          echo "unknown option: $1 (see the top of $0)" >&2; exit 1 ;;
	esac
	shift
done

QEMU_CMD=$(tools/qemu-args.sh $MODE disk.img ovmf_vars.fd)
command -v ${QEMU_CMD%% *} >/dev/null || {
	echo "${QEMU_CMD%% *} not found (Debian/Ubuntu: sudo apt install qemu-system-x86)" >&2
	exit 1
}
echo "Booting GazOS via $MODE"
exec $QEMU_CMD $EXTRA "$@"
