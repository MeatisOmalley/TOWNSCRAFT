/* Translated IBM AT set-1 input for the unchanged Towns keyboard API. */
#ifndef DOSCRAFT_KEYBOARD_H
#define DOSCRAFT_KEYBOARD_H

extern volatile unsigned char g_keyDown[128];
int key_get_event(void);
void key_flush(void);

/* Clear held keys, pending events, and partial E0/E1 sequences. Call before
 * enabling the producer, or to recover after lost input. No allocation/I/O. */
void dos_keyboard_reset(void);

/* Feed one translated set-1 byte, including make/break and E0/E1 prefixes.
 * One serialized producer only: future IRQ1 must run with interrupts masked
 * and must not reenter this function. Foreground feeding requires the same
 * exclusion. Controller commands/replies and hardware access belong outside
 * this module (BAT 0xAA is indistinguishable from left Shift break here).
 * Pause's E1 1D 45 E1 9D C5 is discarded. On a malformed Pause tail, discard
 * the mismatching byte and resume on the next byte; E0/E1 can start anew.
 * Fake E0 shifts, PrintScreen/SysRq, and unsupported keys produce no events.
 * No modifier/NumLock policy: keypad 8/4/5/6/2 retain Towns KEY_NUM_* codes.
 * KEY_EXECUTE has no AT equivalent and is unused by the pinned game. */
void dos_keyboard_scancode(unsigned char byte);

/* REQUIRED platform hooks; deliberately no default/no-op implementation.
 * Save returns an opaque interrupt-state token and masks the producer IRQ
 * before returning. Restore reinstates exactly that state (never blindly
 * enables interrupts). Both must be compiler memory barriers. Implement via
 * the DOS runtime's supported IRQ exclusion, not unrestricted user-mode CLI.
 * Used around reset, dequeue, and flush. IRQ1's feed itself takes no lock.
 * This is a single-CPU ISR contract, not host thread synchronization.
 * When installing IRQ1, lock all adapter code/data and these hooks in memory
 * as required by DPMI, including static tables/state; arrange that outside
 * this module. g_keyDown is read-only to the game; writes belong here. */
unsigned long dos_keyboard_irq_save(void);
void dos_keyboard_irq_restore(unsigned long state);

#endif
