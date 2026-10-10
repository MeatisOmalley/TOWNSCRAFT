/* Fixed primary-target SB1 transport; foreground single-cycle submissions.
 * Requires normal resident DOS startup. Not a streaming mixer or OPL driver. */
#ifndef DOSCRAFT_SB_PCM_H
#define DOSCRAFT_SB_PCM_H
#define DOS_SB_BLOCK_BYTES 2048u
#define DOS_SB_RATE (1000000u / 91u)
int dos_sb_init(void); /* 0 success, -1 failure; no live reinitialization */
void dos_sb_shutdown(void); /* stop/reset DMA before releasing memory */
/* Copy one unsigned mono block into owned conventional memory. 0 accepted,
 * 1 still busy, -1 invalid/uninitialized or DSP command failure. Foreground
 * only: no DOS, mixing or DSP command polling occurs inside IRQ7. */
int dos_sb_submit(const unsigned char *samples,unsigned int bytes);
int dos_sb_busy(void);
unsigned int dos_sb_version(void);
unsigned int dos_sb_dma_address(void);
extern volatile unsigned int dos_sb_completions,dos_sb_spurious;
#endif
