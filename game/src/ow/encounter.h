/*
 * ow/encounter.h — grass encounters: the per-step roll before a wild battle.
 *
 * This is the part of engine/overworld/wildmons.asm that decides whether a
 * step onto a grass cell starts a battle, and which mon. It is pure: the step
 * says whether the cell is grass and which phase of the day it is, and the
 * caller owns the state and the two mons.
 *
 * The odds are not what the table's comment suggests. The rate byte (10 for
 * Route 29) is compared against a 0..255 draw, so the chance is rate/256, not
 * rate/100. The slot is chosen with a 0..99 draw against the cumulative
 * GrassMonProbTable (30, 60, 80, 90, 95, 98, 100).
 *
 * After a battle the original skips a few steps (SetUpFiveStepWildEncounter-
 * Cooldown): the fifth step is the first that rolls again.
 */
#ifndef OW_ENCOUNTER_H
#define OW_ENCOUNTER_H

#include <stdint.h>

#include "gen/wild.h"

/* The original's five-step wait after a battle. */
#define OW_ENCOUNTER_COOLDOWN 5

typedef struct {
    uint32_t rng;
    uint8_t cooldown;      /* steps still to skip; 0 = rolling */
} OwEncounter;

/* What a step turned into: the mon to make and a seed for the battle. */
typedef struct {
    uint16_t species;
    uint8_t level;
    uint32_t seed;
} OwEncounterOut;

/* Seed the RNG; 0 is replaced (an xorshift with a zero state never leaves it). */
void ow_encounter_init(OwEncounter *e, uint32_t seed);

/* Start the five-step wait, as closing a battle does. */
void ow_encounter_cooldown(OwEncounter *e);

/* The grass table a map has, or NULL (most maps have none). */
const PcWildTable *ow_encounter_table(int map);

/* One step that has just ended. `on_grass` is the cell's collision code
 * (ow_cell_grass), `tod` an OW_TOD_* phase. Returns 1 and fills `out` when a
 * battle starts, 0 otherwise. Evening uses the night table, as
 * GetTimeOfDayNotEve does in the original. */
int ow_encounter_step(OwEncounter *e, const PcWildTable *t, int tod, int on_grass,
                      OwEncounterOut *out);

#endif
