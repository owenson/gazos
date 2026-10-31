/*
GazOS Operating System
Copyright (C) 2026  Gareth Owenson <gareth.owenson@gmail.com>
Access to device memory above 4GB (PAE paging, enabled on demand).

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/

#ifndef INCLUDE_PAGING
#define INCLUDE_PAGING

/* Return a pointer through which the kernel can access 'size' bytes of
 * device memory (MMIO) at 64-bit physical address 'phys'.
 *
 * Below 4GB that is just the address itself (paging stays off). Above
 * 4GB, the first call turns on PAE paging with the low 4GB identity
 * mapped (so all existing pointers and DMA addresses keep working), and
 * the region is mapped, uncached, into a window at the top of the 32-bit
 * address space. Returns 0 if it can't be mapped. */
volatile void *map_device(unsigned long long phys, unsigned long size);

extern int paging_enabled;

#endif
