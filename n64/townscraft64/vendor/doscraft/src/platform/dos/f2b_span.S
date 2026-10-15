/* Opaque uncovered runs, using the pinned trap.S pixel pair loop. Row setup
 * remains in f2b_raster.c and uses the ORIGINAL width/prestep/gradient.
 * cdecl: dst, count (1..320), DosF2BTexel*, scale (1 or 2).
 * Updates phase exactly count times, including the last stored texel.
 * No coverage calls, division, allocation, FPU/SIMD or global scratch. */
    .code32
    .text
    .globl _dos_f2b_opaque_span
_dos_f2b_opaque_span:
    movl 12(%esp),%eax
    cmpb $0,16(%eax)
    jne .Lwrap
    cmpl $2,16(%esp)
    je .Lspan2
    jmp .Lspan1
.Lwrap:
    cmpl $2,16(%esp)
    je .Lspan2w
    jmp .Lspan1w

    .macro TEXEL SCALE,WRAP,OFS
    .if \WRAP
    movb (%esi,%ebx),%al
    addl 0(%esp),%ecx
    .else
    movb (%ebx),%al
    addl %esi,%ecx
    .endif
    adcb %dl,%bl
    addw %bp,%cx
    adcb %dh,%bh
    .if \WRAP
    andl $0x0F0F,%ebx
    .endif
    .if \SCALE==2
    movb %al,%ah
    movw %ax,\OFS(%edi)
    .else
    movb %al,\OFS(%edi)
    .endif
    .endm

    .macro SPAN NAME,SCALE,WRAP
\NAME:
    pushl %ebp
    pushl %edi
    pushl %esi
    pushl %ebx
    subl $4,%esp              /* local duFrac, wrap only */
    movl 32(%esp),%eax        /* phase */
    movzwl 12(%eax),%ebx      /* BH=v, BL=u */
    movzwl 4(%eax),%ecx
    shll $16,%ecx
    movw 6(%eax),%cx
    movzwl 14(%eax),%edx      /* DH=dv int, DL=du int */
    movzwl 10(%eax),%ebp     /* BP=dv frac */
    movzwl 8(%eax),%esi
    shll $16,%esi
    .if \WRAP
    movl %esi,0(%esp)
    movl 0(%eax),%esi         /* tile */
    .else
    addl 0(%eax),%ebx         /* initial address high16, NEVER stepped */
    .endif
    movl 24(%esp),%edi        /* dst */
    movl 28(%esp),%eax        /* original pair counter arrangement */
    incl %eax
    shrl $1,%eax
    decl %eax
    shll $16,%eax
    testb $1,28(%esp)
    jz 20f
    subl $\SCALE,%edi
    jmp 21f
20:
    TEXEL \SCALE,\WRAP,0
21:
    TEXEL \SCALE,\WRAP,\SCALE
    addl $2*\SCALE,%edi
    subl $0x10000,%eax
    jnc 20b
    movl 32(%esp),%eax
    movw %bx,12(%eax)
    movw %cx,6(%eax)
    shrl $16,%ecx
    movw %cx,4(%eax)
    addl $4,%esp
    popl %ebx
    popl %esi
    popl %edi
    popl %ebp
    ret
    .endm

    SPAN .Lspan1,1,0
    SPAN .Lspan2,2,0
    SPAN .Lspan1w,1,1
    SPAN .Lspan2w,2,1
