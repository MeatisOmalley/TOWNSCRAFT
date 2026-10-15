/* IRQ7: private locked integer stack, full saved context; no music or mixing.
 * Same DPMI entry/return contract as resident keyboard IRQ transport. */
    .code32
    .text
    .globl _dos_sb_irq_entry
_dos_sb_irq_entry:
    cli
    pushal
    pushl %ds
    pushl %es
    pushl %fs
    pushl %gs
    movw %cs:_dos_sb_data_selector,%ax
    movw %ax,%ds
    movw %ax,%es
    cld
    movl %esp,%edx
    xorl %ecx,%ecx
    movw %ss,%cx
    movw %ax,%ss
    movl $sb_stack_end,%esp
    pushl %ecx
    pushl %edx
    call _dos_sb_irq_body
    popl %edx
    popl %eax
    movw %ax,%ss
    movl %edx,%esp
    popl %gs
    popl %fs
    popl %es
    popl %ds
    popal
    sti
    iret
    .bss
    .balign 16
sb_stack:
    .space 8192
sb_stack_end:
