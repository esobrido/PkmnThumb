/*
 * ow/render.h — the banded map raster.
 *
 * Pure logic over plain framebuffers: called by Mote on both cores with disjoint
 * row bands, and called directly by tests with a fake framebuffer. It decodes
 * the 2bpp art on the fly with each tile's own BG palette, so the four shades
 * keep the spacing the game gives them.
 */
#ifndef OW_RENDER_H
#define OW_RENDER_H

#include "ow/map.h"

/* Draw screen rows [y0, y1) of the map window at (cam_x, cam_y).
 * `fb` is `fbw * fbh` RGB565 pixels; `tiles` is the 2bpp sheet, `pals` the 8 BG
 * palettes of 4 colours. */
void ow_render_band(uint16_t *fb, int fbw, int fbh, int y0, int y1,
                    const OwMap *m, const uint8_t (*tiles)[16],
                    const uint16_t (*pals)[4], int cam_x, int cam_y);

#endif
