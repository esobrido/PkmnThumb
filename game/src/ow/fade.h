/*
 * ow/fade.h — a white fade over an RGB565 framebuffer.
 *
 * The original fades every palette, background and sprites alike, and that is
 * all a door transition is: FadeOutPalettes (10 frames to white), the map swap,
 * FadeInPalettes (10 frames back). Once the 2bpp art is expanded to RGB565 there
 * is no palette left to lean on — sprites carry their colours in their own
 * buffers — so the port fades the pixels themselves, as the last drawing step of
 * the frame. Pure: a buffer and a level in, pixels out, no Mote and no state.
 */
#ifndef OW_FADE_H
#define OW_FADE_H

#include <stdint.h>

/* FadeOutPalettes and FadeInPalettes both pass c = 10 (timeofday_pals.asm), so
 * a door takes 20 frames of screen time: 10 out, 10 in. */
enum { OW_FADE_STEPS = 10 };

/* Blend the first `pixels` RGB565 pixels of `fb` towards white by level/STEPS.
 * level <= 0 leaves the buffer alone; level >= OW_FADE_STEPS makes it white. */
void ow_fade_apply(uint16_t *fb, int pixels, int level);

#endif
