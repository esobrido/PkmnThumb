/*
 * ui/options.h — the Option screen.
 *
 * The original's is a scrolling list of 13 settings plus "Done"
 * (engine/menus/options_menu.asm): UP/DOWN picks a row, LEFT/RIGHT changes its
 * value, A exits on "Done". The values live in three option bytes in RAM and are
 * applied as you change them, so there is nothing to confirm — which is why the
 * port can store them in one small struct and mean it.
 *
 * Everything printed here is the game's own data (gen/options.h): "Text
 * Autoscroll", "Predict", "#dex Units" are the original's labels and its value
 * strings, as charmap bytes. Two numbers are formatted instead of looked up —
 * Frame prints wTextboxFrame + 1 — which is what the original's routine does.
 *
 * The screen is a full-width box of two-line rows (name, then its value) because
 * the GB's 20-tile row does not fit in 16: "Text Autoscroll" alone is 15 tiles.
 *
 * Pure logic + blitting: no Mote calls, no globals, no allocation.
 */
#ifndef UI_OPTIONS_H
#define UI_OPTIONS_H

#include <stdint.h>

#include "gen/options.h"
#include "ui/list.h"

/* Two lines per entry: the name, then the value under it. */
#define UI_OPT_ROW_H 16
#define UI_OPT_PAD 4

/* Seven of the fourteen rows fit; the original shows five of fourteen. */
#define UI_OPT_ROWS 7

#define UI_OPT_X 0
#define UI_OPT_Y 4
#define UI_OPT_W 128
#define UI_OPT_H (UI_OPT_ROWS * UI_OPT_ROW_H + UI_OPT_PAD * 2)

/* "Done" is the list's last row: it is a row, not an option, so its index is
 * PC_OPTION_COUNT and it has no value. */
#define UI_OPT_DONE PC_OPTION_COUNT
#define UI_OPT_ITEMS (PC_OPTION_COUNT + 1)

typedef struct {
    /* The current value of each option, as an index into its value list — the
     * same numbers wOptions1/wOptions2/wOptions3 pack into bits. */
    uint8_t v[PC_OPTION_COUNT];
    UiList list;      /* cursor and window over UI_OPT_ITEMS rows */
    int open;
} PcOptions;

/* The game's defaults (data/options/default_options.asm), not zeroes by luck:
 * text speed Fast, sound Stereo, battle effects On, everything else at 0. */
void options_defaults(PcOptions *o);

void options_open(PcOptions *o);
void options_close(PcOptions *o);
int options_is_open(const PcOptions *o);

/* UP/DOWN. Wraps, like the original's scrolling menu. */
int options_move(PcOptions *o, int delta);

/* LEFT/RIGHT on the selected option: steps to the previous/next value and wraps.
 * Every option is a plain cycle — the toggles are the two-value case — which is
 * what the original's bit arithmetic amounts to. Returns 1 if a value changed, so
 * the caller can play a click. "Done" has no value and never changes. */
int options_cycle(PcOptions *o, int dir);

/* How many values an option has, or 0 for "Done". */
int options_value_count(int item);

/* A: only "Done" does anything (it closes the screen). Returns UI_OPT_DONE when
 * it closed, -1 otherwise. */
int options_confirm(PcOptions *o);

/* Frames per revealed glyph for a text-speed index. The original waits 5/3/1
 * frames per letter and prints instantly, so this is its table (see
 * PrintLetterDelay in home/print_text.asm). */
uint8_t options_text_ticks(int value);

/* Whether the player should run this frame, given whether B is held. The
 * original (.RunCheck in engine/overworld/player_movement.asm) runs when the
 * Running Shoes bit and the B button *disagree*, so with the option Off you hold
 * B to run and with it On you hold B to walk. Returns 1 to run. */
int options_run(const PcOptions *o, int b_held);

/* How many frames each half of a turn in place lasts (OW_TURN_SLOW / OW_TURN_FAST).
 * The original (.GetTurningSpeed in engine/overworld/map_objects.asm) spends 4
 * frames on each half at Slow and 2 at Fast, so a whole turn costs 8 or 4. */
int options_turn_frames(const PcOptions *o);

/* The whole screen: box, rows, cursor, scroll arrows. */
void options_draw(const PcOptions *o, uint16_t *fb, int fbw, int y0, int y1,
                  uint16_t ink, uint16_t paper);

#endif
