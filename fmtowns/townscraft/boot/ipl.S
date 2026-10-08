/*
  TOWNSCRAFT CD-ROM boot sector (IPL).

  The FM TOWNS boot ROM loads CD sector 0 to B000:0000, checks the "IPL4"
  signature and far-calls B000:0004 with BL=boot device.

  This IPL:
    1. Loads the game kernel (KERNEL_SECTORS sectors from KERNEL_LBA) to
       linear address KERNEL_ADDR through the ROM disk BIOS (FFFB:0014),
       64KB at a time.  TOWNS OS's own IPL loads IO.SYS to the same area
       with the same BIOS call, so the ROM does not use this memory.
    2. Switches to 32-bit flat protected mode and jumps to KERNEL_ADDR.

  Assembled with origin 0 (the code only uses CS-relative addressing).
*/
	.code16
	.text
	.globl _start
_start:
	.ascii	"IPL4"
	jmp	ipl_main

	.org	0x10
	/* Patched by mkimage.py */
kernel_lba:	.long	1
kernel_sectors:	.long	0

	.set	KERNEL_ADDR,0x10800

ipl_main:
	cli
	cld
	pushw	%cs
	popw	%ds

	cmpb	$1,%bl			/* SCSI CD boot is not supported */
	je	fail

	movl	%cs:kernel_lba,%esi
	movl	%cs:kernel_sectors,%ebp
	movw	$(KERNEL_ADDR>>4),%ax	/* Destination segment */

load_loop:
	testl	%ebp,%ebp
	jz	load_done

	movl	%ebp,%ebx
	cmpl	$32,%ebx
	jbe	1f
	movl	$32,%ebx
1:
	pushw	%ax
	pushl	%ebx
	pushl	%esi
	pushl	%ebp

	movw	%ax,%ds
	xorw	%di,%di
	movl	%esi,%edx		/* CX:DX = LBA */
	movl	%esi,%ecx
	shrl	$16,%ecx
	movw	$0x05C0,%ax		/* AH=05 Read, AL=C0 CD-ROM */
	lcall	$0xFFFB,$0x0014

	popl	%ebp
	popl	%esi
	popl	%ebx
	popw	%ax
	jc	fail

	addl	%ebx,%esi
	subl	%ebx,%ebp
	shll	$7,%ebx			/* sectors*2048/16 = paragraphs */
	addw	%bx,%ax
	jmp	load_loop

load_done:
	pushw	%cs
	popw	%ds

	/* GDTR base must be linear.  CS base + offset of gdt. */
	xorl	%eax,%eax
	movw	%cs,%ax
	shll	$4,%eax
	addl	$gdt,%eax
	movl	%eax,gdtr+2

	lgdt	gdtr
	movl	%cr0,%eax
	orb	$1,%al
	movl	%eax,%cr0
	/* Far jump to 32-bit flat code segment */
	.byte	0x66,0xEA
	.long	KERNEL_ADDR
	.word	0x0008

fail:
	pushw	%cs
	popw	%ds
	/* Nothing sensible to do without the kernel.  Return to the ROM. */
	lret

	.balign	8
gdt:
	.quad	0x0000000000000000
	.quad	0x00CF9A000000FFFF	/* 08h: code 32-bit flat */
	.quad	0x00CF92000000FFFF	/* 10h: data 32-bit flat */
gdt_end:

gdtr:
	.word	gdt_end-gdt-1
	.long	0

	.org	0x800
