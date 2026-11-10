/* test_script_scene — a map's scene byte, and the cells that fire on it.
 *
 * Two halves, and both are behaviour the player can see. The scene byte is what
 * `setmapscene ROUTE_29, SCENE_ROUTE29_CATCH_TUTORIAL` writes from Elm's Lab, so
 * that walking onto Route 29's tutorial cells later starts the catch tutorial;
 * the coord events are those cells. The tables below are the extracted ones, so
 * a change here means the extractor found or lost a scene or a trigger.
 */
#include <stdio.h>
#include <string.h>

#include "gen/scripts.h"
#include "script/scene.h"
#include "script/vm.h"

static int failures;

#define CHECK(cond, msg)                                                    \
    do {                                                                    \
        if (!(cond)) {                                                      \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, msg);            \
            failures++;                                                     \
        }                                                                   \
    } while (0)

/* The eight maps of the build, in pc_script_maps order. */
static const int scenes_expected[PC_SCRIPT_MAP_COUNT] = { 3, 2, 3, 8, 2, 0, 0, 0 };
static const int coords_expected[PC_SCRIPT_MAP_COUNT] = { 7, 2, 3, 7, 4, 0, 0, 0 };

static int8_t bytes[PC_SCRIPT_MAP_COUNT];
static ScrScenes st;

static void use(int slot) {
    st.bytes = bytes;
    st.slot = (uint8_t)slot;
    st.scene_count = (uint8_t)(slot >= 0 ? pc_script_maps[slot].scene_count : 0);
    st.maps = pc_scene_maps;
    st.map_count = PC_SCENE_MAP_COUNT;
}

/* Runs a whole script and returns its condition register afterwards. */
static uint8_t run(int slot, const uint8_t *code, int len) {
    ScrVm vm;
    ScrOut out;
    use(slot);
    scr_vm_start(&vm, code, (uint16_t)len, 0, 0, 0);
    scr_vm_set_scene(&vm, &st);
    for (int guard = 0; guard < 64; guard++) {
        ScrRun r = scr_vm_next(&vm, &out);
        if (r != SCR_RUN_EVENT) break;
    }
    return vm.cond;
}

static int slot_of(const char *name) {
    for (int i = 0; i < PC_SCRIPT_MAP_COUNT; i++)
        if (strcmp(pc_script_maps[i].map, name) == 0) return i;
    return -1;
}

static void test_table(void) {
    for (int m = 0; m < PC_SCRIPT_MAP_COUNT; m++) {
        const PcScriptMap *sm = &pc_script_maps[m];
        CHECK(sm->scene_count == scenes_expected[m],
              "the map's scene table size is the measured one");
        CHECK(sm->coord_count == coords_expected[m],
              "the map's coord_event count is the measured one");
        for (int i = 0; i < sm->coord_count; i++) {
            const PcCoordEvent *e = &sm->coords[i];
            CHECK(e->x < 64 && e->y < 64, "the trigger's cell is inside a map");
            CHECK(e->scene >= 0 && e->scene < sm->scene_count,
                  "its scene is one of the map's own scenes");
            CHECK(e->script != 0xffff && e->script < sm->len,
                  "its script is a region of the map's blob");
        }
    }
    /* Only the maps with a scene table are in the map-id table, exactly as
     * MapScenes lists them. */
    CHECK(PC_SCENE_MAP_COUNT == 5, "five of the eight maps have scenes");
    use(0);
    for (int i = 0; i < PC_SCENE_MAP_COUNT; i++) {
        const PcSceneMap *s = &pc_scene_maps[i];
        CHECK(s->slot >= 0 && s->slot < PC_SCRIPT_MAP_COUNT, "slot is in range");
        CHECK(pc_script_maps[s->slot].scene_count > 0,
              "a listed map really has a scene table");
        CHECK(pc_scene_slot(&st, s->group, s->number) == s->slot,
              "the map id finds its own slot");
    }
    /* A map id nobody has. */
    CHECK(pc_scene_slot(&st, 200, 200) == -1, "an unknown map id has no slot");
    CHECK(pc_scene_slot(0, 200, 200) == -1, "and no state at all is no slot");
}

static void test_state(void) {
    memset(bytes, 0, sizeof bytes);
    int nb = slot_of("NewBarkTown");
    int house = slot_of("PlayersNeighborsHouse");
    CHECK(nb >= 0 && house >= 0, "the two test maps are in the build");

    /* A new game is in scene 0 everywhere, which is a real scene. */
    use(nb);
    CHECK(pc_scene_active(&st) == 0, "a fresh save is in the first scene");
    pc_scene_set(&st, 2);
    CHECK(pc_scene_active(&st) == 2, "setscene stores the id");
    CHECK(bytes[nb] == 2, "in the caller's byte, not a copy");

    /* A map with no scene table has no scene and no state to write. */
    use(house);
    CHECK(pc_scene_active(&st) == PC_SCENE_NONE, "a map without scenes has no scene");
    pc_scene_set(&st, 3);
    CHECK(bytes[house] == 0, "and setscene there writes nothing");

    /* Out of range and NULL are not crashes. */
    CHECK(pc_scene_active(0) == PC_SCENE_NONE, "no state at all, no scene");
    pc_scene_set(0, 1);
    st.bytes = 0;
    CHECK(pc_scene_active(&st) == PC_SCENE_NONE, "no scene bytes, no scene");
    pc_scene_set(&st, 1);                    /* must not write anywhere */
    CHECK(pc_scene_at(&st, 0) == PC_SCENE_NONE, "and reading a slot is -1 too");
    use(nb);
    CHECK(pc_scene_at(&st, nb) == 2, "a real slot reads the byte");
    CHECK(pc_scene_at(&st, -1) == PC_SCENE_NONE, "a negative slot reads -1");
    CHECK(pc_scene_at(&st, 99) == PC_SCENE_NONE, "a slot past the end reads -1");
}

/* The real triggers: the teacher's cells and Lyra's, on New Bark Town. */
static void test_new_bark_coords(void) {
    int nb = slot_of("NewBarkTown");
    const PcScriptMap *sm = &pc_script_maps[nb];
    uint16_t at = 0;

    /* Scene 0 (TEACHER_STOPS_YOU) stops you on the west pavement, twice. */
    CHECK(pc_coord_event(sm, 1, 8, 0, &at) && at == 7,
          "the teacher's first cell fires in her scene");
    CHECK(pc_coord_event(sm, 1, 9, 0, &at) && at == 39,
          "and the second one");
    CHECK(pc_coord_event(sm, 6, 4, 0, &at) && at == 74,
          "so does Lyra's intro cell");
    /* In her final scene those three are dead, and Lyra's four are live. */
    CHECK(!pc_coord_event(sm, 1, 8, 1, &at), "the teacher's cell is over");
    CHECK(!pc_coord_event(sm, 6, 4, 1, &at), "and so is Lyra's intro");
    CHECK(pc_coord_event(sm, 19, 6, 1, &at) && at == 131,
          "Lyra's goodbye fires in its own scene");
    CHECK(!pc_coord_event(sm, 19, 6, 0, &at), "and not before it");
    CHECK(!pc_coord_event(sm, 19, 6, 2, &at), "nor in the noop scene");

    /* A cell with nothing, and a map with no triggers at all. */
    CHECK(!pc_coord_event(sm, 1, 7, 0, &at), "the cell above the first one is empty");
    CHECK(!pc_coord_event(sm, 250, 250, 0, &at), "so is the far corner");
    int house = slot_of("PlayersNeighborsHouse");
    CHECK(!pc_coord_event(&pc_script_maps[house], 0, 0, 0, &at),
          "a map with no coord events fires nothing");
    CHECK(!pc_coord_event(0, 0, 0, 0, &at), "and no map fires nothing");
    CHECK(!pc_coord_event(sm, 0, 0, 0, 0), "no place to write the script is fine");
}

/* Route 29's tutorial needs Elm's Lab to have set its scene: the whole reason
 * setmapscene carries a map id. */
static void test_tutorial_scene(void) {
    int r29 = slot_of("Route29");
    int lab = slot_of("ElmsLab");
    const PcScriptMap *sm = &pc_script_maps[r29];
    uint16_t at = 0;
    memset(bytes, 0, sizeof bytes);
    use(r29);
    CHECK(pc_scene_active(&st) == 0, "Route 29 starts in its noop scene");
    CHECK(!pc_coord_event(sm, 53, 8, pc_scene_active(&st), &at),
          "so the tutorial cell is silent");

    /* `setmapscene ROUTE_29, SCENE_ROUTE29_CATCH_TUTORIAL` from another map. */
    uint8_t code[] = { PCS_SETMAPSCENE, 24, 1, 1, PCS_END };
    run(lab, code, (int)sizeof code);
    CHECK(bytes[r29] == 1, "the lab moved Route 29's scene");
    use(r29);
    CHECK(pc_coord_event(sm, 53, 8, pc_scene_active(&st), &at),
          "and now the tutorial cell fires");

    /* An unknown map id in the script is a no-op, not a stray write. */
    memset(bytes, 0, sizeof bytes);
    uint8_t bad[] = { PCS_SETMAPSCENE, 200, 200, 1, PCS_END };
    run(lab, bad, (int)sizeof bad);
    for (int i = 0; i < PC_SCRIPT_MAP_COUNT; i++)
        CHECK(bytes[i] == 0, "a map outside the build is left alone");
}

static void test_vm_scene_commands(void) {
    int nb = slot_of("NewBarkTown");
    int house = slot_of("PlayersNeighborsHouse");
    memset(bytes, 0, sizeof bytes);

    /* setscene then checkscene, in one script: the write is visible at once. */
    uint8_t set2[] = { PCS_SETSCENE, 2, PCS_CHECKSCENE, PCS_END };
    CHECK(run(nb, set2, (int)sizeof set2) == 2, "checkscene reads what setscene wrote");
    CHECK(bytes[nb] == 2, "and the byte is the caller's");

    /* Scene 0 is "no triggers" to checkscene, as in Script_checkscene. */
    uint8_t check[] = { PCS_CHECKSCENE, PCS_END };
    memset(bytes, 0, sizeof bytes);
    CHECK(run(nb, check, (int)sizeof check) == 0xff, "scene 0 reads as no scene");
    memset(bytes, 0, sizeof bytes);
    CHECK(run(house, check, (int)sizeof check) == 0xff,
          "and a map without scenes always reads as none");

    /* setscene on a map without a scene table does nothing and does not die. */
    uint8_t set3[] = { PCS_SETSCENE, 3, PCS_END };
    memset(bytes, 0, sizeof bytes);
    run(house, set3, (int)sizeof set3);
    CHECK(bytes[house] == 0, "no scene table, no write");

    /* checkmapscene has no zero special case: it returns the byte as-is. */
    int r29 = slot_of("Route29");
    uint8_t check29[] = { PCS_CHECKMAPSCENE, 24, 1, PCS_END };
    memset(bytes, 0, sizeof bytes);
    CHECK(run(nb, check29, (int)sizeof check29) == 0, "a 0 scene is still 0 here");
    memset(bytes, 0, sizeof bytes);
    bytes[r29] = 1;
    CHECK(run(nb, check29, (int)sizeof check29) == 1, "and another map's scene reads");
    uint8_t checkbad[] = { PCS_CHECKMAPSCENE, 200, 200, PCS_END };
    CHECK(run(nb, checkbad, (int)sizeof checkbad) == 0xff,
          "an unknown map reads as no scene");

    /* The VM with no scene state at all: the tests' scripts never touch scenes,
     * so this has to be harmless. */
    ScrVm vm;
    ScrOut out;
    uint8_t plain[] = { PCS_END };
    scr_vm_start(&vm, plain, (uint16_t)sizeof plain, 0, 0, 0);
    CHECK(scr_vm_next(&vm, &out) == SCR_RUN_END, "a VM without scenes still runs");
}

int main(void) {
    test_table();
    test_state();
    test_new_bark_coords();
    test_tutorial_scene();
    test_vm_scene_commands();
    if (failures) {
        printf("script/scene: %d failure(s)\n", failures);
        return 1;
    }
    printf("script/scene: ok\n");
    return 0;
}
