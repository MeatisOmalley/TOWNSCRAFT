/* Polled, freestanding FM TOWNS SCSI ID 0 / LUN 0 block access. */
#ifndef HDD_H
#define HDD_H

#include "hw.h"

#define HDD_SECTOR_BYTES 512u
#define HDD_MAX_SECTORS 24u

/* Call after sys_init. Returns 1 ready, 0 unavailable. Idempotent once
   ready; a failed probe can be retried. Uses only INQUIRY, TEST UNIT READY,
   REQUEST SENSE (one unit-attention retry), and READ CAPACITY: never writes
   the disk. Missing-controller reads of FF fail immediately; no selection
   response times out after 200ms, including when timer IRQs are disabled.
   Allocates a DMA bounce buffer only after a successful read-only probe.
   Only direct-access, non-removable, 512-byte-sector disks are accepted. */
int hdd_init(void);

/* Usable sector COUNT, not last LBA; 0 until a successful probe.
   Local Tsugaru's TSUGARU/HARDDISK identity reports count directly.
   Ordinary targets report last LBA, to which the driver adds one. */
u32 hdd_sector_count(void);

/* One outstanding request. count must be 1..HDD_MAX_SECTORS, write 0 or 1.
   Returns 0 pending, 1 complete, -1 error. Repeated calls with IDENTICAL
   arguments advance the same request (including its first call). A different
   request while pending fails without replacing the original request.
   After a terminal result the next call starts a NEW request, even with
   identical arguments. Keep the buffer allocated and, for writes, unchanged
   until completion or cancellation. A read buffer is valid only on success.

   Each call drains up to 32 immediately progressing state steps or 2048
   copied bytes, stopping at the first hardware wait without spinning.
   Payloads use DMA channel 1 because
   Tsugaru implements no PIO payload transfers. Command/status/message bytes
   use ports C30/C32. No callbacks, IRQ handler, allocation, or FPU at runtime.
   Poll regularly from one main-loop owner; not reentrant or ISR safe.
   Each hardware wait has a 3s deadline (selection 200ms) and a finite poll
   limit if all clocks stop. Real phase/byte progress refreshes that deadline;
   already-ready hardware is consumed before checking for a timeout. Command
   length and phase order are finite, so progress cannot extend it forever.

   Disk ownership, signatures, record checksums, scratch bounds, and two-slot
   commit policy belong to the caller. This driver checks device bounds only.
   Failed/cancelled writes may have reached the target: no automatic retry.
   It provides command completion, not power-loss durability or atomicity.

   Owns the SCSI bus and DMA channel 1 while pending. Other DMA channels and
   the channel-register selector are preserved. Existing floppy code masks
   ALL DMA channels, so cancel/finish HDD I/O before calling floppy routines.
   Timeout/protocol failure/cancel masks channel 1 and resets this SCSI
   controller; it does not reset the shared DMA controller. */
int hdd_transfer(u32 lba,u32 count,int write,u8 *buffer);

/* Startup-only convenience: polls the same bounded engine to completion.
   Returns 1 complete or -1 error. Do not use in terrain traversal. */
int hdd_transfer_sync(u32 lba,u32 count,int write,u8 *buffer);

/* Stops the pending request, masks its DMA, and releases/reset its SCSI bus.
   No effect when idle. The caller may release its buffer after return. */
void hdd_cancel(void);

/* Diagnostics do not affect the compact transfer return convention. */
enum
{
	HDD_ERROR_NONE,
	HDD_ERROR_ARGUMENT,
	HDD_ERROR_BUSY,
	HDD_ERROR_ABSENT,
	HDD_ERROR_TIMEOUT,
	HDD_ERROR_PROTOCOL,
	HDD_ERROR_STATUS,
	HDD_ERROR_CAPACITY,
	HDD_ERROR_MEMORY,
	HDD_ERROR_CANCELLED,
	HDD_ERROR_DMA
};
int hdd_error(void);

#endif
