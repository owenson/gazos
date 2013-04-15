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
#include "8259.h"

unsigned int irq_mask = 0xFFFF; // All IRQs disabled initially

/* init8259() initialises the 8259 Programmable Interrupt Controller */

void Init8259(void)
{
   outportb(M_PIC, ICW1);       /* Start 8259 initialization    */
   outportb(S_PIC, ICW1);

   outportb(M_PIC+1, M_VEC);    /* Base interrupt vector        */
   outportb(S_PIC+1, S_VEC);

   outportb(M_PIC+1, 1<<2);     /* Bitmask for cascade on IRQ 2 */
   outportb(S_PIC+1, 2);        /* Cascade on IRQ 2             */

   outportb(M_PIC+1, ICW4);     /* Finish 8259 initialization   */
   outportb(S_PIC+1, ICW4);

   outportb(M_IMR, 0xff);       /* Mask all interrupts          */
   outportb(S_IMR, 0xff);
}

/* enables irq irq_no */
void enable_irq(unsigned short irq_no)
{
	irq_mask &= ~(1 << irq_no);
	if(irq_no >= 8)
		irq_mask &= ~(1 << 2);
	
	outportb(M_PIC+1, irq_mask & 0xFF);
	outportb(S_PIC+1, (irq_mask >> 8) & 0xFF);
}

/* disables irq irq_no */
void disable_irq(unsigned short irq_no)
{
	irq_mask |= (1 << irq_no);
	if((irq_mask & 0xFF00)==0xFF00)
		irq_mask |= (1 << 2);
	
	outportb(M_PIC+1, irq_mask & 0xFF);
	outportb(S_PIC+1, (irq_mask >> 8) & 0xFF);
}