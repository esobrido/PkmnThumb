/*
 * ui/list.c — window bookkeeping and drawing for a one-column menu.
 *
 * The maths is the whole point: the original's menu boxes are a fixed size and
 * the list inside scrolls, so keeping `top` and `cursor` consistent is what makes
 * a 30-item bag behave. Drawing is deliberately dumb — a frame, a paper fill, one
 * ▶ and the labels — so it looks like the textbox and shares its palette.
 */
#include "ui/list.h"

#include "ui/frame.h"

/* First glyph at 4 px in (1 px frame + 3 px of air), same as the textbox. */
#define UI_PAD 4

int ui_list_height(int rows) {
    return rows * 8 + 8;                  /* 4 above + rows + 4 below */
}

void ui_list_init(UiList *l, int x, int y, int w, int rows, int count) {
    l->x = x;
    l->y = y;
    l->w = w;
    l->rows = rows < 1 ? 1 : rows > UI_LIST_ROWS_MAX ? UI_LIST_ROWS_MAX : rows;
    l->count = count < 0 ? 0 : count;
    l->top = l->count ? 0 : -1;
    l->cursor = l->count ? 0 : -1;
}

void ui_list_set_count(UiList *l, int count) {
    l->count = count < 0 ? 0 : count;
    if (!l->count) {
        l->cursor = l->top = -1;
        return;
    }
    if (l->cursor < 0) l->cursor = 0;
    ui_list_select(l, l->cursor);
}

static void clamp_window(UiList *l) {
    if (l->cursor < 0) return;
    if (l->cursor < l->top) l->top = l->cursor;
    if (l->cursor >= l->top + l->rows) l->top = l->cursor - l->rows + 1;
    int last_top = l->count > l->rows ? l->count - l->rows : 0;
    if (l->top > last_top) l->top = last_top;
    if (l->top < 0) l->top = 0;
}

int ui_list_move(UiList *l, int delta) {
    if (l->count <= 0 || !delta) return 0;
    int c = (l->cursor + delta) % l->count;
    if (c < 0) c += l->count;
    if (c == l->cursor) return 0;
    l->cursor = c;
    clamp_window(l);
    return 1;
}

void ui_list_select(UiList *l, int index) {
    if (l->count <= 0) {
        l->cursor = l->top = -1;
        return;
    }
    l->cursor = index < 0 ? 0 : index >= l->count ? l->count - 1 : index;
    clamp_window(l);
}

int ui_list_item_at(const UiList *l, int r) {
    if (r < 0 || r >= l->rows) return -1;
    int i = l->top + r;
    return i >= 0 && i < l->count ? i : -1;
}

int ui_list_cursor_row(const UiList *l) {
    return l->cursor < 0 ? -1 : l->cursor - l->top;
}

/* ---- drawing ------------------------------------------------------------ */

void ui_list_draw(const UiList *l, const PcUiLabel *items, const FontNames *names,
                  uint16_t *fb, int fbw, int y0, int y1,
                  uint16_t ink, uint16_t paper) {
    int h = ui_list_height(l->rows);
    int right = l->x + l->w - 1, bottom = l->y + h - 1;

    ui_frame_box(fb, fbw, y0, y1, l->x, l->y, l->w, h, ink, paper);

    if (l->count <= 0) return;

    int col = l->x + UI_PAD;
    int maxw = l->w - UI_PAD * 2;
    for (int r = 0; r < l->rows; r++) {
        int item = ui_list_item_at(l, r);
        if (item < 0) break;
        int gy = l->y + UI_PAD + r * 8;
        if (item == l->cursor) font_draw_glyph(fb, fbw, y0, y1, col, gy, UI_CURSOR, ink);
        font_draw_text(items[item].bytes, items[item].len, names,
                       fb, fbw, y0, y1, col + 8, gy, maxw - 8, ink);
    }

    /* More items than rows: ↑/↓ in the right column say there is more either
     * way, because the cursor alone cannot show it. */
    if (l->top > 0)
        font_draw_glyph(fb, fbw, y0, y1, right - 9, l->y + 2, UI_ARROW_UP, ink);
    if (l->top + l->rows < l->count)
        font_draw_glyph(fb, fbw, y0, y1, right - 9, bottom - 9, UI_ARROW_DOWN, ink);
}
