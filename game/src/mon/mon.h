/*
 * mon/mon.h — one Pokémon, as the game keeps it.
 *
 * The fields are the ones the original stores per party mon that do not need a
 * save format yet: species, level, the four DVs, the six stats (HP included),
 * experience, four moves with their PP, status and a nickname. Types, base
 * stats, catch rate and growth curve are *not* copied in: they belong to the
 * species and are read from `gen/pokemon.h`, which is what the game does too
 * (its mon struct holds no base stats).
 *
 * `dv` is the game's 16-bit DV word (MON_DVS): attack and defence in the high
 * byte, speed and special in the low one, one nibble each. The HP DV is not
 * stored — Gen 2 derives it from the low bit of the other four, and so does
 * `mon_recalc`, so a mon cannot hold a combination the original could not.
 *
 * Pure: a struct in, values out, no Mote, no globals, no allocation.
 */
#ifndef MON_MON_H
#define MON_MON_H

#include <stdint.h>

#include "gen/moves.h"
#include "gen/pokemon.h"

/* Names are the game's charmap bytes: 10 glyphs plus a terminator (MON_NAME_LENGTH). */
#define MON_NICK_LEN 11
#define MON_MOVES 4

/* Stats in the game's order (wBaseStats): HP first, then the five others. */
enum { MON_HP, MON_ATK, MON_DEF, MON_SPE, MON_SAT, MON_SDF, MON_STAT_COUNT };

typedef struct {
    uint16_t species;              /* PC_SPECIES_*, 0 = empty */
    uint8_t level;
    uint16_t dv;                   /* attack/defence, speed/special nibbles */
    uint16_t hp;                   /* current HP */
    uint16_t stat[MON_STAT_COUNT]; /* stat[MON_HP] is the maximum */
    uint32_t exp;
    uint8_t moves[MON_MOVES];
    uint8_t pp[MON_MOVES];
    uint8_t status;                /* 0 = healthy; the bits are BAT-04 */
    uint8_t nickname[MON_NICK_LEN];
} Mon;

/* Fill in a mon: species, level, DVs, full HP, no moves, no nickname. */
void mon_init(Mon *m, int species, int level, uint16_t dv);

/* Recompute the six stats from the species' base stats, the level and the DVs
 * (CalcPkmnStatC in engine/pokemon/mon_stats.asm). Current HP is kept, and
 * clamped to the new maximum. */
void mon_recalc(Mon *m);

/* The species' base value for one stat, or 0 for an empty mon. */
int mon_base_stat(const Mon *m, int which);

/* One of the species' two types. */
int mon_type(const Mon *m, int i);

/* Does the mon have that type? A single-type species repeats its type, as the
 * game does, so this is a straight comparison of both slots. */
int mon_has_type(const Mon *m, int type);

/* The display name: the nickname if there is one, else the species'. `len`
 * receives the number of glyphs (charmap bytes), never counting a terminator. */
const uint8_t *mon_name(const Mon *m, int *len);

/* Teach a move in a slot and fill that slot's PP from the move table. */
void mon_set_move(Mon *m, int slot, int move);

/* A Pokémon Center visit: full HP and no status. PP is not restored, as in the
 * original (that is what Ethers are for). */
void mon_heal(Mon *m);

#endif
