/* ui/battle — the battle screen: windows, sentences and the four actions.
 *
 * The screen is the part of BAT-07 that decides what the player sees and what a
 * button does; the turn itself is battle/turn's. So the checks here are: the HP
 * bar's arithmetic and colour, that the opening sentences come from the mons'
 * own names, that the action grid and the move list navigate, that choosing a
 * move really plays a turn (HP, PP), that a win pays experience and closes the
 * screen, and that every window is drawn inside the 128x128 panel.
 */
#include <stdio.h>

#include "test_util.h"
#include "ui/battle.h"

#define W 128
#define H 128

static uint16_t fb[W * H];
static Mon player, wild;
static BattleUi ui;

#define INK 0x1111
#define PAPER 0x2222
#define GREEN 0x3333
#define YELLOW 0x4444
#define RED 0x5555

static const BsPalette pal = { INK, PAPER, GREEN, YELLOW, RED };

static void setup(int plvl, int wlvl) {
    mon_init(&player, PC_SPECIES_CYNDAQUIL, plvl, 0x1234);
    mon_set_move(&player, 0, PC_MOVE_TACKLE);
    mon_set_move(&player, 1, PC_MOVE_EMBER);
    mon_set_move(&player, 2, PC_MOVE_LEER);
    mon_set_move(&player, 3, PC_MOVE_SMOKESCREEN);
    mon_init(&wild, PC_SPECIES_RATTATA, wlvl, 0x4321);
    mon_set_move(&wild, 0, PC_MOVE_TACKLE);
    mon_set_move(&wild, 1, PC_MOVE_LEER);
    battle_ui_start(&ui, &player, &wild, 0x5eed1234u);
}

/* A on every sentence: the first reveals it, the second moves past it, so the
 * loop presses once and lets the phase decide when there is nothing left. */
static void read_all(void) {
    for (int guard = 0; guard < 128 && ui.phase == BS_PHASE_MSG; guard++)
        battle_ui_key(&ui, BS_KEY_A);
}

static int ink_at(int x, int y) {
    return fb[y * W + x] != 0;
}

static int ink_in(int x, int y, int w, int h) {
    int n = 0;
    for (int py = y; py < y + h; py++)
        for (int px = x; px < x + w; px++)
            if (px >= 0 && px < W && py >= 0 && py < H && fb[py * W + px]) n++;
    return n;
}

static int count_colour(uint16_t c, int x, int y, int w, int h) {
    int n = 0;
    for (int py = y; py < y + h; py++)
        for (int px = x; px < x + w; px++)
            if (px >= 0 && px < W && py >= 0 && py < H && fb[py * W + px] == c) n++;
    return n;
}

static void test_hp_bar(void) {
    /* ComputeHPBarPixels: 48 px across, 0 only when empty. */
    CHECK_EQ(battle_hp_px(0, 100), 0);
    CHECK_EQ(battle_hp_px(1, 100), 1);       /* any HP lights a pixel */
    CHECK_EQ(battle_hp_px(50, 100), 24);
    CHECK_EQ(battle_hp_px(49, 100), 23);
    CHECK_EQ(battle_hp_px(100, 100), 48);
    CHECK_EQ(battle_hp_px(120, 100), 48);    /* never past the bar */
    CHECK_EQ(battle_hp_px(10, 0), 0);

    /* Green above half, yellow down to a fifth, red below (the GB's three). */
    CHECK_EQ(battle_hp_colour(100, 100), 0);
    CHECK_EQ(battle_hp_colour(51, 100), 0);
    CHECK_EQ(battle_hp_colour(50, 100), 1);
    CHECK_EQ(battle_hp_colour(21, 100), 1);
    CHECK_EQ(battle_hp_colour(20, 100), 2);
    CHECK_EQ(battle_hp_colour(0, 100), 2);
}

static void test_opening(void) {
    setup(5, 3);
    CHECK_EQ(ui.phase, BS_PHASE_MSG);
    CHECK_EQ(ui.msg_len, 2);
    CHECK_EQ(ui.glyphs, 0);           /* nothing shown until the first update */

    /* "Wild RATTATA appeared!": the five letters, then the species' own name. */
    const uint8_t *w = 0;
    int wlen = 0;
    w = mon_name(&wild, &wlen);
    CHECK(ui.msg[0].len > 5 + wlen);
    CHECK_EQ(ui.msg[0].g[0], font_code_from_ascii('W'));
    CHECK_EQ(ui.msg[0].g[4], font_code_from_ascii(' '));
    for (int i = 0; i < wlen; i++) CHECK_EQ(ui.msg[0].g[5 + i], w[i]);

    /* "Go! CYNDAQUIL!" */
    const uint8_t *p = mon_name(&player, &wlen);
    CHECK_EQ(ui.msg[1].g[0], font_code_from_ascii('G'));
    CHECK_EQ(ui.msg[1].g[2], font_code_from_ascii('!'));
    for (int i = 0; i < wlen; i++) CHECK_EQ(ui.msg[1].g[4 + i], p[i]);

    /* The reveal is two frames a letter, and A finishes it at once. */
    int len = ui.msg[0].len;
    battle_ui_update(&ui, BS_TICKS);
    CHECK_EQ(ui.glyphs, 1);
    battle_ui_update(&ui, BS_TICKS * 2);
    CHECK_EQ(ui.glyphs, 3);
    battle_ui_key(&ui, BS_KEY_A);
    CHECK_EQ(ui.glyphs, len);
    battle_ui_key(&ui, BS_KEY_A);     /* on to "Go!" */
    CHECK_EQ(ui.msg_at, 1);
    CHECK_EQ(ui.glyphs, 0);

    read_all();
    CHECK_EQ(ui.phase, BS_PHASE_ACTION);
    CHECK_EQ(ui.cursor, 0);
}

static void test_actions(void) {
    setup(5, 3);
    read_all();
    CHECK_EQ(ui.phase, BS_PHASE_ACTION);

    /* The 2x2 grid: FIGHT PKMN / PACK RUN. */
    CHECK_EQ(ui.cursor, BS_ACTION_FIGHT);
    battle_ui_key(&ui, BS_KEY_RIGHT);
    CHECK_EQ(ui.cursor, BS_ACTION_PKMN);
    battle_ui_key(&ui, BS_KEY_DOWN);
    CHECK_EQ(ui.cursor, BS_ACTION_RUN);
    battle_ui_key(&ui, BS_KEY_LEFT);
    CHECK_EQ(ui.cursor, BS_ACTION_PACK);
    battle_ui_key(&ui, BS_KEY_UP);
    CHECK_EQ(ui.cursor, BS_ACTION_FIGHT);

    /* PKMN, PACK and RUN say what they are waiting for and cost the wild mon
     * nothing: no turn was played behind their backs. */
    int hp = wild.hp, pp = player.pp[0];
    battle_ui_key(&ui, BS_KEY_RIGHT);
    battle_ui_key(&ui, BS_KEY_A);
    CHECK_EQ(ui.phase, BS_PHASE_MSG);
    CHECK(ui.msg[0].len > 0);
    read_all();
    CHECK_EQ(ui.phase, BS_PHASE_ACTION);
    CHECK_EQ(wild.hp, hp);
    CHECK_EQ(player.pp[0], pp);
    CHECK_EQ(ui.cursor, BS_ACTION_PKMN);     /* the cursor stays put */
}

static void test_moves(void) {
    setup(5, 3);
    read_all();

    battle_ui_key(&ui, BS_KEY_A);            /* FIGHT */
    CHECK_EQ(ui.phase, BS_PHASE_MOVE);
    CHECK_EQ(ui.cursor, 0);

    battle_ui_key(&ui, BS_KEY_DOWN);
    CHECK_EQ(ui.cursor, 1);
    battle_ui_key(&ui, BS_KEY_UP);
    CHECK_EQ(ui.cursor, 0);
    battle_ui_key(&ui, BS_KEY_UP);           /* wraps to the last slot */
    CHECK_EQ(ui.cursor, MON_MOVES - 1);
    battle_ui_key(&ui, BS_KEY_B);
    CHECK_EQ(ui.phase, BS_PHASE_ACTION);
    CHECK_EQ(ui.cursor, BS_ACTION_FIGHT);

    /* Tackle: the turn is played for real, so PP goes down and the wild mon is
     * hurt. Which of the two moves first depends on speed, so only the player's
     * own bookkeeping is pinned. */
    int wp = wild.hp;
    int pp = player.pp[0];
    battle_ui_key(&ui, BS_KEY_A);            /* FIGHT */
    CHECK_EQ(ui.phase, BS_PHASE_MOVE);
    battle_ui_key(&ui, BS_KEY_A);            /* Tackle */
    CHECK_EQ(ui.phase, BS_PHASE_MSG);
    CHECK(ui.msg_len >= 2);                  /* "used" and the result */
    CHECK_EQ(player.pp[0], pp - 1);
    CHECK(wild.hp < wp);

    /* Every sentence names a move: "… used TACKLE!". */
    int found = 0;
    for (int i = 0; i < ui.msg_len; i++) {
        const BsMsg *m = &ui.msg[i];
        for (int k = 0; k + 4 < m->len; k++) {
            if (m->g[k] == font_code_from_ascii(' ') &&
                m->g[k + 1] == font_code_from_ascii('u') &&
                m->g[k + 2] == font_code_from_ascii('s') &&
                m->g[k + 3] == font_code_from_ascii('e')) found = 1;
        }
    }
    CHECK(found);

    read_all();
    CHECK_EQ(ui.phase, BS_PHASE_ACTION);
    CHECK(!battle_ui_done(&ui));
}

static void test_win(void) {
    setup(5, 3);
    read_all();

    /* One hit faints it, and the experience lands near the next level so the
     * level-up line has to appear: the queue is what a turn reports. */
    wild.hp = 1;
    player.exp = mon_exp_for_level(&player, player.level + 1) - 4;

    battle_ui_key(&ui, BS_KEY_A);            /* FIGHT */
    battle_ui_key(&ui, BS_KEY_A);
    CHECK_EQ(ui.phase, BS_PHASE_MSG);
    CHECK_EQ(ui.battle.outcome, BATTLE_WON);
    CHECK_EQ(wild.hp, 0);

    int saw_exp = 0, saw_level = 0, saw_result = 0;
    for (int i = 0; i < ui.msg_len; i++) {
        const BsMsg *m = &ui.msg[i];
        for (int k = 0; k + 3 < m->len; k++) {
            if (m->g[k] == font_code_from_ascii('E') &&
                m->g[k + 1] == font_code_from_ascii('X') &&
                m->g[k + 2] == font_code_from_ascii('P')) saw_exp = 1;
            if (m->g[k] == font_code_from_ascii('g') &&
                m->g[k + 1] == font_code_from_ascii('r') &&
                m->g[k + 2] == font_code_from_ascii('e') &&
                m->g[k + 3] == font_code_from_ascii('w')) saw_level = 1;
        }
        if (m->g[0] == font_code_from_ascii('Y')) saw_result = 1;   /* You won */
    }
    CHECK(saw_exp);
    CHECK(saw_level);
    CHECK(saw_result);
    CHECK_EQ(player.level, 6);

    /* The result is read out and the last A closes the screen. */
    read_all();
    CHECK(battle_ui_done(&ui));
    CHECK_EQ(ui.phase, BS_PHASE_DONE);
    CHECK(!battle_ui_key(&ui, BS_KEY_A));
}

static void test_draw(void) {
    setup(5, 3);
    for (int i = 0; i < W * H; i++) fb[i] = 0;
    battle_ui_draw(&ui, fb, W, 0, H, &pal);

    /* Both windows are framed, and the message window covers the bottom. */
    CHECK_EQ(fb[BS_ENEMY_Y * W + BS_ENEMY_X], INK);
    CHECK_EQ(fb[BS_PLAYER_Y * W + BS_PLAYER_X], INK);
    CHECK_EQ(fb[BS_BOX_Y * W + BS_BOX_X], INK);
    CHECK_EQ(fb[(BS_BOX_Y + BS_BOX_H - 1) * W + BS_BOX_W - 1], INK);
    CHECK_EQ(fb[(BS_ENEMY_Y + 1) * W + BS_ENEMY_X + 1], PAPER);

    /* "RATTATA" is seven glyphs and the window holds nine, so the seventh cell
     * of the name row carries ink: a truncated name would leave it blank. */
    CHECK(ink_in(BS_ENEMY_X + 4 + 6 * 8, BS_ENEMY_Y + 4, 8, 8) > 0);
    /* The level is on the row below the HP bar, and your own mon also shows its
     * HP numbers there. */
    CHECK(ink_in(BS_ENEMY_X + 4, BS_ENEMY_Y + 24, 8, 8) > 0);

    /* The opening sentence is now revealed and drawn inside the message box. */
    battle_ui_update(&ui, 1000);
    for (int i = 0; i < W * H; i++) fb[i] = 0;
    battle_ui_draw(&ui, fb, W, 0, H, &pal);
    CHECK(ink_at(BS_BOX_X + 4, BS_BOX_Y + 4));

    /* A full bar is green and 48 px long; at a fifth it is red and shorter. */
    for (int i = 0; i < W * H; i++) fb[i] = 0;
    battle_ui_draw(&ui, fb, W, 0, H, &pal);
    int bar_y = BS_ENEMY_Y + 18, bar_x = BS_ENEMY_X + 22;
    CHECK_EQ(count_colour(GREEN, bar_x, bar_y, BS_HP_W, 4), BS_HP_W * 4);

    wild.hp = wild.stat[MON_HP] / 5;
    int px = battle_hp_px(wild.hp, wild.stat[MON_HP]);
    for (int i = 0; i < W * H; i++) fb[i] = 0;
    battle_ui_draw(&ui, fb, W, 0, H, &pal);
    CHECK_EQ(count_colour(RED, bar_x, bar_y, BS_HP_W, 4), px * 4);
    CHECK_EQ(count_colour(GREEN, bar_x, bar_y, BS_HP_W, 4), 0);

    /* The move list and its PP window: four rows inside the box. */
    read_all();
    battle_ui_key(&ui, BS_KEY_A);            /* FIGHT */
    CHECK_EQ(ui.phase, BS_PHASE_MOVE);
    for (int i = 0; i < W * H; i++) fb[i] = 0;
    battle_ui_draw(&ui, fb, W, 0, H, &pal);
    CHECK_EQ(fb[BS_PP_Y * W + BS_PP_X], INK);
    CHECK(ink_at(2, BS_BOX_Y + 3));                     /* the ▶ */
    CHECK(ink_at(18, BS_BOX_Y + 3));                    /* "TACKLE" */
    CHECK(ink_at(18, BS_BOX_Y + 3 + 3 * 9));            /* the fourth slot */

    /* Everything stays on the panel. */
    CHECK(BS_ENEMY_X + BS_ENEMY_W <= W);
    CHECK(BS_PP_X + BS_PP_W <= W);
    CHECK(BS_PLAYER_X + BS_PLAYER_W <= W);
    CHECK(BS_BOX_Y + BS_BOX_H <= H);
    CHECK(BS_PLAYER_Y + BS_PLAYER_H <= BS_BOX_Y);
    CHECK(BS_ENEMY_Y + BS_ENEMY_H <= BS_PLAYER_Y);
}

int main(void) {
    test_hp_bar();
    test_opening();
    test_actions();
    test_moves();
    test_win();
    test_draw();
    return test_report("test_ui_battle");
}
