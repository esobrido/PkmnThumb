/*
 * ui/battle.c — the battle screen's state machine and its windows.
 *
 * The sentences are composed, not read: a turn can say anything, so the words are
 * built from glyph codes — the mon's name out of the species table, the move's out
 * of the move table, and a number. Everything is revealed two frames a letter,
 * like the textbox, and A either finishes the sentence or moves on to the next.
 *
 * Once a sentence queue is read out the screen goes back to the action menu,
 * unless the turn ended the battle: then the result is the last sentence and the
 * next A closes the screen (`battle_ui_done`).
 */
#include "ui/battle.h"

#include "ui/frame.h"
#include "ui/list.h"     /* UI_CURSOR: the same ▶ the lists use */

/* ---- messages ----------------------------------------------------------- */

static void msg_reset(BsMsg *m) {
    m->len = 0;
}

static void msg_code(BsMsg *m, int code) {
    if (m->len < BS_GLYPHS) m->g[m->len++] = (uint8_t)code;
}

static void msg_ascii(BsMsg *m, const char *s) {
    for (; *s; s++) msg_code(m, font_code_from_ascii(*s));
}

static void msg_glyphs(BsMsg *m, const uint8_t *g, int len) {
    for (int i = 0; i < len; i++) msg_code(m, g[i]);
}

/* The mon's display name (nickname or species) in the game's charmap. */
static void msg_mon(BsMsg *m, const Mon *mon) {
    int len = 0;
    const uint8_t *g = mon_name(mon, &len);
    if (g && len) msg_glyphs(m, g, len);
    else msg_ascii(m, "???");
}

static void msg_move(BsMsg *m, int move) {
    if (move < 0 || move >= PC_MOVE_COUNT) {
        msg_ascii(m, "---");
        return;
    }
    msg_glyphs(m, &pc_move_names[pc_moves[move].name], pc_moves[move].name_len);
}

static void msg_number(BsMsg *m, uint32_t v) {
    char buf[11];
    int n = 0;
    do {
        buf[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (n) msg_code(m, font_code_from_ascii(buf[--n]));
}

/* Queue a sentence. A queue longer than BS_MSG_MAX cannot happen with the lines a
 * turn produces, but dropping the extra keeps a bug from writing past the array. */
static void push(BattleUi *u, const BsMsg *m) {
    if (u->msg_len >= BS_MSG_MAX) return;
    u->msg[u->msg_len++] = *m;
}

/* Start a fresh queue: the caller is about to push the sentences of one event. */
static void queue_reset(BattleUi *u) {
    u->msg_len = 0;
    u->msg_at = 0;
    u->glyphs = 0;
    u->ticks = 0;
    u->phase = BS_PHASE_MSG;
}

/* ---- the turn's story --------------------------------------------------- */

static void say_used(BattleUi *u, const BattleHit *h) {
    BsMsg m;
    msg_reset(&m);
    msg_mon(&m, h->side == 0 ? u->player : u->wild);
    msg_ascii(&m, " used ");
    msg_move(&m, h->move);
    msg_code(&m, font_code_from_ascii('!'));
    push(u, &m);
}

static void say_hit(BattleUi *u, const BattleHit *h) {
    Mon *def = h->side == 0 ? u->wild : u->player;
    BsMsg m;
    /* A type immunity is the one "no damage" worth a sentence of its own; a hit
     * that lands says how well it did, which is the GB's three lines. */
    if (h->effect == 0) {
        msg_reset(&m);
        msg_ascii(&m, "It doesn't affect ");
        msg_mon(&m, def);
        msg_ascii(&m, "...");
        push(u, &m);
    } else if (h->effect >= 32) {
        msg_reset(&m);
        msg_ascii(&m, "It's super effective!");
        push(u, &m);
    } else if (h->effect <= 8) {
        msg_reset(&m);
        msg_ascii(&m, "It's not very effective...");
        push(u, &m);
    }
    if (h->fainted) {
        msg_reset(&m);
        msg_mon(&m, def);
        msg_ascii(&m, " fainted!");
        push(u, &m);
    }
}

/* Play the turn for the cursor's move slot and turn its result into sentences. */
static void play_turn(BattleUi *u) {
    BattleTurn t;
    battle_turn(&u->battle, u->cursor, &t);

    queue_reset(u);
    for (int i = 0; i < t.count; i++) {
        say_used(u, &t.hit[i]);
        say_hit(u, &t.hit[i]);
    }

    if (t.exp) {
        BsMsg m;
        msg_reset(&m);
        msg_mon(&m, u->player);
        msg_ascii(&m, " gained ");
        msg_number(&m, t.exp);
        msg_ascii(&m, " EXP. Points!");
        push(u, &m);
    }
    /* One line even when several levels came at once: the number printed is the
     * one the mon is on now, which is what the player has to see. */
    if (t.levels) {
        BsMsg m;
        msg_reset(&m);
        msg_mon(&m, u->player);
        msg_ascii(&m, " grew to level ");
        msg_number(&m, u->player->level);
        msg_code(&m, font_code_from_ascii('!'));
        push(u, &m);
    }

    if (t.outcome == BATTLE_WON || t.outcome == BATTLE_LOST) {
        BsMsg m;
        msg_reset(&m);
        msg_ascii(&m, t.outcome == BATTLE_WON ? "You won the battle!"
                                             : "You lost the battle!");
        push(u, &m);
        u->over = 1;
    }
}

/* ---- public state ------------------------------------------------------- */

void battle_ui_start(BattleUi *u, Mon *player, Mon *wild, uint32_t seed) {
    u->player = player;
    u->wild = wild;
    battle_start(&u->battle, player, wild, seed);

    u->cursor = 0;
    u->over = 0;
    u->done = 0;
    queue_reset(u);

    BsMsg m;
    msg_reset(&m);
    msg_ascii(&m, "Wild ");
    msg_mon(&m, wild);
    msg_ascii(&m, " appeared!");
    push(u, &m);

    msg_reset(&m);
    msg_ascii(&m, "Go! ");
    msg_mon(&m, player);
    msg_code(&m, font_code_from_ascii('!'));
    push(u, &m);
}

void battle_ui_update(BattleUi *u, int frames) {
    if (u->phase != BS_PHASE_MSG || u->msg_at >= u->msg_len) return;
    const BsMsg *m = &u->msg[u->msg_at];
    while (frames-- > 0) {
        if (u->glyphs >= m->len) break;
        if (++u->ticks >= BS_TICKS) {
            u->ticks = 0;
            u->glyphs++;
        }
    }
}

/* The sentence on screen is finished and A moves past it. */
static void next_message(BattleUi *u) {
    u->msg_at++;
    u->glyphs = 0;
    u->ticks = 0;
    if (u->msg_at < u->msg_len) return;

    u->msg_len = 0;
    u->msg_at = 0;
    if (u->over) {
        /* The result stays on screen: the caller closes the battle on this A. */
        u->msg_len = 0;
        u->phase = BS_PHASE_DONE;
        u->done = 1;
    } else {
        u->phase = BS_PHASE_ACTION;
    }
}

static int key_message(BattleUi *u, int key) {
    if (key != BS_KEY_A || u->msg_at >= u->msg_len) return 0;
    if (u->glyphs < u->msg[u->msg_at].len) {
        u->glyphs = u->msg[u->msg_at].len;   /* finish the reveal */
        return 1;
    }
    next_message(u);
    return 1;
}

static int key_action(BattleUi *u, int key) {
    if (key == BS_KEY_UP || key == BS_KEY_DOWN) {
        /* The 2x2 grid: up and down are the same toggle, as a two-row grid has no
         * direction to wrap beyond. */
        u->cursor = ((u->cursor / 2) ? 0 : 2) + u->cursor % 2;
        return 1;
    }
    if (key == BS_KEY_LEFT || key == BS_KEY_RIGHT) {
        u->cursor = (u->cursor & ~1) | ((u->cursor & 1) ^ 1);
        return 1;
    }
    if (key != BS_KEY_A) return 0;

    if (u->cursor == BS_ACTION_FIGHT) {
        u->phase = BS_PHASE_MOVE;
        u->cursor = 0;
        return 1;
    }

    /* The other three entries name the system they are waiting for rather than
     * doing something invented: items are BAT-04, running is BAT-05 and the
     * party screen is GAM-02's. */
    BsMsg m;
    msg_reset(&m);
    if (u->cursor == BS_ACTION_PKMN) msg_ascii(&m, "The party screen is not ported yet.");
    else if (u->cursor == BS_ACTION_PACK) msg_ascii(&m, "Items in battle are not ported yet.");
    else msg_ascii(&m, "Running away is not ported yet.");
    queue_reset(u);
    push(u, &m);
    return 1;
}

static int key_move(BattleUi *u, int key) {
    if (key == BS_KEY_UP) {
        u->cursor = (u->cursor + MON_MOVES - 1) % MON_MOVES;
        return 1;
    }
    if (key == BS_KEY_DOWN) {
        u->cursor = (u->cursor + 1) % MON_MOVES;
        return 1;
    }
    if (key == BS_KEY_B) {
        u->phase = BS_PHASE_ACTION;
        u->cursor = BS_ACTION_FIGHT;
        return 1;
    }
    if (key != BS_KEY_A) return 0;
    play_turn(u);
    return 1;
}

int battle_ui_key(BattleUi *u, int key) {
    if (u->done) return 0;
    switch (u->phase) {
    case BS_PHASE_MSG: return key_message(u, key);
    case BS_PHASE_ACTION: return key_action(u, key);
    case BS_PHASE_MOVE: return key_move(u, key);
    default: return 0;
    }
}

int battle_ui_done(const BattleUi *u) {
    return u->done;
}

/* ---- drawing ------------------------------------------------------------ */

static void fill_rect(uint16_t *fb, int fbw, int y0, int y1,
                      int x, int y, int w, int h, uint16_t c) {
    for (int py = y; py < y + h; py++) {
        if (py < y0 || py >= y1) continue;
        uint16_t *row = &fb[py * fbw];
        for (int px = x; px < x + w; px++)
            if (px >= 0 && px < fbw) row[px] = c;
    }
}

/* Glyph codes, at most `max` of them (a name longer than its window is cut, not
 * squeezed); returns the pixels used. */
static int draw_glyphs(uint16_t *fb, int fbw, int y0, int y1, int x, int y,
                       const uint8_t *g, int len, int max, uint16_t ink) {
    int n = 0;
    for (int i = 0; i < len && n < max; i++, n++)
        font_draw_glyph(fb, fbw, y0, y1, x + n * 8, y, g[i], ink);
    return n * 8;
}

static int ascii_glyphs(const char *s, uint8_t *out, int max) {
    int n = 0;
    for (; *s && n < max; s++) out[n++] = font_code_from_ascii(*s);
    return n;
}

static int digits_of(int v) {
    int n = 1;
    while (v >= 10) { v /= 10; n++; }
    return n;
}

int battle_hp_px(int hp, int max) {
    /* ComputeHPBarPixels: bc * (6 * 8) / de, and a mon with any HP left always
     * lights at least one pixel (the GB doubles the bar and floors at 1). */
    if (max <= 0 || hp <= 0) return 0;
    if (hp >= max) return BS_HP_W;
    int px = hp * BS_HP_W / max;
    return px < 1 ? 1 : px;
}

int battle_hp_colour(int hp, int max) {
    if (max <= 0) return 2;
    if (hp * 2 > max) return 0;
    if (hp * 5 > max) return 1;
    return 2;
}

static void draw_bar(uint16_t *fb, int fbw, int y0, int y1, int x, int y,
                     int hp, int max, const BsPalette *pal) {
    uint16_t c = battle_hp_colour(hp, max) == 0 ? pal->hp_green
               : battle_hp_colour(hp, max) == 1 ? pal->hp_yellow
               : pal->hp_red;
    /* A dark trough with the coloured part over it: at 4 px tall the GB's two
     * rows of bar plus its outline would turn into mush. */
    fill_rect(fb, fbw, y0, y1, x, y, BS_HP_W, 4, pal->ink);
    int px = battle_hp_px(hp, max);
    if (px > 0) fill_rect(fb, fbw, y0, y1, x, y, px, 4, c);
}

/* The name row: the whole species name, up to what 80 px hold (nine glyphs).
 * The GB puts "Lv##" beside it, which does not fit here — a seven-letter name
 * plus a level is wider than the window — so the level goes on the row below. */
static void draw_name(uint16_t *fb, int fbw, int y0, int y1, const Mon *mon,
                      int x, int y, int w, const BsPalette *pal) {
    int name_len = 0;
    const uint8_t *name = mon_name(mon, &name_len);
    int room = (w - 8) / 8;
    if (name && room > 0)
        draw_glyphs(fb, fbw, y0, y1, x, y, name, name_len, room, pal->ink);
}

/* "Lv" and the level: two glyphs of text and the number, as the GB prints it. */
static void draw_level(uint16_t *fb, int fbw, int y0, int y1, const Mon *mon,
                       int x, int y, const BsPalette *pal) {
    uint8_t lv[8];
    int n = 0;
    lv[n++] = font_code_from_ascii('L');
    lv[n++] = font_code_from_ascii('v');
    char buf[4];
    int d = digits_of(mon->level);
    int v = mon->level;
    for (int i = d - 1; i >= 0; i--) { buf[i] = (char)('0' + v % 10); v /= 10; }
    for (int i = 0; i < d; i++) lv[n++] = font_code_from_ascii(buf[i]);
    draw_glyphs(fb, fbw, y0, y1, x, y, lv, n, n, pal->ink);
}

/* One mon's window: frame, name, HP bar with its numbers, and the level. */
static void draw_mon(uint16_t *fb, int fbw, int y0, int y1, const Mon *mon,
                     int x, int y, int w, int h, int numbers,
                     const BsPalette *pal) {
    ui_frame_box(fb, fbw, y0, y1, x, y, w, h, pal->ink, pal->paper);
    draw_name(fb, fbw, y0, y1, mon, x + 4, y + 4, w, pal);

    uint8_t hp[2];
    ascii_glyphs("HP", hp, 2);
    draw_glyphs(fb, fbw, y0, y1, x + 4, y + 16, hp, 2, 2, pal->ink);
    draw_bar(fb, fbw, y0, y1, x + 22, y + 18, mon->hp, mon->stat[MON_HP], pal);

    draw_level(fb, fbw, y0, y1, mon, x + 4, y + 24, pal);
    if (numbers) {
        char buf[16];
        int n = 0;
        char tmp[8];
        int hpv = mon->hp, mxv = mon->stat[MON_HP];
        int d = digits_of(mxv);
        for (int i = d - 1; i >= 0; i--) { tmp[i] = (char)('0' + mxv % 10); mxv /= 10; }
        int k = 0;
        for (; k < d; k++) buf[n++] = tmp[k];
        buf[n++] = '/';
        d = digits_of(hpv);
        for (int i = d - 1; i >= 0; i--) { tmp[i] = (char)('0' + hpv % 10); hpv /= 10; }
        for (k = 0; k < d; k++) buf[n++] = tmp[k];
        buf[n] = 0;                 /* ascii_glyphs walks to the terminator */
        uint8_t g[16];
        int gn = ascii_glyphs(buf, g, 16);
        draw_glyphs(fb, fbw, y0, y1, x + w - 4 - gn * 8, y + 24, g, gn, gn, pal->ink);
    }
}

static void draw_message(const BattleUi *u, uint16_t *fb, int fbw, int y0, int y1,
                         const BsPalette *pal) {
    if (u->msg_at >= u->msg_len) return;
    const BsMsg *m = &u->msg[u->msg_at];
    int shown = u->glyphs < m->len ? u->glyphs : m->len;
    for (int i = 0; i < shown && i < BS_COLS * BS_ROWS; i++)
        font_draw_glyph(fb, fbw, y0, y1,
                        BS_BOX_X + 4 + (i % BS_COLS) * 8,
                        BS_BOX_Y + 4 + (i / BS_COLS) * 8, m->g[i], pal->ink);
}

/* FIGHT / PKMN / PACK / RUN in the same 2x2 grid the GB uses, with ▶ on the
 * current one. */
static void draw_actions(const BattleUi *u, uint16_t *fb, int fbw, int y0, int y1,
                         const BsPalette *pal) {
    static const char *const k_actions[BS_ACTION_COUNT] = { "FIGHT", "PKMN", "PACK", "RUN" };
    for (int i = 0; i < BS_ACTION_COUNT; i++) {
        int x = BS_BOX_X + 8 + (i % 2) * 60;
        int y = BS_BOX_Y + 6 + (i / 2) * 16;
        uint8_t g[8];
        int n = ascii_glyphs(k_actions[i], g, 8);
        if (u->cursor == i)
            font_draw_glyph(fb, fbw, y0, y1, x, y, UI_CURSOR, pal->ink);
        draw_glyphs(fb, fbw, y0, y1, x + 16, y, g, n, n, pal->ink);
    }
}

/* The four move slots, one per line; the PP of the highlighted one goes in the
 * little window at the top-right, because a move name and "PP 25/25" do not fit
 * on one 128 px line. */
static void draw_moves(const BattleUi *u, uint16_t *fb, int fbw, int y0, int y1,
                       const BsPalette *pal) {
    for (int i = 0; i < MON_MOVES; i++) {
        int y = BS_BOX_Y + 3 + i * 9;
        int move = u->player->moves[i];
        if (u->cursor == i)
            font_draw_glyph(fb, fbw, y0, y1, 2, y, UI_CURSOR, pal->ink);
        if (move > 0 && move < PC_MOVE_COUNT)
            draw_glyphs(fb, fbw, y0, y1, 18, y,
                        &pc_move_names[pc_moves[move].name],
                        pc_moves[move].name_len, 13, pal->ink);
        else {
            uint8_t dash[3];
            int n = ascii_glyphs("---", dash, 3);
            draw_glyphs(fb, fbw, y0, y1, 18, y, dash, n, n, pal->ink);
        }
    }
}

static void draw_pp(const BattleUi *u, uint16_t *fb, int fbw, int y0, int y1,
                    const BsPalette *pal) {
    ui_frame_box(fb, fbw, y0, y1, BS_PP_X, BS_PP_Y, BS_PP_W, BS_PP_H,
                 pal->ink, pal->paper);
    uint8_t pp[2];
    ascii_glyphs("PP", pp, 2);
    draw_glyphs(fb, fbw, y0, y1, BS_PP_X + 4, BS_PP_Y + 4, pp, 2, 2, pal->ink);

    /* "12/15" of the highlighted slot, flush right under the label. */
    int move = u->player->moves[u->cursor];
    if (move <= 0 || move >= PC_MOVE_COUNT) return;
    char buf[8];
    int n = 0, cur = u->player->pp[u->cursor], max = pc_moves[move].pp;
    char tmp[4];
    int d = digits_of(cur);
    for (int i = d - 1; i >= 0; i--) { tmp[i] = (char)('0' + cur % 10); cur /= 10; }
    for (int i = 0; i < d; i++) buf[n++] = tmp[i];
    buf[n++] = '/';
    d = digits_of(max);
    for (int i = d - 1; i >= 0; i--) { tmp[i] = (char)('0' + max % 10); max /= 10; }
    for (int i = 0; i < d; i++) buf[n++] = tmp[i];
    buf[n] = 0;
    uint8_t g[8];
    int gn = ascii_glyphs(buf, g, 8);
    draw_glyphs(fb, fbw, y0, y1, BS_PP_X + BS_PP_W - 4 - gn * 8, BS_PP_Y + 16,
                g, gn, gn, pal->ink);
}

void battle_ui_draw(const BattleUi *u, uint16_t *fb, int fbw, int y0, int y1,
                    const BsPalette *pal) {
    draw_mon(fb, fbw, y0, y1, u->wild, BS_ENEMY_X, BS_ENEMY_Y,
             BS_ENEMY_W, BS_ENEMY_H, 0, pal);
    draw_mon(fb, fbw, y0, y1, u->player, BS_PLAYER_X, BS_PLAYER_Y,
             BS_PLAYER_W, BS_PLAYER_H, 1, pal);

    ui_frame_box(fb, fbw, y0, y1, BS_BOX_X, BS_BOX_Y, BS_BOX_W, BS_BOX_H,
                 pal->ink, pal->paper);

    if (u->phase == BS_PHASE_ACTION) draw_actions(u, fb, fbw, y0, y1, pal);
    else if (u->phase == BS_PHASE_MOVE) {
        draw_moves(u, fb, fbw, y0, y1, pal);
        draw_pp(u, fb, fbw, y0, y1, pal);
    } else draw_message(u, fb, fbw, y0, y1, pal);
}
