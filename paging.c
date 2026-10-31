/*
GazOS Operating System
Copyright (C) 2026  Gareth Owenson <gareth.owenson@gmail.com>
Access to device memory above 4GB (PAE paging, enabled on demand).

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

PAE paging uses 64-bit page table entries, so a 32-bit kernel can map
physical addresses above 4GB. We use 2MB pages: one PDPT with 4 entries,
each pointing to a page directory of 512 entries = 4 x 1GB = all 4GB.

  0x00000000 - 0xFF7FFFFF   identity mapped (physical = virtual)
  0xFF800000 - 0xFFFFFFFF   device window: 4 x 2MB pages, handed out by
                            map_device() for memory above 4GB

The window hides the top 8MB of physical address space (firmware flash),
which the kernel never uses. Identity-mapped pages use normal caching
attributes, so the memory type still comes from the firmware's MTRRs
(device memory stays uncached); window pages are explicitly uncached.
*/

#include "paging.h"

#define PTE_P		0x001ULL	/* present */
#define PTE_RW		0x002ULL	/* writable */
#define PTE_PWT		0x008ULL	/* write-through */
#define PTE_PCD		0x010ULL	/* cache disable */
#define PTE_PS		0x080ULL	/* 2MB page */

#define PAGE_2M		0x200000ULL
#define WINDOW_BASE	0xFF800000UL
#define WINDOW_PAGES	4

static unsigned long long pdpt[4] __attribute__ ((aligned(32)));
static unsigned long long pd[4][512] __attribute__ ((aligned(4096)));
static unsigned int window_used = 0;	/* 2MB pages handed out */

int paging_enabled = 0;

static int cpu_has_pae(void)
{
	unsigned long a = 1, b, c, d;
	asm volatile ("cpuid" : "+a" (a), "=b" (b), "=c" (c), "=d" (d));
	return (d >> 6) & 1;
}

static int paging_init(void)
{
	unsigned long i, cr;

	if(!cpu_has_pae())
		return 0;

	for(i = 0; i < 2048; i++)	/* identity map 4GB with 2MB pages */
		pd[i / 512][i % 512] = ((unsigned long long)i * PAGE_2M) | PTE_PS | PTE_RW | PTE_P;
	for(i = 0; i < 4; i++)		/* PDPT entries: only P (RW is reserved) */
		pdpt[i] = (unsigned long)pd[i] | PTE_P;

	asm volatile ("mov %%cr4, %0" : "=r" (cr));
	cr |= 0x20;			/* CR4.PAE */
	asm volatile ("mov %0, %%cr4" : : "r" (cr));
	asm volatile ("mov %0, %%cr3" : : "r" ((unsigned long)pdpt) : "memory");
	asm volatile ("mov %%cr0, %0" : "=r" (cr));
	cr |= 0x80000000UL;		/* CR0.PG */
	asm volatile ("mov %0, %%cr0" : : "r" (cr) : "memory");

	paging_enabled = 1;
	return 1;
}

volatile void *map_device(unsigned long long phys, unsigned long size)
{
	unsigned long long base = phys & ~(PAGE_2M - 1);
	unsigned int pages = (unsigned int)((phys + size - base + PAGE_2M - 1) / PAGE_2M);
	unsigned long virt;
	unsigned int i, slot;

	if(phys + size <= 0x100000000ULL)
		return (volatile void *)(unsigned long)phys;

	if(!paging_enabled && !paging_init())
		return 0;
	if(window_used + pages > WINDOW_PAGES)
		return 0;

	virt = WINDOW_BASE + window_used * (unsigned long)PAGE_2M;
	for(i = 0; i < pages; i++)
	{
		slot = (virt >> 21) + i;	/* index into the 2048 PDEs */
		pd[slot / 512][slot % 512] = (base + i * PAGE_2M) |
			PTE_PS | PTE_PCD | PTE_PWT | PTE_RW | PTE_P;
		asm volatile ("invlpg (%0)" : : "r" (virt + i * (unsigned long)PAGE_2M) : "memory");
	}
	window_used += pages;
	return (volatile void *)(virt + (unsigned long)(phys - base));
}
