/*
GazOS Operating System
Copyright (C) 1999  Gareth Owen <gaz@athene.co.uk>

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/

#ifndef INCLUDE_GAZ_FS
#define INCLUDE_GAZ_FS

typedef struct
{
	unsigned char jmp[4];	/* jump to code (4bytes) */
	unsigned char id[5];	/* Should be 'GazFS' */
	unsigned long version;	/* Should be 1 */
	unsigned long fs_start;	/* LBA pointer to the start of the FS */
	unsigned long krnl_start; /* LBA pointer to kernel file entry */
	unsigned int BytesPerSector; 
	unsigned int SectersPerTrack;
	unsigned int TotalHeads;
	unsigned long TotalSectors;
	unsigned char code[479];
} __attribute__ ((packed)) t_bootsect;

typedef struct 
{
	unsigned char type;	/* 0 = unused. 1 = file entry. 2 = data_entry */
	unsigned long prev_entry;	/* LBA pointer to previous file entry */
	unsigned long next_entry;	/* LBA pointer to next file entry */
	unsigned char filename[255];	/* File name, padded with NULLs */
	/* Attributes
	 * Bit 0: Read
	 * Bit 1: Write
	 * Bit 2: Execute
	 * Bit 3: Hidden
	 * Bit 4: Directory
	 * Rest are unused, and available for future expansion
	 */
	unsigned char attributes;
	unsigned long size;		/* size in bytes */
	unsigned long parent_dir;	/* LBA pointer to parent dir entry */
	unsigned long first_dataentry;	/* LBA pointer to next data struct */
	unsigned char padding[3819];
} __attribute__ ((packed)) t_file_entry; 

typedef struct
{
	unsigned char type;	/* 0 = unused. 1 = file entry. 2 = data_entry */
	unsigned long prev_entry;	/* LBA pointer to previous data/file entry */
	unsigned long next_entry;	/* LBA pointer to next data/file entry */
	unsigned char data[4087];
} __attribute__ ((packed)) t_data_entry;

void gazfs_init();
void displayrootdir();
unsigned long getdata(char *filename, unsigned long offset, unsigned long size, unsigned char *buffer);

#endif
