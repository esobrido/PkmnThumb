/*
 * ui/frame.c — the framed paper box the windows share.
 *
 * Kept apart from ui/list because a box is not a list: the option screen is one
 * full-screen box of two-line rows, and the textbox is a third shape again. One
 * pixel loop, three callers.
 */
#include "ui/frame.h"

void ui_frame_box(uint16_t *fb, int fbw, int y0, int y1,
                  int x, int y, int w, int h, uint16_t ink, uint16_t paper) {
    int right = x + w - 1, bottom = y + h - 1;
    for (int py = y; py <= bottom; py++) {
        if (py < y0 || py >= y1) continue;
        uint16_t *row = &fb[py * fbw];
        for (int px = x; px <= right; px++) {
            if (px < 0 || px >= fbw) continue;
            int frame = py == y || py == bottom || px == x || px == right;
            row[px] = frame ? ink : paper;
        }
    }
}
