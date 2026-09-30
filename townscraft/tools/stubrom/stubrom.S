/*
  TOWNSCRAFT test-only stub system ROM.

  This is NOT an FM TOWNS ROM.  It implements just enough of the boot sequence
  to test TOWNSCRAFT's boot sector in the Tsugaru emulator without real ROM
  images:

    - Power-on reset jumps into this code.
    - CD sector 0 is loaded to B000:0000, the "IPL4" signature is checked, and
      the boot sector is far-called at B000:0004 with BL=8 (internal CD-ROM),
      which is how the real boot ROM calls a CD boot sector.
    - A minimal disk BIOS entry is provided at FFFB:0014 supporting
      AH=05h (read sectors), AL=C0h (internal CD-ROM), CX:DX=LBA,
      BX=sector count, DS:DI=buffer.  Sectors are read from the emulated CD-ROM
      controller by CPU (PIO) transfer.

  The image built from this file covers the last 32KB of FMT_SYS.ROM, which is
  mapped to F8000h-FFFFFh, so offset 0 here is F800:0000.
*/
	.code16
	.text
	.globl _start
_start:
	cli
	cld
	xorw	%ax,%ax
	movw	%ax,%ss
	movw	$0x7000,%sp

	/* Read CD sector 0 into B000:0000 */
	movw	$0xB000,%ax
	movw	%ax,%ds
	xorw	%di,%di
	movw	$0x05C0,%ax
	xorw	%cx,%cx
	xorw	%dx,%dx
	movw	$1,%bx
	lcall	$0xFFFB,$0x0014
	jc	boot_fail

	cmpl	$0x344C5049,%ds:0	/* "IPL4" */
	jne	boot_fail

	movw	$0x0008,%bx		/* BL=8 : Internal CD-ROM */
	lcall	$0xB000,$0x0004

boot_fail:
	cli
1:	hlt
	jmp	1b


/*
  Disk BIOS.  Only AH=05h / AL=C0h is supported.
  In:  CX:DX=LBA  BX=count  DS:DI=buffer
  Out: CF=0,AH=0 on success.  CF=1,AH=80h on error.
*/
disk_bios:
	pushal
	pushw	%es
	pushw	%ds

	cmpb	$0x05,%ah
	jne	bios_error
	cmpb	$0xC0,%al
	jne	bios_error
	testw	%bx,%bx
	jz	bios_ok

	movw	%ds,%ax
	movw	%ax,%es
	movw	%cs,%ax
	movw	%ax,%ds

	/* Start LBA -> EAX, End LBA -> ESI */
	movzwl	%cx,%eax
	shll	$16,%eax
	movw	%dx,%ax
	movzwl	%bx,%esi
	addl	%eax,%esi
	decl	%esi
	movw	%bx,%bp			/* BP = sectors to read */

	movw	$0x04C0,%dx
	movb	$0xC0,%al		/* Clear SIRQ/DEI, disable IRQs */
	outb	%al,%dx

	/* Start MSF (start LBA = end LBA - count + 1), then end MSF */
	movw	%di,%bx			/* keep DI */
	movl	%esi,%eax
	movzwl	%bp,%ecx
	subl	%ecx,%eax
	incl	%eax
	call	send_msf
	movl	%esi,%eax
	call	send_msf
	movw	$0x04C4,%dx
	xorb	%al,%al
	outb	%al,%dx
	outb	%al,%dx
	movw	$0x04C2,%dx
	movb	$0x22,%al		/* MODE1READ + status request */
	outb	%al,%dx

	movw	%bx,%di
sector_loop:
	call	read_status
	cmpb	$0x22,%al		/* Data ready */
	je	do_transfer
	cmpb	$0x21,%al
	je	bios_error
	jmp	sector_loop
do_transfer:
	movw	$0x04C6,%dx
	movb	$0x08,%al		/* CPU transfer */
	outb	%al,%dx
	movw	$0x04C4,%dx
	movw	$2048,%cx
	rep insb
	subw	$2048,%di
	movw	%es,%ax
	addw	$0x80,%ax
	movw	%ax,%es
	decw	%bp
	jnz	sector_loop

wait_done:
	call	read_status
	cmpb	$0x06,%al
	je	bios_ok
	cmpb	$0x21,%al
	je	bios_ok
	jmp	wait_done

bios_ok:
	popw	%ds
	popw	%es
	popal
	xorb	%ah,%ah
	clc
	lret

bios_error:
	popw	%ds
	popw	%es
	popal
	movb	$0x80,%ah
	stc
	lret


/* Wait for a 4-byte status packet.  Returns first byte in AL. */
read_status:
	movw	$0x04C0,%dx
1:	inb	%dx,%al
	testb	$0x02,%al
	jz	1b
	movw	$0x04C2,%dx
	inb	%dx,%al
	movb	%al,%ah
	inb	%dx,%al
	inb	%dx,%al
	inb	%dx,%al
	movb	%ah,%al
	ret


/* EAX=LBA.  Sends BCD minute, second, frame to parameter port. */
send_msf:
	pushl	%ebx
	pushl	%ecx
	pushl	%edx
	addl	$150,%eax
	xorl	%edx,%edx
	movl	$75,%ecx
	divl	%ecx			/* EAX=seconds total, EDX=frame */
	movl	%edx,%ebx		/* BL=frame */
	xorl	%edx,%edx
	movl	$60,%ecx
	divl	%ecx			/* EAX=minute, EDX=second */
	movb	%dl,%bh			/* BH=second */
	call	to_bcd
	movw	$0x04C4,%dx
	outb	%al,%dx
	movb	%bh,%al
	call	to_bcd
	outb	%al,%dx
	movb	%bl,%al
	call	to_bcd
	outb	%al,%dx
	popl	%edx
	popl	%ecx
	popl	%ebx
	ret

to_bcd:
	aam	$10
	shlb	$4,%ah
	orb	%ah,%al
	ret


	/* FFFB:0014 = FFFC4h = offset 7FC4h in this window */
	.org	0x7FC4
	ljmp	$0xF800,$disk_bios

	/* Reset vector FFFF:FFF0 = FFFF0h = offset 7FF0h */
	.org	0x7FF0
	ljmp	$0xF800,$_start
	.org	0x8000
