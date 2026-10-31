/*
GazOS Operating System
Copyright (C) 2026  Gareth Owenson <gareth.owenson@gmail.com>
FAT32 filesystem driver (read-only), on top of the AHCI disk driver.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

Supports: MBR-partitioned disks (type 0x0B/0x0C/0xEF) or an unpartitioned
FAT32 volume, 512-byte sectors, any cluster size, long file names (LFN),
NT lower-case flags on 8.3 names, subdirectories, "." and "..".
*/

#include "fat32.h"
#include "ahci.h"
#include "video.h"

#define FAT_EOC		0x0FFFFFF8UL	/* >= this: end of chain */
#define FAT_BAD		0x0FFFFFF7UL

char fat32_label[12];
unsigned long fat32_part_lba = 0, fat32_cluster_size = 0, fat32_total_clusters = 0;

static unsigned long fat_lba;		/* first sector of FAT #1 */
static unsigned long data_lba;		/* first sector of cluster 2 */
static unsigned long root_cluster;
static unsigned long sec_per_clus;
static int mounted = 0;

static unsigned char fat_cache[512] __attribute__ ((aligned(16)));
static unsigned long fat_cache_lba = 0xFFFFFFFFUL;
static unsigned char sec_buf[512] __attribute__ ((aligned(16)));

static unsigned long cwd_cluster;
static char cwd_path[FAT_MAX_PATH] = "/";

/* ---- small helpers (the kernel has no libc) ---- */

static unsigned int rd16(const unsigned char *p) { return p[0] | (p[1] << 8); }
static unsigned long rd32(const unsigned char *p)
{
	return p[0] | (p[1] << 8) | ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

static char upper(char c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }
static char lower(char c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

static int strcasecmp_k(const char *a, const char *b)
{
	while(*a && upper(*a) == upper(*b)) { a++; b++; }
	return upper(*a) - upper(*b);
}

static unsigned int strlen_k(const char *s)
{
	unsigned int n = 0;
	while(s[n]) n++;
	return n;
}

static void strcpy_k(char *d, const char *s)
{
	while((*d++ = *s++));
}

/* ---- low level ---- */

static int is_valid_cluster(unsigned long c)
{
	return c >= 2 && c < fat32_total_clusters + 2;
}

static unsigned long cluster_lba(unsigned long c)
{
	return data_lba + (c - 2) * sec_per_clus;
}

/* Next cluster in the chain, or 0 at end of chain / error */
static unsigned long fat_next(unsigned long c)
{
	unsigned long lba = fat_lba + (c * 4) / 512;
	unsigned long v;

	if(!is_valid_cluster(c))
		return 0;
	if(lba != fat_cache_lba)
	{
		if(!ahci_read(lba, 1, fat_cache))
		{
			fat_cache_lba = 0xFFFFFFFFUL;
			return 0;
		}
		fat_cache_lba = lba;
	}
	v = rd32(fat_cache + (c * 4) % 512) & 0x0FFFFFFFUL;
	if(v >= FAT_BAD || !is_valid_cluster(v))
		return 0;
	return v;
}

static int is_fat32_bpb(const unsigned char *b)
{
	return b[510] == 0x55 && b[511] == 0xAA &&
	       rd16(b + 11) == 512 &&		/* bytes per sector */
	       b[13] != 0 &&			/* sectors per cluster */
	       rd16(b + 17) == 0 &&		/* root entries: 0 on FAT32 */
	       rd32(b + 36) != 0;		/* FAT size (32-bit) */
}

int fat32_mount(void)
{
	unsigned char *b = sec_buf;
	unsigned long part = 0, rsvd, nfats, fatsz, total, i;
	unsigned char type;

	mounted = 0;
	kprint("Mounting FAT32... ");

	if(!ahci_read(0, 1, b))
	{
		kprint("disk read error\n");
		return 0;
	}

	if(!is_fat32_bpb(b))		/* not a bare volume: read partition table */
	{
		if(b[510] != 0x55 || b[511] != 0xAA)
		{
			kprint("no MBR on disk\n");
			return 0;
		}
		for(i = 0; i < 4; i++)
		{
			type = b[446 + i * 16 + 4];
			/* FAT32 (CHS / LBA), or an EFI System Partition */
			if(type == 0x0B || type == 0x0C || type == 0xEF)
			{
				part = rd32(b + 446 + i * 16 + 8);
				break;
			}
		}
		if(i == 4)
		{
			kprint("no FAT32 partition found\n");
			return 0;
		}
		if(!ahci_read(part, 1, b) || !is_fat32_bpb(b))
		{
			kprint("partition is not a valid FAT32 volume\n");
			return 0;
		}
	}

	sec_per_clus = b[13];
	rsvd = rd16(b + 14);
	nfats = b[16];
	total = rd16(b + 19) ? rd16(b + 19) : rd32(b + 32);
	fatsz = rd32(b + 36);
	root_cluster = rd32(b + 44);

	fat32_part_lba = part;
	fat_lba = part + rsvd;
	data_lba = fat_lba + nfats * fatsz;
	fat32_cluster_size = sec_per_clus * 512;
	fat32_total_clusters = (total - (data_lba - part)) / sec_per_clus;
	if(fat32_total_clusters > fatsz * 128 - 2)	/* can't exceed the FAT */
		fat32_total_clusters = fatsz * 128 - 2;

	for(i = 0; i < 11; i++)
		fat32_label[i] = b[71 + i];
	fat32_label[11] = 0;
	for(i = 11; i > 0 && fat32_label[i - 1] == ' '; i--)
		fat32_label[i - 1] = 0;

	fat_cache_lba = 0xFFFFFFFFUL;
	cwd_cluster = root_cluster;
	strcpy_k(cwd_path, "/");
	mounted = 1;

	kprint("volume \"");
	kprint(fat32_label);
	kprint("\" at LBA ");
	printlong(part);
	kprint(", ");
	printlong(fat32_cluster_size);
	kprint(" byte clusters, ");
	printlong(fat32_total_clusters);
	kprint(" clusters\n");
	return 1;
}

/* ---- directories ---- */

void fat32_opendir(fat_dir *dir, unsigned long cluster)
{
	dir->cluster = cluster ? cluster : root_cluster;
	dir->sector = 0;
	dir->index = 0;
	dir->done = !mounted || !is_valid_cluster(dir->cluster);
	if(!dir->done && !ahci_read(cluster_lba(dir->cluster), 1, dir->buf))
		dir->done = 1;
}

/* advance to the next 32-byte entry; 0 at end of directory */
static int dir_advance(fat_dir *dir)
{
	if(++dir->index < 16)
		return 1;
	dir->index = 0;
	if(++dir->sector >= sec_per_clus)
	{
		dir->sector = 0;
		dir->cluster = fat_next(dir->cluster);
		if(!dir->cluster)
			return 0;
	}
	return ahci_read(cluster_lba(dir->cluster) + dir->sector, 1, dir->buf);
}

static unsigned char lfn_checksum(const unsigned char *name)
{
	unsigned char sum = 0;
	int i;
	for(i = 0; i < 11; i++)
		sum = ((sum & 1) << 7) + (sum >> 1) + name[i];
	return sum;
}

/* offsets of the 13 UCS-2 characters within an LFN entry */
static const unsigned char lfn_offsets[13] = { 1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30 };

int fat32_readdir(fat_dir *dir, fat_dirent *e)
{
	char lfn[FAT_MAX_NAME];
	int lfn_valid = 0;
	unsigned char lfn_sum = 0, lfn_next = 0;

	while(!dir->done)
	{
		unsigned char *d = dir->buf + dir->index * 32;
		unsigned char attr = d[11];

		if(d[0] == 0x00)		/* end of directory */
		{
			dir->done = 1;
			break;
		}

		if(d[0] == 0xE5)		/* deleted */
			lfn_valid = 0;
		else if((attr & 0x3F) == FAT_ATTR_LFN)
		{
			unsigned char seq = d[0] & 0x1F;
			int i, pos;

			if(d[0] & 0x40)		/* last (first stored) LFN entry */
			{
				lfn_valid = 1;
				lfn_sum = d[13];
				if(seq * 13 < FAT_MAX_NAME)
					lfn[seq * 13] = 0;
			}
			else if(!lfn_valid || seq != lfn_next || d[13] != lfn_sum)
				lfn_valid = 0;

			if(lfn_valid && seq >= 1 && seq * 13 < FAT_MAX_NAME)
			{
				pos = (seq - 1) * 13;
				for(i = 0; i < 13; i++)
				{
					unsigned int ch = rd16(d + lfn_offsets[i]);
					if(ch == 0x0000)
					{
						lfn[pos + i] = 0;
						break;
					}
					lfn[pos + i] = (ch < 0x80) ? (char)ch : '?';
				}
				lfn_next = seq - 1;
			}
			else
				lfn_valid = 0;
		}
		else if(attr & FAT_ATTR_VOLUME)	/* volume label */
			lfn_valid = 0;
		else
		{
			/* short (8.3) entry: this is the real file */
			int i, n = 0;
			unsigned char nt = d[12];

			for(i = 0; i < 8 && d[i] != ' '; i++)
			{
				char c = (i == 0 && d[0] == 0x05) ? (char)0xE5 : d[i];
				e->short_name[n++] = (nt & 0x08) ? lower(c) : c;
			}
			if(d[8] != ' ')
			{
				e->short_name[n++] = '.';
				for(i = 8; i < 11 && d[i] != ' '; i++)
					e->short_name[n++] = (nt & 0x10) ? lower(d[i]) : d[i];
			}
			e->short_name[n] = 0;

			if(lfn_valid && lfn_next == 0 && lfn_checksum(d) == lfn_sum)
				strcpy_k(e->name, lfn);
			else
				strcpy_k(e->name, e->short_name);

			e->attr = attr;
			e->cluster = ((unsigned long)rd16(d + 20) << 16) | rd16(d + 26);
			e->size = rd32(d + 28);

			if(!dir_advance(dir))
				dir->done = 1;
			return 1;
		}

		if(!dir_advance(dir))
			dir->done = 1;
	}
	return 0;
}

/* find 'name' in directory 'cluster' */
static int dir_find(unsigned long cluster, const char *name, fat_dirent *e)
{
	fat_dir dir;

	fat32_opendir(&dir, cluster);
	while(fat32_readdir(&dir, e))
		if(!strcasecmp_k(e->name, name) || !strcasecmp_k(e->short_name, name))
		{
			if((e->attr & FAT_ATTR_DIR) && e->cluster == 0)
				e->cluster = root_cluster;	/* ".." pointing at root */
			return 1;
		}
	return 0;
}

static void make_root(fat_dirent *e)
{
	strcpy_k(e->name, "/");
	strcpy_k(e->short_name, "/");
	e->attr = FAT_ATTR_DIR;
	e->cluster = root_cluster;
	e->size = 0;
}

/* Walk 'path'. Fills in 'e' with the final entry. If 'canon' is non-NULL
 * it receives the canonical absolute path (for directories). */
static int resolve(const char *path, fat_dirent *e, char *canon)
{
	char comp[FAT_MAX_NAME];
	char newpath[FAT_MAX_PATH];
	unsigned int n, len;

	if(!mounted)
		return 0;

	if(path[0] == '/')
	{
		make_root(e);
		strcpy_k(newpath, "/");
	}
	else
	{
		strcpy_k(e->name, cwd_path);
		e->attr = FAT_ATTR_DIR;
		e->cluster = cwd_cluster;
		e->size = 0;
		strcpy_k(newpath, cwd_path);
	}

	while(*path)
	{
		while(*path == '/') path++;
		for(n = 0; *path && *path != '/' && n < FAT_MAX_NAME - 1; n++)
			comp[n] = *path++;
		comp[n] = 0;
		if(n == 0 || (n == 1 && comp[0] == '.'))
			continue;
		if(!(e->attr & FAT_ATTR_DIR))
			return 0;		/* "file/something" */

		if(comp[0] == '.' && comp[1] == '.' && comp[2] == 0)
		{
			if(e->cluster == root_cluster)
				continue;	/* root has no parent */
			if(!dir_find(e->cluster, "..", e))
				return 0;
			/* strip the last path component */
			len = strlen_k(newpath);
			while(len > 1 && newpath[len - 1] != '/') len--;
			if(len > 1) len--;
			newpath[len] = 0;
			if(e->cluster == root_cluster)
				make_root(e);
			continue;
		}

		if(!dir_find(e->cluster, comp, e))
			return 0;

		len = strlen_k(newpath);
		if(len + strlen_k(e->name) + 2 >= FAT_MAX_PATH)
			return 0;
		if(len > 1)
			newpath[len++] = '/';
		strcpy_k(newpath + len, e->name);
	}

	if(canon)
		strcpy_k(canon, newpath);
	return 1;
}

int fat32_lookup(const char *path, fat_dirent *e)
{
	return resolve(path, e, 0);
}

int fat32_chdir(const char *path)
{
	fat_dirent e;
	char canon[FAT_MAX_PATH];

	if(!resolve(path, &e, canon) || !(e.attr & FAT_ATTR_DIR))
		return 0;
	cwd_cluster = e.cluster;
	strcpy_k(cwd_path, canon);
	return 1;
}

const char *fat32_getcwd(void)
{
	return cwd_path;
}

/* ---- file data ---- */

long fat32_read(fat_dirent *f, unsigned long offset, void *buf, unsigned long len)
{
	unsigned char *out = buf;
	unsigned long cluster = f->cluster, done = 0, skip, sec, pos, n;

	if(!mounted || offset >= f->size)
		return 0;
	if(len > f->size - offset)
		len = f->size - offset;

	/* skip whole clusters before 'offset' */
	for(skip = offset / fat32_cluster_size; skip > 0; skip--)
		if(!(cluster = fat_next(cluster)))
			return -1;
	pos = offset % fat32_cluster_size;

	while(done < len)
	{
		if(!is_valid_cluster(cluster))
			return -1;
		sec = pos / 512;
		if(!ahci_read(cluster_lba(cluster) + sec, 1, sec_buf))
			return -1;
		n = 512 - pos % 512;
		if(n > len - done)
			n = len - done;
		{
			unsigned long i;
			for(i = 0; i < n; i++)
				out[done + i] = sec_buf[pos % 512 + i];
		}
		done += n;
		pos += n;
		if(pos >= fat32_cluster_size && done < len)
		{
			pos = 0;
			cluster = fat_next(cluster);
		}
	}
	return done;
}
