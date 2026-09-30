/* Interrupt and exception entry stubs. */
	.code32
	.text

	.macro EXC_NOERR n
	.globl exc\n
exc\n:
	pushl	$0
	pushl	$\n
	jmp	exc_common
	.endm

	.macro EXC_ERR n
	.globl exc\n
exc\n:
	pushl	$\n
	jmp	exc_common
	.endm

	EXC_NOERR 0
	EXC_NOERR 1
	EXC_NOERR 2
	EXC_NOERR 3
	EXC_NOERR 4
	EXC_NOERR 5
	EXC_NOERR 6
	EXC_NOERR 7
	EXC_ERR   8
	EXC_NOERR 9
	EXC_ERR   10
	EXC_ERR   11
	EXC_ERR   12
	EXC_ERR   13
	EXC_ERR   14
	EXC_NOERR 15
	EXC_NOERR 16
	EXC_ERR   17
	EXC_NOERR 18
	EXC_NOERR 19

exc_common:
	pushal
	pushl	%esp
	call	exception_handler	/* Does not return */
1:	cli
	hlt
	jmp	1b

	.globl irq_timer
irq_timer:
	pushal
	pushl	%ds
	pushl	%es
	movw	$0x10,%ax
	movw	%ax,%ds
	movw	%ax,%es
	cld
	pushl	40(%esp)		/* Interrupted EIP (for the sampling profiler) */
	call	timer_isr
	addl	$4,%esp
	popl	%es
	popl	%ds
	popal
	iret

	/* VSYNC (IRQ 11): count vertical syncs (used for page flipping) */
	.globl irq_vsync
irq_vsync:
	pushl	%eax
	pushl	%edx
	pushl	%ds
	movw	$0x10,%ax
	movw	%ax,%ds
	incl	g_vsyncCount
	movw	$0x5CA,%dx		/* Any write clears the VSYNC interrupt */
	outb	%al,%dx
	movb	$0x20,%al
	outb	%al,$0x10		/* EOI secondary, then primary */
	outb	%al,$0x00
	popl	%ds
	popl	%edx
	popl	%eax
	iret

	/* Other IRQs are masked; if one ever arrives, EOI and ignore it. */
	.globl irq_master_spurious
irq_master_spurious:
	pushl	%eax
	movb	$0x20,%al
	outb	%al,$0x00
	popl	%eax
	iret

	.globl irq_slave_spurious
irq_slave_spurious:
	pushl	%eax
	movb	$0x20,%al
	outb	%al,$0x10
	outb	%al,$0x00
	popl	%eax
	iret

	.section .note.GNU-stack,"",@progbits
