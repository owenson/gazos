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

#include "video.h"
#include "io.h"
#include "serial.h"
#include "bootinfo.h"

unsigned char *VIDEO_MEMORY = (char *)0xB8000;

/* ------------------------------------------------------------------ */
/* Framebuffer text console, used when booted via UEFI (no VGA text    */
/* mode). An 80x25 grid of 8x16 glyphs, centred on the screen, so it   */
/* looks and behaves like the VGA console. 'shadow' holds the text so  */
/* we can scroll and move the cursor by redrawing, without reading the */
/* (possibly slow, uncached) framebuffer.                              */
/* ------------------------------------------------------------------ */

#define FB_COLS	80
#define FB_ROWS	25
#define FB_FG	0x00AAAAAA		/* VGA light grey (same in RGB/BGR) */
#define FB_BG	0x00000000

extern const unsigned char font8x16[256][16];

static int fb_on = 0;
static volatile unsigned long *fb_text;	/* top-left pixel of the text area */
static unsigned long fb_stride;		/* pixels per scan line */
static unsigned int fb_x = 0, fb_y = 0;
static unsigned char fb_shadow[FB_ROWS][FB_COLS];

static void fb_draw(unsigned int x, unsigned int y, int cursor)
{
   volatile unsigned long *p = fb_text + (y * 16) * fb_stride + x * 8;
   const unsigned char *glyph = font8x16[fb_shadow[y][x]];
   unsigned int row, bit, bits;

   for(row = 0; row < 16; row++, p += fb_stride)
   {
      bits = (cursor && row >= 14) ? 0xFF : glyph[row];
      for(bit = 0; bit < 8; bit++)
         p[bit] = (bits & (0x80 >> bit)) ? FB_FG : FB_BG;
   }
}

static void fb_redraw(void)
{
   unsigned int x, y;
   for(y = 0; y < FB_ROWS; y++)
      for(x = 0; x < FB_COLS; x++)
         fb_draw(x, y, 0);
}

static void fb_putc(unsigned char c)
{
   unsigned int x;

   switch(c)
   {
      case '\n': fb_x = 0; fb_y++; break;
      case '\r': fb_x = 0; break;
      case '\t': fb_x = (fb_x + 4 < FB_COLS) ? fb_x + 4 : FB_COLS - 1; break;
      case 8:				/* backspace: erase previous char */
         if(fb_x > 0) fb_x--;
         else if(fb_y > 0) { fb_y--; fb_x = FB_COLS - 1; }
         fb_shadow[fb_y][fb_x] = ' ';
         fb_draw(fb_x, fb_y, 0);
         break;
      default:
         fb_shadow[fb_y][fb_x] = c;
         fb_draw(fb_x, fb_y, 0);
         if(++fb_x >= FB_COLS) { fb_x = 0; fb_y++; }
         break;
   }

   if(fb_y >= FB_ROWS)		/* scroll up one line */
   {
      unsigned char *sh = &fb_shadow[0][0];
      for(x = 0; x < (FB_ROWS - 1) * FB_COLS; x++)
         sh[x] = sh[x + FB_COLS];
      for(x = 0; x < FB_COLS; x++)
         fb_shadow[FB_ROWS - 1][x] = ' ';
      fb_y = FB_ROWS - 1;
      fb_redraw();
   }
}

static void fb_kprint(char *string)
{
   unsigned char c;

   fb_draw(fb_x, fb_y, 0);		/* remove the cursor */
   while((c = *string++))
   {
      serial_putc(c);
      fb_putc(c);
   }
   fb_draw(fb_x, fb_y, 1);		/* and draw it at its new place */
}

static void fb_cls(void)
{
   unsigned int x;
   unsigned char *sh = &fb_shadow[0][0];
   for(x = 0; x < FB_ROWS * FB_COLS; x++)
      sh[x] = ' ';
   fb_x = fb_y = 0;
   fb_redraw();
   fb_draw(0, 0, 1);
}

/* Switch to the framebuffer console if the boot loader gave us one.
 * Returns 1 if it is in use. */
int video_init(void)
{
   unsigned long i, total;
   volatile unsigned long *fb;

   if(!boot_info || !(boot_info->flags & BOOTINFO_FB) ||
      boot_info->fb_width < FB_COLS * 8 || boot_info->fb_height < FB_ROWS * 16)
      return 0;

   fb = (volatile unsigned long *) boot_info->fb_addr;
   fb_stride = boot_info->fb_pitch / 4;
   total = fb_stride * boot_info->fb_height;
   for(i = 0; i < total; i++)		/* clear the whole screen */
      fb[i] = FB_BG;

   fb_text = fb + ((boot_info->fb_height - FB_ROWS * 16) / 2) * fb_stride
                + (boot_info->fb_width - FB_COLS * 8) / 2;
   fb_on = 1;
   fb_cls();
   return 1;
}

void kprint(char *string)		/* Print text to the screen	*/
{
   unsigned int curchar, vidmem_off, i;

   if(fb_on)
   {
      fb_kprint(string);
      return;
   }

   outportb(0x3d4, 0x0e);		/* Get cursor Y position	*/
   vidmem_off = inportb(0x3d5);
   vidmem_off <<= 8;
   outportb(0x3d4, 0x0f);		/* And add cursor X position	*/
   vidmem_off += inportb(0x3d5);
   vidmem_off <<= 1;

   while((curchar=*string++))		/* Loop through the string	*/
   {
      serial_putc(curchar);		/* Mirror to the serial console */
      switch(curchar)			/* Is it a special character ?  */
      {
         case '\n':			/* Newline found		*/
            vidmem_off = (vidmem_off/160)*160 + 160;
            break;

         case '\r':			/* Carriage return found	*/
            vidmem_off = (vidmem_off/160)*160;
            break;

	case '\t':
	    vidmem_off += 8;
	    break;
	 
	case 8:/* Delete */
	    vidmem_off-=2;
	    VIDEO_MEMORY[vidmem_off] = 0x20;
	    break;

         default:			/* Normal character             */
            VIDEO_MEMORY[vidmem_off++] = curchar;
            VIDEO_MEMORY[vidmem_off++] = 0x07;
            break;
      }

      if(vidmem_off >= 160*25)		/* Are we off-screen ?		*/
      {
         for(i = 0; i < 160*24; i++)	/* Scroll the screen up		*/
         {
            VIDEO_MEMORY[i] = VIDEO_MEMORY[i+160];
         }
         for(i = 0; i < 80; i++)	/* Empty the bottom row		*/
         {
            VIDEO_MEMORY[(160*24)+(i*2)] = 0x20;
            VIDEO_MEMORY[(160*24)+(i*2)+1] = 0x07;
         }
         vidmem_off -= 160;		/* We're on the bottom row	*/
      }
   }

   vidmem_off >>= 1;			/* Set the new cursor position  */
   outportb(0x3d4, 0x0f);
   outportb(0x3d5, vidmem_off & 0x0ff);
   outportw(0x3d4, 0x0e);
   outportb(0x3d5, vidmem_off >> 8);
}

void cls(void)				/* Clear the screen		*/
{
   unsigned int i;

   if(fb_on)
   {
      fb_cls();
      return;
   }

   for(i = 0; i < (80*25); i++)         /* Fill the screen with         */
   {					/* background color		*/
      VIDEO_MEMORY[i*2] = 0x20;
      VIDEO_MEMORY[i*2+1] = 0x07;
   }

   outportb(0x3d4, 0x0f);		/* Set the cursor to the	*/
   outportb(0x3d5, 0);			/* upper-left corner of the	*/
   outportw(0x3d4, 0x0e);		/* screen			*/
   outportb(0x3d5, 0);
}

void printlong(unsigned long i)  /* Convert a dword to a string  */
{
   unsigned char backstr[11], j=0, l=0, m=0;
   unsigned char str[255];

   do					/* Convert string one digit at	*/
   {					/* a time			*/
      backstr[j++] = (i % 10) + '0';	/* Put a digit in backstr	*/
      i /= 10;				/* Next digit			*/
   }					/* And continue until there are */
   while(i);				/* no more digits left		*/

   backstr[j] = '\0';			/* End of the string		*/

   for(l=j-1; m<j; l--)			/* Backstr is backwards (last   */
   {					/* digit first.)  Now we flip   */
      str[m++] = backstr[l];		/* it around...			*/
   }					/* ... and it's ready !!!	*/

   str[j] = '\0';			/* Put the string end on it	*/

   kprint(str);
}