# Makefile for GazOS
#
#   make          build kernel.elf, the boot loaders and disk.img
#   make run      boot disk.img in QEMU via UEFI (q35 machine: disk on AHCI/SATA)
#   make run-bios boot it via legacy BIOS instead
#   make test     boot it and run the end-to-end tests (tools/test.py)
#   make clean
#
# ./build.sh, ./run.sh and ./test.sh wrap these with dependency checks.

REMOVE = rm -f

# Freestanding 32-bit build with the host gcc/binutils
CC      = gcc
CFLAGS  = -m32 -ffreestanding -fno-pie -fno-pic -fno-stack-protector \
          -fno-builtin -fcf-protection=none -fno-asynchronous-unwind-tables \
          -std=gnu89 -fomit-frame-pointer -O -I. -Wall -Wno-pointer-sign \
          -Wno-main -Wno-implicit-function-declaration -Wno-int-conversion
LD      = ld

# The UEFI loader is a 64-bit PE executable, built with MinGW-w64
EFICC   = x86_64-w64-mingw32-gcc
EFICFLAGS = -ffreestanding -fno-stack-protector -fno-stack-check -mno-red-zone \
          -fno-asynchronous-unwind-tables -O2 -Wall -nostdlib
EFILDFLAGS = -nostdlib -Wl,--subsystem,10 -Wl,--dynamicbase -e efi_main
LDFLAGS = -m elf_i386 -N -e _start --build-id=none --no-warn-rwx-segments -Ttext 0x100000
NASM    = nasm

# The object files belonging to the kernel
OBJS = idt.o kernel.o io.o video.o 8259.o keyboard.o pit.o delay.o string.o \
       math.o mem.o serial.o font8x16.o pci.o ahci.o fat32.o paging.o xhci.o

KERNELFN = kernel.elf
BOOT     = boot/mbr.bin boot/vbr.bin boot/stage2.bin
EFILOADER = uefi/BOOTX64.EFI
DISK     = disk.img
DISK_MB  = 64

all: $(DISK)

# Link the kernel statically at 1MB
$(KERNELFN): $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

boot/%.bin: boot/%.asm
	$(NASM) -f bin -o $@ $<

$(EFILOADER): uefi/loader.c uefi/efi.h bootinfo.h
	$(EFICC) $(EFICFLAGS) -o $@ uefi/loader.c $(EFILDFLAGS)

$(DISK): $(KERNELFN) $(BOOT) $(EFILOADER) tools/mkdisk.sh $(wildcard *.c *.h) $(shell find rootfs | sed 's/ /\\ /g')
	tools/mkdisk.sh $@ boot $(KERNELFN) rootfs $(DISK_MB) . $(EFILOADER)

%.o: %.c bootinfo.h
	$(CC) $(CFLAGS) -c -o $@ $<

run: $(DISK)
	$$(tools/qemu-args.sh uefi $(DISK) ovmf_vars.fd)

run-bios: $(DISK)
	$$(tools/qemu-args.sh bios $(DISK))

test: $(DISK)
	python3 tools/test.py

clean:
	$(REMOVE) $(OBJS) $(KERNELFN) $(BOOT) $(EFILOADER) $(DISK)

.PHONY: all run run-bios test clean
