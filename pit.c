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

#include "pit.h"
#include "io.h"

extern void pit_ISR();

void init_pit(float h, unsigned char channel)
{
	unsigned int temp=0;
	
	temp = 1193180/h;
	
	outportb(TMR_CTRL, (channel*0x40) + TMR_BOTH + TMR_MD3);
	outportb((0x40+channel), (unsigned char) temp);
	outportb((0x40+channel), (unsigned char) (temp>>8));
}

unsigned int pit_getchannel(unsigned char channel)
{
	unsigned int x=0;
	
	outportb(TMR_CTRL, (channel*0x40) + TMR_LATCH);
	x = inportb(0x40+channel);
	x += (inportb(0x40+channel) << 8);
	return x;
}

asm (
   ".globl pit_ISR         \n"
   "pit_ISR:               \n"
   "   pusha               \n" /* Save all registers               */
   "   pushw %ds           \n" /* Set up the data segment          */
   "   pushw %es           \n"
   "   pushw %ss           \n" /* Note that ss is always valid     */
   "   pushw %ss           \n"
   "   popw %ds            \n"
   "   popw %es            \n"
   "                       \n"
   "   call pit_handler    \n"
   "                       \n"
   "   popw %es            \n"
   "   popw %ds            \n" /* Restore registers                */
   "   popa                \n"
   "   iret                \n" /* Exit interrupt                   */
);

void pit_handler()
{
	asm("int $0x1C");	// Extra timer interrupt, used by floppy for timeout etc..	
	outportb(0x20, 0x20);
}