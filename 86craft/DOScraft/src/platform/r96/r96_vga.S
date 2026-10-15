/*
  Mode X plane writer for the Pentium software build.

  The 486 presenter deinterleaves the whole 320x240 frame byte by byte into
  a system-memory buffer, then copies each plane to the VGA. On a P54C the
  76,800 byte stores into that buffer miss L1 (no write allocate) and drain
  one by one through the write buffers, and every byte is read and written
  twice. Here a plane's four pixels are gathered from the source row into
  one dword with byte-register loads and stored straight into the VGA plane:
  one 32-bit PCI write per four pixels, no intermediate buffer. The caller
  works in bands of rows small enough to stay in L1 across the four planes.

  void r96_vga_plane_rows(const u8 *src, u32 pitch, u32 rows, u32 *vga)
    src: first pixel of this plane in the band's first row (row + plane)
    vga: the plane's first dword of that row in VGA memory (80 bytes a row)
*/
	.code32
	.text
	.globl	_r96_vga_plane_rows
_r96_vga_plane_rows:
	pushl	%ebp
	pushl	%edi
	pushl	%esi
	pushl	%ebx
	movl	20(%esp),%esi		/* src */
	movl	28(%esp),%ebp		/* rows */
	movl	32(%esp),%edi		/* vga */
	testl	%ebp,%ebp
	jz	9f
1:	/* row: 20 dwords, two per iteration (EAX and EBX interleave) */
	movl	$10,%ecx
2:
	movb	8(%esi),%al
	movb	24(%esi),%bl
	movb	12(%esi),%ah
	movb	28(%esi),%bh
	shll	$16,%eax
	shll	$16,%ebx
	movb	(%esi),%al
	movb	16(%esi),%bl
	movb	4(%esi),%ah
	movb	20(%esi),%bh
	movl	%eax,(%edi)
	movl	%ebx,4(%edi)
	addl	$32,%esi
	addl	$8,%edi
	decl	%ecx
	jnz	2b
	addl	24(%esp),%esi		/* next row: pitch-320 further */
	subl	$320,%esi
	decl	%ebp
	jnz	1b
9:
	popl	%ebx
	popl	%esi
	popl	%edi
	popl	%ebp
	ret
