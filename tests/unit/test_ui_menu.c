/* ui/menu — the START menu's rules.
 *
 * The menu is a list plus three decisions: what opens it, what the cursor does,
 * and what A returns. Those are the parts game.c relies on, so they are pinned
 * here rather than by screenshotting the box.
 */
#include <assert.h>
#include <stdio.h>

#include "ui/list.h"
#include "ui/menu.h"

#define W 128
#define H 128

static uint16_t fb[W * H];

static int ink_in(int x, int y, int w, int h) {
    for (int py = y; py < y + h; py++)
        for (int px = x; px < x + w; px++)
            if (px >= 0 && px < W && py >= 0 && py < H && fb[py * W + px]) return 1;
    return 0;
}

static void test_open_close(void) {
    UiMenu m = { 0 };
    assert(!ui_menu_is_open(&m));
    assert(ui_menu_confirm(&m) == -1);       /* A on a closed menu does nothing */
    assert(!ui_menu_move(&m, 1));

    ui_menu_open(&m);
    assert(ui_menu_is_open(&m));
    assert(m.list.cursor == 0);
    assert(m.list.count == UI_MENU_ITEMS);

    ui_menu_close(&m);
    assert(!ui_menu_is_open(&m));
}

static void test_navigation(void) {
    UiMenu m = { 0 };
    ui_menu_open(&m);
    assert(UI_MENU_ITEMS == 5);
    assert(PC_UI_OPTIONS == 4);

    assert(ui_menu_move(&m, 1));
    assert(m.list.cursor == 1);
    ui_menu_move(&m, 1);
    ui_menu_move(&m, 1);
    assert(m.list.cursor == 3);
    ui_menu_move(&m, 1);
    assert(m.list.cursor == PC_UI_OPTIONS);
    assert(ui_menu_move(&m, 1));             /* wraps, as the game's menu does */
    assert(m.list.cursor == 0);
    assert(ui_menu_move(&m, -1));
    assert(m.list.cursor == PC_UI_OPTIONS);

    /* All five fit: the window never scrolls. */
    assert(m.list.top == 0);
}

static void test_confirm(void) {
    UiMenu m = { 0 };
    ui_menu_open(&m);
    ui_menu_move(&m, 1);
    int chosen = ui_menu_confirm(&m);
    assert(chosen == 1);
    assert(chosen == PC_UI_POKEMON);
    assert(!ui_menu_is_open(&m));
    assert(m.chosen == 1);

    /* The game remembers the cursor: reopening lands on the last entry. */
    ui_menu_open(&m);
    assert(m.list.cursor == 1);
    assert(m.chosen == -1);

    /* And a new confirmation reports the new entry. */
    ui_menu_move(&m, 1);
    assert(ui_menu_confirm(&m) == 2);
    assert(ui_menu_confirm(&m) == -1);       /* already closed */
}

static void test_draw(void) {
    UiMenu m = { 0 };
    for (int i = 0; i < W * H; i++) fb[i] = 0;

    ui_menu_draw(&m, 0, fb, W, 0, H, 1, 0);
    assert(!ink_in(0, 0, W, H));             /* closed: nothing at all */

    ui_menu_open(&m);
    ui_menu_draw(&m, 0, fb, W, 0, H, 1, 0);
    /* On the right of the panel, from the top, like the original's box. */
    assert(ink_in(UI_MENU_X, UI_MENU_Y, 4, 4));
    assert(!ink_in(0, 0, UI_MENU_X, UI_MENU_Y));
    /* Four rows of labels and the cursor, inside the box. */
    assert(ink_in(UI_MENU_X + 4, UI_MENU_Y + 4, 64, 8));
    assert(ink_in(UI_MENU_X + 4, UI_MENU_Y + 4 + 3 * 8, 64, 8));

    /* The box fits the panel. */
    assert(UI_MENU_X + UI_MENU_W <= W);
    assert(UI_MENU_Y + ui_list_height(UI_MENU_ROWS) <= H);
}

int main(void) {
    test_open_close();
    test_navigation();
    test_confirm();
    test_draw();
    printf("ui/menu: ok\n");
    return 0;
}
