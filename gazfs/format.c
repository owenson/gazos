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

#include <string.h>
#include <stdio.h>
#include "../math.c"

unsigned long getfilesize(char *filename);

typedef struct
{
	unsigned char jmp[4];	/* jump to code (4bytes) */
	unsigned char id[5];	/* Should be 'GazFS' */
	unsigned long version;	/* Should be 1 */
	unsigned long fs_start;	/* LBA pointer to the start of the FS */
	unsigned long krnl_size; /* LBA pointer to Kernel file entry */
        unsigned int BytesPerSector;
        unsigned int SectorsPerTrack;
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

// argv[1] = start of FS
// argv[2] = file to put onto FS
	
void main(int argc, char **argv)
{
	t_bootsect bootsect;
	t_file_entry file_entry;
	t_data_entry data_entry;
	unsigned char *data;
	unsigned long _fs_start;
	unsigned long filesize=0;
	unsigned long cur_lbasector = 0;
	char filename[255];
	unsigned long no_of_files, file_loop=0;
	
	unsigned int loop=0;
	FILE *dev, *file;

	if(argc < 3)
	{
		printf("Usage: format <fs_start> <no. of files> <filename> <filename>\n");
		exit(1);
	}

	sscanf(argv[1], "%d", &_fs_start);	
	sscanf(argv[2], "%d", &no_of_files);

	printf("Size of t_bootsect = %d\n", sizeof(bootsect));
	printf("Size of t_file_entry = %d\n", sizeof(t_file_entry));
	printf("Size of t_data_entry = %d\n", sizeof(t_data_entry));
	printf("Size of data_entry.data = %d\n", sizeof(data_entry.data));

	for(loop=0; loop<4; loop++)
		bootsect.jmp[loop] = 0;
	
	bootsect.id[0] = 'G';
	bootsect.id[1] = 'a';
	bootsect.id[2] = 'z';
	bootsect.id[3] = 'F';
	bootsect.id[4] = 'S';
	
	bootsect.version = 1;
	bootsect.fs_start = _fs_start;
	bootsect.BytesPerSector = 512;
	bootsect.SectorsPerTrack = 18;
	bootsect.TotalHeads = 2;
	bootsect.TotalSectors = 2880*512;
	bootsect.krnl_size = _fs_start;
	
	for(loop=0; loop<sizeof(bootsect.code); loop++)
		bootsect.code[loop] = 0;

	dev = fopen("/dev/fd0", "wb");
	if(dev == NULL)
	{
		perror("open");
		exit(1);
	}

	fwrite(&bootsect, 512, 1, dev);
	cur_lbasector++;

	if(_fs_start!=1)
		for(loop=0; loop<_fs_start-1; loop++)
		{	
			fwrite(&bootsect, 512, 1, dev);	
			cur_lbasector++;
		}
	
for(file_loop=3; file_loop<(3+no_of_files); file_loop++)
{	
	printf("in loop, current file = \"%s\"\n", argv[file_loop]);
	strcpy(filename, argv[file_loop]);
	filesize = getfilesize(filename);
	file = fopen(filename, "rb");
	if(file == NULL)
	{
		perror(filename);
		exit(1);
	}
	
	data = (unsigned char *) malloc(filesize+sizeof(data_entry.data));
	if(!data)
	{
		printf("Insufficient memory\n");
		exit(1);
	}
	
	for(loop=0; loop<filesize; loop++)
		data[loop] = fgetc(file);

	file_entry.type = 1; /* Used - File entry */
	file_entry.prev_entry = 0;
	if((file_loop+1)<(3+no_of_files))
		file_entry.next_entry = cur_lbasector + 8 + (ceil((double) filesize/ (double)4096)*8);
	else
		file_entry.next_entry = 0;
	strcpy(file_entry.filename, filename);
	file_entry.attributes = (0xFF & (1 + 2));
	file_entry.size = ftell(file);
	file_entry.parent_dir = 0;
	file_entry.first_dataentry = cur_lbasector+8;
	
	fwrite(&file_entry, 4096, 1, dev);
	cur_lbasector += 8;


	for(loop=0; loop<ceil((double) filesize/ (double)sizeof(data_entry.data)); loop++)
	{
		data_entry.type = 2;
		data_entry.prev_entry = cur_lbasector-8; // _fs_start+(loop*8);
		if(loop+1 == ceil((double) filesize/ (double)sizeof(data_entry.data)))
			data_entry.next_entry = 0;
		else
			data_entry.next_entry = cur_lbasector+8;// _fs_start+((loop+2)*8);
		memcpy(data_entry.data, data+(loop*sizeof(data_entry.data)), sizeof(data_entry.data));	
		fwrite(&data_entry, 4096, 1, dev);
		cur_lbasector+=8;
	}

	fclose(file);
	free(data);
}

	if((2880 - cur_lbasector) != 0)
	{
		data = (unsigned char *)malloc(512);
		if(!data) return;
		memset(data, 0, 512);
		for(loop=0; loop<(2880-cur_lbasector); loop++)
			fwrite(data, 512, 1, dev);
			
		free(data);
	}
	fclose(dev);
}

unsigned long getfilesize(char *filename)
{
	FILE *in;
	unsigned long size=0;
	if((in = fopen(filename, "rb"))==NULL)
		return 0;
		
	fseek(in, 0, SEEK_END);
	size = ftell(in);
	fclose(in);
	return size;
}	