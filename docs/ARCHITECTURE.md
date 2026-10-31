# GazOS architecture

GazOS is a small 32-bit x86 operating system. It boots via UEFI (the
default) or a legacy BIOS from the same disk image, finds a SATA disk
through the AHCI controller, mounts a FAT32 filesystem and runs a command
shell. This document explains how the
pieces fit together, from power-on to the shell prompt.

- [Overview](#overview)
- [Source tree](#source-tree)
- [Build pipeline](#build-pipeline)
- [Disk layout](#disk-layout)
- [Boot sequence: UEFI](#boot-sequence-uefi)
- [Boot sequence: legacy BIOS](#boot-sequence-legacy-bios)
- [Kernel entry and boot protocol](#kernel-entry-and-boot-protocol)
- [Memory map](#memory-map)
- [Kernel](#kernel)
- [Drivers](#drivers)
- [Shell](#shell)
- [Testing and debugging](#testing-and-debugging)
- [Limitations](#limitations)

## Overview

```
 UEFI ──► \EFI\BOOT\BOOTX64.EFI ─────────────────────┐
          (64-bit; reads the file with UEFI services)  │
                                                       ├──► KERNEL.ELF ──► shell
 BIOS ──► MBR ──► VBR ──► stage 2 ─────────────────────┘    (32-bit, the
          LBA 0   FAT32    FAT32 reserved sectors           same file for
                  boot     (16-bit; reads FAT32 itself)     both paths)
                  sector
```

| Layer            | Mode                     | Talks to the disk through        |
|------------------|--------------------------|----------------------------------|
| UEFI loader      | 64-bit long mode         | UEFI Simple File System protocol |
| BIOS boot chain  | 16-bit real/unreal mode  | BIOS INT 13h extensions (LBA)    |
| Kernel           | 32-bit protected mode    | its own AHCI driver (DMA)        |

Both loaders end in the same place: the 32-bit kernel at 1 MB, entered in
protected mode with paging off. The UEFI loader also passes a small
[boot info](#kernel-entry-and-boot-protocol) structure (framebuffer,
memory size). After that the firmware isn't used, so the kernel has its
own PCI, AHCI and FAT32 drivers.

Everything runs in ring 0 with flat 4 GB segments and **paging disabled**,
so linear addresses are physical addresses. The AHCI driver depends on
this: it gives kernel buffer addresses directly to the controller for DMA.

## Source tree

| Path                 | What it is                                              |
|----------------------|---------------------------------------------------------|
| `uefi/loader.c`      | UEFI loader (`BOOTX64.EFI`): loads the kernel, long mode → 32-bit trampoline |
| `uefi/efi.h`         | Minimal UEFI type and protocol definitions              |
| `bootinfo.h`         | Boot protocol between the UEFI loader and the kernel    |
| `boot/mbr.asm`       | Master boot record: chain-loads the active partition    |
| `boot/vbr.asm`       | FAT32 volume boot record: loads stage 2                 |
| `boot/stage2.asm`    | Stage 2: FAT32 reader + ELF loader, enters protected mode |
| `kernel.c`           | Entry point (`_start`), GDT, init sequence, shell       |
| `idt.c` `idt.h`      | Interrupt descriptor table, CPU exception handlers      |
| `gdt.h`              | Descriptor macros (GDT/IDT entry layout)                |
| `8259.c` `8259.h`    | Programmable interrupt controller (IRQ remapping, masks) |
| `pit.c` `pit.h`      | Programmable interval timer (IRQ 0)                     |
| `delay.c` `delay.h`  | Busy-wait delay loop, calibrated against the PIT        |
| `keyboard.c`         | PS/2 keyboard driver (IRQ 1), scancode to ASCII         |
| `video.c` `video.h`  | Console (`kprint`, `printlong`, `cls`): VGA text mode, or a framebuffer text console under UEFI |
| `font8x16.c`         | 8×16 font for the framebuffer console (generated)       |
| `serial.c`           | COM1 serial console; copies all console output to COM1  |
| `mem.c` `mem.h`      | RAM size probe                                          |
| `pci.c` `pci.h`      | PCI configuration space, device search by class         |
| `ahci.c` `ahci.h`    | AHCI/SATA disk driver                                   |
| `fat32.c` `fat32.h`  | FAT32 filesystem driver (read-only)                     |
| `string.c` `math.c` `io.c` | `gets`/`kstrcmp`, `floor`/`ceil`, port I/O        |
| `rootfs/`            | Files copied onto the FAT32 volume                      |
| `tools/mkdisk.sh`    | Builds `disk.img`                                       |
| `tools/qemu-args.sh` | QEMU command line for UEFI or BIOS boots (finds OVMF)   |
| `tools/psf2c.py`     | Converts a PSF console font to `font8x16.c`             |
| `tools/test.py`      | End-to-end test (boots QEMU, types commands)            |
| `build.sh` `run.sh` `test.sh` | Entry points: build, run, test                 |

## Build pipeline

```
*.c ──gcc -m32 -ffreestanding──► *.o ──ld -m elf_i386 -Ttext 0x100000──► kernel.elf
boot/*.asm ──nasm -f bin──► boot/mbr.bin, boot/vbr.bin, boot/stage2.bin
uefi/loader.c ──x86_64-w64-mingw32-gcc──► uefi/BOOTX64.EFI  (PE32+, EFI application)
                                   │
kernel.elf + boot/*.bin + BOOTX64.EFI + rootfs/ + *.[ch] ──tools/mkdisk.sh──► disk.img
```

The kernel is built with the host's GCC and binutils. It is freestanding:
no libc, no libgcc, no startup files. The main compiler flags:

- `-m32 -ffreestanding -fno-builtin`: 32-bit code with no assumptions
  about a C library.
- `-fno-pie -fno-pic`: absolute addresses. The kernel is linked to run at 1 MB.
- `-fno-stack-protector -fcf-protection=none -fno-asynchronous-unwind-tables`:
  turn off features that need runtime support or add unneeded sections.

`ld -N` packs text, data and bss into a single PT_LOAD segment without
page alignment, which keeps the ELF file small and the loaders simple.

UEFI executables are PE32+ files using the Microsoft x64 calling
convention. MinGW-w64's GCC produces exactly that, so the loader is
linked directly as an EFI application (`--subsystem 10`) with no
gnu-efi or EDK2 dependency. `--dynamicbase` adds a `.reloc` section so
the firmware can load it at any address. The few UEFI definitions it
needs are in `uefi/efi.h`.

`tools/mkdisk.sh` does the following:

1. Creates the FAT32 filesystem in a separate file with `mkfs.fat -F 32
   -s 1 -R 32 -b 6 -h 2048` (512-byte clusters, 32 reserved sectors,
   backup boot sector at 6, hidden sectors = partition start).
2. Copies `rootfs/*`, `kernel.elf` as `/KERNEL.ELF`, the UEFI loader as
   `/EFI/BOOT/BOOTX64.EFI` (the default "removable media" boot path) and
   the sources to `/src` with `mcopy`.
3. Writes the VBR code into bytes 90–509 of the boot sector **and** of the
   backup boot sector (sector 6), keeping the BPB written by mkfs.fat.
4. Writes stage 2 into reserved sectors 8–31.
5. Checks the result with `fsck.fat -n`.
6. Creates the disk: `sfdisk` writes an MBR partition table with one
   active partition of type `0xEF` (EFI System Partition) at sector 2048,
   the filesystem is copied in, then the first 446 bytes of the MBR are
   filled with `boot/mbr.bin`.

## Disk layout

```
LBA 0           MBR: boot code (446 bytes) + partition table + 0xAA55
LBA 1-2047      unused (1 MB alignment gap)
LBA 2048        ┌─ FAT32 partition (type 0xEF = ESP, active) ────────┐
  +0            │ boot sector: jmp, BPB (mkfs.fat), VBR code, 0xAA55 │
  +1            │ FSInfo                                            │
  +2..5         │ (free)                                            │
  +6            │ backup boot sector (same VBR code)                │
  +7            │ backup FSInfo                                     │
  +8..31        │ stage 2 (up to 24 sectors = 12 KB)                │
  +32           │ FAT #1                                            │
  +32+F         │ FAT #2                                            │
  +32+2F        │ data: cluster 2 (= root directory), 3, 4, ...     │
                │   /EFI/BOOT/BOOTX64.EFI   UEFI loader             │
                │   /KERNEL.ELF             the kernel (both paths) │
                │   /src, /docs, ...        other files             │
                └───────────────────────────────────────────────────┘
```

**Why one image boots both ways.** UEFI firmware ignores the MBR boot
code. It looks for a FAT partition marked as an EFI System Partition
(MBR type `0xEF`) and runs `\EFI\BOOT\BOOTX64.EFI` from it. A legacy
BIOS ignores partition types and runs the MBR code, which boots
whichever partition is marked active. GazOS's MBR doesn't check the type
either, and the kernel's FAT32 driver accepts types `0x0B`, `0x0C` and
`0xEF`.

Nothing that belongs to the filesystem is overwritten. The reserved sectors
are part of FAT32 and are set aside for boot code, so the volume can still
be mounted and checked normally:

```
mdir -i disk.img@@1M ::/                 # list it with mtools
dd if=disk.img of=part.img bs=512 skip=2048 && fsck.fat -n part.img
```

## Boot sequence: UEFI

This is the default. The firmware under QEMU is OVMF.

### 1. Firmware

UEFI finds the EFI System Partition, loads `\EFI\BOOT\BOOTX64.EFI` (a
PE32+ image) into memory, applies its relocations, and calls
`efi_main(ImageHandle, SystemTable)`. The CPU is in 64-bit long mode
with identity-mapped paging, and the firmware's boot services are
available.

### 2. UEFI loader: `uefi/loader.c`

1. Turns off the firmware watchdog, which would otherwise reset the
   machine after 5 minutes.
2. **Reads the kernel.** Gets the Loaded Image protocol for its own
   image, and through that the Simple File System protocol of the volume
   it was loaded from. Opens `\KERNEL.ELF`, finds the size by seeking to
   the end, and reads the file into pool memory.
3. **Loads the ELF.** Checks it is a 32-bit i386 ELF. For each `PT_LOAD`
   segment, asks the firmware for those exact physical pages
   (`AllocatePages(AllocateAddress)` at 1 MB), copies the file data and
   zeroes `.bss`. Reserving the pages first means no firmware data can
   be sitting there.
4. **Boot info.** Allocates a page below 4 GB for `struct boot_info`.
   Fills in the GOP framebuffer address, size and pitch, if the framebuffer
   is 32 bpp RGB/BGR and below 4 GB.
5. **Trampoline.** Copies a small position-independent code blob into
   another page below 4 GB (see step 4).
6. **Exits boot services.** Gets the memory map and calls
   `ExitBootServices` with its key, retrying if the map changed in
   between. After this, no firmware calls (not even printing) are
   allowed. It works out the usable RAM from the final memory map
   (loader, boot services and conventional memory) for `boot_info`.
7. Calls the trampoline with (page address, kernel entry, boot info).

### 3. Trampoline: long mode → 32-bit protected mode

Runs from its page below 4 GB, which is identity mapped, so its address
is the same with paging on or off:

1. `cli`, loads its own GDT (null, `0x08` 32-bit flat code, `0x10` flat
   data).
2. Far-returns (`lretq`) to the 32-bit code segment. The CPU is now in
   *compatibility mode*: long mode is still active, but it runs 32-bit
   code.
3. Clears CR0.PG. Turning paging off makes the CPU leave long mode.
4. Clears EFER.LME (MSR `0xC0000080`) and CR4.PAE.
5. Loads the flat data selectors, sets ESP to the top of the page, puts
   `BOOTINFO_MAGIC` in EAX and the boot info address in EBX, and jumps to
   the kernel's entry point.

From here on, both boot paths are the same:
[kernel entry](#kernel-entry-and-boot-protocol).

## Boot sequence: legacy BIOS

Used with `./run.sh --bios`, or on machines without UEFI (or with UEFI
CSM enabled).

### 1. BIOS

The BIOS (SeaBIOS under QEMU) reads LBA 0 of the boot disk to `0x7C00`,
checks for the `0xAA55` signature, and jumps there with `DL` = the BIOS
drive number (`0x80` for the first hard disk).

### 2. MBR: `boot/mbr.asm`

1. Sets up segments and a stack below `0x7C00`.
2. Copies itself from `0x7C00` to `0x0600`, because the next boot sector
   has to be loaded at `0x7C00`, and far-jumps to the copy.
3. Finds the partition entry with the active flag (`0x80`).
4. Reads that partition's first sector to `0x7C00` with INT 13h `AH=42h`
   (LBA read using a disk address packet).
5. Checks for `0xAA55` and jumps to `0000:7C00` with `DL` = boot drive.

Error letters on screen: `P` = no active partition, `D` = disk read
error, `V` = partition boot sector has no signature.

### 3. VBR: `boot/vbr.asm`

The partition's first sector starts with `EB 58 90` (written by mkfs.fat),
which jumps over the BPB to offset 90, where our code is.

1. Checks the BPB has at least 32 reserved sectors (`R` if not).
2. Reads 24 sectors from partition sector 8 (the BPB "hidden sectors"
   field gives the partition start) to `0x7E00`.
3. Jumps to `0000:7E00` with `DL` = boot drive. The VBR, including the BPB,
   stays at `0x7C00` for stage 2 to use.

Error letter: `S` = stage 2 read failed.

### 4. Stage 2: `boot/stage2.asm`

Prints `GazOS stage 2: loading /KERNEL.ELF`, enables the A20 line (port
`0x92`), then:

1. **FAT geometry.** From the BPB at `0x7C00`:
   - `fat_lba = hidden + reserved`
   - `data_lba = fat_lba + nfats × fat_size`
   - root directory cluster, sectors per cluster.
2. **Find the kernel.** Reads root directory clusters into `0xB000` and
   compares each entry's 8.3 name with `KERNEL  ELF`. It skips deleted
   entries, LFN entries, the volume label and directories. If the
   directory ends first, it follows the FAT chain to the next cluster.
3. **Load the file.** Starting at the file's first cluster, reads each
   cluster to `0x10000` and onwards, looking up the next cluster in the
   FAT. The FAT sector currently in use is cached at `0x0600`. It stops at
   the end-of-chain marker (≥ `0x0FFFFFF8`). Anything that would go past
   `0x9F000` (where the EBDA starts) is refused, which limits the kernel
   file to about 572 KB.
4. **Unreal mode.** Loads a GDT with flat 4 GB code/data descriptors,
   sets CR0.PE, loads DS/ES with the 4 GB data selector, and clears PE
   again. The CPU is back in real mode, but DS/ES keep their 4 GB limits,
   so 32-bit addresses above 1 MB now work. This is done *after* all BIOS
   calls, because the BIOS may reset segment limits.
5. **ELF load.** Checks the `\x7FELF` magic and that the file is 32-bit
   little-endian. For each `PT_LOAD` program header, copies `p_filesz`
   bytes from the file to `p_vaddr` (1 MB) and zero-fills up to `p_memsz`
   (`.bss`).
6. **Protected mode.** Sets CR0.PE, far-jumps to a 32-bit code selector,
   loads the data segments, sets `ESP = 0x7C00`, sets EAX = 0 (no boot
   info) and jumps to the ELF entry point `e_entry`. Interrupts are still
   disabled.

Error messages: `KERNEL.ELF not found in root directory`, `... too big`,
`... is empty`, `... is not a 32-bit ELF file`, `disk read error`.

## Kernel entry and boot protocol

Both loaders enter the kernel the same way: 32-bit protected mode, flat
segments, paging off, interrupts off, at the ELF entry point (`_start`).
The registers say what kind of boot it was:

| Register | UEFI loader                   | BIOS stage 2       |
|----------|-------------------------------|--------------------|
| EAX      | `0x47415A42` (`BOOTINFO_MAGIC`, "GAZB") | `0`      |
| EBX      | address of `struct boot_info` | (undefined)        |

`struct boot_info` (`bootinfo.h`) uses only 32-bit fields, so the 64-bit
loader and the 32-bit kernel see the same layout:

| Field                          | Meaning                                    |
|--------------------------------|--------------------------------------------|
| `magic`, `flags`, `boot_mode`  | `BOOTINFO_FB` / `BOOTINFO_MEM` say which fields are valid; `BOOT_MODE_UEFI` |
| `fb_addr`, `fb_width`, `fb_height`, `fb_pitch` | 32 bpp linear framebuffer (from GOP) |
| `mem_bytes`                    | usable RAM from the UEFI memory map (capped at 4 GB − 1) |

Without boot info (BIOS boot) the kernel uses VGA text mode and probes
memory itself.

### `_start` (`kernel.c`, assembly)

1. Saves EAX and EBX to `boot_magic` / `boot_info_addr` before anything
   can overwrite them.
2. Loads the kernel's own GDT (null, `0x08` flat code, `0x10` flat data)
   and reloads all segment registers, including CS with a far jump.
3. Switches to the kernel's own 16 KB stack (`KERNEL_STACK`, in `.bss`).
   It doesn't rely on any memory outside the kernel image, which matters
   under UEFI, where the firmware may have used low memory.
4. Clears EFLAGS (interrupts stay off) and calls `kernel_start()`. That
   sets `boot_info` if the magic matched, then calls `main()`.

### Kernel initialisation: `main()`

| Step | Function              | What happens                                        |
|------|-----------------------|-----------------------------------------------------|
| 1    | `serial_init`         | Sets up COM1 at 115200 8N1, if a UART is present    |
| 1a   | `video_init`          | With a boot-info framebuffer: switch the console to it |
| 2    | `Init8259`            | Remaps IRQs 0–7 to vectors `0x68–0x6F` and IRQs 8–15 to `0x70–0x77`; masks them all |
| 3    | `InitIDT`             | 256-entry IDT: CPU exception handlers, a default `iret` handler for everything else; `lidt`; `sti` |
| 4    | `calibrateDelayLoop`  | Runs the PIT at 100 Hz and finds how many loop iterations fit in one tick (gives `delay()` and the CPU MHz estimate) |
| 5    | memory size           | UEFI: `boot_info->mem_bytes`. BIOS: `count_memory` probes RAM 1 MB at a time with the cache disabled |
| 6    | `InitKeyboard`        | Enables IRQ 1 in the 8042 controller; keyboard ISR on vector `0x69`; LEDs off |
| 7    | `init_pit`            | PIT channel 0 at 18.2 Hz on IRQ 0 (vector `0x68`)   |
| 8    | `ahci_init`           | Finds the AHCI controller and first SATA disk       |
| 9    | `fat32_mount`         | Finds the FAT32 partition and reads the BPB         |
| 10   | shell loop            | `GazOS] /: ` prompt                                 |

## Memory map

While the UEFI loader runs, the firmware owns memory. The loader asks it
for the pages it needs: the kernel's exact range at 1 MB, and one page
each (anywhere below 4 GB) for boot info and the trampoline.

While the legacy BIOS boot loaders run (real mode):

| Address             | Contents                                         |
|---------------------|--------------------------------------------------|
| `0x00000–0x004FF`   | Real-mode IVT, BIOS data area                    |
| `0x00600–0x007FF`   | MBR (relocated); later the stage 2 FAT sector buffer |
| `…–0x07BFF`         | Stack (grows down from `0x7C00`)                 |
| `0x07C00–0x07DFF`   | MBR, then the VBR + BPB                          |
| `0x07E00–0x0ADFF`   | Stage 2 code (at most 12 KB)                     |
| `0x0B000–…`         | Stage 2 directory cluster buffer                 |
| `0x10000–0x9EFFF`   | KERNEL.ELF file image                            |
| `0x9FC00–0xFFFFF`   | EBDA, video memory, BIOS ROM                     |
| `0x100000–…`        | Kernel segments, copied from the ELF image       |

While the kernel runs (protected mode, no paging):

| Address             | Contents                                         |
|---------------------|--------------------------------------------------|
| `0xB8000`           | VGA text buffer (80×25), BIOS boots              |
| `0x100000`          | `.text` (`_start` is here), `.rodata` (including the font), `.data` |
| after `.data`       | `.bss`: 16 KB kernel stack, IDT, keyboard buffer, AHCI command list / FIS / command table, FAT32 sector buffers, console text grid |
| boot info page      | `struct boot_info` (UEFI boots; anywhere below 4 GB) |
| GOP framebuffer (e.g. `0x80000000`) | Framebuffer, UEFI boots          |
| PCI BAR5 (e.g. `0xFEBxxxxx`) | AHCI registers (MMIO)                   |

Run `size kernel.elf` or `nm -n kernel.elf` for the current addresses.

## Kernel

### Interrupts

The two 8259 PICs are remapped so hardware IRQs don't collide with CPU
exceptions: master IRQs go to `0x68`, slave IRQs to `0x70`. Every IRQ is
masked until a driver calls `enable_irq()`. Only IRQ 0 (timer) and IRQ 1
(keyboard) are used; the AHCI driver polls instead of using interrupts.

ISRs are written as small assembly stubs (`pusha`, set DS, call a C
handler, send EOI to the PIC, `popa`, `iret`). Exceptions 0–15 print a
message and hang, except vector 11 (segment not present): its handler
exists but isn't installed, so it gets the default `iret` handler.

### Console

`kprint()` handles `\n`, `\r`, `\t` and backspace, and scrolls. It has
two back ends:

- **VGA text mode** (BIOS boots): writes character/attribute pairs to
  `0xB8000` and moves the hardware cursor through the CRTC ports.
- **Framebuffer** (UEFI boots, where there is no text mode): an 80×25
  grid of 8×16 glyphs, centred on the screen in light grey on black, so
  it looks and behaves like the VGA console. Grey has the same value in
  RGB and BGR order, so the pixel format doesn't matter. The text is kept
  in a shadow grid, so scrolling and moving the cursor redraw from it
  rather than reading back the framebuffer, which can be slow on real
  hardware. The cursor is drawn as an underline.

The font (`font8x16.c`) is generated by `tools/psf2c.py` from the Linux
console font `Lat15-VGA16`, which is public domain. It is indexed by
Latin-1 byte value.

Each character is also copied to COM1, with `\n` converted to `\r\n`.
This gives a text log of the whole session, which the test suite uses.

### Keyboard

The IRQ 1 handler reads the scancode (set 1) from port `0x60`. It tracks
Shift/Ctrl/Alt and the lock LEDs, converts the scancode to ASCII with
`scan2ascii_table`, and appends the result to a 255-byte buffer.
`getch()` waits until the buffer has a key, then removes it with
interrupts disabled. The buffer is shared with the ISR, so it is
`volatile`. Ctrl+Alt+Del resets the machine via the keyboard controller.

`InitKeyboard` first sets up the 8042 controller itself: it flushes stale
bytes, sets the "IRQ 1 enable" and "translate to set 1" bits in the
command byte, and enables the keyboard port. A UEFI PS/2 driver usually
polls with keyboard interrupts disabled. A BIOS normally leaves them on,
but setting them is harmless either way.

## Drivers

### PCI (`pci.c`)

Uses configuration mechanism #1: write an address to `0xCF8`, then
read/write data at `0xCFC`. `pci_find_class()` scans every bus, slot and
function (checking the multi-function bit) for a class/subclass/prog-if
match.

### AHCI / SATA (`ahci.c`)

1. Finds the controller: PCI class `01` (storage), subclass `06` (SATA),
   prog-if `01` (AHCI). Enables memory decoding and bus mastering in the
   PCI command register. `ABAR` = BAR5.
2. Sets `GHC.AE` (AHCI mode) and turns off HBA interrupts.
3. Looks through the implemented ports (`PI`) for one with an active
   link (`SSTS.DET = 3`, `IPM = 1`) and a SATA disk signature
   (`0x00000101`).
4. Rebases the port: stops the command engine (clears `ST`/`FRE`, waits
   for `CR`/`FR` to clear), points `CLB` at a 1 KB-aligned command list
   and `FB` at a 256-byte FIS receive area, clears `SERR`/`IS`, then
   starts it again (`FRE`, then `ST`). The firmware (SeaBIOS or OVMF) has
   already been using the port, so this step is required.
5. Sends `IDENTIFY DEVICE` to get the model string and sector count.

Every command uses **command slot 0** with one PRDT entry:

```
command list[0] ──► command table ┬─ CFIS: Register H2D FIS
  (CFL=5, W, PRDTL=1)             │        (READ/WRITE DMA EXT, LBA48, count)
                                  └─ PRDT[0]: buffer address, byte count - 1
```

The driver sets `CI` bit 0 and polls until the controller clears it.
It checks `PxIS.TFES` and `PxTFD.ERR` for errors and gives up after a
fixed number of polls. `ahci_read`/`ahci_write` transfer up to 128
sectors per call into any 2-byte-aligned buffer. This works because
kernel addresses are physical addresses.

### FAT32 (`fat32.c`)

**Mounting.** If LBA 0 is a FAT32 boot sector, the disk is an
unpartitioned volume. Otherwise LBA 0 is read as an MBR and the first
partition of type `0x0B`, `0x0C` (FAT32) or `0xEF` (EFI System
Partition) is used. The BPB gives sectors per
cluster, reserved sectors, the number and size of the FATs, and the root
cluster. Only 512-byte sectors are supported.

**Clusters.** `cluster_lba(c) = data_lba + (c − 2) × sectors_per_cluster`.
`fat_next(c)` reads entry `c` from FAT #1, with a one-sector cache, and
masks it to 28 bits. Values ≥ `0x0FFFFFF7` (bad cluster or end of chain)
and out-of-range values end the chain.

**Directories.** `fat32_readdir()` reads 32-byte entries one sector at a
time, moving to the next cluster through the FAT when needed.
- **Long file names:** LFN entries (attribute `0x0F`) are collected in
  reverse order, 13 UCS-2 characters each. Non-ASCII characters are shown
  as `?`. The long name is used only if its sequence numbers are complete
  and its checksum matches the 8.3 entry that follows.
- **Short names:** 8.3 names are formatted as `NAME.EXT`, with the NT
  lower-case flags (byte 12) applied. Deleted entries (`0xE5`) and the
  volume label are skipped.

**Paths.** `resolve()` walks a path one component at a time from the root
(`/…`) or the current directory. Name comparison is case-insensitive and
checks both the long and the 8.3 name. `.` is skipped. `..` uses the
directory's `..` entry, where cluster 0 means the root. It builds the
canonical path as it goes, which `cd` stores as the new working directory.

**Reading.** `fat32_read(file, offset, buf, len)` skips whole clusters to
reach `offset`, then copies data out one sector at a time.

The driver is **read-only**: it never writes to the FAT or to directories.

## Shell

The shell loop in `kernel.c` reads a line with `gets()` (backspace works;
at most 254 characters), skips leading spaces, and splits off the first
word as the command:

| Command        | Action                                                   |
|----------------|----------------------------------------------------------|
| `ls`/`dir [path]` | List a directory (or show one file); hidden files are skipped |
| `cat`/`type <file>` | Print a file, pausing every 22 lines (`-- more --`) |
| `cd [path]`    | Change directory (`cd` alone goes to `/`)                |
| `pwd`          | Print the current directory                              |
| `disk`         | Show the disk model and size, and the FAT32 volume details |
| `cls`          | Clear the screen                                         |
| `reboot`       | Reset via the keyboard controller                        |
| `help`         | List the commands                                        |

## Testing and debugging

| Command                  | What it does                                           |
|--------------------------|--------------------------------------------------------|
| `./build.sh`             | Checks build tools are installed, then runs `make` (`./build.sh clean` rebuilds from scratch) |
| `./run.sh`               | Builds and boots `disk.img` via UEFI in a QEMU window  |
| `./run.sh --bios`        | Same, via legacy BIOS                                  |
| `./run.sh --serial`      | Also shows the serial console in the terminal          |
| `./run.sh --snapshot`    | Doesn't write anything back to `disk.img`              |
| `./run.sh --gdb`         | Starts QEMU paused, waiting for gdb on port 1234       |
| `./test.sh`              | Runs the end-to-end tests via UEFI, then via BIOS      |
| `./test.sh --firmware uefi` | Tests one boot mode only (`uefi` or `bios`)         |
| `./test.sh --headless`   | Same, with no window (CI)                              |

**End-to-end tests (`tools/test.py`).** The whole suite runs once per
boot mode. QEMU runs with `-snapshot`, so `disk.img` is never modified. The serial console goes to a log file and
the QEMU monitor is on a Unix socket. The test types commands with the
monitor's `sendkey`, exactly like keyboard input, waits for the next
prompt in the serial log, and checks the output. It answers
`-- more --` prompts with a space. The tests cover:
- boot, the boot mode the kernel reports, the UEFI framebuffer and
  memory map, AHCI detection and the FAT32 mount
- that the firmware didn't write `NvVars` to the disk, and that
  `/EFI/BOOT/BOOTX64.EFI` is in place
- `ls`, `cat`, `type`, `cd`, `pwd`
- long file names, case-insensitive paths and `..`
- error messages
- a byte-for-byte comparison of `cat /src/fat32.c` (a file spread over
  many clusters) with the source file

**Debugging with gdb.** Debugging the 32-bit kernel is simplest with a
BIOS boot, since `qemu-system-i386` matches kernel.elf's architecture:

```
./run.sh --bios --gdb               # in one terminal
gdb kernel.elf -ex 'target remote :1234' -ex 'break main' -ex continue
```

kernel.elf has symbols. For the BIOS boot loaders, use
`set architecture i8086` and break at `*0x7c00` (MBR/VBR) or `*0x7e00`
(stage 2). The UEFI loader prints its progress on screen and to the
serial port. If it fails, it prints the reason and the UEFI status code.

**QEMU machine** (`tools/qemu-args.sh`). `-machine q35` provides an ICH9
AHCI controller (on QEMU's PCI bus `0:31.2`). `-device ide-hd,bus=ide.0`
attaches the disk to AHCI port 0, despite the `ide-hd` name. The older
default `pc` machine has only a legacy IDE controller, which GazOS
doesn't support.

- **UEFI boots** use `qemu-system-x86_64`, because OVMF is 64-bit UEFI.
  OVMF runs as split pflash firmware: the code read-only, plus a private
  copy of the variable store (`ovmf_vars.fd`, made on first run). With
  the single-file `-bios OVMF.fd` form, OVMF has nowhere to keep its
  variables and writes an `NvVars` file onto the ESP, which here is
  `disk.img`. The OVMF files are found automatically; set `OVMF_CODE` and
  `OVMF_VARS` to override.
- **BIOS boots** use `qemu-system-i386` with SeaBIOS.

## Limitations

- UEFI: x86-64 firmware only (`BOOTX64.EFI`; there is no `BOOTIA32.EFI`
  for 32-bit UEFI), and the loader isn't signed, so Secure Boot must be
  off. The framebuffer must be 32 bpp and below 4 GB, otherwise only the
  serial console works.
- The keyboard driver is PS/2 only. QEMU and most BIOS machines emulate
  PS/2, but many UEFI-only PCs have just USB keyboards. On those, use the
  serial console for output; there is no input.
- Real hardware must have the SATA controller in AHCI mode.
- FAT32 is read-only, sectors must be 512 bytes, and only the first FAT32
  partition is used.
- AHCI: polled, one command at a time, first SATA disk only, LBAs below
  2³² (2 TB).
- No paging, no user mode, no multitasking: everything runs in ring 0.
- The kernel file is limited to about 572 KB by stage 2's load area, and
  stage 2 itself to 12 KB.
