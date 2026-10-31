/*
GazOS Operating System
Copyright (C) 2026  Gareth Owenson <gareth.owenson@gmail.com>
PCI configuration space access (mechanism #1, ports 0xCF8/0xCFC).

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/

#ifndef INCLUDE_PCI
#define INCLUDE_PCI

#define PCI_VENDOR_ID	0x00
#define PCI_COMMAND	0x04
#define PCI_CLASS	0x08	/* revision | progif<<8 | subclass<<16 | class<<24 */
#define PCI_HEADER_TYPE	0x0C	/* byte 2 of this dword */
#define PCI_BAR5	0x24

#define PCI_CMD_MEMORY	0x0002
#define PCI_CMD_MASTER	0x0004

typedef struct
{
	unsigned char bus, slot, func;
} pci_dev;

unsigned long pci_read(pci_dev *d, unsigned char offset);
void pci_write(pci_dev *d, unsigned char offset, unsigned long value);
/* Find the first device with the given class/subclass/prog-if. 1 = found */
int pci_find_class(unsigned char cls, unsigned char subcls, unsigned char progif, pci_dev *out);

#endif
