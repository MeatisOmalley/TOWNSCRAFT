/* 32-bit DPMI hardware entries: private resident stacks, saved full integer
 * context, CLD for C, and STI immediately before IRET (DPMI requirement).
 * IRQ0 tail-chains the saved PM vector only at the original BIOS cadence.
 * No C iret wrapper is used: it would hide interrupted EIP / conditional chain.
 * Virtual interrupts remain disabled throughout each body. */
    .code32
    .text
    .macro SAVE stack
    cli
    pushal
    pushl %ds
    pushl %es
    pushl %fs
    pushl %gs
    movw %cs:_dos_irq_data_selector,%ax
    movw %ax,%ds
    movw %ax,%es
    cld
    movl %esp,%edx
    xorl %ecx,%ecx
    movw %ss,%cx
    movw %ax,%ss
    movl $\stack,%esp
    pushl %ecx
    pushl %edx
    .endm
    .macro RETURN_STACK
    popl %edx
    popl %eax
    movw %ax,%ss
    movl %edx,%esp
    .endm
    .macro RESTORE
    popl %gs
    popl %fs
    popl %es
    popl %ds
    popal
    .endm
    .globl _dos_irq_timer_entry
_dos_irq_timer_entry:
    SAVE timer_stack_end
    /* CWSDPMI r7 tables.asm irq_common prepends a 12-byte host IRET frame
     * (flags 0x3002), then the original protected-mode frame. Identify its
     * _user_interrupt_return by the host selector and zero-offset far CALL
     * gate opcode (9A 00 00), not flags alone: rmcb_task's return has the same
     * flags but different saved words. This is specific to the pinned normal
     * CWSDPMI r7 binary, not its ring-0 variant or arbitrary DPMI hosts.
     * ES addresses the original locked stack; FS reads resident host code. */
    movw %cx,%es
    xorl %eax,%eax
    xorl %ebx,%ebx
    cmpl $0x3002,%es:56(%edx)
    jne no_profile_frame
    movw %es:52(%edx),%cx
    cmpw _dos_irq_old_timer+4,%cx
    jne no_profile_frame
    movw %cx,%fs
    movl %es:48(%edx),%esi
    cmpb $0x9a,%fs:(%esi)
    jne no_profile_frame
    cmpw $0,%fs:1(%esi)
    jne no_profile_frame
    movzwl %es:64(%edx),%eax
    cmpw _dos_irq_code_selector,%ax
    jne no_profile_frame
    movl %es:60(%edx),%ebx
no_profile_frame:
    pushl %eax /* interrupted application's CS, or zero for foreign frames */
    pushl %ebx /* interrupted application's EIP, not the return trampoline */
    movw %ds,%cx
    movw %cx,%es
    call _dos_irq_timer_body
    addl $8,%esp
    testl %eax,%eax
    jnz chain_timer
    movb $0x20,%al
    outb %al,$0x20
    RETURN_STACK
    RESTORE
    sti
    iret
chain_timer:
    RETURN_STACK
    RESTORE
    ljmp *%cs:_dos_irq_old_timer

    .globl _dos_irq_keyboard_entry
_dos_irq_keyboard_entry:
    SAVE keyboard_stack_end
    call _dos_irq_keyboard_body
    RETURN_STACK
    RESTORE
    sti
    iret

    .bss
    .balign 16
timer_stack:
    .space 8192
timer_stack_end:
keyboard_stack:
    .space 8192
keyboard_stack_end:
