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

#include "dma.h"
#include "fdc.h"
#include "video.h"
#include "delay.h"
#include "gdt.h"
#include "idt.h"
#include "8259.h"
#include "io.h"

/* globals */
static volatile BOOL done = FALSE;
static BOOL dchange = FALSE;
static BOOL motor = FALSE;
static int mtick = 0;
static volatile int tmout = 0;
static BYTE status[7] = { 0 };
static BYTE statsz = 0;
static BYTE sr0 = 0;
static BYTE fdc_track = 0xff;
static DrvGeom geometry = { DG144_HEADS,DG144_TRACKS,DG144_SPT };

unsigned long tbaddr = 0x80000L;    /* physical address of track buffer located below 1M */

/* prototypes */
extern void floppy_ISR();
extern void _int1c();

void sendbyte(int byte);
int getbyte();
void irq6(void);
void int1c(void);
BOOL waitfdc(BOOL sensei);
BOOL fdc_rw(int block,BYTE *blockbuff,BOOL read,unsigned long nosectors);

/* helper functions */

/* init driver */
void init_floppy(void)
{
   int i;
   
   /* allocate track buffer (must be located below 1M) */
   /* see above for address assignment, floppy DMA buffer is at 0x80000) */
   
   /* install IRQ6 handler */
   set_vector(floppy_ISR, M_VEC+6, (D_INT+D_PRESENT+D_DPL1));
   enable_irq(6);

   set_vector(_int1c, 0x1c, (D_INT+D_PRESENT+D_DPL1));

   reset();

   /* get floppy controller version */
   sendbyte(CMD_VERSION);
   i = getbyte();

   kprint("Initialising Floppy driver...  ");

   if (i == 0x80)
     kprint("NEC765 controller found\n");
   else
     kprint("Enhanced controller found\n");
}

/* deinit driver */
//void deinit(void)
//{
//   set_irq_handler(6,NULL,&oldirq6);
//   kprint("uninstalling IRQ6 handler\n");
//   set_irq_handler(0x1c,NULL,&oldint1c);
//   kprint("uninstalling timer handler\n");
//   
//   /* stop motor forcefully */
//   outportb(FDC_DOR,0x0c);
//}

/* sendbyte() routine from intel manual */
void sendbyte(int byte)
{
   volatile int msr;
   int tmo;
   
   for (tmo = 0;tmo < 128;tmo++) {
      msr = inportb(FDC_MSR);
      if ((msr & 0xc0) == 0x80) {
	 outportb(FDC_DATA,byte);
	 return;
      }
      inportb(0x80);   /* delay */
   }
}

/* getbyte() routine from intel manual */
int getbyte()
{
   volatile int msr;
   int tmo;
   
   for (tmo = 0;tmo < 128;tmo++) {
      msr = inportb(FDC_MSR);
      if ((msr & 0xd0) == 0xd0) {
	 return inportb(FDC_DATA);
      }
      inportb(0x80);   /* delay */
   }

   return -1;   /* read timeout */
}

/* this waits for FDC command to complete */
BOOL waitfdc(BOOL sensei)
{
   tmout = 50000;   /* set timeout to 1 second */
     
   /* wait for IRQ6 handler to signal command finished */
   while (!done && tmout)
     ;
   /* read in command result bytes */
   statsz = 0;
   while ((statsz < 7) && (inportb(FDC_MSR) & (1<<4))) {
      status[statsz++] = getbyte();
   }

   if (sensei) {
      /* send a "sense interrupt status" command */
      sendbyte(CMD_SENSEI);
      sr0 = getbyte();
      fdc_track = getbyte();
   }
   
   done = FALSE;
   
   if (!tmout) {
      /* timed out! */
      if (inportb(FDC_DIR) & 0x80)  /* check for diskchange */
	dchange = TRUE;
      return FALSE;
   } else
     return TRUE;
}

/* This is the IRQ6 handler */
void irq6(void)
{
   /* signal operation finished */
   done = TRUE;

   /* EOI the PIC */
   outportb(0x20,0x20);
}

/* This is the timer (int 1ch) handler */
void int1c(void)
{
   if (tmout) --tmout;     /* bump timeout */
   
   if (mtick > 0)
     --mtick;
   else if (!mtick && motor) {
      outportb(FDC_DOR,0x0c);  /* turn off floppy motor */
      motor = FALSE;
   }
}

/*
 * converts linear block address to head/track/sector
 * 
 * blocks are numbered 0..heads*tracks*spt-1
 * blocks 0..spt-1 are serviced by head #0
 * blocks spt..spt*2-1 are serviced by head 1
 * 
 * WARNING: garbage in == garbage out
 */
void block2hts(int block,int *head,int *track,int *sector)
{
   *head = (block % (geometry.spt * geometry.heads)) / (geometry.spt);
   *track = block / (geometry.spt * geometry.heads);
   *sector = block % geometry.spt + 1;
}

/**** disk operations ****/

/* this gets the FDC to a known state */
void reset(void)
{
   /* stop the motor and disable IRQ/DMA */
   outportb(FDC_DOR,0);
   
   mtick = 0;
   motor = FALSE;

   /* program data rate (500K/s) */
   outportb(FDC_DRS,0);

   /* re-enable interrupts */
   outportb(FDC_DOR,0x0c);

   /* resetting triggered an interrupt - handle it */
   done = TRUE;
   waitfdc(TRUE);

   /* specify drive timings (got these off the BIOS) */
   sendbyte(CMD_SPECIFY);
   sendbyte(0xdf);  /* SRT = 3ms, HUT = 240ms */
   sendbyte(0x02);  /* HLT = 16ms, ND = 0 */
   
   /* clear "disk change" status */
   seek(1);
   recalibrate();

   dchange = FALSE;
}

/* this returns whether there was a disk change */
BOOL diskchange(void)
{
   return dchange;
}

/* this turns the motor on */
void motoron(void)
{
   if (!motor) {
      mtick = -1;     /* stop motor kill countdown */
      outportb(FDC_DOR,0x1c);
      delay(500); /* delay 500ms for motor to spin up */
      motor = TRUE;
   }
}

/* this turns the motor off */
void motoroff(void)
{
   if (motor) {
      mtick = 13500;   /* start motor kill countdown: 36 ticks ~ 2s */
   }
}

/* recalibrate the drive */
void recalibrate(void)
{
   /* turn the motor on */
   motoron();
   
   /* send actual command bytes */
   sendbyte(CMD_RECAL);
   sendbyte(0);

   /* wait until seek finished */
   waitfdc(TRUE);
   
   /* turn the motor off */
   motoroff();
}

/* seek to track */
BOOL seek(int track)
{
   if (fdc_track == track)  /* already there? */
     return TRUE;
   
//   motoron();
   
   /* send actual command bytes */
   sendbyte(CMD_SEEK);
   sendbyte(0);
   sendbyte(track);

   /* wait until seek finished */
   if (!waitfdc(TRUE))
     return FALSE;     /* timeout! */

   /* now let head settle for 15ms */
   delay(15);
   
//   motoroff();
   
   /* check that seek worked */
   if ((sr0 != 0x20) || (fdc_track != track))
     return FALSE;
   else
     return TRUE;
}

/* checks drive geometry - call this after any disk change */
BOOL log_disk(DrvGeom *g)
{
   /* get drive in a known status before we do anything */
   reset();

   /* assume disk is 1.68M and try and read block #21 on first track */
   geometry.heads = DG168_HEADS;
   geometry.tracks = DG168_TRACKS;
   geometry.spt = DG168_SPT;

   if (read_block(20,NULL,1)) {
      /* disk is a 1.68M disk */
      if (g) {
	 g->heads = geometry.heads;
	 g->tracks = geometry.tracks;
	 g->spt = geometry.spt;
      }
      return TRUE;             
   }
   
   /* OK, not 1.68M - try again for 1.44M reading block #18 on first track */
   geometry.heads = DG144_HEADS;
   geometry.tracks = DG144_TRACKS;
   geometry.spt = DG144_SPT;

   if (read_block(17,NULL,1)) {
      /* disk is a 1.44M disk */
      if (g) {
	 g->heads = geometry.heads;
	 g->tracks = geometry.tracks;
	 g->spt = geometry.spt;
      }
      return TRUE;
   }
   
   /* it's not 1.44M or 1.68M - we don't support it */
   return FALSE;
}

/* read block (blockbuff is 512 byte buffer) */
BOOL read_block(int block,BYTE *blockbuff, unsigned long nosectors)
{
	int track=0, sector=0, head=0, track2=0, result=0, loop=0;

// The FDC can read multiple sides at once but not multiple tracks
	
	block2hts(block, &head, &track, &sector);
	block2hts(block+nosectors, &head, &track2, &sector);
	
	if(track!=track2)
	{
		for(loop=0; loop<nosectors; loop++)
			result = fdc_rw(block+loop, blockbuff+(loop*512), TRUE, 1);
		return result;
	}
   return fdc_rw(block,blockbuff,TRUE,nosectors);
}

/* write block (blockbuff is 512 byte buffer) */
BOOL write_block(int block,BYTE *blockbuff, unsigned long nosectors)
{
   return fdc_rw(block,blockbuff,FALSE, nosectors);
}

/*
 * since reads and writes differ only by a few lines, this handles both.  This
 * function is called by read_block() and write_block()
 */
BOOL fdc_rw(int block,BYTE *blockbuff,BOOL read,unsigned long nosectors)
{
   int head,track,sector,tries, copycount = 0;
   unsigned char *p_tbaddr = (char *)0x80000;
   unsigned char *p_blockbuff = blockbuff;
   
   /* convert logical address into physical address */
   block2hts(block,&head,&track,&sector);
   
   /* spin up the disk */
   motoron();

   if (!read && blockbuff) {
      /* copy data from data buffer into track buffer */
      for(copycount=0; copycount<(nosectors*512); copycount++) {
      	*p_tbaddr = *p_blockbuff;
      	p_blockbuff++;
      	p_tbaddr++;
      }
   }
   
   for (tries = 0;tries < 3;tries++) {
      /* check for diskchange */
      if (inportb(FDC_DIR) & 0x80) {
	 dchange = TRUE;
	 seek(1);  /* clear "disk change" status */
	 recalibrate();
	 motoroff();
	 kprint("FDC: Disk change detected. Trying again.\n");
	 
	 return fdc_rw(block, blockbuff, read, nosectors);
      }
      /* move head to right track */
      if (!seek(track)) {
	 motoroff();
	 kprint("FDC: Error seeking to track\n");
	 return FALSE;
      }
      
      /* program data rate (500K/s) */
      outportb(FDC_CCR,0);
      
      /* send command */
      if (read) {
	 dma_xfer(2,tbaddr,nosectors*512,FALSE);
	 sendbyte(CMD_READ);
      } else {
	 dma_xfer(2,tbaddr,nosectors*512,TRUE);
	 sendbyte(CMD_WRITE);
      }
      
      sendbyte(head << 2);
      sendbyte(track);
      sendbyte(head);
      sendbyte(sector);
      sendbyte(2);               /* 512 bytes/sector */
      sendbyte(geometry.spt);
      if (geometry.spt == DG144_SPT)
	sendbyte(DG144_GAP3RW);  /* gap 3 size for 1.44M read/write */
      else
	sendbyte(DG168_GAP3RW);  /* gap 3 size for 1.68M read/write */
      sendbyte(0xff);            /* DTL = unused */
      
      /* wait for command completion */
      /* read/write don't need "sense interrupt status" */
      if (!waitfdc(TRUE)) {
	kprint("Timed out, trying operation again after reset()\n");
	reset();
	return fdc_rw(block, blockbuff, read, nosectors);
      }
      
      if ((status[0] & 0xc0) == 0) break;   /* worked! outta here! */

      recalibrate();  /* oops, try again... */
   }
   
   /* stop the motor */
   motoroff();

   if (read && blockbuff) {
      /* copy data from track buffer into data buffer */
      p_blockbuff = blockbuff;
      p_tbaddr = (char *) 0x80000;
      for(copycount=0; copycount<(nosectors*512); copycount++) {
      	*p_blockbuff = *p_tbaddr;
      	p_blockbuff++;
      	p_tbaddr++;
      }
   }

   return (tries != 3);
}

/* this formats a track, given a certain geometry */
BOOL format_track(BYTE track,DrvGeom *g)
{
   int i,h,r,r_id,split;
   BYTE tmpbuff[256];
   unsigned char *p_tbaddr = (char *)0x8000;
   unsigned int copycount = 0;

   /* check geometry */
   if (g->spt != DG144_SPT && g->spt != DG168_SPT)
     return FALSE;
   
   /* spin up the disk */
   motoron();

   /* program data rate (500K/s) */
   outportb(FDC_CCR,0);

   seek(track);  /* seek to track */

   /* precalc some constants for interleave calculation */
   split = g->spt / 2;
   if (g->spt & 1) split++;
   
   for (h = 0;h < g->heads;h++) {
      /* for each head... */
      
      /* check for diskchange */
      if (inportb(FDC_DIR) & 0x80) {
	 dchange = TRUE;
	 seek(1);  /* clear "disk change" status */
	 recalibrate();
	 motoroff();
	 return FALSE;
      }

      i = 0;   /* reset buffer index */
      for (r = 0;r < g->spt;r++) {
	 /* for each sector... */

	 /* calculate 1:2 interleave (seems optimal in my system) */
	 r_id = r / 2 + 1;
	 if (r & 1) r_id += split;
	 
	 /* add some head skew (2 sectors should be enough) */
	 if (h & 1) {
	    r_id -= 2;
	    if (r_id < 1) r_id += g->spt;
	 }
      
	 /* add some track skew (1/2 a revolution) */
	 if (track & 1) {
	    r_id -= g->spt / 2;
	    if (r_id < 1) r_id += g->spt;
	 }
	 
	 /**** interleave now calculated - sector ID is stored in r_id ****/

	 /* fill in sector ID's */
	 tmpbuff[i++] = track;
	 tmpbuff[i++] = h;
	 tmpbuff[i++] = r_id;
	 tmpbuff[i++] = 2;
      }

      /* copy sector ID's to track buffer */
      for(copycount = 0; copycount<i; copycount++) {
      	*p_tbaddr = tmpbuff[copycount];
      	p_tbaddr++;
      }
//      movedata(_my_ds(),(long)tmpbuff,_dos_ds,tbaddr,i);
      
      /* start dma xfer */
      dma_xfer(2,tbaddr,i,TRUE);
      
      /* prepare "format track" command */
      sendbyte(CMD_FORMAT);
      sendbyte(h << 2);
      sendbyte(2);
      sendbyte(g->spt);
      if (g->spt == DG144_SPT)      
	sendbyte(DG144_GAP3FMT);    /* gap3 size for 1.44M format */
      else
	sendbyte(DG168_GAP3FMT);    /* gap3 size for 1.68M format */
      sendbyte(0);     /* filler byte */
	 
      /* wait for command to finish */
      if (!waitfdc(FALSE))
	return FALSE;
      
      if (status[0] & 0xc0) {
	 motoroff();
	 return FALSE;
      }
   }
   
   motoroff();
   
   return TRUE;
}


asm (
   ".globl _int1c        \n"
   "_int1c:              \n"
   "   pusha               \n" /* Save all registers               */
   "   pushw %ds           \n" /* Set up the data segment          */
   "   pushw %es           \n"
   "   pushw %ss           \n" /* Note that ss is always valid     */
   "   pushw %ss           \n"
   "   popw %ds            \n"
   "   popw %es            \n"
   "                       \n"
   "   call int1c          \n"
   "                       \n"
   "   popw %es            \n"
   "   popw %ds            \n" /* Restore registers                */
   "   popa                \n"
   "   iret                \n" /* Exit interrupt                   */
);

asm (
   ".globl floppy_ISR        \n"
   "floppy_ISR:              \n"
   "   pusha               \n" /* Save all registers               */
   "   pushw %ds           \n" /* Set up the data segment          */
   "   pushw %es           \n"
   "   pushw %ss           \n" /* Note that ss is always valid     */
   "   pushw %ss           \n"
   "   popw %ds            \n"
   "   popw %es            \n"
   "                       \n"
   "   call irq6           \n"
   "                       \n"
   "   popw %es            \n"
   "   popw %ds            \n" /* Restore registers                */
   "   popa                \n"
   "   iret                \n" /* Exit interrupt                   */
);