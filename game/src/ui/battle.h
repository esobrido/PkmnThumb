/*
 * ui/battle.h — the battle screen, at 128x128.
 *
 * The original's battle screen is a 20x18 tile panel with four windows: the foe's
 * stats in the top-left corner, yours in the bottom-right, the message window
 * across the bottom, and the four-action menu over its right half. The port keeps
 * that arrangement and loses the two mon pictures (the battle sprites are a
 * separate sheet that has not been extracted yet, BAT-08).
 *
 * What it does: it owns the `Battle` from `battle/turn`, shows the two HP frames,
 * reads the turn's `BattleTurn` back as the game's own sentences, and offers the
 * four actions — FIGHT, PKMN, PACK, RUN. Only FIGHT does anything: the other
 * three are the original's entries, and picking one says which system it is
 * waiting for instead of pretending to work (BAT-04 for items, BAT-05 for
 * running, GAM-02's party screen for switching).
 *
 * Text is composed here, not read from a map's bytecode: a battle sentence names
 * a mon, a move and a number, all of them known only at runtime. The names come
 * from the generated tables as charmap bytes; the literal words are the game's
 * English, converted with `font_code_from_ascii` so they print with the same font.
 *
 * Pure: no Mote, no globals, no allocation, and no `printf`. The caller forwards
 * abstract keys (BS_KEY_*) so the module never sees Mote's button enum.
 */
#ifndef UI_BATTLE_H
#define UI_BATTLE_H

#include <stdint.h>

#include "battle/turn.h"
#include "mon/mon.h"
#include "text/font.h"

/* The panel, in pixels. The foe's window is the top-left corner and yours the
 * bottom-right, so the empty corners are where the pictures would go. */
#define BS_W 128
#define BS_ENEMY_X 0
#define BS_ENEMY_Y 2
#define BS_ENEMY_W 80
#define BS_ENEMY_H 34
#define BS_PP_X 82
#define BS_PP_Y 2
#define BS_PP_W 46
#define BS_PP_H 26
#define BS_PLAYER_X 48
#define BS_PLAYER_Y 48
#define BS_PLAYER_W 80
#define BS_PLAYER_H 38
#define BS_BOX_X 0
#define BS_BOX_Y 88
#define BS_BOX_W 128
#define BS_BOX_H 40

/* The message window is the original's 15 columns by 3 rows. */
#define BS_COLS 15
#define BS_ROWS 3

/* The HP bar is 48 px: the GB's six tiles. */
#define BS_HP_W 48

/* Longest queue a turn can produce: two hits with their effect and faint lines,
 * then experience, a level and the result. */
#define BS_MSG_MAX 10
#define BS_GLYPHS 48

/* Frames per revealed glyph. The game reveals a letter every other frame by
 * default (TB_TICKS), so a battle sentence keeps the same rhythm. */
#define BS_TICKS 2

/* The four entries of the action menu, in the game's order (BattleMenu). */
enum { BS_ACTION_FIGHT = 0, BS_ACTION_PKMN, BS_ACTION_PACK, BS_ACTION_RUN,
       BS_ACTION_COUNT };

/* What the screen is doing. BS_MSG is both "reading a sentence" and "reading the
 * result"; BS_DONE is the frame after the last A of a battle that ended. */
enum { BS_PHASE_MSG = 0, BS_PHASE_ACTION, BS_PHASE_MOVE, BS_PHASE_DONE };

/* Abstract keys: the caller maps the pad, the module never sees Mote. */
enum { BS_KEY_UP = 0, BS_KEY_DOWN, BS_KEY_LEFT, BS_KEY_RIGHT, BS_KEY_A, BS_KEY_B };

/* The screen's colours. `ink`/`paper` are the game's text palette (window 7), so
 * the boxes look like every other window in the port. */
typedef struct {
    uint16_t ink, paper;
    uint16_t hp_green, hp_yellow, hp_red;
} BsPalette;

/* One sentence, as glyph codes: already expanded, ready for font_draw_glyph. */
typedef struct {
    uint8_t g[BS_GLYPHS];
    int len;
} BsMsg;

typedef struct {
    Battle battle;        /* the engine's state: pointers to the two mons */
    Mon *player, *wild;

    BsMsg msg[BS_MSG_MAX];
    int msg_len;          /* sentences queued */
    int msg_at;           /* the one on screen */
    int glyphs;           /* glyphs revealed of it */
    int ticks;            /* frames since the last one */

    int phase;
    int cursor;           /* action (2x2 grid) or move slot */
    int over;             /* the battle ended and the result is being read */
    int done;             /* the last A of the result was pressed */
} BattleUi;

/* Begin a battle: seed the engine, say who appeared and send the player's mon
 * out. The two mons belong to the caller and keep their HP, PP and experience. */
void battle_ui_start(BattleUi *u, Mon *player, Mon *wild, uint32_t seed);

/* Reveal glyphs (1 every BS_TICKS frames) of the sentence on screen. */
void battle_ui_update(BattleUi *u, int frames);

/* Handle one press. Returns 1 when the key did something, 0 when it was not a
 * key this phase uses — so the caller can ignore leftovers. */
int battle_ui_key(BattleUi *u, int key);

/* The battle is over and the result was read: the caller goes back to the map. */
int battle_ui_done(const BattleUi *u);

/* Draw the whole screen: four windows, the message or the menu, the HP bars.
 * Clipped to the band [y0, y1) like every other draw call in the port. */
void battle_ui_draw(const BattleUi *u, uint16_t *fb, int fbw, int y0, int y1,
                    const BsPalette *pal);

/* HP bar length in pixels: `hp * 48 / max`, 0 when empty, never 0 when there is
 * HP left (ComputeHPBarPixels in engine/pokemon/health.asm). */
int battle_hp_px(int hp, int max);

/* Bar colour: 0 above half, 1 down to a fifth, 2 below that (the GB's three
 * shades, which is what the bar's palette index picks). */
int battle_hp_colour(int hp, int max);

#endif
