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

#include "string.h"
#include "ahci.h"
#include "fat32.h"
#include "serial.h"
#include "bootinfo.h"
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

uchar KERNEL_STACK[16384] __attribute__ ((aligned(16)));	/* 16k kernel stack */
struct boot_info *boot_info = 0;	/* from the UEFI loader, or NULL */
unsigned long boot_magic = 0, boot_info_addr = 0;	/* EAX/EBX at entry */
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

/* The kernel entry point. Written in assembly so that EAX/EBX (boot
 * protocol, see bootinfo.h) are saved before anything can clobber them,
 * and so the stack is our own (inside the kernel image) rather than
 * memory the loader may still have been using. */
asm (
   ".globl _start                \n"
   "_start:                      \n"
   "   cli                       \n"
   "   movl %eax, boot_magic     \n" /* Boot protocol: magic and         */
   "   movl %ebx, boot_info_addr \n" /* boot_info pointer                */
   "   lgdtl loadgdt             \n" /* Load our own GDT                 */
   "   movw $0x10, %ax           \n" /* Flat data segment everywhere     */
   "   movw %ax, %ds             \n"
   "   movw %ax, %es             \n"
   "   movw %ax, %fs             \n"
   "   movw %ax, %gs             \n"
   "   movw %ax, %ss             \n"
   "   movl $KERNEL_STACK + 16384, %esp \n"
   "   ljmp $0x08, $1f           \n" /* Reload CS with our code segment  */
   "1: pushl $2                  \n" /* Zero the flags (interrupts off)  */
   "   popf                      \n"
   "   call kernel_start         \n"
   "2: cli                       \n"
   "   hlt                       \n"
   "   jmp 2b                    \n"
);

void kernel_start(void)
{
   if(boot_magic == BOOTINFO_MAGIC && boot_info_addr)
      boot_info = (struct boot_info *) boot_info_addr;

   main();				/* Call main()			*/
   _exit();				/* Finish off			*/
}

/* Now we get the main kernel program 					*/

static void cmd_help(void)
{
	kprint("Commands:\n");
	kprint("  ls [path]     list a directory (also: dir)\n");
	kprint("  cat <file>    print a file (also: type)\n");
	kprint("  cd [path]     change directory (no path = /)\n");
	kprint("  pwd           print the current directory\n");
	kprint("  disk          show disk and filesystem info\n");
	kprint("  cls           clear the screen\n");
	kprint("  reboot        restart the computer\n");
}

static void print_padded(unsigned long n, int width)
{
	unsigned long t = n;
	int digits = 1;
	while(t >= 10) { t /= 10; digits++; }
	while(digits++ < width) kprint(" ");
	printlong(n);
}

static void cmd_ls(char *path)
{
	fat_dirent e;
	fat_dir dir;
	unsigned long files = 0, dirs = 0, bytes = 0;

	if(!fat32_lookup(*path ? path : ".", &e))
	{
		kprint("ls: not found: ");
		kprint(path);
		kprint("\n");
		return;
	}
	if(!(e.attr & FAT_ATTR_DIR))
	{
		print_padded(e.size, 10);
		kprint("  ");
		kprint(e.name);
		kprint("\n");
		return;
	}

	fat32_opendir(&dir, e.cluster);
	while(fat32_readdir(&dir, &e))
	{
		if(e.attr & FAT_ATTR_HIDDEN)
			continue;
		if(e.attr & FAT_ATTR_DIR)
		{
			kprint("     <DIR>  ");
			dirs++;
		}
		else
		{
			print_padded(e.size, 10);
			kprint("  ");
			files++;
			bytes += e.size;
		}
		kprint(e.name);
		kprint(e.attr & FAT_ATTR_DIR ? "/\n" : "\n");
	}
	printlong(files);
	kprint(" file(s), ");
	printlong(bytes);
	kprint(" bytes, ");
	printlong(dirs);
	kprint(" dir(s)\n");
}

static void cmd_cat(char *path)
{
	fat_dirent e;
	unsigned char buf[513];
	unsigned long offset = 0, lines = 0;
	long n, i;
	char ch[2];

	if(!*path)
	{
		kprint("usage: cat <file>\n");
		return;
	}
	if(!fat32_lookup(path, &e))
	{
		kprint("cat: not found: ");
		kprint(path);
		kprint("\n");
		return;
	}
	if(e.attr & FAT_ATTR_DIR)
	{
		kprint("cat: is a directory: ");
		kprint(path);
		kprint("\n");
		return;
	}

	ch[1] = 0;
	while((n = fat32_read(&e, offset, buf, 512)) > 0)
	{
		for(i = 0; i < n; i++)
		{
			ch[0] = buf[i];
			kprint(ch);
			if(buf[i] == '\n' && ++lines >= 22)
			{
				kprint("-- more (any key) --");
				getch();
				kprint("\n");
				lines = 0;
			}
		}
		offset += n;
	}
	if(n < 0)
		kprint("\ncat: disk read error\n");
}

static void cmd_cd(char *path)
{
	if(!fat32_chdir(*path ? path : "/"))
	{
		kprint("cd: no such directory: ");
		kprint(path);
		kprint("\n");
	}
}

static void cmd_disk(void)
{
	kprint("Disk:   ");
	kprint(ahci_model);
	kprint(" on AHCI port ");
	printlong(ahci_port);
	kprint(", ");
	printlong(ahci_sectors);
	kprint(" sectors (");
	printlong(ahci_sectors >> 11);
	kprint(" MB)\n");
	kprint("Volume: \"");
	kprint(fat32_label);
	kprint("\" FAT32 at LBA ");
	printlong(fat32_part_lba);
	kprint(", ");
	printlong(fat32_total_clusters);
	kprint(" clusters of ");
	printlong(fat32_cluster_size);
	kprint(" bytes\n");
}

/* split "cmd args" in place: returns pointer to args (trimmed) */
static char *split_args(char *line)
{
	while(*line && *line != ' ') line++;
	if(!*line) return line;
	*line++ = 0;
	while(*line == ' ') line++;
	return line;
}

void main()
{
	char line[256];
	char *cmd, *args;
	unsigned long delaycount=0;
	int disk_ok = 0;

	serial_init();
	video_init();
	kprint("\n\nGazOS Version 1.2 Loading......\n");
	if(boot_info && boot_info->boot_mode == BOOT_MODE_UEFI)
	{
		kprint("Booted via UEFI");
		if(boot_info->flags & BOOTINFO_FB)
		{
			kprint(", framebuffer ");
			printlong(boot_info->fb_width);
			kprint("x");
			printlong(boot_info->fb_height);
		}
		kprint("\n");
	}
	else
		kprint("Booted via legacy BIOS\n");
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
	if(boot_info && (boot_info->flags & BOOTINFO_MEM))
		mem_end = boot_info->mem_bytes;	/* from the UEFI memory map */
	else
		count_memory();			/* probe it ourselves */

	kprint("Initializing Keyboard...\n");
	InitKeyboard();

	init_pit(18.2, 0);	// Every 54.945ms (18.2 times per second)
	set_vector(pit_ISR, M_VEC, (D_INT + D_PRESENT + D_DPL3));
	enable_irq(0);

	if(ahci_init())
		disk_ok = fat32_mount();

	kprint("CPU = ");
	printlong(cpuspeed);
	kprint("Mhz  -  Mem.Total = ");
	printlong(mem_end/1024);
	kprint("K  -  Total Pages = ");
	printlong(mem_end/4096);
	kprint("\n");

	if(!disk_ok)
		kprint("WARNING: no FAT32 disk mounted\n");
	kprint("Type 'help' for a list of commands.\n");

	while(1)
	{
		kprint("\nGazOS] ");
		kprint((char *)fat32_getcwd());
		kprint(": ");
		gets(line);
		kprint("\n");

		cmd = line;
		while(*cmd == ' ') cmd++;
		args = split_args(cmd);
		if(!cmd[0]) continue;
		else if(!kstrcmp(cmd, "help")) cmd_help();
		else if(!kstrcmp(cmd, "ls")) cmd_ls(args);
		else if(!kstrcmp(cmd, "dir")) cmd_ls(args);
		else if(!kstrcmp(cmd, "cat")) cmd_cat(args);
		else if(!kstrcmp(cmd, "type")) cmd_cat(args);
		else if(!kstrcmp(cmd, "cd")) cmd_cd(args);
		else if(!kstrcmp(cmd, "pwd")) { kprint((char *)fat32_getcwd()); kprint("\n"); }
		else if(!kstrcmp(cmd, "disk")) cmd_disk();
		else if(!kstrcmp(cmd, "cls")) cls();
		else if(!kstrcmp(cmd, "reboot")) _exit();
		else
		{
			kprint("unknown command: ");
			kprint(cmd);
			kprint(" (try 'help')\n");
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
