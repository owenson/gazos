#!/bin/sh
# Print the QEMU command line for booting GazOS.
#
#   tools/qemu-args.sh <uefi|bios> <disk.img> [ovmf vars file]
#
# KEYBOARD=both (default) gives the machine a PS/2 keyboard and a USB
# keyboard on an xHCI controller; KEYBOARD=usb removes the PS/2 (8042)
# controller entirely, like many new PCs; KEYBOARD=ps2 has no USB.
#
# Both modes use the q35 machine, whose ICH9 AHCI controller is where the
# disk is attached (port 0). UEFI mode uses OVMF as split firmware: the
# code read-only, and a private, writable copy of the variable store (so
# the firmware never writes NvVars onto the disk image). The OVMF files are
# found automatically, or set OVMF_CODE and OVMF_VARS.
set -e
MODE=$1; DISK=$2; VARS=${3:-ovmf_vars.fd}

case "${KEYBOARD:-both}" in
both) MACHINE=q35;            USB="-device qemu-xhci,id=xhci -device usb-kbd,bus=xhci.0" ;;
usb)  MACHINE=q35,i8042=off;  USB="-device qemu-xhci,id=xhci -device usb-kbd,bus=xhci.0" ;;
ps2)  MACHINE=q35;            USB="" ;;
*)    echo "KEYBOARD must be both, usb or ps2" >&2; exit 1 ;;
esac

COMMON="-machine $MACHINE -m 256 -name GazOS $USB
  -drive file=$DISK,format=raw,if=none,id=sata0
  -device ide-hd,drive=sata0,bus=ide.0"

case "$MODE" in
bios)
	echo "qemu-system-i386 $COMMON"
	;;
uefi)
	if [ -z "$OVMF_CODE" ]; then
		for code in \
			/usr/share/OVMF/OVMF_CODE_4M.fd \
			/usr/share/OVMF/OVMF_CODE.fd \
			/usr/share/edk2/x64/OVMF_CODE.4m.fd \
			/usr/share/edk2/ovmf/OVMF_CODE.fd \
			/usr/share/edk2-ovmf/x64/OVMF_CODE.fd \
			/usr/share/qemu/edk2-x86_64-code.fd; do
			[ -f "$code" ] && { OVMF_CODE=$code; break; }
		done
	fi
	if [ -z "$OVMF_VARS" ] && [ -n "$OVMF_CODE" ]; then
		for vars in "$(echo "$OVMF_CODE" | sed 's/CODE/VARS/; s/code/vars/')" \
			/usr/share/qemu/edk2-i386-vars.fd; do
			[ -f "$vars" ] && { OVMF_VARS=$vars; break; }
		done
	fi
	if [ ! -f "$OVMF_CODE" ] || [ ! -f "$OVMF_VARS" ]; then
		echo "UEFI firmware (OVMF) not found. Install it (Debian/Ubuntu: sudo apt install ovmf)," >&2
		echo "set OVMF_CODE and OVMF_VARS, or boot with legacy BIOS instead." >&2
		exit 1
	fi
	[ -f "$VARS" ] || cp "$OVMF_VARS" "$VARS"
	echo "qemu-system-x86_64 $COMMON
  -drive if=pflash,format=raw,readonly=on,file=$OVMF_CODE
  -drive if=pflash,format=raw,file=$VARS"
	;;
*)
	echo "usage: $0 <uefi|bios> <disk.img> [ovmf vars file]" >&2
	exit 1
	;;
esac
