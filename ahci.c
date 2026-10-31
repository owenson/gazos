/*
GazOS Operating System
Copyright (C) 2026  Gareth Owenson <gareth.owenson@gmail.com>
AHCI (SATA) disk driver - polled, single command slot.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

The kernel runs with paging disabled, so linear addresses are physical
addresses and our static buffers can be handed straight to the HBA's DMA
engine. Interrupts from the HBA are left disabled; commands are polled.
*/

#include "ahci.h"
#include "pci.h"
#include "video.h"

/* HBA (generic host control) registers */
#define HBA_CAP		0x00
#define HBA_GHC		0x04
#define HBA_IS		0x08
#define HBA_PI		0x0C
#define GHC_AE		0x80000000UL	/* AHCI enable */

/* Per-port registers, at 0x100 + port * 0x80 */
#define PX_CLB		0x00
#define PX_CLBU		0x04
#define PX_FB		0x08
#define PX_FBU		0x0C
#define PX_IS		0x10
#define PX_IE		0x14
#define PX_CMD		0x18
#define PX_TFD		0x20
#define PX_SIG		0x24
#define PX_SSTS		0x28
#define PX_SERR		0x30
#define PX_CI		0x38

#define PXCMD_ST	0x0001
#define PXCMD_FRE	0x0010
#define PXCMD_FR	0x4000
#define PXCMD_CR	0x8000
#define PXIS_TFES	0x40000000UL	/* task file error */
#define TFD_ERR		0x01
#define TFD_DRQ		0x08
#define TFD_BSY		0x80

#define SIG_SATA_DISK	0x00000101UL

#define FIS_TYPE_REG_H2D 0x27
#define ATA_CMD_READ_DMA_EXT	0x25
#define ATA_CMD_WRITE_DMA_EXT	0x35
#define ATA_CMD_IDENTIFY	0xEC

#define TIMEOUT		10000000UL

/* Command header (one entry of the 32-slot command list) */
typedef struct
{
	unsigned short flags;	/* CFL[4:0], A, W[6], P, R, B, C, PMP */
	unsigned short prdtl;	/* number of PRDT entries */
	volatile unsigned long prdbc;	/* bytes transferred */
	unsigned long ctba;	/* command table base (128-byte aligned) */
	unsigned long ctbau;
	unsigned long rsv[4];
} __attribute__ ((packed)) hba_cmd_header;

typedef struct
{
	unsigned long dba;	/* data base address */
	unsigned long dbau;
	unsigned long rsv;
	unsigned long dbc;	/* byte count - 1, bit 31 = interrupt */
} __attribute__ ((packed)) hba_prdt_entry;

typedef struct
{
	unsigned char cfis[64];
	unsigned char acmd[16];
	unsigned char rsv[48];
	hba_prdt_entry prdt[1];
} __attribute__ ((packed)) hba_cmd_table;

static hba_cmd_header cmd_list[32] __attribute__ ((aligned(1024)));
static unsigned char fis_area[256] __attribute__ ((aligned(256)));
static hba_cmd_table cmd_table __attribute__ ((aligned(128)));
static unsigned short identify_buf[256] __attribute__ ((aligned(16)));

static volatile unsigned char *abar = 0;
static volatile unsigned char *port_regs = 0;

char ahci_model[41];
unsigned long ahci_sectors = 0;
int ahci_port = -1;

static inline unsigned long rd(volatile unsigned char *base, unsigned int reg)
{
	return *(volatile unsigned long *)(base + reg);
}

static inline void wr(volatile unsigned char *base, unsigned int reg, unsigned long v)
{
	*(volatile unsigned long *)(base + reg) = v;
}

static void zero(void *p, unsigned int n)
{
	unsigned char *c = p;
	while(n--) *c++ = 0;
}

static int port_stop(volatile unsigned char *p)
{
	unsigned long t;

	wr(p, PX_CMD, rd(p, PX_CMD) & ~PXCMD_ST);
	for(t = 0; t < TIMEOUT && (rd(p, PX_CMD) & PXCMD_CR); t++);
	wr(p, PX_CMD, rd(p, PX_CMD) & ~PXCMD_FRE);
	for(t = 0; t < TIMEOUT && (rd(p, PX_CMD) & PXCMD_FR); t++);
	return !(rd(p, PX_CMD) & (PXCMD_CR | PXCMD_FR));
}

static int port_start(volatile unsigned char *p)
{
	unsigned long t;

	for(t = 0; t < TIMEOUT && (rd(p, PX_TFD) & (TFD_BSY | TFD_DRQ)); t++);
	wr(p, PX_CMD, rd(p, PX_CMD) | PXCMD_FRE);
	wr(p, PX_CMD, rd(p, PX_CMD) | PXCMD_ST);
	return 1;
}

/* Build and run a single ATA command in slot 0, polling for completion */
static int ahci_cmd(unsigned char ata_cmd, unsigned long lba, unsigned int count,
		    void *buf, unsigned long bytes, int write)
{
	volatile unsigned char *p = port_regs;
	unsigned char *fis = cmd_table.cfis;
	unsigned long t;

	if(!p || bytes == 0 || bytes > 4UL * 1024 * 1024 || ((unsigned long)buf & 1))
		return 0;

	for(t = 0; t < TIMEOUT && (rd(p, PX_TFD) & (TFD_BSY | TFD_DRQ)); t++);
	if(t == TIMEOUT)
		return 0;

	zero(&cmd_table, sizeof(cmd_table));
	zero(&cmd_list[0], sizeof(hba_cmd_header));

	cmd_list[0].flags = 5 | (write ? 0x40 : 0);	/* 5 dword H2D FIS */
	cmd_list[0].prdtl = 1;
	cmd_list[0].ctba = (unsigned long)&cmd_table;

	cmd_table.prdt[0].dba = (unsigned long)buf;
	cmd_table.prdt[0].dbc = bytes - 1;

	fis[0] = FIS_TYPE_REG_H2D;
	fis[1] = 0x80;			/* this is a command */
	fis[2] = ata_cmd;
	fis[4] = lba & 0xFF;
	fis[5] = (lba >> 8) & 0xFF;
	fis[6] = (lba >> 16) & 0xFF;
	fis[7] = 0x40;			/* LBA mode */
	fis[8] = (lba >> 24) & 0xFF;
	fis[9] = 0;			/* LBA bits 32-47: we only do 32-bit LBAs */
	fis[10] = 0;
	fis[12] = count & 0xFF;
	fis[13] = (count >> 8) & 0xFF;

	asm volatile ("" ::: "memory");
	wr(p, PX_IS, 0xFFFFFFFFUL);	/* clear pending status */
	wr(p, PX_CI, 1);		/* issue slot 0 */

	for(t = 0; t < TIMEOUT; t++)
	{
		if(!(rd(p, PX_CI) & 1))
			break;
		if(rd(p, PX_IS) & PXIS_TFES)
			return 0;
	}
	asm volatile ("" ::: "memory");
	if(t == TIMEOUT || (rd(p, PX_IS) & PXIS_TFES) || (rd(p, PX_TFD) & TFD_ERR))
		return 0;
	return 1;
}

int ahci_read(unsigned long lba, unsigned int count, void *buf)
{
	if(count == 0 || count > AHCI_MAX_SECTORS) return 0;
	return ahci_cmd(ATA_CMD_READ_DMA_EXT, lba, count, buf,
			(unsigned long)count * SECTOR_SIZE, 0);
}

int ahci_write(unsigned long lba, unsigned int count, const void *buf)
{
	if(count == 0 || count > AHCI_MAX_SECTORS) return 0;
	return ahci_cmd(ATA_CMD_WRITE_DMA_EXT, lba, count, (void *)buf,
			(unsigned long)count * SECTOR_SIZE, 1);
}

static void read_identify(void)
{
	int i;
	unsigned long hi;

	ahci_model[0] = 0;
	if(!ahci_cmd(ATA_CMD_IDENTIFY, 0, 0, identify_buf, 512, 0))
		return;

	/* model: words 27-46, two chars per word, byte swapped */
	for(i = 0; i < 20; i++)
	{
		ahci_model[i * 2] = identify_buf[27 + i] >> 8;
		ahci_model[i * 2 + 1] = identify_buf[27 + i] & 0xFF;
	}
	ahci_model[40] = 0;
	for(i = 39; i >= 0 && ahci_model[i] == ' '; i--)
		ahci_model[i] = 0;

	/* LBA48 sector count (words 100-103), else LBA28 (words 60-61) */
	hi = identify_buf[102] | ((unsigned long)identify_buf[103] << 16);
	ahci_sectors = identify_buf[100] | ((unsigned long)identify_buf[101] << 16);
	if(hi)
		ahci_sectors = 0xFFFFFFFFUL;
	if(ahci_sectors == 0)
		ahci_sectors = identify_buf[60] | ((unsigned long)identify_buf[61] << 16);
}

int ahci_init(void)
{
	pci_dev dev;
	unsigned long pi, ssts;
	int i;

	kprint("Initialising AHCI (SATA)... ");

	/* class 01 (storage), subclass 06 (SATA), prog-if 01 (AHCI) */
	if(!pci_find_class(0x01, 0x06, 0x01, &dev))
	{
		kprint("no AHCI controller found\n");
		return 0;
	}

	/* enable MMIO decoding and bus mastering (DMA) */
	pci_write(&dev, PCI_COMMAND,
		  pci_read(&dev, PCI_COMMAND) | PCI_CMD_MEMORY | PCI_CMD_MASTER);
	abar = (volatile unsigned char *)(pci_read(&dev, PCI_BAR5) & 0xFFFFFFF0UL);

	wr(abar, HBA_GHC, rd(abar, HBA_GHC) | GHC_AE);
	wr(abar, HBA_GHC, rd(abar, HBA_GHC) & ~0x2UL);	/* no HBA interrupts */

	kprint("controller at PCI ");
	printlong(dev.bus); kprint(":"); printlong(dev.slot); kprint(".");
	printlong(dev.func);
	kprint("\n");

	pi = rd(abar, HBA_PI);
	for(i = 0; i < 32; i++)
	{
		volatile unsigned char *p = abar + 0x100 + i * 0x80;

		if(!(pi & (1UL << i)))
			continue;
		ssts = rd(p, PX_SSTS);
		if((ssts & 0x0F) != 3 || ((ssts >> 8) & 0x0F) != 1)
			continue;	/* no device / link not active */
		if(rd(p, PX_SIG) != SIG_SATA_DISK)
			continue;	/* ATAPI, port multiplier, ... */

		if(!port_stop(p))
		{
			kprint("  port won't stop, skipping\n");
			continue;
		}
		zero(cmd_list, sizeof(cmd_list));
		zero(fis_area, sizeof(fis_area));
		wr(p, PX_CLB, (unsigned long)cmd_list);
		wr(p, PX_CLBU, 0);
		wr(p, PX_FB, (unsigned long)fis_area);
		wr(p, PX_FBU, 0);
		wr(p, PX_IE, 0);
		wr(p, PX_SERR, 0xFFFFFFFFUL);
		wr(p, PX_IS, 0xFFFFFFFFUL);
		port_start(p);

		port_regs = p;
		ahci_port = i;
		read_identify();

		kprint("  SATA disk on port ");
		printlong(i);
		kprint(": ");
		kprint(ahci_model[0] ? ahci_model : "(unknown model)");
		kprint(", ");
		printlong(ahci_sectors >> 11);
		kprint(" MB\n");
		return 1;
	}

	kprint("  no SATA disk found\n");
	return 0;
}
