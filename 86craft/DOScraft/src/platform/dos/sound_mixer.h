/* Foreground-only integer PCM mixer; no hardware or gameplay flag handling. */
#ifndef DOSCRAFT_SOUND_MIXER_H
#define DOSCRAFT_SOUND_MIXER_H

#define DOS_MIXER_DEFAULT_RATE (1000000u / 91u) /* 10989 Hz, SB time constant 165 */
#define DOS_MIXER_MAX_RENDER_FRAMES 65536u

/* Assets must already have been built. Every init clears all voices and the
 * round-robin cursor, including a failed init. Returns 0 on success, -1 for a
 * zero rate or unavailable wave. Any positive unsigned rate is accepted.
 * Storage is borrowed from sound_assets; stop before rebuilding those assets.
 * All calls must be serialized in the foreground, never from an IRQ handler. */
int dos_mixer_init(unsigned int output_rate);

/* Seven one-shot voices, replaced round-robin only for an accepted, audible
 * request. Invalid IDs/descriptors, vol outside 0..255, and scaled vol == 0
 * are ignored. Pitch is an integer with 256 = nominal; even negative pitch
 * follows the lower clamp. FD = clamp(base_pitch*8*pitch >> 8, 64, 65535),
 * calculated wide. ENV = caller_vol*base_vol >> 8.
 * Pan clamps to -15..15. Source L=15-max(0,pan), R=15-max(0,-pan) fold to
 * (L+R)/30: center gain 1, either edge gain 1/2. Each signed source sample
 * contributes trunc_toward_zero((sample-128)*ENV*(L+R)/(256*30)). Contributions
 * sum directly, then saturate to -128..127 and re-bias to unsigned 8-bit.
 * This is a documented mono policy, not a claim of analog RF5c68 fidelity. */
void dos_mixer_play(int id,int pitch,int vol,int pan);

/* Eighth voice, centered, at nominal pitch. Updating the same active ID keeps
 * its phase; changing ID restarts it. A valid ID with zero scaled volume stops
 * only the loop. Invalid requests are ignored (including invalid ID + vol 0).
 * Loop phase wraps modulo its whole Q16 length, including multiple wraps. */
void dos_mixer_loop(int id,int vol);

/* Nearest source sample by floor(Q16 phase), no interpolation. The Q16 step
 * is floor(20833*FD*65536/(2048*output_rate)), computed in 64 bits when a voice
 * starts, outside rendering. One-shots become silent at their end.
 * The caller supplies at least frames writable bytes; frames may be 0..MAX.
 * Returns 0 on success; -1 for NULL with nonzero frames or frames > MAX,
 * without writing or advancing voices. NULL with zero frames is valid.
 * Before init/after failed init, valid renders produce unsigned silence 128. */
int dos_mixer_render(unsigned char *out,unsigned int frames);

/* Clear all voices and reset the round-robin cursor; retain the output rate. */
void dos_mixer_stop(void);

/* g_sfxOn, positional attenuation/listener state and music remain the future
 * gameplay adapter's responsibility. This module neither reads nor sets them. */
#endif
