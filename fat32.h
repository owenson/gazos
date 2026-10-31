/*
GazOS Operating System
Copyright (C) 2026  Gareth Owenson <gareth.owenson@gmail.com>
FAT32 filesystem driver (read-only), on top of the AHCI disk driver.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/

#ifndef INCLUDE_FAT32
#define INCLUDE_FAT32

#define FAT_ATTR_READONLY	0x01
#define FAT_ATTR_HIDDEN		0x02
#define FAT_ATTR_SYSTEM		0x04
#define FAT_ATTR_VOLUME		0x08
#define FAT_ATTR_DIR		0x10
#define FAT_ATTR_ARCHIVE	0x20
#define FAT_ATTR_LFN		0x0F

#define FAT_MAX_NAME	256
#define FAT_MAX_PATH	256

/* A directory entry, with its long file name resolved */
typedef struct
{
	char name[FAT_MAX_NAME];	/* long name if present, else 8.3 */
	char short_name[13];		/* "NAME.EXT" */
	unsigned char attr;
	unsigned long cluster;		/* first cluster (0 = empty file / root) */
	unsigned long size;		/* bytes */
} fat_dirent;

/* Directory iterator */
typedef struct
{
	unsigned long cluster;		/* current cluster */
	unsigned long sector;		/* sector within cluster */
	unsigned int index;		/* entry within sector */
	int done;
	unsigned char buf[512];
} fat_dir;

/* Mount the first FAT32 partition on the SATA disk. 1 = success. */
int fat32_mount(void);

/* Look up a path (absolute, or relative to the current directory).
 * 1 = found, entry filled in. The root directory is reported as a
 * directory with cluster = root cluster. */
int fat32_lookup(const char *path, fat_dirent *entry);

/* Directory iteration. fat32_opendir takes a directory's first cluster. */
void fat32_opendir(fat_dir *dir, unsigned long cluster);
int fat32_readdir(fat_dir *dir, fat_dirent *entry);	/* 1 = got entry */

/* Read up to 'len' bytes from 'offset' in a file. Returns bytes read,
 * or -1 on a disk error. */
long fat32_read(fat_dirent *file, unsigned long offset, void *buf, unsigned long len);

/* Current working directory */
int fat32_chdir(const char *path);	/* 1 = success */
const char *fat32_getcwd(void);

/* Volume information (valid after fat32_mount) */
extern char fat32_label[12];
extern unsigned long fat32_part_lba, fat32_cluster_size, fat32_total_clusters;

#endif
