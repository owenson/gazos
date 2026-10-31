; GazOS Operating System
; Copyright (C) 1999, 2026  Gareth Owenson <gareth.owenson@gmail.com>
;
; This program is free software; you can redistribute it and/or
; modify it under the terms of the GNU General Public License
; as published by the Free Software Foundation; either version 2
; of the License, or (at your option) any later version.

; GazOS stage 2 boot loader: load /KERNEL.ELF from a FAT32 volume.
;
; Loaded by the VBR to 0x7E00 (from the FAT32 reserved sectors), with
; DL = boot drive and the VBR (and so the BPB) still at 0x7C00.
;
;  1. find "KERNEL  ELF" in the root directory
;  2. follow its cluster chain, reading the file to 0x10000
;  3. switch to unreal mode and copy the ELF PT_LOAD segments to their
;     link address (1MB), zeroing .bss
;  4. enter 32-bit protected mode and jump to the ELF entry point
;
; Memory map while running:
;   0x0600  FAT sector buffer       0x7C00  VBR/BPB, stack below it
;   0x7E00  this code (<= 12K)      0xB000  directory cluster buffer
;   0x10000 - 0x9F000  kernel file (so max kernel file size ~572K)

[BITS 16]
[ORG 0x7E00]

BPB		equ	0x7C00
BPB_SPC		equ	BPB + 0x0D	; sectors per cluster (byte)
BPB_RSVD	equ	BPB + 0x0E	; reserved sectors (word)
BPB_NFATS	equ	BPB + 0x10	; number of FATs (byte)
BPB_HIDDEN	equ	BPB + 0x1C	; partition start LBA (dword)
BPB_FATSZ	equ	BPB + 0x24	; sectors per FAT (dword)
BPB_ROOT	equ	BPB + 0x2C	; root directory cluster (dword)

FATBUF		equ	0x0600
FATBUF_SEG	equ	0x0060
DIRBUF_SEG	equ	0x0B00
ELF_SEG		equ	0x1000
ELF		equ	0x10000
ELF_END_SEG	equ	0x9F00		; don't touch the EBDA

stage2:
	cli
	xor	ax, ax
	mov	ds, ax
	mov	es, ax
	mov	ss, ax
	mov	sp, 0x7C00
	sti
	cld
	mov	[drive], dl

	mov	si, msg_loading
	call	print

	in	al, 0x92		; enable A20 (fast A20 gate)
	or	al, 2
	and	al, 0xFE
	out	0x92, al

	; FAT geometry from the BPB
	movzx	eax, word [BPB_RSVD]
	add	eax, [BPB_HIDDEN]
	mov	[fat_lba], eax
	movzx	cx, byte [BPB_NFATS]
.fats:	add	eax, [BPB_FATSZ]
	loop	.fats
	mov	[data_lba], eax
	movzx	ax, byte [BPB_SPC]
	mov	[spc], ax

	; ---- 1. search the root directory ----
	mov	eax, [BPB_ROOT]
.dir_cluster:
	mov	bx, DIRBUF_SEG
	call	read_cluster
	mov	es, bx
	xor	di, di
	mov	cx, [spc]
	shl	cx, 4			; 16 entries per sector
.entry:
	cmp	byte [es:di], 0		; end of directory
	je	not_found
	test	byte [es:di + 11], 0x18	; skip volume labels, LFNs, dirs
	jnz	.next
	push	cx
	push	di
	mov	si, kernel_name
	mov	cx, 11
	repe	cmpsb
	pop	di
	pop	cx
	je	found
.next:
	add	di, 32
	loop	.entry
	call	next_cluster
	jnc	.dir_cluster
not_found:
	mov	si, msg_notfound
	jmp	fatal

found:
	mov	ax, [es:di + 20]	; first cluster: high word
	shl	eax, 16
	mov	ax, [es:di + 26]	; ... low word
	mov	ecx, [es:di + 28]	; file size
	xor	bx, bx
	mov	es, bx
	mov	si, msg_toobig
	cmp	ecx, (ELF_END_SEG - ELF_SEG) * 16
	ja	fatal
	mov	si, msg_empty
	cmp	eax, 2
	jb	fatal

	; ---- 2. read the file's cluster chain ----
	mov	bx, ELF_SEG
.load:
	mov	si, msg_toobig
	cmp	bx, ELF_END_SEG
	jae	fatal
	call	read_cluster
	mov	dx, [spc]
	shl	dx, 5			; 512 bytes = 32 paragraphs
	add	bx, dx
	call	next_cluster
	jnc	.load

	; ---- 3. unreal mode + ELF load ----
	; The BIOS calls are finished, so 4GB segment limits now stick.
	cli
	lgdt	[gdtinfo]
	mov	eax, cr0
	or	al, 1
	mov	cr0, eax
	mov	bx, 0x10
	mov	ds, bx
	mov	es, bx
	and	al, 0xFE
	mov	cr0, eax
	xor	bx, bx
	mov	ds, bx
	mov	es, bx

	mov	si, msg_badelf
	cmp	dword [dword ELF], 0x464c457f	; "\x7fELF"
	jne	fatal
	cmp	word [dword ELF + 4], 0x0101	; 32-bit, little endian
	jne	fatal

	mov	ebx, [dword ELF + 0x1c]	; e_phoff
	add	ebx, ELF
	movzx	edx, word [dword ELF + 0x2c]	; e_phnum
.phdr:
	cmp	dword [ebx], 1		; PT_LOAD ?
	jne	.nextph
	mov	esi, [ebx + 4]		; p_offset
	add	esi, ELF
	mov	edi, [ebx + 8]		; p_vaddr
	mov	ecx, [ebx + 0x10]	; p_filesz
	a32	rep movsb
	mov	ecx, [ebx + 0x14]	; .bss = p_memsz - p_filesz
	sub	ecx, [ebx + 0x10]
	xor	al, al
	a32	rep stosb
.nextph:
	movzx	eax, word [dword ELF + 0x2a]	; e_phentsize
	add	ebx, eax
	dec	edx
	jnz	.phdr

	; ---- 4. protected mode ----
	mov	eax, cr0
	or	al, 1
	mov	cr0, eax
	jmp	dword 0x08:pmode

; read_cluster: EAX = cluster, BX = destination segment. Preserves all.
read_cluster:
	pushad
	sub	eax, 2
	movzx	ecx, word [spc]
	mul	ecx
	add	eax, [data_lba]
	mov	cx, [spc]
	call	read_lba
	popad
	ret

; read_lba: EAX = LBA, CX = sector count, BX = segment (offset 0)
read_lba:
	pushad
	mov	[dap.count], cx
	mov	[dap.seg], bx
	mov	[dap.lba], eax
	mov	si, dap
	mov	ah, 0x42
	mov	dl, [drive]
	int	0x13
	mov	si, msg_disk
	jc	fatal
	popad
	ret

; next_cluster: EAX = cluster -> EAX = next cluster, CF = 1 at end of chain
next_cluster:
	push	ebx
	push	ecx
	mov	ecx, eax
	shr	eax, 7			; 128 FAT entries per sector
	add	eax, [fat_lba]
	cmp	eax, [fat_cached]
	je	.cached
	mov	[fat_cached], eax
	push	cx
	mov	bx, FATBUF_SEG
	mov	cx, 1
	call	read_lba
	pop	cx
.cached:
	and	ecx, 127
	mov	eax, [FATBUF + ecx * 4]
	and	eax, 0x0FFFFFFF
	cmp	eax, 2
	jb	.end
	cmp	eax, 0x0FFFFFF7		; bad cluster / end of chain
	jae	.end
	clc
	jmp	.done
.end:	stc
.done:	pop	ecx
	pop	ebx
	ret

; print: DS:SI = zero terminated string
print:
	pusha
	mov	ah, 0x0E
	mov	bx, 7
.loop:	lodsb
	or	al, al
	jz	.done
	int	0x10
	jmp	.loop
.done:	popa
	ret

fatal:
	xor	ax, ax			; we may be in unreal mode, DS = 0 anyway
	mov	ds, ax
	call	print
.halt:	cli
	hlt
	jmp	.halt

[BITS 32]
pmode:
	mov	ax, 0x10
	mov	ds, ax
	mov	es, ax
	mov	ss, ax
	mov	esp, 0x7C00
	xor	eax, eax		; no boot_info (see bootinfo.h)
	jmp	[ELF + 0x18]		; e_entry

drive		db	0
spc		dw	0
fat_lba		dd	0
data_lba	dd	0
fat_cached	dd	0xFFFFFFFF
kernel_name	db	"KERNEL  ELF"

msg_loading	db	"GazOS stage 2: loading /KERNEL.ELF", 13, 10, 0
msg_notfound	db	"KERNEL.ELF not found in root directory", 0
msg_toobig	db	"KERNEL.ELF too big", 0
msg_empty	db	"KERNEL.ELF is empty", 0
msg_badelf	db	"KERNEL.ELF is not a 32-bit ELF file", 0
msg_disk	db	"disk read error", 0

dap:		db	0x10, 0
.count:		dw	0
.off:		dw	0
.seg:		dw	0
.lba:		dq	0

gdtinfo:
	dw	gdt_end - gdt - 1
	dd	gdt
gdt:	dq	0
	dq	0x00CF9A000000FFFF	; 0x08: flat 4GB code
	dq	0x00CF92000000FFFF	; 0x10: flat 4GB data
gdt_end:

%if ($-$$) > 24 * 512
%error "stage 2 is bigger than the 24 reserved sectors set aside for it"
%endif
