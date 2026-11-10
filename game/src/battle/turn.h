/*
 * battle/turn.h — one turn of a wild 1 vs 1 battle.
 *
 * This is BAT-01, and only BAT-01: who moves first, the damage of a plain hit
 * (type, STAB and the 85..100 roll) and the experience a KO is worth. Accuracy
 * and critical hits are BAT-02, a move's effect is BAT-03, status changes are
 * BAT-04, and there is no switching, no capture and no items yet. A status move
 * therefore spends its PP and does nothing else, which is honest rather than
 * pretending to work.
 *
 * The two mons belong to the caller: the battle keeps pointers, never copies,
 * so the party and the wild mon keep their HP, PP and experience afterwards.
 * The wild mon's choice is random among the moves it can still use, which is
 * the whole AI of BAT-01.
 *
 * Pure: no Mote, no globals, no allocation. The RNG is a field of `Battle` so a
 * test (or a replay) can seed it and get the same turn.
 */
#ifndef BATTLE_TURN_H
#define BATTLE_TURN_H

#include <stdint.h>

#include "mon/mon.h"

typedef enum {
    BATTLE_ONGOING = 0,
    BATTLE_WON,
    BATTLE_LOST
} BattleOutcome;

/* One attack, as it came out. `side` is 0 for the player, 1 for the wild mon. */
typedef struct {
    int side;
    int move;      /* PC_MOVE_* actually used */
    int damage;    /* HP taken off the defender */
    int effect;    /* Q4 type modifier: 0 immune, 16 neutral, 32 x2, 64 x4 */
    int fainted;   /* the defender reached 0 HP */
} BattleHit;

/* What happened between the caller's last call and this one. */
typedef struct {
    BattleHit hit[2]; /* at most one attack each */
    int count;        /* 0 when the battle was already over */
    int outcome;      /* BATTLE_* after the turn */
    uint32_t exp;     /* experience the KO gave the player's mon */
    int levels;       /* levels it gained from that experience */
} BattleTurn;

typedef struct {
    Mon *player;   /* the active party member */
    Mon *wild;     /* the wild mon */
    uint32_t rng;
    uint32_t turns;
    int outcome;
} Battle;

/* Point the battle at two mons and seed its RNG (0 is replaced: an xorshift
 * with a zero state never leaves it). */
void battle_start(Battle *b, Mon *player, Mon *wild, uint32_t seed);

/* Play one turn, with the player choosing a move slot (0..MON_MOVES-1).
 *
 * A slot with no move or no PP falls back to the first slot that has both, and
 * a mon with no PP left uses Struggle, as the game does. The faster mon attacks
 * first; on equal speed the RNG decides, as `CheckSpeed` does. A hit that
 * faints its target ends the turn, so the loser never answers.
 *
 * On a win the wild mon's experience goes to the player's mon through
 * `mon_add_exp`, and `out` says how much and how many levels it bought. */
void battle_turn(Battle *b, int slot, BattleTurn *out);

/* What beating that mon is worth: base_exp * level / 7, at least 1. */
uint32_t battle_exp_reward(const Mon *beaten);

/* A move's priority (data/moves/priorities.asm); 0 for everything not listed. */
int battle_priority(int move);

#endif
