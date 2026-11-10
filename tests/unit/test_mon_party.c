/* mon_party: six slots, in the player's order. */
#include "mon/party.h"
#include "test_util.h"

static Mon make(int species, int level) {
    Mon m;
    mon_init(&m, species, level, 0);
    return m;
}

/* An empty team, and a mon added lands in slot 0. */
static void test_init_add(void) {
    Party p;
    party_init(&p);
    CHECK_EQ(p.count, 0);
    CHECK_EQ(party_alive(&p), 0);
    CHECK_EQ(party_first_alive(&p), -1);

    Mon b = make(PC_SPECIES_BULBASAUR, 5);
    CHECK_EQ(party_add(&p, &b), 0);
    CHECK_EQ(p.count, 1);
    CHECK_EQ(p.mon[0].species, PC_SPECIES_BULBASAUR);
    CHECK_EQ(p.mon[0].stat[MON_HP], b.stat[MON_HP]);
}

/* Order is insertion order, and the seventh mon does not fit. */
static void test_full(void) {
    Party p;
    party_init(&p);
    for (int i = 0; i < PARTY_MAX; i++) {
        Mon m = make(PC_SPECIES_BULBASAUR + i, 5 + i);
        CHECK_EQ(party_add(&p, &m), i);
    }
    CHECK_EQ(p.count, PARTY_MAX);
    CHECK_EQ(p.mon[3].level, 8);

    Mon extra = make(PC_SPECIES_MEWTWO, 70);
    CHECK_EQ(party_add(&p, &extra), -1);
    CHECK_EQ(p.count, PARTY_MAX);
}

/* Removing shifts the rest down: no holes, no reordering. */
static void test_remove(void) {
    Party p;
    party_init(&p);
    for (int i = 0; i < 4; i++) {
        Mon m = make(PC_SPECIES_BULBASAUR + i, 5);
        party_add(&p, &m);
    }
    CHECK_EQ(party_remove(&p, 1), 1);
    CHECK_EQ(p.count, 3);
    CHECK_EQ(p.mon[0].species, PC_SPECIES_BULBASAUR);
    CHECK_EQ(p.mon[1].species, PC_SPECIES_BULBASAUR + 2);
    CHECK_EQ(p.mon[2].species, PC_SPECIES_BULBASAUR + 3);
    CHECK_EQ(p.mon[3].species, 0);

    /* Removing the last one, then an index that holds nobody. */
    CHECK_EQ(party_remove(&p, 2), 1);
    CHECK_EQ(p.count, 2);
    CHECK_EQ(party_remove(&p, 2), 0);
    CHECK_EQ(party_remove(&p, -1), 0);
    CHECK_EQ(p.count, 2);
}

/* Fainted members are still on the team; only the live ones count. */
static void test_fainted(void) {
    Party p;
    party_init(&p);
    for (int i = 0; i < 3; i++) {
        Mon m = make(PC_SPECIES_BULBASAUR, 5);
        party_add(&p, &m);
    }
    p.mon[0].hp = 0;
    CHECK_EQ(p.count, 3);
    CHECK_EQ(party_alive(&p), 2);
    CHECK_EQ(party_first_alive(&p), 1);

    p.mon[1].hp = 0;
    p.mon[2].hp = 0;
    CHECK_EQ(party_alive(&p), 0);
    CHECK_EQ(party_first_alive(&p), -1);
}

/* A Pokémon Center heals the whole team and nobody else. */
static void test_heal(void) {
    Party p;
    party_init(&p);
    for (int i = 0; i < 3; i++) {
        Mon m = make(PC_SPECIES_BULBASAUR, 10);
        party_add(&p, &m);
    }
    for (int i = 0; i < 3; i++) {
        p.mon[i].hp = 1;
        p.mon[i].status = 8;
    }
    party_heal(&p);
    CHECK_EQ(party_alive(&p), 3);
    for (int i = 0; i < 3; i++) {
        CHECK_EQ(p.mon[i].hp, p.mon[i].stat[MON_HP]);
        CHECK_EQ(p.mon[i].status, 0);
    }
}

/* Adding copies the mon: later changes to the caller's copy leave the team
 * alone, which is what an "add from a trade or a catch" needs. */
static void test_copy(void) {
    Party p;
    party_init(&p);
    Mon b = make(PC_SPECIES_BULBASAUR, 5);
    party_add(&p, &b);
    b.level = 9;
    b.hp = 1;
    CHECK_EQ(p.mon[0].level, 5);
    CHECK(p.mon[0].hp > 1);
}

int main(void) {
    test_init_add();
    test_full();
    test_remove();
    test_fainted();
    test_heal();
    test_copy();
    return test_report("test_mon_party");
}
