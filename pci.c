/*
GazOS Operating System
Copyright (C) 2026  Gareth Owenson <gareth.owenson@gmail.com>
PCI configuration space access (mechanism #1, ports 0xCF8/0xCFC).

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/

#include "pci.h"

static inline void outl(unsigned short port, unsigned long value)
{
	asm volatile ("outl %0, %1" : : "a" (value), "Nd" (port));
}

static inline unsigned long inl(unsigned short port)
{
	unsigned long value;
	asm volatile ("inl %1, %0" : "=a" (value) : "Nd" (port));
	return value;
}

static unsigned long pci_addr(unsigned char bus, unsigned char slot,
			      unsigned char func, unsigned char offset)
{
	return 0x80000000UL | ((unsigned long)bus << 16) |
	       ((unsigned long)(slot & 0x1F) << 11) |
	       ((unsigned long)(func & 7) << 8) | (offset & 0xFC);
}

unsigned long pci_read(pci_dev *d, unsigned char offset)
{
	outl(0xCF8, pci_addr(d->bus, d->slot, d->func, offset));
	return inl(0xCFC);
}

void pci_write(pci_dev *d, unsigned char offset, unsigned long value)
{
	outl(0xCF8, pci_addr(d->bus, d->slot, d->func, offset));
	outl(0xCFC, value);
}

int pci_find_class(unsigned char cls, unsigned char subcls, unsigned char progif, pci_dev *out)
{
	pci_dev d;
	unsigned int bus, slot, func, nfuncs;
	unsigned long id, cc;

	for(bus = 0; bus < 256; bus++)
		for(slot = 0; slot < 32; slot++)
		{
			d.bus = bus; d.slot = slot; d.func = 0;
			id = pci_read(&d, PCI_VENDOR_ID);
			if((id & 0xFFFF) == 0xFFFF)
				continue;
			/* multi-function device? */
			nfuncs = (pci_read(&d, PCI_HEADER_TYPE) & 0x00800000) ? 8 : 1;
			for(func = 0; func < nfuncs; func++)
			{
				d.func = func;
				id = pci_read(&d, PCI_VENDOR_ID);
				if((id & 0xFFFF) == 0xFFFF)
					continue;
				cc = pci_read(&d, PCI_CLASS);
				if(((cc >> 24) & 0xFF) == cls &&
				   ((cc >> 16) & 0xFF) == subcls &&
				   ((cc >> 8) & 0xFF) == progif)
				{
					*out = d;
					return 1;
				}
			}
		}
	return 0;
}
