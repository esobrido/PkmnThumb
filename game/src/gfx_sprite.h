/*
 * gfx/sprite.h — 2bpp sprite expansion and blitting.
 *
 * Overworld sprites are 16x96 sheets: 6 frames of 16x16 (2x2 tiles), in the
 * order down, up, left (standing + one walk frame). Colour index 0 is
 * transparent. Expansion is done once into RGB565 (0 = transparent); blitting
 * clips to the framebuffer and honours the horizontal mirror used for facing
 * right.
 *
 * Pure logic over plain arrays: no Mote calls, so it is unit-testable.
 */
#ifndef GFX_SPRITE_H
#define GFX_SPRITE_H

#include <stdint.h>

#define SPR_TILE_PX 8
#define SPR_TILE_PX2 (SPR_TILE_PX * SPR_TILE_PX)
#define SPR_FRAME_TILES 4        /* one 16x16 frame = 2x2 tiles */

/* Expand one 2bpp GB tile (16 bytes) into 64 RGB565 pixels.
 * Index 0 maps to pal[0], which the caller sets to 0 for transparency. */
void sprite_expand_tile(uint16_t *dst, const uint8_t *src, const uint16_t *pal);

/* Draw the 16x16 frame `frame` of `sheet` (already expanded) at (x, y). */
void sprite_draw_frame(uint16_t *fb, int fbw, int fbh, const uint16_t *sheet,
                       int frame, int x, int y, int flip);

#endif
