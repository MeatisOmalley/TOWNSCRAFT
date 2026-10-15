/* Loaded by the IPL at 10800h.  Everything must fit in a 2MB machine:
   code, data and small BSS live in conventional memory below B8000h, and
   large arrays are allocated at run time (see sys.c heap). */
ENTRY(_start)
SECTIONS
{
	. = 0x10800;
	.text : { *(.text.entry) *(.text .text.*) }
	.rodata : { *(.rodata .rodata.*) }
	.data : { *(.data .data.*) }
	__image_end = .;

	.bss (NOLOAD) : {
		. = ALIGN(16);
		__bss_start = .;
		*(.bss .bss.*) *(COMMON)
		. = ALIGN(16);
		__bss_end = .;
	}
	__stack_top = 0xBFFF0;
	/DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
