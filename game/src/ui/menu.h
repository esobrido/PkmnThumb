/*
 * ui/menu.h — the START menu.
 *
 * The original's pause menu is a box on the right of the screen with one entry
 * per line (engine/menus/start_menu.asm). It builds that list at runtime from
 * save flags — no Pokédex before you get one, no Pokémon entry with an empty
 * party — which is a save-file concern; until saves exist this shows the four
 * entries that always apply, in the game's order: #dex, #mon, Bag, <PLAYER>.
 *
 * The entries are the game's own labels, extracted as charmap bytes, so `#dex`
 * prints "POKédex" and <PLAYER> your name without a second font path.
 *
 * Pure logic: the caller owns the framebuffer and the input.
 */
#ifndef UI_MENU_H
#define UI_MENU_H

#include <stdint.h>

#include "ui/list.h"

/* The four entries, as they come from start_menu.asm (PC_UI_* in gen/ui.h). */
#define UI_MENU_ITEMS PC_UI_LABEL_COUNT

/* The original's box is 10 tiles wide (menu_coords 10, 0, 19, 17) and flush with
 * the right edge. Ten tiles is also what holds the longest label, "POKédex", with
 * the cursor and the usual padding, so the box keeps the original's width. */
#define UI_MENU_X 48
#define UI_MENU_Y 8
#define UI_MENU_W 80
#define UI_MENU_ROWS UI_MENU_ITEMS

typedef struct {
    UiList list;
    int open;
    int chosen;      /* last entry confirmed with A, or -1 */
} UiMenu;

/* Open the box with the previous selection (the game remembers it between
 * openings in wBattleMenuCursorBuffer). */
void ui_menu_open(UiMenu *m);
void ui_menu_close(UiMenu *m);
int ui_menu_is_open(const UiMenu *m);

/* A / B / START. `delta` is usually +1 or -1 (UP/DOWN). */
int ui_menu_move(UiMenu *m, int delta);

/* A on a row: records the entry and closes the menu. Returns the chosen item, so
 * the caller can route it (none of the four screens exist yet). */
int ui_menu_confirm(UiMenu *m);

void ui_menu_draw(const UiMenu *m, const FontNames *names,
                  uint16_t *fb, int fbw, int y0, int y1,
                  uint16_t ink, uint16_t paper);

#endif
