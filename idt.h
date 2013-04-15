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

#ifndef INCLUDE_IDT
#define INCLUDE_IDT

void set_vector(void *handler, unsigned char interrupt, unsigned short control_major);
void InitIDT();

void int_null();

void _int0();
void _int1();
void _int2();
void _int3();
void _int4();
void _int5();
void _int6();
void _int7();
void _int8();
void _int9();
void _int10();
void _int11();
void _int12();
void _int13();
void _int14();
void _int15();
void _int16();

#endif
