/*
GazOS Operating System
Copyright (C) 2026  Gareth Owenson <gareth.owenson@gmail.com>
Serial console (COM1): mirrors screen output for logging and testing.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/

#include "serial.h"
#include "io.h"

#define COM1		0x3F8
#define UART_DATA	0	/* (DLAB=1: divisor low byte) */
#define UART_IER	1	/* (DLAB=1: divisor high byte) */
#define UART_FCR	2
#define UART_LCR	3
#define UART_MCR	4
#define UART_LSR	5
#define UART_SCRATCH	7
#define LSR_THR_EMPTY	0x20

static int serial_ok = 0;

void serial_init(void)
{
	/* Is there a UART at all? Check the scratch register holds a value. */
	outportb(COM1 + UART_SCRATCH, 0xA5);
	if(inportb(COM1 + UART_SCRATCH) != 0xA5)
		return;

	outportb(COM1 + UART_IER, 0x00);	/* no interrupts */
	outportb(COM1 + UART_LCR, 0x80);	/* DLAB on: set baud divisor */
	outportb(COM1 + UART_DATA, 0x01);	/* 115200 / 1 */
	outportb(COM1 + UART_IER, 0x00);
	outportb(COM1 + UART_LCR, 0x03);	/* 8 bits, no parity, 1 stop */
	outportb(COM1 + UART_FCR, 0xC7);	/* enable + clear FIFOs */
	outportb(COM1 + UART_MCR, 0x03);	/* DTR + RTS */
	serial_ok = 1;
}

void serial_putc(char c)
{
	unsigned long t;

	if(!serial_ok)
		return;
	if(c == '\n')
		serial_putc('\r');
	for(t = 0; t < 100000 && !(inportb(COM1 + UART_LSR) & LSR_THR_EMPTY); t++);
	outportb(COM1 + UART_DATA, c);
}
