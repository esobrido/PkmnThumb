/*
 * text/box.h — a textbox over the game's own text bytecode.
 *
 * The text a map carries is not a string: it is the stream the original prints,
 * with $56/$57/$58/$59 for page/line breaks and $52 for the end. This module
 * turns that stream into a 15-column, 3-row page, re-wrapping the original's
 * 20-column lines (the GB is 160 px wide, this panel is 128), reveals it one
 * glyph every other frame, and waits for A between pages.
 *
 * The reflow joins the original's lines with a space and breaks on words, so a
 * `<PARA>` becomes a new page and a `<LINE>` a new line as the game intended,
 * but the exact pixel column of a break may differ. Text that relies on manual
 * alignment (the Pokédex, menus) will not use this path anyway.
 *
 * Pure logic: no Mote calls, no globals, no allocation.
 */
#ifndef TEXT_BOX_H
#define TEXT_BOX_H

#include <stdint.h>

#include "text/font.h"

#define TB_COLS 15
#define TB_ROWS 3
#define TB_W (TB_COLS * 8 + 8)   /* 128 px: the whole panel width */
#define TB_H (TB_ROWS * 8 + 8)   /* 32 px */

/* One glyph every TB_TICKS frames, like the GB's text printer. This is only the
 * default: game.c sets it from the Option screen (textbox_set_speed), because the
 * game's own text speed is 5/3/1 frames per letter or instant. */
#define TB_TICKS 2

/* ▼: the "press A" arrow in the bottom-right cell (charmap $ed). */
#define TB_ARROW 0xed

typedef struct {
    const uint8_t *src;
    int len, pos;

    /* The current code may expand to several glyphs (Poké, <PLAYER>, ...). */
    uint8_t sub[FONT_EXPAND_MAX];
    int sub_len, sub_at;

    uint8_t cell[TB_ROWS][TB_COLS];
    uint8_t seq[TB_ROWS][TB_COLS];   /* 1..seq_count = order of appearance */
    int seq_count;
    int revealed;
    int ticks_per_glyph;             /* 0 = print the whole page at once */
    int ticks;                       /* frames shown, for the arrow blink */
    int wait_frames;
    int waiting;                     /* fully revealed: waiting for A */
    int more;                        /* another page follows */
    int open;

    uint8_t word[TB_COLS];
    uint8_t pend[TB_COLS];
    int pend_len;
    FontNames names;
} TextBox;

/* Start showing `src[0..len)`. Nothing is drawn until the first update. */
void textbox_open(TextBox *b, const uint8_t *src, int len, const FontNames *names);

/* Reveal glyphs (1 per TB_TICKS frames). Ignored once the page is complete. */
void textbox_update(TextBox *b, int frames);

/* Glyph speed: frames to wait between letters (0 shows the page immediately).
 * The original's Slow/Medium/Fast are 5/3/1 and Instant is 0; TB_TICKS is the
 * default until the caller says otherwise. */
void textbox_set_speed(TextBox *b, int ticks);

/* A pressed: finish revealing, or go to the next page, or close. */
void textbox_advance(TextBox *b);

int textbox_is_open(const TextBox *b);
int textbox_waiting(const TextBox *b);

/* Glyph code in a cell of the current page, 0 if the cell is empty. */
uint8_t textbox_cell(const TextBox *b, int row, int col);

/* Has the cell been revealed yet? (A hidden cell still occupies its slot.) */
int textbox_visible(const TextBox *b, int row, int col);

/* Draw the box with its top-left corner at (x, y), clipped to screen rows
 * [y0, y1) — it is called from the band renderer, on both cores. */
void textbox_draw(const TextBox *b, uint16_t *fb, int fbw, int y0, int y1,
                  int x, int y, uint16_t ink, uint16_t paper);

#endif
