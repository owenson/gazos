#!/bin/sh
# Build a bootable GazOS SATA disk image:
#
#   LBA 0       MBR (boot/mbr.bin) + partition table
#   LBA 2048..  FAT32 partition (type 0xEF = EFI System Partition, active):
#                 sector 0        VBR: BPB from mkfs.fat + boot/vbr.bin code
#                 sector 6        backup VBR (same)
#                 sectors 8..31   boot/stage2.bin (FAT32 reserved area)
#                 files           rootfs/*, /KERNEL.ELF, /src/*.[ch] and
#                                 /EFI/BOOT/BOOTX64.EFI
#
# The same image boots two ways:
#   UEFI:        firmware -> \EFI\BOOT\BOOTX64.EFI -> /KERNEL.ELF
#   legacy BIOS: MBR -> VBR -> stage 2 -> /KERNEL.ELF
#
# usage: mkdisk.sh <out.img> <boot dir> <kernel.elf> <rootfs dir> [size MB]
#                  [src dir] [BOOTX64.EFI]
set -e
PATH="$PATH:/usr/sbin:/sbin"

OUT=$1; BOOT=$2; KERNEL=$3; ROOTFS=$4; SIZE_MB=${5:-64}; SRCDIR=$6; EFILOADER=$7
[ -n "$ROOTFS" ] || { echo "usage: $0 out.img bootdir kernel.elf rootfs [size_mb]" >&2; exit 1; }

PART_START=2048
STAGE2_SECTOR=8		# must match boot/vbr.asm
STAGE2_MAX=24

S2SECTORS=$(( ($(stat -c%s "$BOOT/stage2.bin") + 511) / 512 ))
if [ "$S2SECTORS" -gt "$STAGE2_MAX" ]; then
	echo "stage2.bin is $S2SECTORS sectors, max $STAGE2_MAX" >&2
	exit 1
fi

PART=$OUT.part
rm -f "$OUT" "$PART"

# FAT32 filesystem, built separately then copied into the partition
truncate -s $(( SIZE_MB * 1024 * 1024 - PART_START * 512 )) "$PART"
mkfs.fat -F 32 -s 1 -R 32 -b 6 -h $PART_START -n GAZOS "$PART" >/dev/null
mcopy -s -i "$PART" "$ROOTFS"/* ::/
mcopy -i "$PART" "$KERNEL" ::/KERNEL.ELF
if [ -n "$EFILOADER" ]; then		# UEFI removable-media boot path
	mmd -i "$PART" ::/EFI ::/EFI/BOOT
	mcopy -i "$PART" "$EFILOADER" ::/EFI/BOOT/BOOTX64.EFI
fi
if [ -n "$SRCDIR" ]; then		# ship the OS's own sources in /src
	mmd -i "$PART" ::/src
	mcopy -i "$PART" "$SRCDIR"/*.c "$SRCDIR"/*.h ::/src
fi

# Boot code: VBR code area (bytes 90-509) in the boot sector and its
# backup, keeping mkfs.fat's BPB; stage 2 in the reserved sectors.
for sector in 0 6; do
	dd if="$BOOT/vbr.bin" of="$PART" bs=1 skip=90 seek=$(( sector * 512 + 90 )) \
	   count=420 conv=notrunc status=none
done
dd if="$BOOT/stage2.bin" of="$PART" bs=512 seek=$STAGE2_SECTOR conv=notrunc status=none
fsck.fat -n "$PART" >/dev/null

# Disk with one active partition. Type 0xEF marks it as the EFI System
# Partition for UEFI; our MBR boots any active partition, and the kernel
# accepts 0xEF as FAT32.
truncate -s ${SIZE_MB}M "$OUT"
printf 'label: dos\nstart=%d, type=ef, bootable\n' $PART_START | sfdisk -q "$OUT"
dd if="$PART" of="$OUT" bs=512 seek=$PART_START conv=notrunc status=none
rm -f "$PART"

# MBR code: first 446 bytes only, keeping sfdisk's partition table
dd if="$BOOT/mbr.bin" of="$OUT" bs=446 count=1 conv=notrunc status=none

echo "$OUT: ${SIZE_MB}MB, FAT32 at LBA $PART_START, stage 2 $S2SECTORS sectors," \
     "KERNEL.ELF $(stat -c%s "$KERNEL") bytes"
