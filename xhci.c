/*
GazOS Operating System
Copyright (C) 2026  Gareth Owenson <gareth.owenson@gmail.com>
xHCI (USB 3) host controller driver and USB HID boot keyboard driver.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

Scope: the first xHCI controller, devices on its root ports (no hubs),
and the first HID keyboard found, used in the HID "boot protocol" (the
fixed 8-byte report every USB keyboard supports for BIOSes).

Everything is polled: the controller's interrupter is set up but its
interrupts are not enabled; usb_poll() reads the event ring. As with the
AHCI driver, paging is off, so the static buffers below can be handed to
the controller as physical addresses.

Data structures (xHCI spec 1.2, chapter 6):
  - DCBAA: array of pointers to each slot's output Device Context
  - Command ring / Transfer rings: arrays of 16-byte TRBs, ending in a
    Link TRB back to the start; ownership is tracked by a cycle bit
  - Event ring: TRBs written by the controller (command completions,
    transfer completions, port changes), described by a one-entry ERST
*/

#include "usb.h"
#include "pci.h"
#include "video.h"
#include "delay.h"
#include "keyboard.h"
#include "io.h"
#include "paging.h"

#define MAX_SCRATCH	32	/* scratchpad pages we can give the controller */
#define MAX_DEVS	8	/* devices we'll enumerate */
#define RING_TRBS	64	/* TRBs per command/transfer ring */
#define EVENT_TRBS	64

/* ---- registers ---- */

/* capability registers */
#define CAP_CAPLENGTH	0x00
#define CAP_HCSPARAMS1	0x04
#define CAP_HCSPARAMS2	0x08
#define CAP_HCCPARAMS1	0x10
#define CAP_DBOFF	0x14
#define CAP_RTSOFF	0x18

/* operational registers */
#define OP_USBCMD	0x00
#define OP_USBSTS	0x04
#define OP_PAGESIZE	0x08
#define OP_CRCR		0x18
#define OP_DCBAAP	0x30
#define OP_CONFIG	0x38
#define OP_PORTSC(n)	(0x400 + 0x10 * (n))	/* n = 0 .. MaxPorts-1 */

#define CMD_RS		0x0001
#define CMD_HCRST	0x0002
#define STS_HCH		0x0001
#define STS_CNR		0x0800

/* interrupter 0 (in the runtime registers) */
#define IR0_IMAN	0x20
#define IR0_ERSTSZ	0x28
#define IR0_ERSTBA	0x30
#define IR0_ERDP	0x38

/* PORTSC */
#define PORT_CCS	(1UL << 0)	/* device connected */
#define PORT_PED	(1UL << 1)	/* port enabled (write 1 = disable!) */
#define PORT_PR		(1UL << 4)	/* port reset */
#define PORT_PP		(1UL << 9)	/* port power */
#define PORT_SPEED(p)	(((p) >> 10) & 0xF)
#define PORT_CHANGES	(0x7FUL << 17)	/* CSC PEC WRC OCC PRC PLC CEC: write 1 to clear */
#define PORT_PRC	(1UL << 21)

/* speeds (default Protocol Speed IDs) */
#define SPEED_FULL	1
#define SPEED_LOW	2
#define SPEED_HIGH	3
#define SPEED_SUPER	4

/* TRB types */
#define TRB_NORMAL	1
#define TRB_SETUP	2
#define TRB_DATA	3
#define TRB_STATUS	4
#define TRB_LINK	6
#define TRB_ENABLE_SLOT	9
#define TRB_ADDRESS_DEV	11
#define TRB_CONFIG_EP	12
#define TRB_EVAL_CTX	13
#define TRB_TRANSFER_EV	32
#define TRB_CMD_DONE_EV	33

#define TRB_TYPE(t)	((unsigned long)(t) << 10)
#define TRB_GET_TYPE(c)	(((c) >> 10) & 0x3F)
#define TRB_CYCLE	(1UL << 0)
#define TRB_TC		(1UL << 1)	/* link TRB: toggle cycle */
#define TRB_ISP		(1UL << 2)	/* interrupt on short packet */
#define TRB_IOC		(1UL << 5)	/* interrupt (= event) on completion */
#define TRB_IDT		(1UL << 6)	/* immediate data (setup stage) */
#define TRB_DIR_IN	(1UL << 16)

#define CC_SUCCESS	1
#define CC_SHORT_PACKET	13

/* ---- memory shared with the controller ---- */

typedef struct
{
	volatile unsigned long param_lo, param_hi, status, control;
} trb_t;

typedef struct
{
	trb_t *trbs;
	unsigned int enqueue;
	unsigned long cycle;
} ring_t;

static unsigned long long dcbaa[256] __attribute__ ((aligned(64)));
static unsigned long long scratch_array[MAX_SCRATCH] __attribute__ ((aligned(64)));
static unsigned char scratch_pages[MAX_SCRATCH][4096] __attribute__ ((aligned(4096)));
static trb_t cmd_trbs[RING_TRBS] __attribute__ ((aligned(4096)));
static trb_t event_trbs[EVENT_TRBS] __attribute__ ((aligned(4096)));
static volatile unsigned long erst[4] __attribute__ ((aligned(64)));
static unsigned char out_ctx[MAX_DEVS][4096] __attribute__ ((aligned(4096)));
static unsigned char in_ctx[4096] __attribute__ ((aligned(4096)));
static trb_t ep0_trbs[MAX_DEVS][RING_TRBS] __attribute__ ((aligned(4096)));
static trb_t kbd_trbs[RING_TRBS] __attribute__ ((aligned(4096)));
static unsigned char ctrl_buf[512] __attribute__ ((aligned(64)));
static unsigned char kbd_report[8] __attribute__ ((aligned(64)));

/* ---- driver state ---- */

static volatile unsigned char *cap, *op, *rt, *db;
static unsigned int max_slots, max_ports, ctx_size;
static ring_t cmd_ring, ep0_ring[MAX_DEVS], kbd_ring;
static unsigned int ev_dequeue;
static unsigned long ev_cycle;
static int ndevs = 0;
static int running = 0;

/* most recent completions, set by process_event() */
static volatile int cmd_done, cmd_cc, cmd_slot;
static volatile int xfer_done, xfer_cc, xfer_slot, xfer_ep;

int usb_keyboard_present = 0;
static int kbd_slot, kbd_dci;
static unsigned char kbd_prev[8];
static int kbd_caps = 0;

static void handle_report(void);

/* ---- helpers ---- */

static inline unsigned long rd(volatile unsigned char *base, unsigned int reg)
{
	return *(volatile unsigned long *)(base + reg);
}

static inline void wr(volatile unsigned char *base, unsigned int reg, unsigned long v)
{
	*(volatile unsigned long *)(base + reg) = v;
}

static inline void wr64(volatile unsigned char *base, unsigned int reg, unsigned long v)
{
	wr(base, reg, v);		/* we only use 32-bit physical addresses */
	wr(base, reg + 4, 0);
}

static void zero(void *p, unsigned int n)
{
	unsigned char *c = p;
	while(n--) *c++ = 0;
}

#define barrier()	asm volatile ("" ::: "memory")

/* wait (up to 'ms' milliseconds) for (reg & mask) == value */
static int wait_reg(volatile unsigned char *base, unsigned int reg,
		    unsigned long mask, unsigned long value, unsigned int ms)
{
	while((rd(base, reg) & mask) != value)
	{
		if(ms-- == 0)
			return 0;
		delay(1);
	}
	return 1;
}

/* ---- rings ---- */

static void ring_init(ring_t *r, trb_t *trbs)
{
	zero(trbs, RING_TRBS * sizeof(trb_t));
	r->trbs = trbs;
	r->enqueue = 0;
	r->cycle = 1;
	/* last TRB links back to the first and flips the cycle state */
	trbs[RING_TRBS - 1].param_lo = (unsigned long)trbs;
	trbs[RING_TRBS - 1].control = TRB_TYPE(TRB_LINK) | TRB_TC;
}

/* Put a TRB on a ring; returns its address. The cycle bit is written
 * last: that hands the TRB to the controller. */
static unsigned long ring_push(ring_t *r, unsigned long p_lo, unsigned long p_hi,
			       unsigned long status, unsigned long control)
{
	trb_t *t = &r->trbs[r->enqueue];

	t->param_lo = p_lo;
	t->param_hi = p_hi;
	t->status = status;
	barrier();
	t->control = (control & ~TRB_CYCLE) | r->cycle;

	if(++r->enqueue == RING_TRBS - 1)	/* reached the link TRB */
	{
		trb_t *link = &r->trbs[RING_TRBS - 1];
		barrier();
		link->control = (link->control & ~TRB_CYCLE) | r->cycle;
		r->cycle ^= 1;
		r->enqueue = 0;
	}
	return (unsigned long)t;
}

static inline void ring_doorbell(unsigned int slot, unsigned int target)
{
	barrier();
	wr(db, slot * 4, target);
}

/* ---- events ---- */

static void process_event(trb_t *e)
{
	unsigned int type = TRB_GET_TYPE(e->control);
	int cc = e->status >> 24;
	int slot = e->control >> 24;

	if(type == TRB_CMD_DONE_EV)
	{
		cmd_cc = cc;
		cmd_slot = slot;
		cmd_done = 1;
	}
	else if(type == TRB_TRANSFER_EV)
	{
		int ep = (e->control >> 16) & 0x1F;

		if(usb_keyboard_present && slot == kbd_slot && ep == kbd_dci)
		{
			if(cc == CC_SUCCESS || cc == CC_SHORT_PACKET)
			{
				handle_report();
				/* queue the next report */
				ring_push(&kbd_ring, (unsigned long)kbd_report, 0, 8,
					  TRB_TYPE(TRB_NORMAL) | TRB_IOC | TRB_ISP);
				ring_doorbell(kbd_slot, kbd_dci);
			}
			else
			{
				kprint("USB keyboard: transfer error, keyboard disabled\n");
				usb_keyboard_present = 0;
			}
			return;
		}
		xfer_cc = cc;
		xfer_slot = slot;
		xfer_ep = ep;
		xfer_done = 1;
	}
	/* port status change and other events: nothing to do */
}

/* Handle all events the controller has posted. */
static void poll_events(void)
{
	int n = 0;

	for(;;)
	{
		trb_t *e = &event_trbs[ev_dequeue];
		if((e->control & TRB_CYCLE) != ev_cycle)
			break;		/* not written by the controller yet */
		barrier();
		process_event(e);
		n++;
		if(++ev_dequeue == EVENT_TRBS)
		{
			ev_dequeue = 0;
			ev_cycle ^= 1;
		}
	}
	if(n)	/* tell the controller how far we got (and clear EHB) */
		wr64(rt, IR0_ERDP, (unsigned long)&event_trbs[ev_dequeue] | 0x8);
}

/* Run a command; returns the completion code (CC_SUCCESS = 1). */
static int run_command(unsigned long p_lo, unsigned long control, int *slot_out)
{
	int ms;

	cmd_done = 0;
	ring_push(&cmd_ring, p_lo, 0, 0, control);
	ring_doorbell(0, 0);
	for(ms = 0; ms < 1000; ms++)
	{
		poll_events();
		if(cmd_done)
		{
			if(slot_out)
				*slot_out = cmd_slot;
			return cmd_cc;
		}
		delay(1);
	}
	return -1;
}

/* ---- control transfers on endpoint 0 ---- */

static int control(int dev, int slot, unsigned char type, unsigned char request,
		   unsigned int value, unsigned int index, unsigned int length)
{
	ring_t *r = &ep0_ring[dev];
	int in = type & 0x80;
	int ms;

	/* setup stage: the 8-byte request goes in the TRB itself (IDT) */
	ring_push(r, type | (request << 8) | ((unsigned long)value << 16),
		  index | ((unsigned long)length << 16), 8,
		  TRB_TYPE(TRB_SETUP) | TRB_IDT |
		  (length ? (in ? (3UL << 16) : (2UL << 16)) : 0));
	if(length)
		ring_push(r, (unsigned long)ctrl_buf, 0, length,
			  TRB_TYPE(TRB_DATA) | (in ? TRB_DIR_IN : 0));
	ring_push(r, 0, 0, 0, TRB_TYPE(TRB_STATUS) | TRB_IOC |
		  ((length && in) ? 0 : TRB_DIR_IN));

	xfer_done = 0;
	ring_doorbell(slot, 1);
	for(ms = 0; ms < 1000; ms++)
	{
		poll_events();
		if(xfer_done && xfer_slot == slot)
			return xfer_cc == CC_SUCCESS || xfer_cc == CC_SHORT_PACKET;
		delay(1);
	}
	return 0;
}

/* ---- device setup ---- */

static unsigned long *ctx(unsigned char *base, int index)
{
	return (unsigned long *)(base + index * ctx_size);
}

/* Configure the keyboard's interrupt IN endpoint and start polling it. */
static int setup_keyboard(int dev, int slot, int port, int speed,
			  unsigned char ep_addr, unsigned int mps, unsigned int binterval)
{
	int dci = (ep_addr & 0x0F) * 2 + 1;	/* IN endpoint context index */
	unsigned long *icc, *sc, *ec;
	unsigned int interval = 0, i;

	/* xHCI interval = 2^n * 125us. FS/LS bInterval is in 1ms frames,
	 * HS/SS bInterval is already an exponent (2^(b-1) microframes). */
	if(speed == SPEED_FULL || speed == SPEED_LOW)
	{
		for(i = binterval * 8; i > 1; i >>= 1)
			interval++;
		if(interval < 3) interval = 3;
		if(interval > 10) interval = 10;
	}
	else
		interval = binterval ? binterval - 1 : 0;

	ring_init(&kbd_ring, kbd_trbs);

	zero(in_ctx, sizeof(in_ctx));
	icc = ctx(in_ctx, 0);
	sc = ctx(in_ctx, 1);
	ec = ctx(in_ctx, dci + 1);
	icc[1] = (1UL << 0) | (1UL << dci);		/* add: slot, endpoint */
	sc[0] = ((unsigned long)speed << 20) | ((unsigned long)dci << 27);
	sc[1] = (unsigned long)port << 16;
	ec[0] = (unsigned long)interval << 16;
	ec[1] = (3UL << 1) | (7UL << 3) | ((unsigned long)mps << 16);	/* CErr 3, Interrupt IN */
	ec[2] = (unsigned long)kbd_trbs | 1;		/* dequeue pointer, DCS = 1 */
	ec[3] = 0;
	ec[4] = 8 | ((unsigned long)mps << 16);		/* avg TRB length, max ESIT payload */

	if(run_command((unsigned long)in_ctx,
		       TRB_TYPE(TRB_CONFIG_EP) | ((unsigned long)slot << 24), 0) != CC_SUCCESS)
		return 0;

	kbd_slot = slot;
	kbd_dci = dci;
	zero(kbd_prev, sizeof(kbd_prev));
	usb_keyboard_present = 1;

	ring_push(&kbd_ring, (unsigned long)kbd_report, 0, 8,
		  TRB_TYPE(TRB_NORMAL) | TRB_IOC | TRB_ISP);
	ring_doorbell(slot, dci);
	return 1;
}

/* Address a newly connected device; set it up if it's a keyboard. */
static void enumerate(int port, int speed)
{
	int dev = ndevs, slot = 0;
	unsigned long *icc, *sc, *ep0;
	unsigned int mps0, total, off, cfg_value, i;
	int kbd_if = -1, in_kbd_if = 0;
	unsigned char kbd_ep = 0;
	unsigned int kbd_mps = 8, kbd_interval = 10;

	if(dev >= MAX_DEVS || run_command(0, TRB_TYPE(TRB_ENABLE_SLOT), &slot) != CC_SUCCESS
	   || slot <= 0 || slot > (int)max_slots)
		return;
	ndevs++;

	zero(out_ctx[dev], sizeof(out_ctx[dev]));
	dcbaa[slot] = (unsigned long)out_ctx[dev];
	ring_init(&ep0_ring[dev], ep0_trbs[dev]);

	/* default max packet size for endpoint 0, by speed */
	mps0 = speed == SPEED_SUPER ? 512 : speed == SPEED_HIGH ? 64 : 8;

	zero(in_ctx, sizeof(in_ctx));
	icc = ctx(in_ctx, 0);
	sc = ctx(in_ctx, 1);
	ep0 = ctx(in_ctx, 2);
	icc[1] = (1UL << 0) | (1UL << 1);		/* add: slot, EP0 */
	sc[0] = ((unsigned long)speed << 20) | (1UL << 27);	/* 1 context entry */
	sc[1] = (unsigned long)port << 16;		/* root hub port number */
	ep0[1] = (3UL << 1) | (4UL << 3) | ((unsigned long)mps0 << 16);	/* CErr 3, Control */
	ep0[2] = (unsigned long)ep0_trbs[dev] | 1;	/* dequeue pointer, DCS = 1 */
	ep0[3] = 0;
	ep0[4] = 8;					/* average TRB length */

	if(run_command((unsigned long)in_ctx, TRB_TYPE(TRB_ADDRESS_DEV) |
		       ((unsigned long)slot << 24), 0) != CC_SUCCESS)
	{
		kprint("  USB: port ");
		printlong(port);
		kprint(": device didn't accept an address\n");
		return;
	}
	delay(2);

	/* the first 8 bytes of the device descriptor give EP0's real max
	 * packet size; update the context if our guess was wrong */
	if(!control(dev, slot, 0x80, 6, 0x0100, 0, 8))
		return;
	i = speed == SPEED_SUPER ? (1U << ctrl_buf[7]) : ctrl_buf[7];
	if(i && i != mps0)
	{
		zero(in_ctx, sizeof(in_ctx));
		icc[1] = 1UL << 1;			/* evaluate EP0 */
		ep0[1] = (3UL << 1) | (4UL << 3) | ((unsigned long)i << 16);
		run_command((unsigned long)in_ctx, TRB_TYPE(TRB_EVAL_CTX) |
			    ((unsigned long)slot << 24), 0);
	}

	/* configuration descriptor: header first, then all of it */
	if(!control(dev, slot, 0x80, 6, 0x0200, 0, 9))
		return;
	total = ctrl_buf[2] | (ctrl_buf[3] << 8);
	if(total > sizeof(ctrl_buf))
		total = sizeof(ctrl_buf);
	if(!control(dev, slot, 0x80, 6, 0x0200, 0, total))
		return;
	cfg_value = ctrl_buf[5];

	/* look for a HID boot keyboard interface and its interrupt IN endpoint */
	for(off = 0; off + 2 <= total && ctrl_buf[off] >= 2; off += ctrl_buf[off])
	{
		unsigned char *d = ctrl_buf + off;
		if(d[1] == 4)				/* interface */
		{
			in_kbd_if = (d[5] == 3 && d[6] == 1 && d[7] == 1);
			if(in_kbd_if && kbd_if < 0)
				kbd_if = d[2];
			else
				in_kbd_if = 0;
		}
		else if(d[1] == 5 && in_kbd_if && !kbd_ep &&	/* endpoint */
			(d[2] & 0x80) && (d[3] & 3) == 3)
		{
			kbd_ep = d[2];
			kbd_mps = (d[4] | (d[5] << 8)) & 0x7FF;
			kbd_interval = d[6];
		}
	}

	kprint("  USB: port ");
	printlong(port);
	kprint(speed == SPEED_SUPER ? " (SuperSpeed)" : speed == SPEED_HIGH ? " (high speed)" :
	       speed == SPEED_LOW ? " (low speed)" : " (full speed)");

	if(kbd_if < 0 || !kbd_ep || usb_keyboard_present)
	{
		kprint(kbd_if < 0 ? ": not a keyboard, ignored\n" : ": second keyboard, ignored\n");
		return;
	}

	if(!control(dev, slot, 0x00, 9, cfg_value, 0, 0) ||	/* SET_CONFIGURATION */
	   !control(dev, slot, 0x21, 0x0B, 0, kbd_if, 0))	/* SET_PROTOCOL: boot */
	{
		kprint(": keyboard setup failed\n");
		return;
	}
	control(dev, slot, 0x21, 0x0A, 0, kbd_if, 0);	/* SET_IDLE: only report changes */

	if(setup_keyboard(dev, slot, port, speed, kbd_ep, kbd_mps, kbd_interval))
		kprint(": USB keyboard ready\n");
	else
		kprint(": couldn't configure the keyboard endpoint\n");
}

/* Take the controller from the BIOS/UEFI firmware (USB Legacy Support
 * extended capability), so it stops emulating a PS/2 keyboard via SMIs. */
static void bios_handoff(void)
{
	unsigned long xecp = (rd(cap, CAP_HCCPARAMS1) >> 16) << 2;
	int guard = 0;

	while(xecp && guard++ < 64)
	{
		volatile unsigned char *c = cap + xecp;
		unsigned long v = rd(c, 0);

		if((v & 0xFF) == 1)			/* USB Legacy Support */
		{
			wr(c, 0, v | (1UL << 24));	/* OS owned semaphore */
			if(!wait_reg(c, 0, (1UL << 16), 0, 1000))	/* BIOS lets go */
				wr(c, 0, (rd(c, 0) & ~(1UL << 16)) | (1UL << 24));
			wr(c, 4, 0xE0000000UL);		/* SMIs off, clear their status */
			return;
		}
		if(!((v >> 8) & 0xFF))
			return;
		xecp += ((v >> 8) & 0xFF) << 2;
	}
}

int usb_init(void)
{
	pci_dev dev;
	unsigned long bar, hcs1, hcs2, port, i, nscratch;
	unsigned long long bar64;

	kprint("Initialising USB (xHCI)... ");

	/* class 0C (serial bus), subclass 03 (USB), prog-if 30 (xHCI) */
	if(!pci_find_class(0x0C, 0x03, 0x30, &dev))
	{
		kprint("no xHCI controller\n");
		return 0;
	}
	pci_write(&dev, PCI_COMMAND,
		  pci_read(&dev, PCI_COMMAND) | PCI_CMD_MEMORY | PCI_CMD_MASTER);
	bar = pci_read(&dev, 0x10);
	bar64 = bar & 0xFFFFFFF0UL;
	if((bar & 0x6) == 0x4)			/* 64-bit BAR: high half in BAR1 */
		bar64 |= (unsigned long long)pci_read(&dev, 0x14) << 32;
	/* UEFI firmware often puts 64-bit BARs above 4GB; map_device()
	 * switches on PAE paging to reach them */
	cap = (volatile unsigned char *)map_device(bar64, 0x10000);
	if(!cap)
	{
		kprint("can't map registers at above-4GB address\n");
		return 0;
	}
	op = cap + (rd(cap, CAP_CAPLENGTH) & 0xFF);
	rt = cap + (rd(cap, CAP_RTSOFF) & ~0x1FUL);
	db = cap + (rd(cap, CAP_DBOFF) & ~0x3UL);

	hcs1 = rd(cap, CAP_HCSPARAMS1);
	hcs2 = rd(cap, CAP_HCSPARAMS2);
	max_slots = hcs1 & 0xFF;
	max_ports = (hcs1 >> 24) & 0xFF;
	ctx_size = (rd(cap, CAP_HCCPARAMS1) & 0x4) ? 64 : 32;
	nscratch = (((hcs2 >> 21) & 0x1F) << 5) | ((hcs2 >> 27) & 0x1F);

	kprint("controller at PCI ");
	printlong(dev.bus); kprint(":"); printlong(dev.slot); kprint(".");
	printlong(dev.func);
	if(paging_enabled)
		kprint(" (above 4GB: PAE paging on)");
	kprint(", ");
	printlong(max_ports);
	kprint(" ports\n");

	if(nscratch > MAX_SCRATCH)
	{
		kprint("  USB: controller wants too many scratchpad pages\n");
		return 0;
	}
	if(!(rd(op, OP_PAGESIZE) & 1))
	{
		kprint("  USB: controller doesn't support 4K pages\n");
		return 0;
	}

	bios_handoff();

	/* stop and reset the controller */
	wr(op, OP_USBCMD, rd(op, OP_USBCMD) & ~CMD_RS);
	wait_reg(op, OP_USBSTS, STS_HCH, STS_HCH, 100);
	wr(op, OP_USBCMD, CMD_HCRST);
	if(!wait_reg(op, OP_USBCMD, CMD_HCRST, 0, 1000) ||
	   !wait_reg(op, OP_USBSTS, STS_CNR, 0, 1000))
	{
		kprint("  USB: controller reset timed out\n");
		return 0;
	}

	if(max_slots > MAX_DEVS)
		max_slots = MAX_DEVS;
	wr(op, OP_CONFIG, max_slots);

	/* device context base address array (+ scratchpad buffers) */
	zero(dcbaa, sizeof(dcbaa));
	for(i = 0; i < nscratch; i++)
		scratch_array[i] = (unsigned long)scratch_pages[i];
	if(nscratch)
		dcbaa[0] = (unsigned long)scratch_array;
	wr64(op, OP_DCBAAP, (unsigned long)dcbaa);

	/* command ring */
	ring_init(&cmd_ring, cmd_trbs);
	wr64(op, OP_CRCR, (unsigned long)cmd_trbs | 1);	/* RCS = 1 */

	/* event ring: one segment */
	zero(event_trbs, sizeof(event_trbs));
	ev_dequeue = 0;
	ev_cycle = 1;
	erst[0] = (unsigned long)event_trbs;
	erst[1] = 0;
	erst[2] = EVENT_TRBS;
	erst[3] = 0;
	wr(rt, IR0_ERSTSZ, 1);
	wr64(rt, IR0_ERDP, (unsigned long)event_trbs);
	wr64(rt, IR0_ERSTBA, (unsigned long)erst);
	wr(rt, IR0_IMAN, 1);		/* clear pending, interrupts stay off */

	wr(op, OP_USBCMD, CMD_RS);
	if(!wait_reg(op, OP_USBSTS, STS_HCH, 0, 100))
	{
		kprint("  USB: controller won't start\n");
		return 0;
	}
	running = 1;

	/* give devices time to connect and USB 3 links time to train */
	delay(100);

	for(port = 0; port < max_ports; port++)
	{
		unsigned long sc = rd(op, OP_PORTSC(port));

		if(!(sc & PORT_PP))
		{
			wr(op, OP_PORTSC(port), PORT_PP);
			delay(20);
			sc = rd(op, OP_PORTSC(port));
		}
		if(!(sc & PORT_CCS))
			continue;

		/* USB 2 ports need a reset to become enabled; USB 3 ports
		 * enable themselves when the link comes up */
		if(!(sc & PORT_PED))
		{
			wr(op, OP_PORTSC(port), PORT_PP | PORT_PR | PORT_CHANGES);
			wait_reg(op, OP_PORTSC(port), PORT_PRC, PORT_PRC, 200);
			delay(10);			/* reset recovery */
			sc = rd(op, OP_PORTSC(port));
		}
		wr(op, OP_PORTSC(port), PORT_PP | PORT_CHANGES);	/* clear change bits */
		if(!(sc & PORT_PED))
			continue;

		enumerate(port + 1, PORT_SPEED(sc));
	}

	if(!usb_keyboard_present)
		kprint("  USB: no keyboard found\n");
	return usb_keyboard_present;
}

void usb_poll(void)
{
	if(running)
		poll_events();
}

/* ---- HID boot keyboard ---- */

/* HID usage IDs 0x04-0x38 to ASCII: unshifted, shifted */
static const char hid_plain[] =
	"abcdefghijklmnopqrstuvwxyz1234567890\n\033\b\t -=[]\\#;'`,./";
static const char hid_shift[] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZ!@#$%^&*()\n\033\b\t _+{}|~:\"~<>?";
/* keypad 0x54-0x63 */
static const char hid_keypad[] = "/*-+\n1234567890.";

static void key_pressed(unsigned char usage, unsigned char mods)
{
	int shift = mods & 0x22, ctrl = mods & 0x11, alt = mods & 0x44;
	char c = 0;

	if(usage == 0x39)			/* Caps Lock */
	{
		kbd_caps ^= 1;
		return;
	}
	if(usage == 0x4C && ctrl && alt)	/* Ctrl+Alt+Del */
		_exit();

	if(usage >= 0x04 && usage <= 0x38)
	{
		c = (shift ? hid_shift : hid_plain)[usage - 0x04];
		if(kbd_caps && c >= 'a' && c <= 'z') c -= 32;
		else if(kbd_caps && c >= 'A' && c <= 'Z') c += 32;
	}
	else if(usage >= 0x54 && usage <= 0x63)
		c = hid_keypad[usage - 0x54];

	if(!c)
		return;
	if(ctrl && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')))
		c &= 0x1F;			/* Ctrl+letter = control code */
	keyboard_put(c);
}

/* A boot report is: modifiers, reserved, up to 6 keys currently held.
 * A key is newly pressed if it wasn't in the previous report. */
static void handle_report(void)
{
	int i, j, seen;

	if(kbd_report[2] == 0x01)		/* phantom state: too many keys */
		return;
	for(i = 2; i < 8; i++)
	{
		if(kbd_report[i] < 0x04)
			continue;
		for(seen = 0, j = 2; j < 8; j++)
			if(kbd_prev[j] == kbd_report[i])
				seen = 1;
		if(!seen)
			key_pressed(kbd_report[i], kbd_report[0]);
	}
	for(i = 0; i < 8; i++)
		kbd_prev[i] = kbd_report[i];
}
