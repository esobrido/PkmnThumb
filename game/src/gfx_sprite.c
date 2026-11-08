#include "gfx_sprite.h"

void sprite_expand_tile(uint16_t *dst, const uint8_t *src, const uint16_t *pal) {
    for (int y = 0; y < SPR_TILE_PX; y++) {
        uint8_t lo = src[y * 2], hi = src[y * 2 + 1];
        for (int x = 0; x < SPR_TILE_PX; x++) {
            int bit = 7 - x;
            int idx = ((lo >> bit) & 1) | (((hi >> bit) & 1) << 1);
            dst[y * SPR_TILE_PX + x] = pal[idx];
        }
    }
}

void sprite_draw_frame(uint16_t *fb, int fbw, int fbh, const uint16_t *sheet,
                       int frame, int x, int y, int flip) {
    sheet += (long)frame * SPR_FRAME_TILES * SPR_TILE_PX2;

    /* Mirror the whole 16x16 frame, not each 8x8 tile: mirroring per tile alone
     * would leave the two tile columns in place and produce a torn sprite. */
    for (int gy = 0; gy < 2 * SPR_TILE_PX; gy++) {
        int sy = y + gy;
        if (sy < 0 || sy >= fbh) continue;
        for (int gx = 0; gx < 2 * SPR_TILE_PX; gx++) {
            int sx = x + (flip ? (2 * SPR_TILE_PX - 1 - gx) : gx);
            if (sx < 0 || sx >= fbw) continue;
            int tile_i = (gy / SPR_TILE_PX) * 2 + (gx / SPR_TILE_PX);
            uint16_t c = sheet[tile_i * SPR_TILE_PX2 +
                               (gy % SPR_TILE_PX) * SPR_TILE_PX +
                               (gx % SPR_TILE_PX)];
            if (c) fb[sy * fbw + sx] = c;   /* 0 = transparent */
        }
    }
}
