/*
GazOS Operating System
Copyright (C) 2026  Gareth Owenson <gareth.owenson@gmail.com>
UEFI boot loader: \EFI\BOOT\BOOTX64.EFI

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

What it does:
  1. reads \KERNEL.ELF from the volume this loader was started from
  2. allocates the kernel's physical memory (1MB) and copies the ELF
     PT_LOAD segments there, zeroing .bss
  3. collects boot information: GOP framebuffer, memory size
  4. exits boot services
  5. jumps to a trampoline (copied below 4GB) that drops from 64-bit long
     mode to 32-bit protected mode with paging off, and enters the kernel
     with EAX = BOOTINFO_MAGIC, EBX = &boot_info  (see ../bootinfo.h)

The kernel itself is the same 32-bit ELF the legacy BIOS loader uses.
*/

#include "efi.h"
#include "../bootinfo.h"

static EFI_SYSTEM_TABLE *ST;
static EFI_BOOT_SERVICES *BS;

/* gcc may emit calls to these even in freestanding code */
void *memset(void *d, int c, unsigned long long n)
{
	unsigned char *p = d;
	while(n--) *p++ = (unsigned char)c;
	return d;
}

void *memcpy(void *d, const void *s, unsigned long long n)
{
	unsigned char *dp = d;
	const unsigned char *sp = s;
	while(n--) *dp++ = *sp++;
	return d;
}

/* ---- console output (only valid before ExitBootServices) ---- */

static void print(const char *s)
{
	CHAR16 buf[128];
	unsigned int n = 0;

	while(*s)
	{
		if(*s == '\n')
			buf[n++] = '\r';
		buf[n++] = (unsigned char)*s++;
		if(n >= 126 || !*s)
		{
			buf[n] = 0;
			ST->ConOut->OutputString(ST->ConOut, buf);
			n = 0;
		}
	}
}

static void print_hex(UINT64 v)
{
	char buf[19];
	int i;

	buf[0] = '0'; buf[1] = 'x';
	for(i = 0; i < 16; i++)
		buf[2 + i] = "0123456789ABCDEF"[(v >> (60 - i * 4)) & 0xF];
	buf[18] = 0;
	print(buf);
}

static void print_dec(UINT64 v)
{
	char buf[21];
	int i = 20;

	buf[i] = 0;
	do { buf[--i] = '0' + v % 10; v /= 10; } while(v);
	print(buf + i);
}

static EFI_STATUS fail(const char *msg, EFI_STATUS status)
{
	print("GazOS loader error: ");
	print(msg);
	print(" (status ");
	print_hex(status);
	print(")\nSystem halted.\n");
	for(;;)
		__asm__ volatile ("hlt");
	return status;
}

/* ---- trampoline: long mode -> 32-bit protected mode ----
 *
 * Copied to a page below 4GB and called as
 *     tramp(page_base, kernel_entry, boot_info)   (MS ABI: RCX, RDX, R8)
 * It's position independent: everything is addressed from RSI = base.
 * UEFI identity-maps memory, so the page has the same address with
 * paging on and off. */
extern const unsigned char tramp_start[], tramp_end[];
__asm__ (
	".text\n"
	".globl tramp_start\n"
	".globl tramp_end\n"
	".code64\n"
	"tramp_start:\n"
	"	cli\n"
	"	mov %rcx, %rsi\n"			/* page base */
	"	mov %rdx, %rdi\n"			/* kernel entry */
	"	mov %r8, %rbx\n"			/* boot_info */
	"	lea (t_gdt - tramp_start)(%rsi), %rax\n"	/* patch GDT base */
	"	mov %rax, (t_gdtr - tramp_start + 2)(%rsi)\n"
	"	lgdt (t_gdtr - tramp_start)(%rsi)\n"
	"	lea (t_compat - tramp_start)(%rsi), %rax\n"
	"	pushq $0x08\n"				/* 32-bit code segment */
	"	push %rax\n"
	"	lretq\n"				/* -> compatibility mode */
	".code32\n"
	"t_compat:\n"
	"	mov %cr0, %eax\n"			/* paging off: leaves long mode */
	"	and $0x7FFFFFFF, %eax\n"
	"	mov %eax, %cr0\n"
	"	mov $0xC0000080, %ecx\n"		/* EFER.LME = 0 */
	"	rdmsr\n"
	"	and $0xFFFFFEFF, %eax\n"
	"	wrmsr\n"
	"	mov %cr4, %eax\n"			/* PAE off (tidy up) */
	"	and $0xFFFFFFDF, %eax\n"
	"	mov %eax, %cr4\n"
	"	mov $0x10, %ax\n"			/* flat data segments */
	"	mov %ax, %ds\n"
	"	mov %ax, %es\n"
	"	mov %ax, %fs\n"
	"	mov %ax, %gs\n"
	"	mov %ax, %ss\n"
	"	lea 0x1000(%esi), %esp\n"		/* stack: top of this page */
	"	mov $0x47415A42, %eax\n"		/* BOOTINFO_MAGIC */
	"	jmp *%edi\n"				/* EBX = boot_info */
	".balign 8\n"
	"t_gdt:\n"
	"	.quad 0\n"
	"	.quad 0x00CF9A000000FFFF\n"		/* 0x08: 32-bit flat code */
	"	.quad 0x00CF92000000FFFF\n"		/* 0x10: 32-bit flat data */
	"t_gdtr:\n"
	"	.word 23\n"
	"	.quad 0\n"
	"tramp_end:\n"
	".code64\n"
);

/* ---- ELF32 ---- */

typedef struct
{
	UINT8 e_ident[16];
	UINT16 e_type, e_machine;
	UINT32 e_version, e_entry, e_phoff, e_shoff, e_flags;
	UINT16 e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
} Elf32_Ehdr;

typedef struct
{
	UINT32 p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags, p_align;
} Elf32_Phdr;

#define PT_LOAD	1

static EFI_STATUS read_kernel(EFI_HANDLE image, void **data, UINTN *size)
{
	EFI_GUID lip_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
	EFI_GUID sfs_guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
	EFI_LOADED_IMAGE_PROTOCOL *li;
	EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs;
	EFI_FILE_PROTOCOL *root, *file;
	EFI_STATUS s;
	UINT64 len;
	UINTN done = 0, n;

	if((s = BS->HandleProtocol(image, &lip_guid, (void **)&li)))
		return fail("no loaded image protocol", s);
	if((s = BS->HandleProtocol(li->DeviceHandle, &sfs_guid, (void **)&fs)))
		return fail("boot volume has no file system", s);
	if((s = fs->OpenVolume(fs, &root)))
		return fail("can't open boot volume", s);
	if((s = root->Open(root, &file, L"\\KERNEL.ELF", EFI_FILE_MODE_READ, 0)))
		return fail("\\KERNEL.ELF not found", s);

	/* file size: seek to the end and ask where we are */
	file->SetPosition(file, 0xFFFFFFFFFFFFFFFFULL);
	file->GetPosition(file, &len);
	file->SetPosition(file, 0);

	if((s = BS->AllocatePool(EfiLoaderData, len, data)))
		return fail("out of memory reading the kernel", s);
	while(done < len)
	{
		n = len - done;
		if((s = file->Read(file, &n, (UINT8 *)*data + done)) || n == 0)
			return fail("error reading \\KERNEL.ELF", s);
		done += n;
	}
	file->Close(file);
	root->Close(root);
	*size = len;
	return EFI_SUCCESS;
}

/* Copy the PT_LOAD segments to their physical addresses. */
static EFI_STATUS load_elf(void *data, UINTN size, UINT32 *entry)
{
	Elf32_Ehdr *eh = data;
	Elf32_Phdr *ph;
	EFI_PHYSICAL_ADDRESS start, end;
	EFI_STATUS s;
	int i;

	if(size < sizeof(*eh) || eh->e_ident[0] != 0x7F || eh->e_ident[1] != 'E' ||
	   eh->e_ident[2] != 'L' || eh->e_ident[3] != 'F' ||
	   eh->e_ident[4] != 1 /* 32-bit */ || eh->e_machine != 3 /* i386 */)
		return fail("\\KERNEL.ELF is not a 32-bit x86 ELF file", 0);

	for(i = 0; i < eh->e_phnum; i++)
	{
		ph = (Elf32_Phdr *)((UINT8 *)data + eh->e_phoff + i * eh->e_phentsize);
		if(ph->p_type != PT_LOAD || ph->p_memsz == 0)
			continue;
		if(ph->p_offset + (UINT64)ph->p_filesz > size)
			return fail("\\KERNEL.ELF is truncated", 0);

		start = ph->p_paddr & ~0xFFFULL;
		end = ((UINT64)ph->p_paddr + ph->p_memsz + 0xFFF) & ~0xFFFULL;
		if((s = BS->AllocatePages(AllocateAddress, EfiLoaderData,
					  (end - start) / 4096, &start)))
		{
			print("kernel needs memory at ");
			print_hex(ph->p_paddr);
			print(" - ");
			print_hex((UINT64)ph->p_paddr + ph->p_memsz);
			print("\n");
			return fail("that memory is in use by the firmware", s);
		}
		memcpy((void *)(UINTN)ph->p_paddr, (UINT8 *)data + ph->p_offset, ph->p_filesz);
		memset((void *)(UINTN)(ph->p_paddr + ph->p_filesz), 0, ph->p_memsz - ph->p_filesz);
	}
	*entry = eh->e_entry;
	return EFI_SUCCESS;
}

static void *alloc_low_page(const char *what)
{
	EFI_PHYSICAL_ADDRESS addr = 0xFFFFFFFFULL;	/* anywhere below 4GB */
	EFI_STATUS s;

	if((s = BS->AllocatePages(AllocateMaxAddress, EfiLoaderData, 1, &addr)))
		fail(what, s);
	memset((void *)(UINTN)addr, 0, 4096);
	return (void *)(UINTN)addr;
}

static void get_framebuffer(struct boot_info *bi)
{
	EFI_GUID gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
	EFI_GRAPHICS_OUTPUT_PROTOCOL *gop;
	EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info;

	if(BS->LocateProtocol(&gop_guid, 0, (void **)&gop) || !gop->Mode || !gop->Mode->Info)
	{
		print("No graphics output: console on the serial port only\n");
		return;
	}
	info = gop->Mode->Info;
	if(info->PixelFormat != PixelRedGreenBlueReserved8BitPerColor &&
	   info->PixelFormat != PixelBlueGreenRedReserved8BitPerColor)
	{
		print("Unsupported framebuffer pixel format: serial console only\n");
		return;
	}
	if(gop->Mode->FrameBufferBase + gop->Mode->FrameBufferSize > 0x100000000ULL)
	{
		print("Framebuffer is above 4GB: serial console only\n");
		return;
	}

	bi->fb_addr = (UINT32)gop->Mode->FrameBufferBase;
	bi->fb_width = info->HorizontalResolution;
	bi->fb_height = info->VerticalResolution;
	bi->fb_pitch = info->PixelsPerScanLine * 4;
	bi->flags |= BOOTINFO_FB;

	print("Framebuffer: ");
	print_dec(bi->fb_width);
	print("x");
	print_dec(bi->fb_height);
	print(" at ");
	print_hex(bi->fb_addr);
	print("\n");
}

/* Sum of RAM the kernel may use once boot services are gone */
static UINT32 usable_memory(UINT8 *map, UINTN map_size, UINTN desc_size)
{
	UINT64 total = 0;
	UINTN off;

	for(off = 0; off + desc_size <= map_size; off += desc_size)
	{
		EFI_MEMORY_DESCRIPTOR *d = (EFI_MEMORY_DESCRIPTOR *)(map + off);
		switch(d->Type)
		{
			case EfiLoaderCode: case EfiLoaderData:
			case EfiBootServicesCode: case EfiBootServicesData:
			case EfiConventionalMemory:
				total += d->NumberOfPages * 4096;
		}
	}
	return total > 0xFFFFFFFFULL ? 0xFFFFFFFFU : (UINT32)total;
}

EFI_STATUS efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *systab)
{
	struct boot_info *bi;
	void *kernel, *tramp;
	UINTN kernel_size, map_size, map_key, desc_size, map_alloc;
	UINT32 desc_ver, entry;
	UINT8 *map;
	EFI_STATUS s;
	int tries;

	ST = systab;
	BS = systab->BootServices;
	BS->SetWatchdogTimer(0, 0, 0, 0);	/* don't reset us after 5 minutes */

	print("GazOS UEFI loader: loading \\KERNEL.ELF\n");

	read_kernel(image, &kernel, &kernel_size);
	load_elf(kernel, kernel_size, &entry);

	bi = alloc_low_page("can't allocate boot info");
	bi->magic = BOOTINFO_MAGIC;
	bi->boot_mode = BOOT_MODE_UEFI;
	get_framebuffer(bi);

	tramp = alloc_low_page("can't allocate trampoline");
	memcpy(tramp, tramp_start, tramp_end - tramp_start);

	/* Memory map buffer, with room for the entries our own allocation adds */
	map_size = 0;
	BS->GetMemoryMap(&map_size, 0, &map_key, &desc_size, &desc_ver);
	map_alloc = map_size + 8 * desc_size;
	if((s = BS->AllocatePool(EfiLoaderData, map_alloc, (void **)&map)))
		return fail("can't allocate memory map", s);

	print("Starting kernel at ");
	print_hex(entry);
	print("\n");

	/* ExitBootServices fails if the map changed since GetMemoryMap;
	 * nothing may allocate in between, including printing. */
	for(tries = 0; tries < 4; tries++)
	{
		map_size = map_alloc;
		if((s = BS->GetMemoryMap(&map_size, (EFI_MEMORY_DESCRIPTOR *)map,
					 &map_key, &desc_size, &desc_ver)))
			return fail("GetMemoryMap failed", s);
		if(BS->ExitBootServices(image, map_key) == EFI_SUCCESS)
			break;
	}
	if(tries == 4)
		return fail("ExitBootServices failed", 0);

	/* From here on: no firmware services, no printing. */
	bi->mem_bytes = usable_memory(map, map_size, desc_size);
	bi->flags |= BOOTINFO_MEM;

	((void (*)(UINT64, UINT64, UINT64))tramp)((UINT64)(UINTN)tramp, entry,
						    (UINT64)(UINTN)bi);
	for(;;);
}
