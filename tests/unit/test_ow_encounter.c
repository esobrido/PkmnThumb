/* ow_encounter: the per-step grass roll and the game's own tables. */
#include "ow/encounter.h"

#include "gen/pokemon.h"
#include "gen/world.h"
#include "ow/clock.h"
#include "test_util.h"

/* A table where every slot is a different species and level, so a hit says
 * which slot it came from: phase p, slot i -> species 1 + 10p + i, level 2 + i. */
static PcWildTable make_table(int rate) {
    PcWildTable t;
    t.map = 0;
    t.rate = (uint8_t)rate;
    t.name = "test";
    for (int p = 0; p < 3; p++)
        for (int i = 0; i < PC_WILD_SLOTS; i++) {
            t.slot[p][i].species = (uint16_t)(1 + 10 * p + i);
            t.slot[p][i].level = (uint8_t)(2 + i);
        }
    return t;
}

/* Roll one step and say which slot it hit, or -1. */
static int hit_slot(OwEncounter *e, const PcWildTable *t, int tod) {
    OwEncounterOut out;
    if (!ow_encounter_step(e, t, tod, 1, &out)) return -1;
    /* Recover the slot from the species the table gave (1 + 10p + i). */
    return (int)((out.species - 1u) % 10u);
}

static void test_no_roll(void) {
    OwEncounter e;
    PcWildTable t = make_table(255);

    /* Not on grass: never, whatever the rate. */
    ow_encounter_init(&e, 1);
    for (int i = 0; i < 500; i++) {
        OwEncounterOut out;
        CHECK_EQ(ow_encounter_step(&e, &t, OW_TOD_DAY, 0, &out), 0);
    }
    /* No table: never. */
    CHECK_EQ(ow_encounter_step(&e, 0, OW_TOD_DAY, 1, 0), 0);
    /* Rate 0: never. */
    PcWildTable zero = make_table(0);
    for (int i = 0; i < 500; i++) {
        OwEncounterOut out;
        CHECK_EQ(ow_encounter_step(&e, &zero, OW_TOD_DAY, 1, &out), 0);
    }
}

static void test_rate(void) {
    /* Rate 255 draws from 0..255, so a step misses only on 255: practically
     * every step battles. */
    OwEncounter e;
    PcWildTable all = make_table(255);
    ow_encounter_init(&e, 12345);
    int hits = 0;
    for (int i = 0; i < 50; i++) hits += hit_slot(&e, &all, OW_TOD_DAY) >= 0;
    CHECK(hits >= 45);

    /* Route 29's 10 is compared against 0..255, not 0..99: ~3.9% a step. */
    PcWildTable ten = make_table(10);
    ow_encounter_init(&e, 0x51ee2d01u);
    hits = 0;
    const int steps = 20000;
    for (int i = 0; i < steps; i++) hits += hit_slot(&e, &ten, OW_TOD_DAY) >= 0;
    CHECK(hits > steps * 30 / 1000);      /* 3.0% */
    CHECK(hits < steps * 50 / 1000);      /* 5.0% */
}

static void test_slots(void) {
    /* GrassMonProbTable: 30, 30, 20, 10, 5, 3, 2 per cent. */
    static const int expect[PC_WILD_SLOTS] = { 30, 30, 20, 10, 5, 3, 2 };
    OwEncounter e;
    PcWildTable t = make_table(255);
    ow_encounter_init(&e, 0xabcdef01u);
    int count[PC_WILD_SLOTS] = { 0 };
    const int steps = 40000;
    int hits = 0;
    for (int i = 0; i < steps; i++) {
        int s = hit_slot(&e, &t, OW_TOD_DAY);
        if (s < 0) continue;
        count[s]++;
        hits++;
    }
    CHECK(hits > steps * 9 / 10);
    for (int i = 0; i < PC_WILD_SLOTS; i++) {
        int want = hits * expect[i] / 100;
        int slack = hits / 50 + 5;        /* ~2% absolute */
        CHECK(count[i] >= want - slack && count[i] <= want + slack);
    }
}

static void test_time_of_day(void) {
    OwEncounter e;
    PcWildTable t = make_table(255);
    OwEncounterOut out;

    ow_encounter_init(&e, 7);
    CHECK_EQ(ow_encounter_step(&e, &t, OW_TOD_MORN, 1, &out), 1);
    CHECK_EQ((out.species - 1) / 10, 0);   /* phase 0 */

    ow_encounter_init(&e, 7);
    CHECK_EQ(ow_encounter_step(&e, &t, OW_TOD_DAY, 1, &out), 1);
    CHECK_EQ((out.species - 1) / 10, 1);   /* phase 1 */

    /* Evening reuses the night table (GetTimeOfDayNotEve). */
    ow_encounter_init(&e, 7);
    CHECK_EQ(ow_encounter_step(&e, &t, OW_TOD_EVE, 1, &out), 1);
    CHECK_EQ((out.species - 1) / 10, 2);   /* phase 2 */

    /* Level comes from the same slot. */
    CHECK(out.level >= 2 && out.level <= 2 + PC_WILD_SLOTS - 1);
}

static void test_cooldown(void) {
    OwEncounter e;
    PcWildTable t = make_table(255);
    OwEncounterOut out;
    ow_encounter_init(&e, 99);
    ow_encounter_cooldown(&e);

    /* Four steps are skipped; the fifth rolls. */
    for (int i = 0; i < 4; i++)
        CHECK_EQ(ow_encounter_step(&e, &t, OW_TOD_DAY, 1, &out), 0);
    CHECK_EQ(ow_encounter_step(&e, &t, OW_TOD_DAY, 1, &out), 1);
    /* And the cooldown is gone: the next step rolls too. */
    CHECK_EQ(ow_encounter_step(&e, &t, OW_TOD_DAY, 1, &out), 1);
}

static void test_tables(void) {
    /* Only the maps with a grass table have one, and it is the original's. */
    CHECK(PC_WILD_COUNT >= 1);
    const PcWildTable *r = ow_encounter_table(PC_MAP_ROUTE29);
    CHECK(r != 0);
    CHECK_EQ(r->rate, 10);
    CHECK_EQ(ow_encounter_table(PC_MAP_NEWBARKTOWN), 0);
    CHECK_EQ(ow_encounter_table(-1), 0);
    CHECK_EQ(ow_encounter_table(PC_MAP_COUNT), 0);

    /* Route 29, data/wild/johto_grass.asm: same seven per phase. */
    CHECK_EQ(r->slot[OW_TOD_MORN][0].species, PC_SPECIES_PIDGEY);
    CHECK_EQ(r->slot[OW_TOD_MORN][0].level, 2);
    CHECK_EQ(r->slot[OW_TOD_MORN][1].species, PC_SPECIES_SENTRET);
    CHECK_EQ(r->slot[OW_TOD_MORN][4].species, PC_SPECIES_RATTATA);
    CHECK_EQ(r->slot[OW_TOD_MORN][5].species, PC_SPECIES_HOPPIP);
    CHECK_EQ(r->slot[OW_TOD_DAY][2].level, 3);
    CHECK_EQ(r->slot[OW_TOD_NITE][0].species, PC_SPECIES_HOOTHOOT);
    CHECK_EQ(r->slot[OW_TOD_NITE][1].species, PC_SPECIES_RATTATA);
    CHECK_EQ(r->slot[OW_TOD_NITE][2].level, 3);
}

int main(void) {
    test_no_roll();
    test_rate();
    test_slots();
    test_time_of_day();
    test_cooldown();
    test_tables();
    return test_report("test_ow_encounter");
}
