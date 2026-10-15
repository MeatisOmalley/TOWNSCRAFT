/*
  Kernel entry.  The IPL jumps here in 32-bit flat protected mode (CS=08h)
  with interrupts disabled.
*/
	.code32
	.section .text.entry,"ax"
	.globl _start
_start:
	cli
	cld
	/* Switch to our own GDT.  The IPL's copy lives in memory that the
	   kernel reuses. */
	lgdt	%cs:gdtr		/* DS still holds the real-mode segment */
	ljmp	$0x08,$1f
1:
	movw	$0x10,%ax
	movw	%ax,%ds
	movw	%ax,%es
	movw	%ax,%fs
	movw	%ax,%gs
	movw	%ax,%ss
	movl	$__stack_top,%esp

	/* Clear BSS */
	movl	$__bss_start,%edi
	movl	$__bss_end,%ecx
	subl	%edi,%ecx
	shrl	$2,%ecx
	xorl	%eax,%eax
	rep stosl

	call	kmain
2:	cli
	hlt
	jmp	2b

	.section .rodata
	.balign	8
gdt:
	.quad	0x0000000000000000
	.quad	0x00CF9A000000FFFF	/* 08h: code 32-bit flat */
	.quad	0x00CF92000000FFFF	/* 10h: data 32-bit flat */
gdt_end:
	.balign	4
	.word	0
gdtr:
	.word	gdt_end-gdt-1
	.long	gdt

	.section .note.GNU-stack,"",@progbits
