; GazOS Operating System
; Copyright (C) 1999  Gareth Owen <gaz@athene.co.uk>
;
; This program is free software; you can redistribute it and/or
; modify it under the terms of the GNU General Public License
; as published by the Free Software Foundation; either version 2
; of the License, or (at your option) any later version.
;
; This program is distributed in the hope that it will be useful,
; but WITHOUT ANY WARRANTY; without even the implied warranty of
; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
; GNU General Public License for more details.
;
; You should have received a copy of the GNU General Public License
; along with this program; if not, write to the Free Software
; Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.


%include "gdtnasm.inc"
; GazOS bootsector by Gareth Owen <gaz@athene.co.uk>

[ORG 0x7c00]
jmp start
nop

;START BOOTSECTOR HEADER

id		db	'GazFS'   ; Filing System ID
version		dd	1h 	  ; Filing System Version
fs_start	dd	52	  ; LBA address for start of root dir
krnl_size	dd	51	  ; Kernel size in sectors, starts at sector 1

BytesPerSector	dw	512
SectorsPerTrack	dw	18
TotalHeads	dw	2
TotalSectors	dd	2880	 ; 1474560/512 for a 1.44meg disk

;END BOOTSECTOR HEADER

;BOOTSECTOR DATA
file_entry_nextdata	equ	273	; Offset in file_entry structure to the nextdata LBA pointer
data_entry_data		equ	9	; Offset in data_entry structure to the data
bootdrv		db	0

;END BOOTSECTOR DATA

start:
xor ax, ax
mov ds, ax
mov [bootdrv], dl

; First get into protected mode
cli				;{0}
n5:	in	al, 0x64		;Enable A20 {4A} {5}
	test	al, 2
	jnz	n5
	mov	al, 0xD1
	out	0x64, al
n6:	in	al, 0x64
	test	al, 2
	jnz	n6
	mov	al, 0xDF
	out	0x60, al
        lgdt    [gdtinfo]               ;Load GDT
	mov	ecx, CR0		;Switch to protected mode
	inc	cx
	mov	CR0, ecx

	mov	ax, flat_data-gdt_table	; Selector for 4Gb data seg
	mov	ds, ax			; {2} Extend limit for ds
	mov	es, ax			; Extend limit for es
	mov	fs, ax			; fs and...
	mov 	gs, ax			; gs
	dec cx				; switch back to real mode
	mov CR0, ecx

	sti

	xor ax, ax
	mov ds, ax

	mov dl, [bootdrv]	; Store the boot drive

	mov bx, 0x60
	mov es, bx
	mov eax, 1
	mov ecx, [krnl_size]
	mov di, 1

load_loop:
	call read_sectors
	inc eax
	mov bx, es
	add bx, 32
	mov es, bx

	loop load_loop

	; Turn off the floppy motor, its annoying leaving it on !
        mov edx,0x3f2
        mov al,0x0c
        out dx,al


;lets convert the ELF file to a linear binary so we can execute it
        cmp dword [0x600],464c457fh  ; The ELF signature is \07fELF
        jne ldr_ELF_err            ; Ugh... not an ELF file !!!
        cmp word [0x600+4],101h    ; It should be statically linked etc.
        jne ldr_ELF_err
	cmp byte [0x600+6],1
        jne ldr_ELF_err

jmp short skip_err_handler
	ldr_ELF_err:
	
	mov ax, 'E'+0x0E00
        mov bx, 7
        int 10h
	mov al, 'L'
	int 10h
	mov al, 'F'
	int 10h

	cli
	hlt
	skip_err_handler:

	mov eax, [0x600+18h]
	mov [krnl_entry], eax

        xor ecx,ecx                     ; Get the number of sections in cx
        mov cx,[0x600+2ch]

sectionloop:
        dec cx                          ; Next section
        push cx                         ; Save cx on the stack while we load
                                        ; the section into memory

        mov eax,[0x600+2ah]        ; Get the program header entry size
        mul cx                          ; Calculate the offset from the start
                                        ; of the program header table
        mov ebx,[0x600+1ch]        ; Get the PHT offset in ebx
        add ebx,eax                     ; Add it to our PHT entry offset
        add ebx,0x600              ; Calculate the address of the entry

        cmp dword [bx],1                ; Does this section have to be
                                        ; loaded into memory ?
        jne nextsect                    ; No, next section

        mov dword ecx,[bx+4h]           ; Get the offset of the segment in
                                        ; the ELF file

        mov dword ebp,[bx+10h]          ; Get the size of the segment in the
                                        ; ELF file

        mov dword edi,[bx+8h]           ; Get the memory address of the sect.
        mov dword eax,[bx+14h]          ; Get the size of the section in
        mov ebx,eax                     ; the memory into ebx

; ds:dx  = Address of ASCIIZ filename
; es:edi = Where in memory to put it
; ecx    = Offset in file to start reading (bytes)
; ebp    = Length of segment to read (bytes)
;
; Returns:
; eax    = Length of file that was loaded
; eax    = 0 if an error occured


	push ebp
	pusha
	mov esi, 0x600
	add esi, ecx
	mov ecx, ebp
	call memcopy
	popa
	pop eax

        sub ebx,eax                     ; This amount needs to be zeroed
        jz nextsect                     ; It's ok, next section

        add edi,eax                     ; Zero the memory from this address
        xor ax,ax                       ; edi is an absolute address
        mov ecx,ebx
        call zero_memblock              ; Zero the rest of the section

nextsect:
        pop cx                          ; Restore our section count
        or cx,cx                        ; Was this the last one ?
	jnz sectionloop


; Re-enter protected mode ! A20 is already enabled
	cli		; No interrupts please at all
	lgdt [gdtinfo]
	mov ecx, cr0
	inc cx
	mov cr0, ecx
	mov ax, flat_data-gdt_table
	mov ds, ax
	mov es, ax
	mov fs, ax
	mov gs, ax

	jmp dword (flat_code-gdt_table):pmode1

	pmode1:
[BITS 32]

	push dword 2
	popfd

	mov eax, [krnl_entry]

	call eax

	cli
	hlt

;Hang the system..
hang:	jmp hang

[BITS 16]
	read_sectors:
; Input:
;	EAX = LBN
;	DI  = sector count
;	ES = segment
; Output:
;	BL = low byte of ES
;	EBX high half cleared
;	DL = 0x80
;	EDX high half cleared
;	ESI = 0

	pusha

	cdq			;edx = 0
	movzx	ebx, byte [SectorsPerTrack]
	div	ebx		;EAX=track ;EDX=sector-1
	mov	cx, dx		;CL=sector-1 ;CH=0
	inc	cx		;CL=Sector number
	xor	dx, dx
	mov	bl, [TotalHeads]
	div	ebx

	mov	dh, dl		;Head
	mov	dl, [bootdrv]	;Drive 0
	xchg	ch, al		;CH=Low 8 bits of cylinder number; AL=0
	shr	ax, 2		;AL[6:7]=High two bits of cylinder; AH=0
	or	cl, al		;CX = Cylinder and sector
	mov	ax, di		;AX = Maximum sectors to xfer
retry:	mov	ah, 2		;Read
	xor	bx, bx
	int	13h
	jc retry

	popa

	ret

; zero_memblock:  Fills the specified memory block with zeros (0x0)
;
; Takes parameters:
; ax    = segment/selector of memory to be cleared
; edi   = offset of memory to be cleared
; ecx   = number of bytes to clear
;
; Returns:
; nothing

zero_memblock:
        push eax                ; Save the registers
        push edi
        push ecx
        push es
        mov es,ax
        xor eax,eax             ; Fill the memory with zeros (0x0)
        cld                     ; Clear the direction flag; rep increments di
        a32 rep stosb           ; Fill the memory (one byte at a time)
        pop es                  ; Restore the registers
        pop ecx
        pop edi
        pop eax
        ret                     ; Return to the main program

; Parameters
; DS:ESI = Source
; DS:EDI = Destination
; CX = length
memcopy:
	pusha
memcopy_loop:
	mov al, [esi]
	mov [edi], al
	inc edi
	inc esi
	loop memcopy_loop
	popa
	ret
	
gdtinfo:

dw	gdtlength
dd	gdt_table

;********* GDT TABLE
gdt_table:

null_desc	desc	0,0,0

flat_code	desc	0, 0xFFFFF, D_CODE + D_READ + D_BIG + D_BIG_LIM

flat_data	desc	0, 0xFFFFF, D_DATA + D_WRITE + D_BIG + D_BIG_LIM

gdtlength equ $ - gdt_table - 1
;********* END GDT TABLE
krnl_entry	dd	0

times 510-($-$$) db 0
dw 0xAA55
