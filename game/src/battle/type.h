/*
 * battle/type.h — how much a move's type hurts a defender, in Q4.
 *
 * The original's `data/types/type_matchups.asm` is not a matrix: it lists only
 * the pairs that are not neutral, and `_CheckTypeMatchup` walks the list adding
 * doubles and halvings to a running $10. The port does the same over the same
 * rows (`pc_type_matchups` in `gen/pokemon.h`), so a dual-type defender really
 * is x4 or x0.25 rather than a float product, and an immunity stops the walk.
 *
 * Pure: a `Mon` in, a multiplier out, no Mote, no globals, no allocation.
 */
#ifndef BATTLE_TYPE_H
#define BATTLE_TYPE_H

#include "mon/mon.h"

/* Q4: 16 is neutral, 32 double, 8 half, 0 immune. */
#define TYPE_NEUTRAL 16

/* The multiplier of one attacking type against one defending type, as the
 * table lists it. Ground vs Flying is not in the table (the original asks the
 * airborne state instead), so that pair reads as neutral here. */
int type_pair(int attack_type, int defend_type);

/* The multiplier against both of the mon's types, combined the way the
 * original combines them: each match doubles or halves the running value, and
 * a 0 ends the walk. Ground against a Flying defender is 0 here, which is the
 * one immunity the table does not carry. */
int type_effect(const Mon *defender, int attack_type);

#endif
