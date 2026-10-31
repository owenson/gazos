/*
GazOS Operating System
Copyright (C) 2026  Gareth Owenson <gareth.owenson@gmail.com>
Serial console (COM1): mirrors screen output for logging and testing.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/

#ifndef INCLUDE_SERIAL
#define INCLUDE_SERIAL

void serial_init(void);		/* 115200 8N1; does nothing if no UART */
void serial_putc(char c);	/* no-op until serial_init found a UART */

#endif
