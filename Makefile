# Makefile for Freedows Bootloader Demonstration Kernel

# Programs to delete and copy files with
REMOVE = rm -f

# The C and C++ compilers (should be i386-GCC)
GCC = gcc
G++ = gcc

# The linker
LD  = ld

# The object files belonging to the kernel
OBJS = idt.o kernel.o io.o video.o 8259.o keyboard.o pit.o delay.o dma.o fdc.o gazfs.o string.o math.o mem.o

# The kernel filename
KERNELFN = kernel.elf

# Link the kernel statically with fixed text+data address @1M
$(KERNELFN) : $(OBJS)
	$(LD)  -o $@ $(OBJS)  -Ttext 0x100000

# Compile the source files
.cc.o:
	$(G++)  -Wall -fomit-frame-pointer -O -I. -c -o $@ $<

.cc.s:
	$(G++)  -Wall -fomit-frame-pointer -O -I. -S -o $@ $<

.c.o:
	$(GCC)  -Wall -fomit-frame-pointer -O -I. -c -o $@ $<

.c.s:
	$(GCC)  -Wall -fomit-frame-pointer -O -I. -S -o $@ $<

.S.o:
	$(GCC)  -Wall -fomit-frame-pointer -c -o $@ $<

# Clean up the junk
clean:
	$(REMOVE) $(OBJS) $(KERNELFN)
