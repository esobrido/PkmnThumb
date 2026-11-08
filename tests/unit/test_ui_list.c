/* ui/list — cursor wrapping, the scrolling window, and what lands on screen.
 *
 * The interesting failures here are off-by-one ones: a cursor one past the window,
 * an empty row drawn with the cursor on it, or a menu that scrolls when the last
 * item is already visible. Each assert names the behaviour it pins down.
 */
#include <assert.h>
#include <stdio.h>

#include "gen/ui.h"
#include "ui/list.h"

#define W 128
#define H 128

static uint16_t fb[W * H];

/* A ten-item list needs ten labels: the module trusts the caller to pass an
 * array at least `count` long, like every other table in the port. */
static const uint8_t one_glyph[1] = { 0x80 };   /* 'A' */
static PcUiLabel many[10];

/* The 8x8 cell at (x, y) is exactly the glyph `code`, pixel for pixel. Used for
 * the scroll arrows, where "some ink showed up" was not enough: the first version
 * drew $f3/$f4, which are "↓" and "′", and the list still looked plausible. */
static int cell_is_glyph(int x, int y, uint8_t code) {
    const uint8_t *g = font_glyph(code);
    if (!g) return 0;
    int on = 0, same = 0;
    for (int sy = 0; sy < 8; sy++)
        for (int sx = 0; sx < 8; sx++) {
            int want = (g[sy] >> (7 - sx)) & 1;
            int have = fb[(y + sy) * W + x + sx] ? 1 : 0;
            on += want;
            same += want == have;
        }
    return on > 0 && same == 64;
}


static void init_many(void) {
    for (int i = 0; i < 10; i++) {
        many[i].name = "X";
        many[i].bytes = one_glyph;
        many[i].len = 1;
    }
}

static void clear(uint16_t v) {
    for (int i = 0; i < W * H; i++) fb[i] = v;
}

static int ink_at(int x, int y) {
    return fb[y * W + x] != 0;
}

/* Is there any ink in this rectangle? */
static int ink_in(int x, int y, int w, int h) {
    for (int py = y; py < y + h; py++)
        for (int px = x; px < x + w; px++)
            if (px >= 0 && px < W && py >= 0 && py < H && fb[py * W + px]) return 1;
    return 0;
}

static void test_height(void) {
    assert(ui_list_height(1) == 16);
    assert(ui_list_height(4) == 40);
    /* Same box as the textbox for the same number of rows. */
    assert(ui_list_height(3) == 32);
}

static void test_empty(void) {
    UiList l;
    ui_list_init(&l, 0, 0, 72, 4, 0);
    assert(l.cursor == -1 && l.top == -1);
    assert(!ui_list_move(&l, 1));
    assert(ui_list_item_at(&l, 0) == -1);
    assert(ui_list_cursor_row(&l) == -1);
    clear(0);
    ui_list_draw(&l, pc_ui_labels, 0, fb, W, 0, H, 1, 0);
    assert(ink_at(0, 0));            /* the empty box is still a frame */
}

static void test_wrap(void) {
    UiList l;
    ui_list_init(&l, 0, 0, 72, 4, 4);
    assert(l.cursor == 0 && l.top == 0);

    assert(ui_list_move(&l, -1));    /* wraps to the last item, like the game */
    assert(l.cursor == 3);
    assert(l.top == 0);              /* all four are visible: no scroll */
    assert(ui_list_move(&l, 1));     /* and back around */
    assert(l.cursor == 0);
    assert(!ui_list_move(&l, 0));    /* nothing to do, nothing reported */
}

static void test_scroll(void) {
    UiList l;
    ui_list_init(&l, 0, 0, 72, 3, 10);
    assert(l.cursor == 0 && l.top == 0);

    ui_list_move(&l, 1);
    ui_list_move(&l, 1);
    assert(l.cursor == 2 && l.top == 0);      /* still on the last visible row */
    ui_list_move(&l, 1);
    assert(l.cursor == 3 && l.top == 1);      /* the window follows */

    ui_list_move(&l, -1);
    ui_list_move(&l, -1);
    ui_list_move(&l, -1);
    assert(l.cursor == 0 && l.top == 0);      /* and follows back up */

    ui_list_select(&l, 9);
    assert(l.cursor == 9 && l.top == 7);      /* last item: window at the end */
    ui_list_move(&l, 1);                      /* wraps to the top ... */
    assert(l.cursor == 0 && l.top == 0);      /* ... and the window resets */

    /* set_count: a shorter list pulls the cursor back into range. */
    ui_list_select(&l, 9);
    ui_list_set_count(&l, 4);
    assert(l.cursor == 3 && l.top == 1);
    ui_list_set_count(&l, 0);
    assert(l.cursor == -1 && ui_list_item_at(&l, 0) == -1);
}

static void test_rows(void) {
    UiList l;
    ui_list_init(&l, 0, 0, 72, 3, 10);
    ui_list_select(&l, 5);          /* top = 3 */
    assert(l.top == 3);
    assert(ui_list_item_at(&l, 0) == 3);
    assert(ui_list_item_at(&l, 2) == 5);
    assert(ui_list_item_at(&l, 3) == -1);     /* past the visible rows */
    assert(ui_list_item_at(&l, -1) == -1);
    assert(ui_list_cursor_row(&l) == 2);

    /* A list shorter than the window draws nothing in the spare rows. */
    ui_list_init(&l, 0, 0, 72, 4, 2);
    assert(ui_list_item_at(&l, 1) == 1);
    assert(ui_list_item_at(&l, 2) == -1);

    /* Selecting out of range clamps instead of corrupting the window. */
    ui_list_select(&l, 99);
    assert(l.cursor == 1);
    ui_list_select(&l, -5);
    assert(l.cursor == 0);

    /* More rows than the list allows are clamped at init. */
    ui_list_init(&l, 0, 0, 72, 99, 4);
    assert(l.rows == UI_LIST_ROWS_MAX);
}

static void test_draw(void) {
    UiList l;
    ui_list_init(&l, 48, 8, 72, 4, 4);

    clear(0);
    ui_list_draw(&l, pc_ui_labels, 0, fb, W, 0, H, 1, 0);

    /* Frame, paper inside, and the cursor glyph on the first row only. */
    assert(ink_at(48, 8));                       /* top-left frame pixel */
    assert(ink_at(48 + 71, 8));
    assert(ink_at(48, 8 + ui_list_height(4) - 1));
    assert(!ink_at(50, 10));                     /* paper */

    assert(ink_in(48 + 4, 8 + 4, 8, 8));          /* ▶ on row 0 */
    assert(ink_in(48 + 12, 8 + 4, 56, 8));        /* "POKédex" */
    assert(!ink_in(48 + 4, 8 + 4 + 8, 8, 8));     /* ... and not on row 1 */

    /* The cursor moves with the selection: row 1, not row 0. */
    ui_list_move(&l, 1);
    clear(0);
    ui_list_draw(&l, pc_ui_labels, 0, fb, W, 0, H, 1, 0);
    assert(!ink_in(48 + 4, 8 + 4, 8, 8));
    assert(ink_in(48 + 4, 8 + 4 + 8, 8, 8));

    /* Scrolling indicators: a 10-item list in a 3-row window. */
    init_many();
    UiList s;
    ui_list_init(&s, 8, 8, 72, 3, 10);
    clear(0);
    ui_list_draw(&s, many, 0, fb, W, 0, H, 1, 0);
    int right = 8 + 72 - 1;
    assert(!ink_at(right - 9, 8 + 2));            /* at the top: no ↑ */
    assert(cell_is_glyph(right - 9, 8 + ui_list_height(3) - 10, UI_ARROW_DOWN));
    ui_list_select(&s, 9);
    clear(0);
    ui_list_draw(&s, many, 0, fb, W, 0, H, 1, 0);
    assert(cell_is_glyph(right - 9, 8 + 2, UI_ARROW_UP));
    assert(!ink_in(right - 9, 8 + ui_list_height(3) - 10, 8, 8));

    /* Band clipping: a band that ends above the box gets no pixels at all. */
    clear(0);
    ui_list_init(&l, 48, 60, 72, 4, 4);
    ui_list_draw(&l, pc_ui_labels, 0, fb, W, 0, 40, 1, 0);
    assert(!ink_in(0, 0, W, H));

    /* And a band through the middle only paints what is inside it. */
    clear(0);
    ui_list_draw(&l, pc_ui_labels, 0, fb, W, 60, 70, 1, 0);
    for (int y = 70; y < H; y++)
        for (int x = 0; x < W; x++) assert(!fb[y * W + x]);
}

int main(void) {
    test_height();
    test_empty();
    test_wrap();
    test_scroll();
    test_rows();
    test_draw();
    printf("ui/list: ok\n");
    return 0;
}
