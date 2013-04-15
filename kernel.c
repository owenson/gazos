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

#include "string.h"
#include "gazfs.h"
#include "fdc.h"
#include "video.h"
#include "gdt.h"
#include "idt.h"
#include "io.h"
#include "8259.h"
#include "keyboard.h"
#include "delay.h"
#include "pit.h"
#include "math.h"
#include "mem.h"

#define uchar  unsigned char		/* Defentitions to make life	*/
#define uint   unsigned int		/* Easy for us:  uchar, uint	*/
#define ulong  unsigned long int	/* and ulong			*/
#define iuchar inline unsigned char	/* Inline functions		*/
#define ivoid  inline void

uchar KERNEL_STACK[8192];		/* The stack to use: 8k stack	*/
unsigned int cpuspeed=0;		/* CPU Speed in Megahertz */
unsigned long memtotal = 0;		/* Memory Total in bytes */

void _start(void);			/* The entry point		*/
void main();				/* The main kernel program 	*/
void _exit(void);			/* What to do when finished	*/
void ulong_strcnv(uchar *str, ulong i); /* Convert a dword to a string  */

extern void pit_ISR(void);
extern unsigned long uptime_count;

desc_table(GDT, 3)
{
	{dummy:0},
	stnd_desc(0, 0xFFFFF, (D_CODE + D_READ + D_BIG + D_BIG_LIM)),
	stnd_desc(0, 0xFFFFF, (D_DATA + D_WRITE + D_BIG + D_BIG_LIM)),
};

struct
{
	unsigned short limit __attribute__ ((packed));
	union DT_entry *idt __attribute__ ((packed));
} loadgdt = { (3 * sizeof(union DT_entry) - 1), GDT };

void _start(void)			/* The kernel entry point	*/
{
   asm ("pushl $2; popf");		/* Zero the flags		*/

	 asm volatile                /* Load the GDT and the kernel stack     */
	 (                           /* We assume the loader has cleared ints */
      		"lgdtl (loadgdt)     \n" /* Load our own GDT                      */
	        "movw $0x10,%%ax     \n" /* Init data registers with flat_data    */
      		"movw %%ax,%%ds      \n"
      		"movw %%ax,%%es      \n"
      		"movw %%ax,%%fs      \n"
      		"movw %%ax,%%gs      \n"
      		"movw %%ax,%%ss      \n" /* ... and the stack, too                */
      		"movl $0xFFFF,%%esp       \n" /* Set the SP to the end of the stack    */
      		"ljmp $0x08,$next    \n" /* Flush prefetch queue and skip MBHeadr */
		"nop\n"
		"nop\n"
	        "next:               \n" /* Continue here                         */
      		:
      		: "r" (GDT), "p" (KERNEL_STACK+4096)
      		: "%eax"
   	);

   main();				/* Call main()			*/
   _exit();				/* Finish off			*/
}

/* Now we get the main kernel program 					*/

void main()
{
	unsigned char tempstring[255];
	unsigned long delaycount=0, loop=0, result, catcount = 0;
	unsigned char buf[8192];

	kprint("\n\nGazOS Version 1.0 Loading......\n");
	kprint("Initializing the 8259 PIC...\n");
	Init8259();

	kprint("Loading IDT...\n");
	InitIDT();

        /* We want to calibrate the delay loop while all interrupts are */
        /* masked, and before the scheduler is loaded.                  */
	kprint("Calibrating delay loop... [");
        delaycount = calibrateDelayLoop();
        printlong(delaycount);
        kprint("]\n");

	cpuspeed = floor(delaycount/500);
	count_memory();

	kprint("Initializing Keyboard...\n");
	InitKeyboard();

	init_pit(18.2, 0);	// Every 54.945ms (18.2 times per second)
	set_vector(pit_ISR, M_VEC, (D_INT + D_PRESENT + D_DPL3));
	enable_irq(0);

	init_floppy();

	gazfs_init();

	kprint("CPU = ");
	printlong(cpuspeed);
	kprint("Mhz  -  Mem.Total = ");
	printlong(mem_end/1024);
	kprint("K  -  Total Pages = ");
	printlong(mem_end/4096);
	kprint("\n");

	while(1)
	{
		kprint("\nGazOS] /: ");
		gets(tempstring);
		kprint("\n");
		
		if(!kstrcmp(tempstring, "ls")) displayrootdir();
		if(tempstring[0] == 'c' && tempstring[1] == 'a' &&
		   tempstring[2] == 't')
		{
		   	result = getdata(tempstring+4, 0, (unsigned long)(-1), buf);
		   	if(result > 0)
		   	{
		   		catcount = 0;
				for(loop=0; loop<result; loop++)
				{
					tempstring[0] = buf[loop];
					tempstring[1] = 0;
					kprint(tempstring);
					if(tempstring[0] == 10) catcount++;
					if(catcount >= 23)
					{
						kprint("Press any key...");
						getch();
						kprint("\n");
						catcount = 0;
					}
				}
		   	}
		   	else
		   		kprint("File not found or error\n");
		}
	}
}

void _exit(void)			/* Exit point of the kernel	*/
{
   while(inportb(0x64) & 0x02);		/* Reboot the computer		*/
   outportb(0x64, 0xfe);

   asm("cli;hlt");			/* Should it not work we halt 	*/
					/* the computer now so the we	*/
					/* do no harm			*/
}
