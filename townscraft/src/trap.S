/*
  Textured trapezoid scan loops.

  One call draws all rows of a trapezoid (the part of a polygon between two
  vertex events).  Per row it computes the span ends from the left and
  right edges, the texture gradient across the span (64-bit multiply by a
  reciprocal of the span width), a prestep when the span is clipped at the
  left screen edge, and then runs the texel loop.  Doing the row setup here
  instead of in C removes most of the per-scanline cost.

  Textures are atlas pages with a 256-byte row stride; the tile pointer
  (ESI) selects a 16x16 tile.  In the texel loop BH=v and BL=u (integer
  texels, masked to wrap every 16), ECX holds the fractions (u in the upper
  half, v in the lower half), and stepping is ADD/ADC, so a pixel costs
  about ten instructions.  The pixel counter lives in the upper half of EAX.

  void trap_xxx(Trap *t)     (see raster.c for the structure)
*/
	.code32
	.text

	/* Trap structure offsets */
	.set T_ROW,0
	.set T_STRIDE,4
	.set T_ROWS,8
	.set T_LX,12
	.set T_LDX,16
	.set T_LU,20
	.set T_LDU,24
	.set T_LV,28
	.set T_LDV,32
	.set T_RX,36
	.set T_RDX,40
	.set T_RU,44
	.set T_RDU,48
	.set T_RV,52
	.set T_RDV,56
	.set T_TILE,60
	.set T_MAXX,64
	.set T_PIXELS,68

	/* SCALE: 1 or 2 bytes per pixel.  TRANSP: skip texel 0. */
	.macro TRAP name,SCALE,TRANSP
	.globl \name
\name:
	pushl	%ebp
	pushl	%edi
	pushl	%esi
	pushl	%ebx
	movl	20(%esp),%eax
	movl	%eax,trapPtr
	cmpl	$0,T_ROWS(%eax)
	jle	99f

10:	/* ---- per row ---- */
	movl	trapPtr,%ebp
	movl	T_LX(%ebp),%eax
	addl	$0x7FFF,%eax
	sarl	$16,%eax		/* xl */
	movl	T_RX(%ebp),%ebx
	addl	$0x7FFF,%ebx
	sarl	$16,%ebx		/* xr */
	cmpl	T_MAXX(%ebp),%ebx
	jle	11f
	movl	T_MAXX(%ebp),%ebx
11:
	xorl	%esi,%esi		/* esi = 1 if clipped on the left */
	testl	%eax,%eax
	jge	12f
	xorl	%eax,%eax
	incl	%esi
12:
	subl	%eax,%ebx		/* w */
	jle	80f
	movl	%eax,rowX
	movl	%ebx,rowW
	addl	%ebx,T_PIXELS(%ebp)

	/* Span width in pixels for the gradient */
	movl	T_RX(%ebp),%ecx
	subl	T_LX(%ebp),%ecx
	addl	$0x8000,%ecx
	sarl	$16,%ecx
	cmpl	$1,%ecx
	jge	13f
	movl	$1,%ecx
13:
	cmpl	$320,%ecx
	jle	14f
	movl	$320,%ecx
14:
	movl	g_recip15(,%ecx,4),%ecx

	movl	T_RU(%ebp),%eax
	subl	T_LU(%ebp),%eax
	imull	%ecx
	shrdl	$15,%edx,%eax
	movl	%eax,rowDU
	movl	T_RV(%ebp),%eax
	subl	T_LV(%ebp),%eax
	imull	%ecx
	shrdl	$15,%edx,%eax
	movl	%eax,rowDV

	movl	T_LU(%ebp),%ecx
	movl	T_LV(%ebp),%edx
	testl	%esi,%esi
	jz	15f
	/* Clipped: advance u,v to the first visible pixel center */
	movl	rowX,%eax
	shll	$16,%eax
	addl	$0x8000,%eax
	subl	T_LX(%ebp),%eax
	sarl	$8,%eax
	movl	%eax,%ebx
	movl	rowDU,%eax
	sarl	$8,%eax
	imull	%ebx,%eax
	addl	%eax,%ecx
	movl	rowDV,%eax
	sarl	$8,%eax
	imull	%ebx,%eax
	addl	%eax,%edx
15:
	/* Destination */
	movl	T_ROW(%ebp),%edi
	movl	rowX,%eax
	.if \SCALE==2
	addl	%eax,%edi
	.endif
	addl	%eax,%edi
	movl	T_TILE(%ebp),%esi

	/* Texel index BH=v int, BL=u int; ECX = u frac<<16 | v frac */
	movl	%edx,%ebx
	shrl	$8,%ebx
	andl	$0x0F00,%ebx
	movl	%ecx,%eax
	shrl	$16,%eax
	andl	$15,%eax
	orl	%eax,%ebx
	shll	$16,%ecx
	movw	%dx,%cx

	movl	rowDU,%eax
	movl	%eax,%edx
	shll	$16,%eax
	movl	%eax,duFracT
	sarl	$16,%edx		/* DL = du int */
	movl	rowDV,%ebp		/* BP = dv frac */
	movl	%ebp,%eax
	sarl	$16,%eax
	movb	%al,%dh			/* DH = dv int */
	movl	rowW,%eax
	decl	%eax
	shll	$16,%eax		/* Counter in the upper half of EAX */

20:	/* ---- per pixel ---- */
	movb	(%esi,%ebx),%al
	addl	duFracT,%ecx
	adcb	%dl,%bl
	addw	%bp,%cx
	adcb	%dh,%bh
	andl	$0x0F0F,%ebx
	.if \TRANSP
	testb	%al,%al
	jz	21f
	.endif
	.if \SCALE==2
	movb	%al,%ah
	movw	%ax,(%edi)
	.else
	movb	%al,(%edi)
	.endif
21:
	.if \SCALE==2
	addl	$2,%edi
	.else
	incl	%edi
	.endif
	subl	$0x10000,%eax
	jnc	20b

80:	/* ---- next row ---- */
	movl	trapPtr,%ebp
	movl	T_LDX(%ebp),%eax
	addl	%eax,T_LX(%ebp)
	movl	T_LDU(%ebp),%eax
	addl	%eax,T_LU(%ebp)
	movl	T_LDV(%ebp),%eax
	addl	%eax,T_LV(%ebp)
	movl	T_RDX(%ebp),%eax
	addl	%eax,T_RX(%ebp)
	movl	T_RDU(%ebp),%eax
	addl	%eax,T_RU(%ebp)
	movl	T_RDV(%ebp),%eax
	addl	%eax,T_RV(%ebp)
	movl	T_STRIDE(%ebp),%eax
	addl	%eax,T_ROW(%ebp)
	decl	T_ROWS(%ebp)
	jnz	10b
99:
	popl	%ebx
	popl	%esi
	popl	%edi
	popl	%ebp
	ret
	.endm

	TRAP trap_opaque1,1,0
	TRAP trap_opaque2,2,0
	TRAP trap_transp1,1,1
	TRAP trap_transp2,2,1

	.bss
	.align 4
trapPtr:	.long 0
rowX:		.long 0
rowW:		.long 0
rowDU:		.long 0
rowDV:		.long 0
duFracT:	.long 0

	.section .note.GNU-stack,"",@progbits
