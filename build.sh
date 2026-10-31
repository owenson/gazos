#!/bin/sh
# Build GazOS: kernel, boot loaders (UEFI + legacy BIOS) and the bootable
# disk image (disk.img).
#
#   ./build.sh          incremental build
#   ./build.sh clean    remove build output, then build from scratch
set -e
cd "$(dirname "$0")"
PATH="$PATH:/usr/sbin:/sbin"	# mkfs.fat, fsck.fat and sfdisk live here

missing=""
for tool in gcc ld nasm make mkfs.fat fsck.fat mcopy mmd sfdisk x86_64-w64-mingw32-gcc; do
	command -v $tool >/dev/null 2>&1 || missing="$missing $tool"
done
if [ -n "$missing" ]; then
	echo "Missing build tools:$missing" >&2
	echo "On Debian/Ubuntu: sudo apt install build-essential nasm dosfstools mtools fdisk gcc-mingw-w64-x86-64" >&2
	exit 1
fi
if ! echo 'int x;' | gcc -m32 -ffreestanding -c -x c -o /dev/null - 2>/dev/null; then
	echo "gcc can't build 32-bit code (-m32)" >&2
	exit 1
fi

[ "$1" = clean ] && make clean
make
