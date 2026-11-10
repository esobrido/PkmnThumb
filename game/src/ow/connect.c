#include "ow/connect.h"

const PcConnection *ow_connect_at(const PcConnection *conns, int count, int dir) {
    if (!conns || dir == OW_DIR_NONE) return 0;
    for (int i = 0; i < count; i++)
        if (conns[i].dir == (uint8_t)dir) return &conns[i];
    return 0;
}

int ow_connect_on_edge(int cx, int cy, int w, int h, int dir) {
    int cw = w * 2, ch = h * 2;
    if (cx < 0 || cy < 0 || cx >= cw || cy >= ch) return 0;
    switch (dir) {
        case OW_DIR_LEFT:  return cx == 0;
        case OW_DIR_RIGHT: return cx == cw - 1;
        case OW_DIR_UP:    return cy == 0;
        case OW_DIR_DOWN:  return cy == ch - 1;
        default:           return 0;
    }
}

int ow_connect_land(const PcConnection *c, int dest_w, int dest_h, int cx, int cy,
                    int *px, int *py) {
    if (!c || c->map < 0 || dest_w <= 0 || dest_h <= 0) return 0;
    int cw = dest_w * 2, ch = dest_h * 2;
    int x = cx, y = cy;
    int shift = 2 * c->offset;

    switch (c->dir) {
        /* Leaving west puts you on the destination's east edge, and so on: the
         * two cells touch across the seam. The offset runs along the edge, which
         * is y for a east/west seam and x for a north/south one. */
        case OW_DIR_LEFT:  x = cw - 1; y = cy - shift; break;
        case OW_DIR_RIGHT: x = 0;      y = cy - shift; break;
        case OW_DIR_UP:    y = ch - 1; x = cx - shift; break;
        case OW_DIR_DOWN:  y = 0;      x = cx - shift; break;
        default: return 0;
    }

    if (x < 0 || x >= cw || y < 0 || y >= ch) return 0;
    *px = x * OW_STEP_PX;
    *py = y * OW_STEP_PX;
    return 1;
}
