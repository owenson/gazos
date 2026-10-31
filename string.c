/*
GazOS Operating System
Copyright (C) 1999  Gareth Owenson <gareth.owenson@gmail.com>

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

#include "keyboard.h"
#include "video.h"

void gets(char *string)
{
	unsigned char c;
	unsigned int count = 0;
	char echo[2];

	echo[1] = 0;
	while(1)
	{
		c = getch();
		if(c == 10) break;
		if(c == 8)			/* backspace */
		{
			if(count > 0)
			{
				count--;
				echo[0] = 8;
				kprint(echo);
			}
			continue;
		}
		if(c == 0 || count >= 254)	/* non-printing key / line full */
			continue;
		string[count++] = c;
		echo[0] = c;
		kprint(echo);
	}
	string[count] = '\0';
}

int kstrcmp(char *str1, char *str2)
{
	while(*str1 == *str2 && *str1!=0 && *str2!=0) { str1++; str2++; }
	if(*str1 == *str2) return 0;
	if(*str1 > *str2) return 1;
	if(*str1 < *str2) return -1;
	return -1;
}

