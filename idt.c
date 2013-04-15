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

#include "8259.h"
#include "gdt.h"
#include "idt.h"
#include "video.h"
#include "io.h"

extern void int_null();

desc_table(IDT, 256)
{
};

struct
{ 
  unsigned short limit __attribute__ ((packed));
  union DT_entry *idt  __attribute__ ((packed)); 
} loadidt= { (256 * sizeof(union DT_entry) - 1), IDT };


void InitIDT()
{
	int count=0;

	for(count = 16; count < 256; count++)
		set_vector(int_null, count, D_PRESENT + D_INT + D_DPL3);

	set_vector(_int0, 0, D_PRESENT +  D_INT + D_DPL3);
	set_vector(_int1, 1, D_PRESENT +  D_INT + D_DPL3);
	set_vector(_int2, 2, D_PRESENT +  D_INT + D_DPL3);
	set_vector(_int3, 3, D_PRESENT +  D_INT + D_DPL3);
	set_vector(_int4, 4, D_PRESENT +  D_INT + D_DPL3);
	set_vector(_int5, 5, D_PRESENT +  D_INT + D_DPL3);
	set_vector(_int6, 6, D_PRESENT +  D_INT + D_DPL3);
	set_vector(_int7, 7, D_PRESENT +  D_INT + D_DPL3);
	set_vector(_int8, 8, D_PRESENT +  D_INT + D_DPL3);
	set_vector(_int9, 9, D_PRESENT +  D_INT + D_DPL3);
	set_vector(_int10, 10, D_PRESENT +  D_INT + D_DPL3);
	set_vector(_int12, 12, D_PRESENT +  D_INT + D_DPL3);
	set_vector(_int13, 13, D_PRESENT +  D_INT + D_DPL3);
	set_vector(_int14, 14, D_PRESENT +  D_INT + D_DPL3);
	set_vector(_int15, 15, D_PRESENT +  D_INT + D_DPL3);
	set_vector(_int16, 16, D_PRESENT +  D_INT + D_DPL3);

	asm (
		"lidt (%0)                 \n"   /* Load the IDT                */
        	"pushfl                    \n"   /* Clear the NT flag           */
	        "andl $0xffffbfff,(%%esp)  \n"
	        "popfl                     \n"
	        :
	        : "r" ((char *) &loadidt)
	);
	asm("sti");
}


void set_vector(void *handler, unsigned char interrupt, unsigned short control_major)
{
   unsigned short codesegment = 0x08;
   asm volatile("movw %%cs,%0":"=g" (codesegment));

   IDT[interrupt].gate.offset_low    = (unsigned short) (((unsigned long)handler)&0xffff);
   IDT[interrupt].gate.selector      = codesegment;
   IDT[interrupt].gate.access        = control_major;
   IDT[interrupt].gate.offset_high   = (unsigned short) (((unsigned long)handler) >> 16);
}

asm (
   ".globl int_null        \n"
   "int_null:              \n"
   "   iret                \n" /* Exit interrupt                   */
);

void _int0()
{
	kprint("int0: Divide Error\n");
	while(1);
}
void _int1()
{
	kprint("int1: Debug exception\n");
	while(1);
}
void _int2()
{
	kprint("int2: unknown error\n");
	while(1);
}
void _int3()
{
	kprint("int3: Breakpoint\n");
	while(1);
}
void _int4()
{
	kprint("int4: Overflow\n");
	while(1);
}
void _int5()
{
	kprint("int 5: Bounds check\n");
	while(1);
}
void _int6()
{
	kprint("int6: Invalid opcode\n");
	while(1);
}
void _int7()
{
	kprint("int7: Coprocessor not available\n");
	while(1);
}
void _int8()
{
	kprint("int8: Double fault\n");
	while(1);
}
void _int9()
{
	kprint("int9: Coprocessor Segment overrun\n");
	while(1);
}
void _int10()
{
	kprint("int10: Invalid TSS\n");
	while(1);
}
void _int11()
{
	kprint("int11: Segment not present\n");
	while(1);
}
void _int12()
{
	kprint("int12: Stack exception\n");
	while(1);
}
void _int13()
{
	kprint("int13: General Protection\n");
	while(1);
}
void _int14()
{
	kprint("int14: Page fault\n");
	while(1);
}
void _int15()
{
	kprint("in15: Unknown error\n");
	while(1);
}
void _int16()
{
	kprint("int16: Coprocessor error\n");
	while(1);
}
