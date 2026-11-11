/* ow/encounter.c — one grass roll per step, over the game's own tables. */
#include "ow/encounter.h"

#include "ow/clock.h"

/* GrassMonProbTable (data/wild/probabilities.asm): cumulative percents. */
static const uint8_t grass_prob[PC_WILD_SLOTS] = { 30, 60, 80, 90, 95, 98, 100 };

static uint32_t rng_next(OwEncounter *e) {
    uint32_t x = e->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    e->rng = x;
    return x;
}

void ow_encounter_init(OwEncounter *e, uint32_t seed) {
    e->rng = seed ? seed : 0x2545f491u;
    e->cooldown = 0;
}

void ow_encounter_cooldown(OwEncounter *e) {
    e->cooldown = OW_ENCOUNTER_COOLDOWN;
}

const PcWildTable *ow_encounter_table(int map) {
    if (map < 0) return 0;
    for (int i = 0; i < PC_WILD_COUNT; i++)
        if (pc_wild[i].map == map) return &pc_wild[i];
    return 0;
}

int ow_encounter_step(OwEncounter *e, const PcWildTable *t, int tod, int on_grass,
                      OwEncounterOut *out) {
    if (!t || !on_grass || t->rate == 0) return 0;

    /* CheckWildEncounterCooldown: it counts down and the step that reaches
     * zero is allowed to roll, so a cooldown of 5 skips four steps. */
    if (e->cooldown) {
        e->cooldown--;
        if (e->cooldown) return 0;
    }

    /* .EncounterRate: `call Random` is 0..255, `cp b` carries when below the
     * rate byte. Comparing the low byte of the xorshift gives the same odds. */
    if ((rng_next(e) & 0xffu) >= t->rate) return 0;

    int roll = (int)(rng_next(e) % 100u);
    int slot = 0;
    while (slot < PC_WILD_SLOTS - 1 && roll >= grass_prob[slot]) slot++;

    /* GetTimeOfDayNotEve: evening is night's table. */
    if (tod == OW_TOD_EVE) tod = OW_TOD_NITE;
    if (tod < OW_TOD_MORN || tod > OW_TOD_NITE) tod = OW_TOD_DAY;

    out->species = t->slot[tod][slot].species;
    out->level = t->slot[tod][slot].level;
    out->seed = rng_next(e);
    return 1;
}
