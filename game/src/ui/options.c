/*
 * ui/options.c — the Option screen's state and its two-line rows.
 *
 * The state is one index per option. The original keeps the same numbers packed
 * into wOptions1/2/3 and toggles bits by hand (Options_BattleStyle walks Set ->
 * Switch -> Predict with three `bit` tests); an index per option is the same
 * information with none of the shifting, and it makes `options_cycle` one line
 * for all thirteen.
 *
 * What the values *do* lives at the call site, not here: game.c reads the text
 * speed out of this struct when it opens a textbox. The rest — sound, battle
 * effects, fonts, frames — wait for the systems they configure.
 */
#include "ui/options.h"

#include "ui/frame.h"

void options_defaults(PcOptions *o) {
    /* data/options/default_options.asm, option by option:
     *   wOptions1 = (1 << BATTLE_EFFECTS) | (1 << STEREO) | FAST_TEXT
     *   wOptions2 = 0, wOptions3 = 0, wTextboxFrame = 0                     */
    o->v[PC_OPT_TEXT_SPEED] = 2;      /* FAST_TEXT (%10) */
    o->v[PC_OPT_TEXT_AUTOSCROLL] = 0; /* AUTOSCROLL_NONE */
    o->v[PC_OPT_FRAME] = 0;           /* FRAME_1, drawn as "01" */
    o->v[PC_OPT_TYPEFACE] = 0;        /* NORMAL_FONT */
    o->v[PC_OPT_KEYBOARD] = 0;        /* QWERTY_KEYBOARD_F clear: ABCDEF */
    o->v[PC_OPT_SOUND] = 1;           /* STEREO set */
    o->v[PC_OPT_BATTLE_EFFECTS] = 1;  /* BATTLE_EFFECTS set */
    o->v[PC_OPT_BATTLE_STYLE] = 0;    /* neither BATTLE_SWITCH nor PREDICT: Set */
    o->v[PC_OPT_NICKNAMES] = 0;       /* neither ALWAYS nor NEVER: Ask */
    o->v[PC_OPT_RUNNING_SHOES] = 0;   /* RUNNING_SHOES clear */
    o->v[PC_OPT_TURNING_SPEED] = 0;   /* TURNING_SPEED clear: Slow */
    o->v[PC_OPT_CLOCK_FORMAT] = 0;    /* CLOCK_FORMAT clear: 12-hour */
    o->v[PC_OPT_POKEDEX_UNITS] = 0;   /* POKEDEX_UNITS clear: Imperial */
    o->open = 0;
    ui_list_init(&o->list, UI_OPT_X, UI_OPT_Y, UI_OPT_W, UI_OPT_ROWS, UI_OPT_ITEMS);
    ui_list_select(&o->list, 0);
}

void options_open(PcOptions *o) {
    o->open = 1;
    /* Reopening lands on the last option looked at, like the original, which
     * keeps wMenuSelection between runs of the menu. */
    if (o->list.cursor < 0) ui_list_select(&o->list, 0);
}

void options_close(PcOptions *o) {
    o->open = 0;
}

int options_is_open(const PcOptions *o) {
    return o->open;
}

int options_move(PcOptions *o, int delta) {
    if (!o->open) return 0;
    return ui_list_move(&o->list, delta);
}

int options_value_count(int item) {
    if (item < 0 || item >= PC_OPTION_COUNT) return 0;
    return pc_options[item].count;
}

int options_cycle(PcOptions *o, int dir) {
    if (!o->open || !dir) return 0;
    int item = o->list.cursor;
    int count = options_value_count(item);
    if (count <= 0) return 0;               /* "Done" */
    int v = (o->v[item] + (dir > 0 ? 1 : -1)) % count;
    if (v < 0) v += count;
    if (v == o->v[item]) return 0;
    o->v[item] = (uint8_t)v;
    return 1;
}

int options_confirm(PcOptions *o) {
    if (!o->open) return -1;
    if (o->list.cursor != UI_OPT_DONE) return -1;   /* values apply immediately */
    options_close(o);
    return UI_OPT_DONE;
}

uint8_t options_text_ticks(int value) {
    /* Slow/Mid/Fast wait 5/3/1 frames per letter; Instant prints the page. */
    static const uint8_t k_ticks[] = { 5, 3, 1, 0 };
    if (value < 0 || value > 3) return 1;
    return k_ticks[value];
}

/* ---- drawing ------------------------------------------------------------ */

/* The value of `item` as glyph codes. Frame is the one number: wTextboxFrame + 1
 * printed in two columns, which is what Options_Frame does with PrintNum. */
static int value_text(const PcOptions *o, int item, uint8_t *out) {
    if (item < 0 || item >= PC_OPTION_COUNT) return 0;
    const PcOptionDef *def = &pc_options[item];
    if (def->values) {
        const PcOptionString *s = &def->values[o->v[item] % def->count];
        int n = s->len;
        for (int i = 0; i < n; i++) out[i] = s->bytes[i];
        return n;
    }
    int num = (int)o->v[item] + 1;          /* Frame 1..20 */
    out[0] = font_code_from_ascii((char)('0' + (num / 10) % 10));
    out[1] = font_code_from_ascii((char)('0' + num % 10));
    return 2;
}

/* Fill a rectangle of `c`, clipped to the band — the selected row's bar. */
static void fill_rect(uint16_t *fb, int fbw, int y0, int y1,
                      int x, int y, int w, int h, uint16_t c) {
    for (int py = y; py < y + h; py++) {
        if (py < y0 || py >= y1) continue;
        uint16_t *row = &fb[py * fbw];
        for (int px = x; px < x + w; px++)
            if (px >= 0 && px < fbw) row[px] = c;
    }
}

void options_draw(const PcOptions *o, uint16_t *fb, int fbw, int y0, int y1,
                  uint16_t ink, uint16_t paper) {
    const int x = UI_OPT_X, y = UI_OPT_Y;
    ui_frame_box(fb, fbw, y0, y1, x, y, UI_OPT_W, UI_OPT_H, ink, paper);

    /* The name line gets 120 px, which is exactly the longest option name
     * ("Text Autoscroll", 15 tiles) — so there is no room for a ▶ as well. The
     * selected row is drawn as an inverted bar instead, which is how the port's
     * highlight has to work on a 16-tile screen. */
    const int name_x = x + UI_OPT_PAD;
    const int name_w = UI_OPT_W - UI_OPT_PAD * 2;      /* 120 px = 15 tiles */
    const int value_x = name_x + 8;
    const int value_w = UI_OPT_W - UI_OPT_PAD - 8 - UI_OPT_PAD;

    for (int r = 0; r < o->list.rows; r++) {
        int item = ui_list_item_at(&o->list, r);
        if (item < 0) break;
        int gy = y + UI_OPT_PAD + r * UI_OPT_ROW_H;

        if (item == UI_OPT_DONE) {
            int done = o->list.cursor == item;
            if (done) fill_rect(fb, fbw, y0, y1, x + 1, gy, UI_OPT_W - 2, 8, ink);
            font_draw_text(pc_option_done.bytes, pc_option_done.len, 0, fb, fbw, y0, y1,
                           name_x, gy, name_w, done ? paper : ink);
            continue;
        }

        int sel = o->list.cursor == item;
        if (sel) fill_rect(fb, fbw, y0, y1, x + 1, gy, UI_OPT_W - 2, 8, ink);
        font_draw_text(pc_option_names[item].bytes, pc_option_names[item].len, 0,
                       fb, fbw, y0, y1, name_x, gy, name_w, sel ? paper : ink);

        uint8_t buf[PC_OPTION_MAX_VALUES];
        int n = value_text(o, item, buf);
        font_draw_text(buf, n, 0, fb, fbw, y0, y1, value_x, gy + 8, value_w, ink);
    }

    /* Seven rows of fourteen: say there is more above and below. The arrows sit
     * on the value line, whose right end is empty (a value is at most "Imperial",
     * 64 px), because the name line uses the full width. */
    int arrow_x = x + UI_OPT_W - 9;
    if (o->list.top > 0)
        font_draw_glyph(fb, fbw, y0, y1, arrow_x, y + UI_OPT_PAD + 8, UI_ARROW_UP, ink);
    if (o->list.top + o->list.rows < o->list.count)
        font_draw_glyph(fb, fbw, y0, y1, arrow_x,
                        y + UI_OPT_PAD + (o->list.rows - 1) * UI_OPT_ROW_H + 8,
                        UI_ARROW_DOWN, ink);
}

int options_run(const PcOptions *o, int b_held) {
    /* .RunCheck: (B held) XOR (Running Shoes On), as a truth table:
     *   Off + B held   -> run       On + B held    -> walk
     *   Off + B up     -> walk      On + B up      -> run  */
    return (o->v[PC_OPT_RUNNING_SHOES] != 0) != (b_held != 0);
}

int options_turn_frames(const PcOptions *o) {
    /* .GetTurningSpeed in map_objects.asm: Slow spends 4 frames on each half of
     * the turn, Fast 2. A turn has two halves (old facing, then new), so Slow is
     * 8 frames and Fast 4. */
    return o->v[PC_OPT_TURNING_SPEED] != 0 ? 2 : 4;
}
