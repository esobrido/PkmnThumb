/*
 * ui/frame.h — the box every window in the port is drawn in.
 *
 * The original's textboxes, menus and option screens all share one look: a frame
 * with a paper fill, drawn with palettes 7 (white on black). That is the shape the
 * port copies — a 1 px frame, not the GB's 6-frame tileset, because at 128x128 the
 * GB's thick border eats a whole tile of text. Where a frame `tileset` is used is
 * `text/box.c`'s business; this is the plain one the menus use.
 *
 * Pure blitting: no globals, no allocation, clipped to the band [y0, y1).
 */
#ifndef UI_FRAME_H
#define UI_FRAME_H

#include <stdint.h>

/* 1 px frame of `ink` around a `paper` fill, top-left at (x, y). The interior is
 * x+1 .. x+w-2 by y+1 .. y+h-2, so a box of w=128 fills the panel's full width. */
void ui_frame_box(uint16_t *fb, int fbw, int y0, int y1,
                  int x, int y, int w, int h, uint16_t ink, uint16_t paper);

#endif
