/* battle/turn.c — turn order, one hit each, and the experience of a KO. */
#include "battle/turn.h"

#include <stddef.h>

#include "battle/damage.h"
#include "battle/type.h"
#include "gen/moves.h"
#include "gen/pokemon.h"

/* The last priority entry in the original is a -1 terminator; the extractor
 * already dropped it, so this is a plain lookup. */
int battle_priority(int move) {
    for (int i = 0; i < PC_MOVE_PRIORITY_COUNT; i++)
        if (pc_move_priorities[i].move == move)
            return pc_move_priorities[i].priority;
    return 0;
}

uint32_t battle_exp_reward(const Mon *beaten) {
    if (beaten == NULL || beaten->species == 0 ||
        beaten->species >= PC_SPECIES_COUNT)
        return 0;
    uint32_t base = pc_species[beaten->species].base_exp;
    uint32_t exp = base * (uint32_t)beaten->level / 7u;
    /* "make sure to give at least 1 exp": a level 1 mon still pays. */
    return exp ? exp : 1u;
}

static uint32_t rng_next(Battle *b) {
    uint32_t x = b->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    b->rng = x;
    return x;
}

/* Uniform in 0..n-1. */
static int rng_range(Battle *b, int n) {
    if (n <= 1)
        return 0;
    return (int)(rng_next(b) % (uint32_t)n);
}

void battle_start(Battle *b, Mon *player, Mon *wild, uint32_t seed) {
    b->player = player;
    b->wild = wild;
    b->rng = seed ? seed : 0x9e3779b9u;
    b->turns = 0;
    b->outcome = BATTLE_ONGOING;
}

/* The slot the player asked for if it can be used, else the first usable one,
 * else -1 (Struggle). */
static int usable_slot(const Mon *m, int slot) {
    if (slot >= 0 && slot < MON_MOVES && m->moves[slot] && m->pp[slot])
        return slot;
    for (int i = 0; i < MON_MOVES; i++)
        if (m->moves[i] && m->pp[i])
            return i;
    return -1;
}

/* The wild mon picks at random among the moves it can still use. */
static int wild_slot(Battle *b) {
    int slots[MON_MOVES], n = 0;
    for (int i = 0; i < MON_MOVES; i++)
        if (b->wild->moves[i] && b->wild->pp[i])
            slots[n++] = i;
    if (n == 0)
        return -1;
    return slots[rng_range(b, n)];
}

/* Priority first, then speed, then a coin: CheckMovePriority + CheckSpeed. */
static int player_first(Battle *b, int pmove, int emove) {
    int pp = battle_priority(pmove);
    int ep = battle_priority(emove);
    if (pp != ep)
        return pp > ep;
    if (b->player->stat[MON_SPE] != b->wild->stat[MON_SPE])
        return b->player->stat[MON_SPE] > b->wild->stat[MON_SPE];
    return rng_next(b) & 1u;
}

static void attack(Battle *b, int side, int slot, int move, BattleHit *hit) {
    Mon *atk = side ? b->wild : b->player;
    Mon *def = side ? b->player : b->wild;
    const PcMove *mv = &pc_moves[move];

    hit->side = side;
    hit->move = move;
    hit->damage = 0;
    hit->effect = TYPE_NEUTRAL;
    hit->fainted = 0;

    if (slot >= 0 && slot < MON_MOVES && atk->pp[slot])
        atk->pp[slot]--;

    hit->effect = type_effect(def, mv->type);
    /* Immunity hurts nothing; a status move, or one whose power the effect
     * computes (power 1), is BAT-03's business and does no damage here. */
    if (hit->effect == 0 || mv->category == PC_CAT_STATUS || mv->power <= 1)
        return;

    int special = mv->category == PC_CAT_SPECIAL;
    int base = damage_base(atk->level, mv->power,
                           atk->stat[special ? MON_SAT : MON_ATK],
                           def->stat[special ? MON_SDF : MON_DEF]);
    int q4 = hit->effect;
    /* STAB is 1.5x, folded into the same Q4 value: hTypeMatchup + hTypeMatchup
     * / 2 in BattleCommand_stab. */
    if (mon_has_type(atk, mv->type))
        q4 += q4 / 2;
    int dmg = damage_modify(base, q4, 85 + rng_range(b, 16));
    if (dmg > def->hp)
        dmg = def->hp;
    def->hp = (uint16_t)(def->hp - dmg);
    hit->damage = dmg;
    hit->fainted = def->hp == 0;
}

void battle_turn(Battle *b, int slot, BattleTurn *out) {
    out->count = 0;
    out->outcome = b->outcome;
    out->exp = 0;
    out->levels = 0;
    if (b->outcome != BATTLE_ONGOING)
        return;

    b->turns++;
    int pslot = usable_slot(b->player, slot);
    int eslot = wild_slot(b);
    int pmove = pslot < 0 ? PC_MOVE_STRUGGLE : b->player->moves[pslot];
    int emove = eslot < 0 ? PC_MOVE_STRUGGLE : b->wild->moves[eslot];

    int first = player_first(b, pmove, emove);
    for (int i = 0; i < 2; i++) {
        BattleHit *hit = &out->hit[out->count];
        /* i == 0 is whoever goes first; a fainted target skips the rest. */
        int side = (i == 0) ? (first ? 0 : 1) : (first ? 1 : 0);
        if (side == 0)
            attack(b, 0, pslot, pmove, hit);
        else
            attack(b, 1, eslot, emove, hit);
        out->count++;
        if (hit->fainted)
            break;
    }

    if (b->wild->hp == 0) {
        b->outcome = BATTLE_WON;
        /* GiveExperiencePoints gives nothing at level 100. */
        if (b->player->level < MON_MAX_LEVEL) {
            uint32_t exp = battle_exp_reward(b->wild);
            out->exp = exp;
            out->levels = mon_add_exp(b->player, exp);
        }
    } else if (b->player->hp == 0) {
        b->outcome = BATTLE_LOST;
    }
    out->outcome = b->outcome;
}
