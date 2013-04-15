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

#include "io.h"

unsigned long mem_end, bse_end;

void count_memory(void)
 {
         register unsigned long *mem;
         unsigned long mem_count, a;
         unsigned short  memkb;
         unsigned char   irq1, irq2;
         unsigned long cr0;

         /* save IRQ's */
         irq1=inportb(0x21);
         irq2=inportb(0xA1);

         /* kill all irq's */
         outportb(0x21, 0xFF);
         outportb(0xA1, 0xFF);

         mem_count=0;
         memkb=0;

         // store a copy of CR0
         __asm__ __volatile("movl %%cr0, %%eax":"=a"(cr0)::"eax");

         // invalidate the cache
         // write-back and invalidate the cache
         __asm__ __volatile__ ("wbinvd");

         // plug cr0 with just PE/CD/NW
         // cache disable(486+), no-writeback(486+), 32bit mode(386+)
         __asm__ __volatile__("movl %%eax, %%cr0" :: "a" (cr0 | 0x00000001 | 0x40000000 | 0x20000000) : "eax");

         do
         {
                 memkb++;
                 mem_count+=1024*1024;
                 mem=(unsigned long *)mem_count;

                 a=*mem;

                 *mem=0x55AA55AA;
                 
                 // the empty asm calls tell gcc not to rely on whats in its registers
                 // as saved variables (this gets us around GCC optimisations)
                 asm("":::"memory");
                 if(*mem!=0x55AA55AA)
                         mem_count=0;
                 else
                 {
                         *mem=0xAA55AA55;
                         asm("":::"memory");
                         if(*mem!=0xAA55AA55)
                                 mem_count=0;
                 }

                 asm("":::"memory");
                 *mem=a;
         }while(memkb<4096 && mem_count!=0);

         __asm__ __volatile__("movl %%eax, %%cr0" :: "a" (cr0) : "eax");

         mem_end=memkb<<20;
         mem=(unsigned long *)0x413;
         bse_end=((*mem)&0xFFFF)<<6;

         outportb(0x21, irq1);
         outportb(0xA1, irq2);
 }
