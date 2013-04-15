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

#include "math.h"
#include "gazfs.h"
#include "string.h"
#include "fdc.h"
#include "video.h"
#include "keyboard.h"

t_bootsect bootsector;

void gazfs_init()
{
	if(read_block(0, (unsigned char *) &bootsector, 1)==FALSE)
	{
		kprint("Kernel Panic: FDC not responding\n");
		kprint("Press any key to try again\n");
		getch();
		gazfs_init();
		return;
	}
	if(bootsector.id[0] != 'G' || bootsector.id[1] != 'a' || bootsector.id[2] != 'z'
	|| bootsector.id[3] != 'F' || bootsector.id[4] != 'S' || bootsector.version != 1)
	{
		kprint("Error: Disk in Floppy drive is not of format GazFS\n");
		kprint("Insert a GazFS formatted floppy and press any key\n");
		getch();
		gazfs_init();
		return;
	}

	kprint("GazFS module initialized\n");
}

void displayrootdir()
{
	t_file_entry file_entry;
	unsigned long next_file=0;
	next_file = bootsector.fs_start;
	
	kprint("\nRoot directory contents:\n");
	while(next_file != 0)
	{
		read_block(next_file, (unsigned char *)&file_entry,1);
		
		kprint(file_entry.filename);
		kprint("      ");
		printlong(file_entry.size);
		kprint("\n");
		next_file = file_entry.next_entry;
	}

	kprint("\nEnd of directory listing\n");
}

unsigned long getdata(char *filename, unsigned long offset, unsigned long size, unsigned char *buffer)
{ 
	unsigned long next_file = bootsector.fs_start;
	unsigned long file_start = 0;
	unsigned long loop = 0, loop2 = 0, result = 0;
	t_file_entry file_entry;
	t_data_entry data_entry;
	unsigned long temp=0, start_cluster=0, size_left = 0, next_cluster = 0;

	while(next_file != 0)
	{
		read_block(next_file, (unsigned char*)&file_entry,1);

		if(!kstrcmp(filename, file_entry.filename)) 
		{
			file_start = next_file;
			break;
		}
		next_file = file_entry.next_entry;
	}
	if(next_file == 0) return 0;

	if(offset+size > file_entry.size) size = file_entry.size - offset; 

	if(offset > sizeof(data_entry.data))
	{
		temp = floor(offset / sizeof(data_entry.data));
		
		// data_entry needs to store the first cluster that contains data we need
		
		read_block(file_entry.first_dataentry, (unsigned char *)&data_entry,1);

		for(loop=0; loop<temp; loop++)
		{
			start_cluster = data_entry.next_entry;
			read_block(start_cluster, (unsigned char *)&data_entry,1);
		}
		
		// temp2 now contains pointer to the first 512 bytes of first cluster !
		// So lets get the whole cluster since it contains all the data
		read_block(start_cluster, (unsigned char *)&data_entry,8);
		
		offset = offset - (temp*(sizeof(data_entry.data)));	
	
		for(loop=0; loop<((sizeof(data_entry.data))-offset); loop++)
			buffer[loop] = data_entry.data[offset+loop];
			
		size_left = size - (sizeof(data_entry.data)-offset);
		
		if(data_entry.next_entry == 0 || size_left == 0) return size;
		
		next_cluster = data_entry.next_entry;

		read_block(next_cluster, (unsigned char *)&data_entry,8);
	}
	else
	{
		start_cluster = file_entry.first_dataentry;
		read_block(start_cluster, (unsigned char *)&data_entry,8);
		
		size_left = size;
	}

	for(loop=0; loop<floor(size/sizeof(data_entry.data)); loop++)
	{
		for(loop2 = 0; loop2<sizeof(data_entry.data); loop2++)
			buffer[loop2+(size-size_left)] = data_entry.data[loop2];
		
		size_left -= sizeof(data_entry.data);
		next_cluster = data_entry.next_entry;
		if(next_cluster == 0) break;

		result = read_block(next_cluster, (unsigned char *)&data_entry,8);
	}

	if(size_left > 0)
	{
		for(loop=0; loop<size_left; loop++)
			buffer[loop+(size-size_left)] = data_entry.data[loop];
		size_left = 0;
	}

	return size;
}
