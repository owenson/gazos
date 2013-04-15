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

unsigned long floor (double __x)
{
  register double __value;
  __volatile unsigned short int __cw, __cwtmp;

  __asm __volatile ("fnstcw %0" : "=m" (__cw));
  __cwtmp = (__cw & 0xf3ff) | 0x0400; /* rounding down */
  __asm __volatile ("fldcw %0" : : "m" (__cwtmp));
  __asm __volatile ("frndint" : "=t" (__value) : "0" (__x));
  __asm __volatile ("fldcw %0" : : "m" (__cw));

  return __value;
}

unsigned long ceil (double __x)
{
  register double __value;
  __volatile unsigned short int __cw, __cwtmp;

  __asm __volatile ("fnstcw %0" : "=m" (__cw));
  __cwtmp = (__cw & 0xf3ff) | 0x0800; /* rounding up */
  __asm __volatile ("fldcw %0" : : "m" (__cwtmp));
  __asm __volatile ("frndint" : "=t" (__value) : "0" (__x));
  __asm __volatile ("fldcw %0" : : "m" (__cw));

  return __value;
}