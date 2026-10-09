/* DOS hardware transport for the unchanged 100 Hz clock and key API. */
#ifndef DOSCRAFT_IRQ_H
#define DOSCRAFT_IRQ_H
int dos_irq_init(void);
void dos_irq_shutdown(void);
/* No callback until audio is ported. A callback and everything it accesses
 * must be resident, bounded, integer-only, and must not call DOS/BIOS/heap. */
void dos_irq_set_tick_hook(void (*hook)(void));
extern volatile unsigned int dos_irq_keyboard_count;
extern volatile unsigned int dos_irq_bios_count;
#endif
