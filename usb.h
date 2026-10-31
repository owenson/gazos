/*
GazOS Operating System
Copyright (C) 2026  Gareth Owenson <gareth.owenson@gmail.com>
USB support: xHCI host controller + HID boot-protocol keyboard.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/

#ifndef INCLUDE_USB
#define INCLUDE_USB

/* Find an xHCI controller and a USB keyboard on one of its root ports.
 * Returns 1 if a keyboard is ready. Call after calibrateDelayLoop(). */
int usb_init(void);

/* Process pending controller events; keystrokes from a USB keyboard go
 * into the same buffer as PS/2 keys. Cheap when there's nothing to do.
 * Called from getch() while it waits for a key. */
void usb_poll(void);

extern int usb_keyboard_present;

#endif
