; GazOS Operating System
; Copyright (C) 2026  Gareth Owenson <gareth.owenson@gmail.com>
; Licensed under the GNU General Public License, version 2 or later.
;
; GazOS FAT32 volume boot record (stage 1).
;
; Only bytes 90-509 of this sector are installed: bytes 0-89 (jump + BPB)
; are left as mkfs.fat wrote them; its "EB 58 90" jump lands on 'start'.
;
; Loads stage 2 from the FAT32 reserved sectors (partition-relative
; sectors 8..31: 0 = this sector, 1 = FSInfo, 6/7 = backups) to 0x7E00
; and jumps to it with DL = boot drive.

[BITS 16]
[ORG 0x7C00]

STAGE2_SECTOR	equ	8
STAGE2_COUNT	equ	24
BPB_HIDDEN	equ	0x7C00 + 0x1C	; partition start LBA
BPB_RESERVED	equ	0x7C00 + 0x0E

	jmp	short start
	nop
	times 90-($-$$) db 0		; BPB, supplied by mkfs.fat

start:
	cli
	xor	ax, ax
	mov	ds, ax
	mov	es, ax
	mov	ss, ax
	mov	sp, 0x7C00
	sti
	cld
	mov	[drive], dl

	mov	al, 'R'			; reserved area too small for stage 2
	cmp	word [BPB_RESERVED], STAGE2_SECTOR + STAGE2_COUNT
	jb	error

	mov	eax, [BPB_HIDDEN]
	add	eax, STAGE2_SECTOR
	mov	[dap.lba], eax
	mov	si, dap
	mov	ah, 0x42
	mov	dl, [drive]
	int	0x13
	mov	al, 'S'
	jc	error

	mov	dl, [drive]
	jmp	0:0x7E00

error:
	mov	ah, 0x0E
	mov	bx, 7
	int	0x10
.halt:	cli
	hlt
	jmp	.halt

drive	db	0
dap:	db	0x10, 0
	dw	STAGE2_COUNT
	dw	0x7E00, 0		; offset, segment
.lba:	dq	0

%if ($-$$) > 510
%error "VBR code too big"
%endif
	times 510-($-$$) db 0
	dw	0xAA55
