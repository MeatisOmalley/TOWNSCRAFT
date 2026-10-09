#ifndef DOS_INPUT_H
#define DOS_INPUT_H
/* Foreground INT 33 polling; the resident DOS driver owns serial IRQ4.
 * init returns 1 when installed, 0 when absent or unavailable. Keyboard-only
 * use remains valid. Shutdown does not uninstall the externally loaded TSR. */
int dos_input_init(void);
void dos_input_shutdown(void);
int dos_input_mouse_present(void);
#endif
