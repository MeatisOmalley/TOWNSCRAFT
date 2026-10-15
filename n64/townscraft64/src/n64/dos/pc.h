/* N64 stand-in for DJGPP <pc.h>: there is no PC speaker or I/O port space.
 * DOScraft's game_audio.c only reaches these on its PC-speaker fallback,
 * which the N64 never selects (its audio interface always starts). */
#ifndef TOWNSCRAFT64_PC_H
#define TOWNSCRAFT64_PC_H
static inline void outportb(unsigned short port, unsigned char v) { (void)port; (void)v; }
static inline unsigned char inportb(unsigned short port) { (void)port; return 0; }
#endif
