/*
GazOS Operating System
Copyright (C) 2026  Gareth Owenson <gareth.owenson@gmail.com>
Boot information handed from the UEFI loader to the kernel.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

The kernel is entered in 32-bit protected mode, paging off, interrupts
off. If EAX == BOOTINFO_MAGIC then EBX points to a struct boot_info
(below 4GB). The legacy BIOS loader (boot/stage2.asm) enters with EAX = 0
and no boot info: the kernel then uses VGA text mode and probes memory.

This header is shared by the kernel (gcc -m32) and the UEFI loader
(x86_64 mingw), so it only uses fixed-size types.
*/

#ifndef INCLUDE_BOOTINFO
#define INCLUDE_BOOTINFO

#define BOOTINFO_MAGIC		0x47415A42	/* "GAZB" */

#define BOOTINFO_FB		0x0001	/* framebuffer fields are valid */
#define BOOTINFO_MEM		0x0002	/* mem_bytes is valid */

#define BOOT_MODE_BIOS		0
#define BOOT_MODE_UEFI		1

struct boot_info
{
	unsigned int magic;		/* BOOTINFO_MAGIC */
	unsigned int flags;		/* BOOTINFO_* */
	unsigned int boot_mode;		/* BOOT_MODE_* */

	/* linear 32 bits-per-pixel framebuffer (from UEFI GOP) */
	unsigned int fb_addr;		/* physical address, below 4GB */
	unsigned int fb_width;		/* pixels */
	unsigned int fb_height;
	unsigned int fb_pitch;		/* bytes per scan line */

	/* usable RAM (from the UEFI memory map), capped at 4GB - 1 */
	unsigned int mem_bytes;
} __attribute__ ((packed));

extern struct boot_info *boot_info;	/* kernel: NULL for BIOS boots */

#endif
