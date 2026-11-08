/* ui/options — the Option screen's rules.
 *
 * What matters here is that the screen showed the original's settings, that the
 * cursor and the window behave over fourteen rows, that LEFT/RIGHT wraps every
 * option's values (the toggles included), and that the defaults are the game's
 * own, not zeroes.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "gen/options.h"
#include "ui/options.h"

#include "test_util.h"

#define W 128
#define H 128

static uint16_t fb[W * H];

static void clear(void) { memset(fb, 0, sizeof fb); }

static int ink_in(int x, int y, int w, int h) {
    for (int py = y; py < y + h; py++)
        for (int px = x; px < x + w; px++)
            if (px >= 0 && px < W && py >= 0 && py < H && fb[py * W + px]) return 1;
    return 0;
}

/* The 8x8 cell at (x, y) is exactly the glyph `code`, pixel for pixel. */
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

static void test_defaults(void) {
    PcOptions o;
    memset(&o, 0xff, sizeof o);
    options_defaults(&o);

    /* data/options/default_options.asm: FAST_TEXT, STEREO, BATTLE_EFFECTS. */
    CHECK_EQ(o.v[PC_OPT_TEXT_SPEED], 2);
    CHECK_EQ(o.v[PC_OPT_SOUND], 1);
    CHECK_EQ(o.v[PC_OPT_BATTLE_EFFECTS], 1);
    /* wOptions2 = wOptions3 = 0 and wTextboxFrame = 0. */
    CHECK_EQ(o.v[PC_OPT_BATTLE_STYLE], 0);
    CHECK_EQ(o.v[PC_OPT_NICKNAMES], 0);
    CHECK_EQ(o.v[PC_OPT_CLOCK_FORMAT], 0);
    CHECK_EQ(o.v[PC_OPT_POKEDEX_UNITS], 0);
    CHECK_EQ(o.v[PC_OPT_FRAME], 0);
    CHECK(!options_is_open(&o));
    CHECK_EQ(o.list.cursor, 0);
    CHECK_EQ(o.list.count, UI_OPT_ITEMS);
    CHECK_EQ(UI_OPT_ITEMS, 14);          /* 13 options + Done */
}

static void test_counts(void) {
    /* The original's value counts: 4/4/20/8/2/2/2/3/3/2/2/2/2. */
    CHECK_EQ(options_value_count(PC_OPT_TEXT_SPEED), 4);
    CHECK_EQ(options_value_count(PC_OPT_TEXT_AUTOSCROLL), 4);
    CHECK_EQ(options_value_count(PC_OPT_FRAME), 20);
    CHECK_EQ(options_value_count(PC_OPT_TYPEFACE), 8);
    CHECK_EQ(options_value_count(PC_OPT_BATTLE_STYLE), 3);
    CHECK_EQ(options_value_count(PC_OPT_NICKNAMES), 3);
    CHECK_EQ(options_value_count(UI_OPT_DONE), 0);
    CHECK_EQ(options_value_count(-1), 0);
    CHECK_EQ(options_value_count(UI_OPT_ITEMS), 0);
}

static void test_cycle(void) {
    PcOptions o;
    options_defaults(&o);
    options_open(&o);

    /* Right from Fast goes to Instant and wraps back to Slow. */
    CHECK_EQ(o.v[PC_OPT_TEXT_SPEED], 2);
    CHECK(options_cycle(&o, 1));
    CHECK_EQ(o.v[PC_OPT_TEXT_SPEED], 3);
    CHECK(options_cycle(&o, 1));
    CHECK_EQ(o.v[PC_OPT_TEXT_SPEED], 0);
    CHECK(options_cycle(&o, -1));
    CHECK_EQ(o.v[PC_OPT_TEXT_SPEED], 3);

    /* A toggle wraps both ways too. */
    o.list.cursor = PC_OPT_SOUND;
    CHECK_EQ(o.v[PC_OPT_SOUND], 1);
    CHECK(options_cycle(&o, 1));
    CHECK_EQ(o.v[PC_OPT_SOUND], 0);
    CHECK(options_cycle(&o, -1));
    CHECK_EQ(o.v[PC_OPT_SOUND], 1);

    /* Battle Style: Set -> Switch -> Predict -> Set, and Set -> Predict backwards,
     * which is the original's three-bit dance. */
    o.list.cursor = PC_OPT_BATTLE_STYLE;
    CHECK_EQ(o.v[PC_OPT_BATTLE_STYLE], 0);
    options_cycle(&o, 1);
    CHECK_EQ(o.v[PC_OPT_BATTLE_STYLE], 1);
    options_cycle(&o, 1);
    CHECK_EQ(o.v[PC_OPT_BATTLE_STYLE], 2);
    options_cycle(&o, 1);
    CHECK_EQ(o.v[PC_OPT_BATTLE_STYLE], 0);
    options_cycle(&o, -1);
    CHECK_EQ(o.v[PC_OPT_BATTLE_STYLE], 2);

    /* Frame counts 1..20 and wraps at the end, like Options_Frame. */
    o.list.cursor = PC_OPT_FRAME;
    for (int i = 0; i < 20; i++) CHECK(options_cycle(&o, 1));
    CHECK_EQ(o.v[PC_OPT_FRAME], 0);
    CHECK(options_cycle(&o, -1));
    CHECK_EQ(o.v[PC_OPT_FRAME], 19);

    /* Done has no value: LEFT/RIGHT do nothing there. */
    o.list.cursor = UI_OPT_DONE;
    CHECK(!options_cycle(&o, 1));
    CHECK(!options_cycle(&o, -1));

    /* A closed screen ignores the pad. */
    options_close(&o);
    CHECK(!options_cycle(&o, 1));
}

static void test_navigation(void) {
    PcOptions o;
    options_defaults(&o);
    options_open(&o);

    /* Down through all fourteen rows scrolls the seven-row window. */
    for (int i = 1; i < UI_OPT_ITEMS; i++) CHECK(options_move(&o, 1));
    CHECK_EQ(o.list.cursor, UI_OPT_DONE);
    CHECK_EQ(o.list.top, UI_OPT_ITEMS - UI_OPT_ROWS);
    CHECK_EQ(ui_list_cursor_row(&o.list), UI_OPT_ROWS - 1);
    /* Wraps around to the top, as the original's menu does. */
    CHECK(options_move(&o, 1));
    CHECK_EQ(o.list.cursor, 0);
    CHECK_EQ(o.list.top, 0);

    /* Reopening remembers the cursor. */
    o.list.cursor = 5;
    options_close(&o);
    options_open(&o);
    CHECK_EQ(o.list.cursor, 5);
}

static void test_confirm(void) {
    PcOptions o;
    options_defaults(&o);
    options_open(&o);

    /* A on an option only closes the list, never the screen. */
    CHECK_EQ(options_confirm(&o), -1);
    CHECK(options_is_open(&o));

    o.list.cursor = UI_OPT_DONE;
    CHECK_EQ(options_confirm(&o), UI_OPT_DONE);
    CHECK(!options_is_open(&o));
    CHECK_EQ(options_confirm(&o), -1);    /* already closed */
}

static void test_text_ticks(void) {
    /* PrintLetterDelay: Slow/Mid/Fast wait 5/3/1 frames, Instant prints at once. */
    CHECK_EQ(options_text_ticks(0), 5);
    CHECK_EQ(options_text_ticks(1), 3);
    CHECK_EQ(options_text_ticks(2), 1);
    CHECK_EQ(options_text_ticks(3), 0);
}

static void test_run(void) {
    /* .RunCheck: run when the Running Shoes bit and B *disagree*, so the same
     * button means "run" with the option off and "walk" with it on. */
    PcOptions o;
    options_defaults(&o);
    CHECK_EQ(o.v[PC_OPT_RUNNING_SHOES], 0);         /* off by default */
    CHECK_EQ(options_run(&o, 0), 0);                /* off, B up: walk */
    CHECK_EQ(options_run(&o, 1), 1);                /* off, B held: run */

    o.v[PC_OPT_RUNNING_SHOES] = 1;                  /* "On" */
    CHECK_EQ(options_run(&o, 0), 1);                /* on, B up: run */
    CHECK_EQ(options_run(&o, 1), 0);                /* on, B held: walk */

    /* the other options do not move it, and it takes any non-zero as "held" */
    o.v[PC_OPT_RUNNING_SHOES] = 0;
    o.v[PC_OPT_TEXT_SPEED] = 3;
    CHECK_EQ(options_run(&o, 0), 0);
    CHECK_EQ(options_run(&o, 7), 1);

    /* cycling the row reaches both values, so the screen can turn it on */
    options_defaults(&o);
    options_open(&o);
    ui_list_select(&o.list, PC_OPT_RUNNING_SHOES);
    CHECK_EQ(options_cycle(&o, 1), 1);
    CHECK_EQ(o.v[PC_OPT_RUNNING_SHOES], 1);
    CHECK_EQ(options_run(&o, 0), 1);
    CHECK_EQ(options_cycle(&o, 1), 1);
    CHECK_EQ(o.v[PC_OPT_RUNNING_SHOES], 0);
}

static void test_turn_frames(void) {
    /* .GetTurningSpeed: 4 frames per half of the turn at Slow, 2 at Fast, so a
     * full turn costs 8 or 4 frames in the player. */
    PcOptions o;
    options_defaults(&o);
    CHECK_EQ(o.v[PC_OPT_TURNING_SPEED], 0);          /* Slow by default */
    CHECK_EQ(options_turn_frames(&o), 4);

    o.v[PC_OPT_TURNING_SPEED] = 1;                   /* "Fast" */
    CHECK_EQ(options_turn_frames(&o), 2);

    /* other rows are irrelevant, and the screen's cycle reaches Fast */
    options_defaults(&o);
    o.v[PC_OPT_TEXT_SPEED] = 3;
    CHECK_EQ(options_turn_frames(&o), 4);
    options_open(&o);
    ui_list_select(&o.list, PC_OPT_TURNING_SPEED);
    CHECK_EQ(options_cycle(&o, 1), 1);
    CHECK_EQ(options_turn_frames(&o), 2);
    CHECK_EQ(options_cycle(&o, 1), 1);
    CHECK_EQ(options_turn_frames(&o), 4);
}

static void test_draw(void) {
    PcOptions o;
    options_defaults(&o);
    options_open(&o);
    clear();
    options_draw(&o, fb, W, 0, H, 1, 0);

    /* The box covers the panel width and its frame is ink. */
    CHECK_EQ(fb[UI_OPT_Y * W + 0], 1);
    CHECK_EQ(fb[UI_OPT_Y * W + W - 1], 1);
    CHECK_EQ(fb[(UI_OPT_Y + UI_OPT_H - 1) * W + 64], 1);
    /* Interior is paper. */
    CHECK_EQ(fb[(UI_OPT_Y + 2) * W + 100], 0);

    /* Row 0 (Text Speed) is the selected one: an inverted bar across the name
     * line, so most of that strip is ink... */
    int base = UI_OPT_Y + UI_OPT_PAD;
    int bar = 0;
    for (int px = UI_OPT_X + 1; px < UI_OPT_X + UI_OPT_W - 1; px++)
        bar += fb[base * W + px] == 1;
    /* (Every 8th cell holds a glyph, cut out of the bar in paper, so the strip is
     * ink except where the name is.) */
    CHECK(bar > UI_OPT_W * 3 / 4);
    CHECK_EQ(fb[base * W + UI_OPT_X + 1], 1);
    CHECK_EQ(fb[base * W + UI_OPT_X + UI_OPT_W - 2], 1);
    /* Row 1 is not selected: no bar, just its name, then its value under it. */
    int y1 = base + UI_OPT_ROW_H;
    CHECK_EQ(fb[y1 * W + 2], 0);                          /* no fill on row 1 */
    CHECK(ink_in(UI_OPT_X + 4, y1, 80, 8));               /* the name */
    CHECK(ink_in(UI_OPT_X + 12, y1 + 8, 4, 8));           /* "None" under it */
    /* Seven rows were drawn, not eight: the eighth would be past the box. */
    CHECK(ink_in(UI_OPT_X + 4, base + 6 * UI_OPT_ROW_H, 80, 8));
    /* The eighth row would start at y=120, inside the box's bottom frame: only
     * the frame is ink there. */
    CHECK(!ink_in(UI_OPT_X + 4, base + 7 * UI_OPT_ROW_H, 80, 3));

    /* One arrow at each end. At the bottom of the list only ↑ is drawn: there is
     * nothing below the last visible row. */
    for (int i = 0; i < 13; i++) options_move(&o, 1);
    clear();
    options_draw(&o, fb, W, 0, H, 1, 0);
    CHECK_EQ(o.list.top, UI_OPT_ITEMS - UI_OPT_ROWS);
    int arrow_x = UI_OPT_X + UI_OPT_W - 9;   /* where options_draw puts it */
    int arrow_up_y = UI_OPT_Y + UI_OPT_PAD + 8;
    int arrow_dn_y = UI_OPT_Y + UI_OPT_PAD + (UI_OPT_ROWS - 1) * UI_OPT_ROW_H + 8;
    CHECK(cell_is_glyph(arrow_x, arrow_up_y, UI_ARROW_UP));
    CHECK(!ink_in(arrow_x, arrow_dn_y, 8, 8));       /* ↓ would mean more below */

    /* In the middle of the list both are drawn. */
    ui_list_select(&o.list, 0);
    for (int i = 0; i < 7; i++) options_move(&o, 1);
    clear();
    options_draw(&o, fb, W, 0, H, 1, 0);
    CHECK_EQ(o.list.top, 1);
    CHECK(cell_is_glyph(arrow_x, arrow_up_y, UI_ARROW_UP));
    CHECK(cell_is_glyph(arrow_x, arrow_dn_y, UI_ARROW_DOWN));

    /* Nothing outside the box is touched: the four pixel margins stay clear. */
    CHECK(!ink_in(0, 0, W, UI_OPT_Y));
}

static void test_draw_frame_number(void) {
    /* Frame prints wTextboxFrame + 1 with two digits: index 0 is "01", 19 is "20". */
    PcOptions o;
    options_defaults(&o);
    options_open(&o);
    o.list.cursor = PC_OPT_FRAME;
    clear();
    options_draw(&o, fb, W, 0, H, 1, 0);

    /* Frame is row 2; its value line must have ink (the digits), and the width of
     * two glyphs, not more. */
    int col = UI_OPT_X + UI_OPT_PAD + 8;
    int y = UI_OPT_Y + UI_OPT_PAD + 2 * UI_OPT_ROW_H + 8;
    CHECK(ink_in(col, y, 8, 8));
    CHECK(ink_in(col + 8, y, 8, 8));
    CHECK(!ink_in(col + 16, y, 24, 8));
}

int main(void) {
    test_defaults();
    test_counts();
    test_cycle();
    test_navigation();
    test_confirm();
    test_text_ticks();
    test_run();
    test_turn_frames();
    test_draw();
    test_draw_frame_number();
    return test_report("test_ui_options");
}
