/*
GazOS Operating System
Copyright (C) 2026  Gareth Owenson <gareth.owenson@gmail.com>
AHCI (SATA) disk driver - polled, single command slot.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/

#ifndef INCLUDE_AHCI
#define INCLUDE_AHCI

#define SECTOR_SIZE	512
#define AHCI_MAX_SECTORS 128	/* per ahci_read/ahci_write call */

/* Find the AHCI controller and the first SATA disk. 1 = success. */
int ahci_init(void);

/* Read/write 'count' 512-byte sectors starting at 'lba'.
 * 'buf' must be 2-byte aligned. Returns 1 on success, 0 on error. */
int ahci_read(unsigned long lba, unsigned int count, void *buf);
int ahci_write(unsigned long lba, unsigned int count, const void *buf);

/* Info about the attached disk (valid after ahci_init) */
extern char ahci_model[41];
extern unsigned long ahci_sectors;	/* capped at 2^32-1 */
extern int ahci_port;

#endif
