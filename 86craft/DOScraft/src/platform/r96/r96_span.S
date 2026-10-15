/*
  Pentium perspective trapezoid loops (1996 software renderer).

  One call draws every row of a trapezoid: the rows of a polygon between two
  vertex events. The polygon's plane gives q=1/z, s=u*q and t=v*q as linear
  functions of the screen (see r96_soft.c). Each span is cut into segments
  of 16 pixels; u=s/q and v=t/q are exact at segment ends and linear inside.

  The divide is the Quake technique. On a Pentium, FDIV runs in the FPU for
  39 cycles (19 at the single precision r96_soft.c selects) while the
  integer pipes keep issuing, as long as no FPU instruction needs its result.
  So the reciprocal for the end of the NEXT segment is started before the
  integer texel loop of the current one. During a span's last segment the
  divide for the NEXT ROW is started instead.

  Spans of up to 16 pixels (half of all spans at long view distances) take
  one reciprocal, at the span's middle: u,v and their screen derivatives
  there give the linear mapping of the span. Its error bound equals that
  of a chord over one segment, it has no inner seam to show, and its only
  divide is the one started during the previous row, so such a row never
  waits for the FPU. Longer spans use exact segment ends, seamless across
  their 16-pixel segments; their first segment end, the one divide they
  wait for, runs under that segment's integer setup.

  Full segments get their steps from integer subtracts and shifts, so only
  the end values pass through FIST (6 cycles, unpaired); 16-pixel endpoints
  come from precomputed 16-pixel plane steps (adds only).

  The texel loop is a jump into a 16-pixel unrolled block (no counter). It
  steps u and v with byte carries; the v fraction rides in the upper half of
  EAX, whose low byte receives the texel, so no 16-bit prefixed add remains.
  The P54C does not allocate cache lines on write misses: byte stores into a
  frame buffer line that is not cached drain one by one through the two
  write buffers. Each segment therefore reads its first and last destination
  byte first, which brings both lines in; the stores then hit L1.

  Texture tiles live in 4 KB atlas pages with a 256-byte row stride; in the
  no-wrap loops EBX is the texel address (BL=level*16+u, BH=page bits+v), in
  the wrap loops EBX is the masked v:u index under the tile pointer in ESI.

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
	.set T_DQDX16,76
	.set T_DSDX16,80
	.set T_DTDX16,84

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
	flds	T_S(%ebp)
	fadds	T_DSDY(%ebp)
	flds	T_T(%ebp)
	fadds	T_DTDY(%ebp)
	fstps	T_T(%ebp)
	fstps	T_S(%ebp)
	fstps	T_Q(%ebp)
	decl	T_ROWS(%ebp)
	.endm

	/* Span of the current row: xl, count (spanLeft), destination and the
	   plane at xl, or for spans of up to 16 pixels at their middle.
	   Nonempty: starts 1/q0 there, leaving it on the FPU stack. */
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
	cmpl	$16,%ecx
	jg	70f
	fadds	halfN(,%ecx,4)		/* short span: its middle */
70:
	flds	T_DQDX(%ebp)
	fmul	%st(1),%st
	flds	T_DSDX(%ebp)
	fmul	%st(2),%st
	flds	T_DTDX(%ebp)
	fmulp	%st,%st(3)
	fadds	T_S(%ebp)
	fstps	s0
	fadds	T_Q(%ebp)
	fxch	%st(1)
	fadds	T_T(%ebp)
	fstps	t0
	fsts	q0
	fdivrs	one
73:
	.endm

	/* Endpoint of the segment starting at P0 (q0,s0,t0) for spanLeft
	   pixels: k=16 when more remain, else the last pixel (k=spanLeft-1).
	   Sets segLen and segK, computes q1,s1,t1 and starts 1/q1. */
	.macro ENDPOINT
	movl	spanLeft,%ecx
	movl	trapP,%ebp
	cmpl	$16,%ecx
	jle	74f
	movl	$16,segK
	movl	$16,segLen
	flds	s0
	fadds	T_DSDX16(%ebp)
	flds	t0
	fadds	T_DTDX16(%ebp)
	flds	q0
	fadds	T_DQDX16(%ebp)
	fxch	%st(2)
	fstps	s1
	fstps	t1
	jmp	75f
74:
	movl	%ecx,segLen
	decl	%ecx
	movl	%ecx,segK
	fildl	segK
	flds	T_DSDX(%ebp)
	fmul	%st(1),%st
	flds	T_DTDX(%ebp)
	fmul	%st(2),%st
	flds	T_DQDX(%ebp)
	fmulp	%st,%st(3)
	fadds	t0
	fstps	t1
	fadds	s0
	fstps	s1
	fadds	q0
75:
	fsts	q1
	fdivrs	one
	.endm

	/* Clamp an integer coordinate into the tile (no-wrap loops) and note
	   that the steps must then come from the clamped ends. */
	.macro CLAMP_UV reg
	cmpl	$UVLOW,\reg
	jge	76f
	movl	$UVLOW,\reg
	movl	$1,clamped
76:
	cmpl	$UVTOP,\reg
	jle	77f
	movl	$UVTOP,\reg
	movl	$1,clamped
77:
	.endm

	/* Integer setup of the segment at curDest, curLen pixels, from u0,v0:
	   texel base, fractions, store pointer and entry. Touches both ends of
	   the destination so the stores hit L1 (see above). */
	.macro SEG_PREP name,WRAP
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
	movl	trapP,%eax
	addl	T_TILE(%eax),%ebx
	.endif
	movl	%ebx,segBase
	movl	u0,%ecx
	shll	$16,%ecx
	movl	%ecx,segUf
	movl	v0,%eax
	shll	$16,%eax
	movl	%eax,segVf
	movl	curLen,%ecx
	movl	entry_\name(,%ecx,4),%eax
	movl	%eax,segTarget
	movl	curDest,%edi
	movb	(%edi),%al
	movb	-1(%edi,%ecx),%al
	leal	-16(%edi,%ecx),%edi
	movl	%edi,segEdi
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
	cmpl	$16,spanLeft
	jle	80f
	flds	s0
	fmul	%st(1),%st
	flds	t0
	fmulp	%st,%st(2)
	fsts	u0f
	fistpl	u0
	fsts	v0f
	fistpl	v0
	movl	$0,clamped
	.if !\WRAP
	CLAMP_UV u0
	CLAMP_UV v0
	.endif
	ENDPOINT
	/* Integer work under the divide of the first segment end */
	movl	spanDest,%eax
	movl	%eax,curDest
	movl	segLen,%ecx
	movl	%ecx,curLen
	movl	segK,%eax
	movl	%eax,curK
	SEG_PREP \name,\WRAP

30:	/* ---- segment: 1/q1 on the FPU stack ---- */
	flds	s1
	fmul	%st(1),%st
	flds	t1
	fmulp	%st,%st(2)
	fsts	u1f
	fistl	u1
	fxch	%st(1)
	fsts	v1f
	fistl	v1
	fxch	%st(1)
	cmpl	$16,curK
	jne	38f
	/* Full segment: integer steps from the ends */
	fstp	%st(0)
	fstp	%st(0)
	.if !\WRAP
	CLAMP_UV u1
	CLAMP_UV v1
	.endif
	movl	u1,%eax
	subl	u0,%eax
	sarl	$4,%eax
	movl	%eax,du
	movl	v1,%eax
	subl	v0,%eax
	sarl	$4,%eax
	movl	%eax,dv
	jmp	39f
38:	/* Last segment: (end-start)/k from the floats */
	movl	curK,%eax
	fsubs	u0f
	fmuls	invK(,%eax,4)
	fistpl	du
	fsubs	v0f
	fmuls	invK(,%eax,4)
	fistpl	dv
	.if !\WRAP
	CLAMP_UV u1
	CLAMP_UV v1
	cmpl	$0,clamped
	je	39f
	call	resteps
	.endif
39:

	/* What follows this segment: more segments (start the next endpoint
	   divide), one last pixel, or the next row (start its divide). */
	movl	spanLeft,%eax
	subl	curLen,%eax
	cmpl	$1,%eax
	jl	33f
	je	34f
	/* Next segment starts at P1. */
	movl	%eax,spanLeft
	movl	curDest,%eax
	addl	curLen,%eax
	movl	%eax,spanDest
	movl	q1,%eax
	movl	%eax,q0
	movl	s1,%eax
	movl	%eax,s0
	movl	t1,%eax
	movl	%eax,t0
	ENDPOINT
	movl	$1,nextState
	jmp	35f
33:	/* Last segment: prefetch the next row's start. */
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
	movl	curDest,%eax
	addl	curLen,%eax
	movl	%eax,spanDest
	movl	$4,nextState
35:
	/* ---- texel loop (the divide runs on) ---- */
	movl	du,%edx
	movl	%edx,%esi
	shll	$16,%esi
	sarl	$16,%edx
	movl	dv,%ebp
	movl	%ebp,%ecx
	sarl	$16,%ecx
	movb	%cl,%dh
	shll	$16,%ebp
	.if \WRAP
	movl	%esi,duFrac
	movl	trapP,%esi
	movl	T_TILE(%esi),%esi
	.endif
	movl	segBase,%ebx
	movl	segUf,%ecx
	movl	segVf,%eax
	movl	segEdi,%edi
	jmp	*segTarget		/* pixel i runs block index 16-n+i */
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
	jne	36f
	/* Next segment: its setup runs under its endpoint divide, so no FPU
	   instruction here (it would wait for the divide). */
	movl	u1f,%eax
	movl	%eax,u0f
	movl	v1f,%eax
	movl	%eax,v0f
	movl	$0,clamped
	movl	spanDest,%eax
	movl	%eax,curDest
	movl	segLen,%eax
	movl	%eax,curLen
	movl	segK,%eax
	movl	%eax,curK
	SEG_PREP \name,\WRAP
	jmp	30b
36:
	cmpl	$2,%eax
	je	20b		/* next row, its start divide in flight */
	cmpl	$3,%eax
	je	99f		/* trapezoid done */
	cmpl	$5,%eax
	je	51f		/* prefetched row is empty */
	jmp	40f		/* one last pixel at u0,v0 */

80:	/* ---- span of up to 16 pixels: value and slope at its middle ---- */
	movl	trapP,%ebp
	movl	spanLeft,%ecx
	flds	s0
	fmul	%st(1),%st
	fstps	umf
	flds	t0
	fmul	%st(1),%st
	fstps	vmf
	flds	umf
	fmuls	T_DQDX(%ebp)
	fsubrs	T_DSDX(%ebp)
	fmul	%st(1),%st
	fsts	duf
	fistpl	du
	flds	vmf
	fmuls	T_DQDX(%ebp)
	fsubrs	T_DTDX(%ebp)
	fmulp	%st,%st(1)
	fsts	dvf
	fistpl	dv
	flds	duf
	fmuls	halfN(,%ecx,4)
	fsubrs	umf
	fistpl	u0
	flds	dvf
	fmuls	halfN(,%ecx,4)
	fsubrs	vmf
	fistpl	v0
	.if !\WRAP
	/* Both ends inside the tile; clamped ends give the steps instead. */
	flds	duf
	fmuls	halfN(,%ecx,4)
	fadds	umf
	fistpl	u1
	flds	dvf
	fmuls	halfN(,%ecx,4)
	fadds	vmf
	fistpl	v1
	movl	$0,clamped
	CLAMP_UV u0
	CLAMP_UV v0
	CLAMP_UV u1
	CLAMP_UV v1
	cmpl	$0,clamped
	je	81f
	cmpl	$1,%ecx
	je	81f
	decl	%ecx
	movl	%ecx,curK
	call	resteps
	movl	spanLeft,%ecx
81:
	.endif
	movl	spanDest,%eax
	movl	%eax,curDest
	movl	%ecx,curLen
	SEG_PREP \name,\WRAP
	/* The span is the row: start the next row's divide under it. */
	ROW_ADVANCE
	movl	$3,nextState
	cmpl	$0,T_ROWS(%ebp)
	jle	35b
	ROW_SETUP
	movl	$2,nextState
	cmpl	$0,spanLeft
	jg	35b
	movl	$5,nextState
	jmp	35b

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

/* Steps from the clamped integer ends: (end-start)*invK[curK]. */
resteps:
	movl	curK,%eax
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
one:	.float	1.0
halfN:	.float	0.0		/* (n-1)/2: middle of an n-pixel span */
	.float	0.0,0.5,1.0,1.5,2.0,2.5,3.0,3.5,4.0,4.5,5.0,5.5,6.0,6.5,7.0,7.5
invK:	.float	1.0
	.float	1.0,0.5,0.33333334,0.25,0.2,0.16666667,0.14285715,0.125
	.float	0.11111111,0.1,0.09090909,0.083333336,0.07692308,0.071428575,0.06666667,0.0625

	.bss
	.align 4
trapP:		.long 0
spanX:		.long 0
spanLeft:	.long 0
spanDest:	.long 0
nextState:	.long 0
segLen:		.long 0
segK:		.long 0
curLen:		.long 0
curK:		.long 0
curDest:	.long 0
clamped:	.long 0
segBase:	.long 0
segUf:		.long 0
segVf:		.long 0
segEdi:		.long 0
segTarget:	.long 0
u0:		.long 0
v0:		.long 0
u1:		.long 0
v1:		.long 0
du:		.long 0
dv:		.long 0
duFrac:		.long 0
umf:		.long 0
vmf:		.long 0
duf:		.long 0
dvf:		.long 0
u0f:		.long 0
v0f:		.long 0
u1f:		.long 0
v1f:		.long 0
q0:		.long 0
s0:		.long 0
t0:		.long 0
q1:		.long 0
s1:		.long 0
t1:		.long 0
