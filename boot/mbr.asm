; GazOS Operating System
; Copyright (C) 2026  Gareth Owenson <gareth.owenson@gmail.com>
; Licensed under the GNU General Public License, version 2 or later.
;
; GazOS master boot record (LBA 0).
;
; Relocates itself to 0x0600, finds the active partition and chain-loads
; that partition's boot sector (VBR) to 0x7C00, using the BIOS INT 13h
; extensions. DL = boot drive and DS:SI = partition entry are passed on.
; The partition table (bytes 446-509) is written by sfdisk, not by us.

[BITS 16]
[ORG 0x0600]

start:
	cli
	xor	ax, ax
	mov	ds, ax
	mov	es, ax
	mov	ss, ax
	mov	sp, 0x7C00
	cld
	mov	si, 0x7C00		; move ourselves out of the VBR's way
	mov	di, 0x0600
	mov	cx, 256
	rep	movsw
	jmp	0:relocated

relocated:
	sti
	mov	[drive], dl

	mov	si, 0x0600 + 446	; partition table
	mov	cx, 4
.find:
	test	byte [si], 0x80		; active?
	jnz	.found
	add	si, 16
	loop	.find
	mov	al, 'P'			; no active partition
	jmp	error

.found:
	mov	eax, [si + 8]		; partition start LBA
	mov	[dap.lba], eax
	push	si
	mov	si, dap
	mov	ah, 0x42
	mov	dl, [drive]
	int	0x13
	pop	si
	mov	al, 'D'
	jc	error
	mov	al, 'V'
	cmp	word [0x7DFE], 0xAA55
	jne	error
	mov	dl, [drive]
	jmp	0:0x7C00

error:					; print AL and halt
	mov	ah, 0x0E
	mov	bx, 7
	int	0x10
.halt:	cli
	hlt
	jmp	.halt

drive	db	0
dap:	db	0x10, 0
	dw	1			; sectors
	dw	0x7C00, 0		; offset, segment
.lba:	dq	0

%if ($-$$) > 446
%error "MBR code overlaps the partition table"
%endif
	times 446-($-$$) db 0
