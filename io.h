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

#ifndef IO_INCLUDE
#define IO_INCLUDE

inline unsigned char inportb(unsigned int port);
inline void outportb(unsigned int port, unsigned char value);
inline void outportw(unsigned int port, unsigned int value);
inline unsigned char inportb_p(unsigned int port);
inline void outportb_p(unsigned int port, unsigned char value);
inline void outportw_p(unsigned int port, unsigned int value);

#endif
