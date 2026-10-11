/* Planar Low trapezoid loops (DOS_LOW_PLANAR). Same Trap layout, row setup
 * and texel stepping as the pinned trap.S, so every texel matches the chunky
 * scale-2 loops; only the destination differs. The renderer's logical pixel x
 * of a row goes to byte x>>1 of plane-pair row A (even x, VGA planes 0/1) or
 * C=A+PLANE_C (odd x, planes 2/3). T_ROW is the A row, T_STRIDE one logical
 * row (two packed VGA rows). The presenter uploads A/C without repacking. */
	.code32
	.text

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
	.set PLANE_C,38400	/* Two 19,200-byte Mode X planes: plane 2 base. */

	/* One texel step and byte store at OFS(%edi); see trap.S TEXEL. */
	.macro PTEXEL TRANSP,WRAP,OFS
	.if \WRAP
	movb	(%esi,%ebx),%al
	addl	duFracP,%ecx
	.else
	movb	(%ebx),%al
	addl	%esi,%ecx
	.endif
	adcb	%dl,%bl
	addw	%bp,%cx
	adcb	%dh,%bh
	.if \WRAP
	andl	$0x0F0F,%ebx
	.endif
	.if \TRANSP
	testb	%al,%al
	jz	1f
	.endif
	movb	%al,\OFS(%edi)
1:
	.endm

	.macro TRAPP name,TRANSP,WRAP
	.globl \name
\name:
	pushl	%ebp
	pushl	%edi
	pushl	%esi
	pushl	%ebx
	movl	20(%esp),%eax
	movl	%eax,trapPtrP
	cmpl	$0,T_ROWS(%eax)
	jle	99f

10:	/* ---- per row (identical to trap.S) ---- */
	movl	trapPtrP,%ebp
	movl	T_LX(%ebp),%eax
	addl	$0x7FFF,%eax
	sarl	$16,%eax
	movl	T_RX(%ebp),%ebx
	addl	$0x7FFF,%ebx
	sarl	$16,%ebx
	cmpl	T_MAXX(%ebp),%ebx
	jle	11f
	movl	T_MAXX(%ebp),%ebx
11:
	xorl	%esi,%esi
	testl	%eax,%eax
	jge	12f
	xorl	%eax,%eax
	incl	%esi
12:
	subl	%eax,%ebx
	jle	80f
	movl	%eax,rowXP
	movl	%ebx,rowWP
	addl	%ebx,T_PIXELS(%ebp)

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
	movl	_g_recip14(,%ecx,4),%ecx

	movl	T_RU(%ebp),%eax
	subl	T_LU(%ebp),%eax
	sarl	$8,%eax
	imull	%ecx,%eax
	sarl	$6,%eax
	movl	%eax,rowDUP
	movl	T_RV(%ebp),%eax
	subl	T_LV(%ebp),%eax
	sarl	$8,%eax
	imull	%ecx,%eax
	sarl	$6,%eax
	movl	%eax,rowDVP

	movl	T_LU(%ebp),%ecx
	movl	T_LV(%ebp),%edx
	testl	%esi,%esi
	jz	15f
	movl	rowXP,%eax
	shll	$16,%eax
	addl	$0x8000,%eax
	subl	T_LX(%ebp),%eax
	sarl	$8,%eax
	movl	%eax,%ebx
	movl	rowDUP,%eax
	sarl	$8,%eax
	imull	%ebx,%eax
	addl	%eax,%ecx
	movl	rowDVP,%eax
	sarl	$8,%eax
	imull	%ebx,%eax
	addl	%eax,%edx
15:
	/* Destination: byte x>>1 of the A row */
	movl	T_ROW(%ebp),%edi
	movl	rowXP,%eax
	shrl	$1,%eax
	addl	%eax,%edi

	movl	%edx,%ebx
	shrl	$8,%ebx
	andl	$0x0F00,%ebx
	movl	%ecx,%eax
	shrl	$16,%eax
	andl	$15,%eax
	orl	%eax,%ebx
	shll	$16,%ecx
	movw	%dx,%cx
	.if \WRAP
	movl	T_TILE(%ebp),%esi
	.else
	addl	T_TILE(%ebp),%ebx
	.endif

	movl	rowDUP,%eax
	movl	%eax,%edx
	shll	$16,%eax
	.if \WRAP
	movl	%eax,duFracP
	.else
	movl	%eax,%esi
	.endif
	sarl	$16,%edx
	movl	rowDVP,%ebp
	movl	%ebp,%eax
	sarl	$16,%eax
	movb	%al,%dh

	/* Odd first pixel: C byte, then continue on even-aligned pairs. */
	testb	$1,rowXP
	jz	20f
	PTEXEL	\TRANSP,\WRAP,PLANE_C
	incl	%edi
	decl	rowWP
20:
	movl	rowWP,%eax
	shrl	$1,%eax			/* Whole pairs */
	jz	24f
	testb	$1,%al
	jz	21f
	PTEXEL	\TRANSP,\WRAP,0
	PTEXEL	\TRANSP,\WRAP,PLANE_C
	incl	%edi
	movl	rowWP,%eax
	shrl	$2,%eax			/* Remaining two-pair groups */
	jz	24f
	jmp	22f
21:
	shrl	$1,%eax
22:
	decl	%eax
	shll	$16,%eax		/* Group counter in the upper half of EAX */
23:	/* ---- four texels: A, C, A+1, C+1 ---- */
	PTEXEL	\TRANSP,\WRAP,0
	PTEXEL	\TRANSP,\WRAP,PLANE_C
	PTEXEL	\TRANSP,\WRAP,1
	PTEXEL	\TRANSP,\WRAP,PLANE_C+1
	addl	$2,%edi
	subl	$0x10000,%eax
	jnc	23b
24:
	testb	$1,rowWP
	jz	80f
	PTEXEL	\TRANSP,\WRAP,0

80:	/* ---- next row (identical to trap.S) ---- */
	movl	trapPtrP,%ebp
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

	TRAPP _trap_opaqueP,0,0
	TRAPP _trap_transpP,1,0
	TRAPP _trap_opaquePw,0,1
	TRAPP _trap_transpPw,1,1

	.bss
	.align 4
trapPtrP:	.long 0
rowXP:		.long 0
rowWP:		.long 0
rowDUP:		.long 0
rowDVP:		.long 0
duFracP:	.long 0
