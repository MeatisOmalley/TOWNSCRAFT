/*
  Pentium perspective trapezoid loops (1996 software renderer).

  One call draws every row of a trapezoid: the rows of a polygon between two
  vertex events. The polygon's plane gives q=1/z, s=u*q and t=v*q as linear
  functions of the screen (see r96_soft.c). Each span is cut into segments
  of 16 pixels; u=s/q and v=t/q are exact at segment ends and linear inside.

  The divide is the Quake technique. On a Pentium, FDIV runs in the FPU for
  39 cycles (19 at single precision) while the integer pipes keep issuing,
  as long as no FPU instruction needs its result. So the reciprocal for the
  end of the NEXT segment is started before the integer texel loop of the
  current one, and is ready by the time the loop finishes. During a span's
  last segment the divide for the start of the NEXT ROW is started instead,
  so a row normally pays only for the divide at its first segment end.

  The texel loop is a jump into a 16-pixel unrolled block (no counter). It
  steps u and v in 16.16 with the byte carry trick of the 486 loops, minus
  their 16-bit prefixed ADD: the v fraction rides in the upper half of EAX,
  whose low byte receives the texel. Texture tiles live in 4 KB atlas pages
  with a 256-byte row stride; in the no-wrap loops EBX is the texel address
  (BL=level*16+u, BH=page bits+v), in the wrap loops EBX is the masked v:u
  index under the tile pointer in ESI.

  void r96_persp_{opaque,transp}[w](R96Trap *t)   (layout: r96_soft.c)
*/
	.code32
	.text

	.set T_ROW,0
	.set T_STRIDE,4
	.set T_ROWS,8
	.set T_LX,12
	.set T_LDX,16
	.set T_RX,20
	.set T_RDX,24
	.set T_MAXX,28
	.set T_TILE,32
	.set T_PIXELS,36
	.set T_Q,40
	.set T_S,44
	.set T_T,48
	.set T_DQDX,52
	.set T_DSDX,56
	.set T_DTDX,60
	.set T_DQDY,64
	.set T_DSDY,68
	.set T_DTDY,72

	/* u,v range of the no-wrap loops: inside one 16x16 tile, with a margin
	   for the rounding of the 16.16 steps (at most 8 units per segment). */
	.set UVLOW,0x80
	.set UVTOP,0x000FFF80

	/* One pixel of the unrolled block. OFS: store offset from EDI. */
	.macro PIX TRANSP,WRAP,OFS
	.if \WRAP
	movb	(%esi,%ebx),%al
	addl	duFrac,%ecx
	.else
	movb	(%ebx),%al
	addl	%esi,%ecx
	.endif
	adcb	%dl,%bl
	addl	%ebp,%eax
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

	/* Edge, row pointer and plane values to the next scanline. */
	.macro ROW_ADVANCE
	movl	trapP,%ebp
	movl	T_LDX(%ebp),%eax
	addl	%eax,T_LX(%ebp)
	movl	T_RDX(%ebp),%eax
	addl	%eax,T_RX(%ebp)
	movl	T_STRIDE(%ebp),%eax
	addl	%eax,T_ROW(%ebp)
	flds	T_Q(%ebp)
	fadds	T_DQDY(%ebp)
	fstps	T_Q(%ebp)
	flds	T_S(%ebp)
	fadds	T_DSDY(%ebp)
	fstps	T_S(%ebp)
	flds	T_T(%ebp)
	fadds	T_DTDY(%ebp)
	fstps	T_T(%ebp)
	decl	T_ROWS(%ebp)
	.endm

	/* Span of the current row: xl, count (spanLeft), destination and the
	   plane at xl. Nonempty: starts 1/q0, leaving it on the FPU stack. */
	.macro ROW_SETUP
	movl	trapP,%ebp
	movl	T_LX(%ebp),%eax
	addl	$0x7FFF,%eax
	sarl	$16,%eax
	movl	T_RX(%ebp),%ecx
	addl	$0x7FFF,%ecx
	sarl	$16,%ecx
	cmpl	T_MAXX(%ebp),%ecx
	jle	71f
	movl	T_MAXX(%ebp),%ecx
71:
	testl	%eax,%eax
	jge	72f
	xorl	%eax,%eax
72:
	subl	%eax,%ecx
	movl	%ecx,spanLeft
	jle	73f
	movl	%eax,spanX
	addl	%ecx,T_PIXELS(%ebp)
	movl	T_ROW(%ebp),%edi
	addl	%eax,%edi
	movl	%edi,spanDest
	fildl	spanX
	flds	T_DQDX(%ebp)
	fmul	%st(1),%st
	fadds	T_Q(%ebp)
	fstps	q0
	flds	T_DSDX(%ebp)
	fmul	%st(1),%st
	fadds	T_S(%ebp)
	fstps	s0
	flds	T_DTDX(%ebp)
	fmulp	%st,%st(1)
	fadds	T_T(%ebp)
	fstps	t0
	fld1
	fdivs	q0
73:
	.endm

	/* Endpoint of the segment starting at P0 (q0,s0,t0) for spanLeft
	   pixels: k=16 when more remain, else the last pixel (k=spanLeft-1).
	   Sets segLen, k and q1,s1,t1, and starts 1/q1. spanLeft>=2. */
	.macro ENDPOINT
	movl	spanLeft,%ecx
	movl	$16,%eax
	cmpl	$16,%ecx
	jg	74f
	movl	%ecx,%eax
	decl	%eax
74:
	movl	%eax,segK
	cmpl	$16,%ecx
	jle	75f
	movl	$16,%ecx
75:
	movl	%ecx,segLen
	movl	trapP,%ebp
	fildl	segK
	flds	T_DQDX(%ebp)
	fmul	%st(1),%st
	fadds	q0
	fstps	q1
	flds	T_DSDX(%ebp)
	fmul	%st(1),%st
	fadds	s0
	fstps	s1
	flds	T_DTDX(%ebp)
	fmulp	%st,%st(1)
	fadds	t0
	fstps	t1
	fld1
	fdivs	q1
	.endm

	/* Clamp an integer coordinate into the tile (no-wrap loops). */
	.macro CLAMP_UV reg
	cmpl	$UVLOW,\reg
	jge	76f
	movl	$UVLOW,\reg
76:
	cmpl	$UVTOP,\reg
	jle	77f
	movl	$UVTOP,\reg
77:
	.endm

	.macro PERSP name,TRANSP,WRAP
	.globl	_\name
_\name:
	pushl	%ebp
	pushl	%edi
	pushl	%esi
	pushl	%ebx
	movl	20(%esp),%eax
	movl	%eax,trapP
	cmpl	$0,T_ROWS(%eax)
	jle	99f

10:	/* ---- row with no divide in flight ---- */
	ROW_SETUP
	cmpl	$0,spanLeft
	jle	50f

20:	/* ---- row body: 1/q0 is on the FPU stack ---- */
	flds	s0
	fmul	%st(1),%st
	fistpl	u0
	flds	t0
	fmulp	%st,%st(1)
	fistpl	v0
	.if !\WRAP
	CLAMP_UV u0
	CLAMP_UV v0
	.endif
	cmpl	$1,spanLeft
	je	40f
	ENDPOINT

30:	/* ---- segment: 1/q1 on the FPU stack ---- */
	flds	s1
	fmul	%st(1),%st
	fistl	u1
	fisubl	u0
	movl	segK,%eax
	fmuls	invK(,%eax,4)
	fistpl	du
	flds	t1
	fmulp	%st,%st(1)
	fistl	v1
	fisubl	v0
	fmuls	invK(,%eax,4)
	fistpl	dv
	.if !\WRAP
	/* Clamp the end; recompute steps only when it moved (rare). */
	movl	u1,%eax
	movl	v1,%ecx
	CLAMP_UV u1
	CLAMP_UV v1
	cmpl	u1,%eax
	jne	31f
	cmpl	v1,%ecx
	je	32f
31:
	call	resteps
32:
	.endif

	/* What follows this segment: more segments (start the next endpoint
	   divide), one last pixel, or the next row (start its divide). The
	   row prefetch replaces spanDest, so this segment keeps its own. */
	movl	spanDest,%eax
	movl	%eax,curDest
	addl	segLen,%eax
	movl	%eax,spanDest
	movl	spanLeft,%eax
	subl	segLen,%eax
	movl	%eax,nextLeft
	cmpl	$1,%eax
	jl	33f
	je	34f
	/* Next segment starts at P1. */
	flds	q1
	fstps	q0
	flds	s1
	fstps	s0
	flds	t1
	fstps	t0
	movl	segLen,%ecx
	movl	%ecx,curLen
	movl	%eax,spanLeft
	ENDPOINT
	movl	$1,nextState
	jmp	35f
33:	/* Last segment: prefetch the next row's start. */
	movl	segLen,%ecx
	movl	%ecx,curLen
	ROW_ADVANCE
	movl	$3,nextState
	cmpl	$0,T_ROWS(%ebp)
	jle	35f
	ROW_SETUP
	movl	$2,nextState
	cmpl	$0,spanLeft
	jg	35f
	movl	$5,nextState	/* empty: advance again, no divide in flight */
	jmp	35f
34:	/* One pixel remains, at P1. */
	movl	segLen,%ecx
	movl	%ecx,curLen
	movl	$4,nextState
35:
	/* ---- texel loop of curLen pixels from u0,v0 (FDIV runs on) ---- */
	movl	du,%edx
	movl	%edx,%esi
	shll	$16,%esi
	sarl	$16,%edx
	movl	dv,%ebp
	movl	%ebp,%ecx
	sarl	$16,%ecx
	movb	%cl,%dh
	shll	$16,%ebp
	movl	v0,%ebx
	sarl	$16,%ebx
	shll	$8,%ebx
	movl	u0,%eax
	sarl	$16,%eax
	.if \WRAP
	movl	%esi,duFrac
	movb	%al,%bl
	andl	$0x0F0F,%ebx
	movl	trapP,%esi
	movl	T_TILE(%esi),%esi
	.else
	addl	%eax,%ebx
	movl	trapP,%eax
	addl	T_TILE(%eax),%ebx
	.endif
	movl	u0,%ecx
	shll	$16,%ecx
	movl	v0,%eax
	shll	$16,%eax
	/* Pixel i runs block index 16-n+i, storing at EDI+16-n+i. */
	movl	curLen,%edi
	movl	entry_\name(,%edi,4),%edi
	movl	%edi,jumpTarget
	movl	curLen,%edi
	addl	curDest,%edi
	subl	$16,%edi
	jmp	*jumpTarget		/* into the unrolled block */
	.irp	n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
61\n:
	PIX	\TRANSP,\WRAP,\n
	.endr

	/* ---- continue ---- */
	movl	u1,%eax
	movl	%eax,u0
	movl	v1,%eax
	movl	%eax,v0
	movl	nextState,%eax
	cmpl	$1,%eax
	je	30b		/* next segment, its divide in flight */
	cmpl	$2,%eax
	je	20b		/* next row, its start divide in flight */
	cmpl	$3,%eax
	je	99f		/* trapezoid done */
	cmpl	$5,%eax
	je	51f		/* prefetched row is empty */
	movl	nextLeft,%eax
	movl	%eax,spanLeft
	/* fall through: one last pixel at u0,v0 */

40:	/* ---- single pixel at u0,v0, then the next row ---- */
	movl	v0,%ebx
	sarl	$16,%ebx
	shll	$8,%ebx
	movl	u0,%eax
	sarl	$16,%eax
	.if \WRAP
	movb	%al,%bl
	andl	$0x0F0F,%ebx
	.else
	addl	%eax,%ebx
	.endif
	movl	trapP,%eax
	addl	T_TILE(%eax),%ebx
	movb	(%ebx),%al
	.if \TRANSP
	testb	%al,%al
	jz	41f
	.endif
	movl	spanDest,%edi
	movb	%al,(%edi)
41:
	incl	spanDest

50:	/* ---- empty or finished row without prefetch ---- */
	ROW_ADVANCE
51:
	movl	trapP,%ebp
	cmpl	$0,T_ROWS(%ebp)
	jg	10b
99:
	popl	%ebx
	popl	%esi
	popl	%edi
	popl	%ebp
	ret

	.data
	.align 4
	/* Entry for n pixels: block index 16-n. */
entry_\name:
	.long	610b		/* n=0, unused */
	.long	6115b,6114b,6113b,6112b,6111b,6110b,619b,618b
	.long	617b,616b,615b,614b,613b,612b,611b,610b
	.text
	.endm

/* Steps from the clamped integer ends: (end-start)*invK[segK]. */
resteps:
	movl	segK,%eax
	fildl	u1
	fisubl	u0
	fmuls	invK(,%eax,4)
	fistpl	du
	fildl	v1
	fisubl	v0
	fmuls	invK(,%eax,4)
	fistpl	dv
	ret

	PERSP	r96_persp_opaque,0,0
	PERSP	r96_persp_transp,1,0
	PERSP	r96_persp_opaquew,0,1
	PERSP	r96_persp_transpw,1,1

	.data
	.align 4
invK:	.float	1.0
	.float	1.0,0.5,0.33333334,0.25,0.2,0.16666667,0.14285715,0.125
	.float	0.11111111,0.1,0.09090909,0.083333336,0.07692308,0.071428575,0.06666667,0.0625

	.bss
	.align 4
trapP:		.long 0
spanX:		.long 0
spanLeft:	.long 0
spanDest:	.long 0
nextLeft:	.long 0
nextState:	.long 0
segLen:		.long 0
segK:		.long 0
curLen:		.long 0
curDest:	.long 0
jumpTarget:	.long 0
u0:		.long 0
v0:		.long 0
u1:		.long 0
v1:		.long 0
du:		.long 0
dv:		.long 0
duFrac:		.long 0
q0:		.long 0
s0:		.long 0
t0:		.long 0
q1:		.long 0
s1:		.long 0
t1:		.long 0
