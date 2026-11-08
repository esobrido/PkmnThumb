#include "ow/map.h"

int ow_block_at(const OwMap *m, int bx, int by) {
    if (bx < 0 || bx >= m->w || by < 0 || by >= m->h) return 0;
    return m->blocks[by * m->w + bx];
}

int ow_cell_blocked(const OwMap *m, int cx, int cy) {
    /* Check the cell first: C truncates towards zero, so -1 / 2 == 0 and a
     * negative cell would otherwise read block 0. */
    if (cx < 0 || cy < 0) return 1;
    int bx = cx / 2, by = cy / 2;              /* 16 px cell -> 32 px block */
    if (bx >= m->w || by >= m->h) return 1;
    if (m->cells && m->cells[cy * (m->w * 2) + cx]) return 1;
    int quad = (cy & 1) * 2 + (cx & 1);        /* TL, TR, BL, BR */
    return (m->mt_coll[ow_block_at(m, bx, by)] >> quad) & 1;
}

int ow_px_blocked(const OwMap *m, int px, int py) {
    return ow_cell_blocked(m, px / OW_STEP_PX, py / OW_STEP_PX);
}
