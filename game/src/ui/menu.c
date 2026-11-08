/*
 * ui/menu.c — the START menu box.
 *
 * It is only a list plus the open/close/confirm rules; the drawing and the
 * cursor/scroll maths live in ui/list. Keeping the rules here means game.c has
 * one call per button press and no menu state of its own.
 */
#include "ui/menu.h"

void ui_menu_open(UiMenu *m) {
    /* The game keeps the cursor between openings: reopening the menu puts you
     * back on the entry you last looked at, not on the first one. */
    int keep = m->list.cursor;   /* 0 the first time (the struct starts zeroed) */
    ui_list_init(&m->list, UI_MENU_X, UI_MENU_Y, UI_MENU_W, UI_MENU_ROWS,
                 UI_MENU_ITEMS);
    ui_list_select(&m->list, keep);
    m->open = 1;
    m->chosen = -1;
}

void ui_menu_close(UiMenu *m) {
    m->open = 0;
}

int ui_menu_is_open(const UiMenu *m) {
    return m->open;
}

int ui_menu_move(UiMenu *m, int delta) {
    if (!m->open) return 0;
    return ui_list_move(&m->list, delta);
}

int ui_menu_confirm(UiMenu *m) {
    if (!m->open) return -1;
    m->chosen = m->list.cursor;
    m->open = 0;
    return m->chosen;
}

void ui_menu_draw(const UiMenu *m, const FontNames *names,
                  uint16_t *fb, int fbw, int y0, int y1,
                  uint16_t ink, uint16_t paper) {
    if (!m->open) return;
    ui_list_draw(&m->list, pc_ui_labels, names, fb, fbw, y0, y1, ink, paper);
}
