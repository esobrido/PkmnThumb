#include "ow/render.h"

void ow_render_band(uint16_t *fb, int fbw, int fbh, int y0, int y1,
                    const OwMap *m, const uint8_t (*tiles)[16],
                    const uint16_t (*pals)[4], int cam_x, int cam_y) {
    if (y0 < 0) y0 = 0;
    if (y1 > fbh) y1 = fbh;
    if (y1 <= y0) return;

    /* Walk whole 8x8 tiles and clip their rows to this band. */
    int ty0 = (y0 + cam_y) >> 3, ty1 = ((y1 - 1) + cam_y) >> 3;
    for (int ty = ty0; ty <= ty1; ty++) {
        int my = ty * 8;
        int sy0 = my - cam_y;
        if (sy0 < y0) sy0 = y0;
        int sy1 = my + 8 - cam_y;
        if (sy1 > y1) sy1 = y1;
        if (sy1 <= sy0) continue;

        for (int tx = 0; tx < fbw / 8; tx++) {
            int mx = tx * 8 + cam_x;
            int b = ow_block_at(m, mx >> 5, my >> 5);
            int ti = (((my >> 3) & 3) * 4) + ((mx >> 3) & 3);
            const uint8_t *src = tiles[m->mt_tiles[b][ti]];
            const uint16_t *pal = pals[m->mt_pal ? m->mt_pal[b * 16 + ti] : 0];

            for (int sy = sy0; sy < sy1; sy++) {
                int row = (sy + cam_y) - my;
                uint16_t *dst = &fb[sy * fbw + tx * 8];
                uint8_t lo = src[row * 2], hi = src[row * 2 + 1];
                for (int x = 0; x < 8; x++) {
                    int bit = 7 - x;
                    dst[x] = pal[((lo >> bit) & 1) | (((hi >> bit) & 1) << 1)];
                }
            }
        }
    }
}
