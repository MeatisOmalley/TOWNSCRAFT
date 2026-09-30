/*
  Textured span inner loops.

  Textures are stored as atlas pages: 16 rows with a 256-byte stride.  A
  "tile" pointer points at the first texel of a 16x16 tile in a page, so a
  texel is tile[v*256+u].  The texel index lives in BX (BH=v, BL=u) and the
  fractions in ECX (upper half u, lower half v).  Stepping uses ADD/ADC so
  a pixel costs about nine instructions.

  void span_xxx(u8 *dst,int count,const u8 *tile,u32 u,u32 v,int du,int dv)
  u,v,du,dv are 16.16 and wrap every 16 texels, so a quad covering several
  blocks repeats the texture.
*/
	.code32
	.text

	.macro SPAN_SETUP
	pushl	%ebp
	pushl	%edi
	pushl	%esi
	pushl	%ebx
	movl	20(%esp),%edi		/* dst */
	movl	24(%esp),%eax		/* count */
	testl	%eax,%eax
	jle	9f
	decl	%eax
	shll	$16,%eax		/* Counter in the upper half of EAX */
	movl	28(%esp),%esi		/* tile */
	movl	32(%esp),%ecx		/* u */
	movl	36(%esp),%edx		/* v */
	movl	%edx,%ebx
	shrl	$8,%ebx
	andl	$0x0F00,%ebx		/* BH = v int */
	movl	%ecx,%ebp
	shrl	$16,%ebp
	andl	$15,%ebp
	orl	%ebp,%ebx		/* BL = u int */
	shll	$16,%ecx		/* ECX upper = u frac */
	movw	%dx,%cx			/* CX = v frac */
	movl	40(%esp),%edx		/* du */
	movl	%edx,%ebp
	shll	$16,%ebp
	movl	%ebp,duFrac
	sarl	$16,%edx		/* DL = du int */
	movl	44(%esp),%ebp		/* dv */
	pushl	%ebp
	sarl	$16,%ebp
	movb	%dl,%dh
	movb	%dl,%al			/* keep du int */
	movl	%ebp,%edx
	movb	%dl,%dh			/* DH = dv int */
	movb	%al,%dl			/* DL = du int */
	popl	%ebp			/* BP = dv frac (low half) */
	.endm

	.macro SPAN_END
9:
	popl	%ebx
	popl	%esi
	popl	%edi
	popl	%ebp
	ret
	.endm

	.globl span_opaque1
span_opaque1:
	SPAN_SETUP
1:
	movb	(%esi,%ebx),%al
	addl	duFrac,%ecx
	adcb	%dl,%bl
	addw	%bp,%cx
	adcb	%dh,%bh
	andl	$0x0F0F,%ebx		/* Wrap u and v inside the 16x16 tile */
	movb	%al,(%edi)
	incl	%edi
	subl	$0x10000,%eax
	jnc	1b
	SPAN_END

	.globl span_opaque2
span_opaque2:
	SPAN_SETUP
1:
	movb	(%esi,%ebx),%al
	addl	duFrac,%ecx
	adcb	%dl,%bl
	addw	%bp,%cx
	adcb	%dh,%bh
	andl	$0x0F0F,%ebx		/* Wrap u and v inside the 16x16 tile */
	movb	%al,%ah
	movw	%ax,(%edi)
	addl	$2,%edi
	subl	$0x10000,%eax
	jnc	1b
	SPAN_END

	.globl span_transp1
span_transp1:
	SPAN_SETUP
1:
	movb	(%esi,%ebx),%al
	addl	duFrac,%ecx
	adcb	%dl,%bl
	addw	%bp,%cx
	adcb	%dh,%bh
	andl	$0x0F0F,%ebx		/* Wrap u and v inside the 16x16 tile */
	testb	%al,%al
	jz	2f
	movb	%al,(%edi)
2:
	incl	%edi
	subl	$0x10000,%eax
	jnc	1b
	SPAN_END

	.globl span_transp2
span_transp2:
	SPAN_SETUP
1:
	movb	(%esi,%ebx),%al
	addl	duFrac,%ecx
	adcb	%dl,%bl
	addw	%bp,%cx
	adcb	%dh,%bh
	andl	$0x0F0F,%ebx		/* Wrap u and v inside the 16x16 tile */
	testb	%al,%al
	jz	2f
	movb	%al,%ah
	movw	%ax,(%edi)
2:
	addl	$2,%edi
	subl	$0x10000,%eax
	jnc	1b
	SPAN_END

	.bss
	.align 4
duFrac:	.long	0

	.section .note.GNU-stack,"",@progbits
