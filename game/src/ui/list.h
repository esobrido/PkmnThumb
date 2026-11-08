/*
 * ui/list.h — a vertical menu: a framed box with one row per item and a ▶ on
 * the selected one.
 *
 * This is the piece both the pause menu and (later) the party, bag and option
 * screens share: it owns the *window* (which items are visible, where the cursor
 * is), never the items themselves — those arrive as the game's own bytecode, so
 * `#mon` still prints "Pokémon" and <PLAYER> the player's name.
 *
 * The cursor wraps around, like the original's menu flags ($a8: wrap around,
 * START closes), and the window follows it, so a list can be longer than the box.
 *
 * Pure logic + blitting into a framebuffer that the caller owns: no Mote calls,
 * no globals, no allocation.
 */
#ifndef UI_LIST_H
#define UI_LIST_H

#include <stdint.h>

#include "gen/ui.h"
#include "text/font.h"

/* The game's own menu glyphs (constants/charmap.asm): ▶ $f0, ◀ $ef, ▲ $ee,
 * ▼ $ed, and the two scroll arrows from the "common" font, ↑ $f2 and ↓ $f3.
 * Getting these off by one is invisible until a list is long enough to scroll. */
#define UI_CURSOR 0xf0
#define UI_ARROW_UP 0xf2
#define UI_ARROW_DOWN 0xf3

/* Most rows a UiList can draw. Four fit the pause menu; the scrolling tests use
 * a shorter window over a longer list. */
#define UI_LIST_ROWS_MAX 8

typedef struct {
    int x, y;     /* top-left of the box, in pixels */
    int w;        /* box width, in pixels (the height follows from `rows`) */
    int rows;     /* visible item rows */
    int count;    /* items in the list */
    int top;      /* first visible item */
    int cursor;   /* selected item */
} UiList;

/* Box height for `rows` rows: a 1 px frame and 3 px of padding above and below. */
int ui_list_height(int rows);

void ui_list_init(UiList *l, int x, int y, int w, int rows, int count);

/* Change the list length (a bag grows, a party is healed): clamps the cursor and
 * tries to keep it on screen. */
void ui_list_set_count(UiList *l, int count);

/* Move the cursor (normally +1/-1), wrapping around. Returns 1 if it moved, so
 * the caller can play a click only when something visible happened. */
int ui_list_move(UiList *l, int delta);

/* Select an item directly (START remembers the last entry). */
void ui_list_select(UiList *l, int index);

/* Item drawn on visible row `r`, or -1 past the end of the list. */
int ui_list_item_at(const UiList *l, int r);

/* Visible row the cursor sits on, or -1 when the list is empty. */
int ui_list_cursor_row(const UiList *l);

/* Frame, background, rows and cursor. `items` must hold at least `l->count`
 * entries; `names` supplies <PLAYER>/<RIVAL> (NULL for the defaults) and may be
 * NULL for lists that have neither. Clipped to the band [y0, y1), like every
 * other draw call in the port. */
void ui_list_draw(const UiList *l, const PcUiLabel *items, const FontNames *names,
                  uint16_t *fb, int fbw, int y0, int y1,
                  uint16_t ink, uint16_t paper);

#endif
