/* test_script_trigger — what a bg_event cell answers, and from where.
 *
 * The lookup is the second half of A in front of something (the first is an
 * object_event, which is game.c's business). These cases pin the extracted
 * table: every cell that has a script points inside its map's blob, a
 * BGEVENT_JUMPTEXT cell really is `jumptext <text>`, and a directional cell
 * only answers from the side it names.
 */
#include <stdio.h>

#include "gen/scripts.h"
#include "ow/dir.h"
#include "script/trigger.h"

static int failures;

#define CHECK(cond, msg)                                                    \
    do {                                                                    \
        if (!(cond)) {                                                      \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, msg);            \
            failures++;                                                     \
        }                                                                   \
    } while (0)

/* One row per map: cells, cells with a script. Measured from the eight maps
 * of the build; a change here means the extractor found or lost a cell. */
static const int cells_expected[PC_SCRIPT_MAP_COUNT] = { 5, 3, 4, 16, 4, 1, 4, 3 };
static const int scripts_expected[PC_SCRIPT_MAP_COUNT] = { 4, 3, 3, 8, 4, 0, 4, 2 };

static void test_table(void) {
    for (int m = 0; m < PC_SCRIPT_MAP_COUNT; m++) {
        const PcScriptMap *sm = &pc_script_maps[m];
        CHECK(sm->bg_count == cells_expected[m], "the map's bg cell count is the measured one");
        if (sm->bg_count != cells_expected[m]) {
            continue;
        }
        int with = 0;
        for (int i = 0; i < sm->bg_count; i++) {
            const PcBgEvent *e = &sm->bg[i];
            CHECK(e->x < 64 && e->y < 64, "cell is inside a plausible map");
            if (e->script == 0xffff) {
                /* Either a kind the port cannot run yet (std scripts need the
                 * std script table, and hidden items carry an item id), or a
                 * READ whose script lives in another file (the radio's
                 * InitialRadio), which stays $ffff and is reported. */
                CHECK(e->kind == PC_BGEVENT_READ || e->kind >= PC_BGEVENT_JUMPSTD,
                      "a cell without a script is unported or lives elsewhere");
                continue;
            }
            with++;
            CHECK(e->script < sm->len, "script offset is inside the map's blob");
            if (e->kind == PC_BGEVENT_JUMPTEXT) {
                CHECK(sm->bytes[e->script] == PCS_JUMPTEXT,
                      "a jump-text cell starts with jumptext");
                int t = sm->bytes[e->script + 1] | (sm->bytes[e->script + 2] << 8);
                CHECK(t < PCS_TEXT_INDEX_COUNT, "and points at a text inside pc_texts");
            }
        }
        CHECK(with == scripts_expected[m], "cell count with a script is the measured one");
    }
}

static void test_lookup(void) {
    /* Every READ and JUMPTEXT cell answers from any side it is approached
     * from; a directional one answers only from its own. */
    for (int m = 0; m < PC_SCRIPT_MAP_COUNT; m++) {
        const PcScriptMap *sm = &pc_script_maps[m];
        for (int i = 0; i < sm->bg_count; i++) {
            const PcBgEvent *e = &sm->bg[i];
            if (e->script == 0xffff) {
                continue;
            }
            if (e->kind == PC_BGEVENT_READ || e->kind == PC_BGEVENT_JUMPTEXT) {
                for (int d = 0; d < 4; d++) {
                    CHECK(pc_bg_event_script(sm, e->x, e->y, d) == e->script,
                          "a sign answers from every side");
                }
                continue;
            }
            int own = e->kind == PC_BGEVENT_UP ? OW_DIR_UP
                : e->kind == PC_BGEVENT_DOWN ? OW_DIR_DOWN
                : e->kind == PC_BGEVENT_RIGHT ? OW_DIR_RIGHT : OW_DIR_LEFT;
            CHECK(pc_bg_event_script(sm, e->x, e->y, own) == e->script,
                  "a directional cell answers from its own side");
            CHECK(pc_bg_event_script(sm, e->x, e->y, own ^ 1) == 0xffff,
                  "and not from the opposite side");
        }
    }

    /* Nothing where there is nothing: an empty cell, and no map at all. */
    CHECK(pc_bg_event_script(&pc_script_maps[0], 250, 250, OW_DIR_DOWN) == 0xffff,
          "an empty cell answers nothing");
    CHECK(pc_bg_event_script(NULL, 0, 0, OW_DIR_DOWN) == 0xffff,
          "no map answers nothing");
}

int main(void) {
    test_table();
    test_lookup();
    if (failures) {
        printf("script/trigger: %d failure(s)\n", failures);
        return 1;
    }
    printf("script/trigger: ok\n");
    return 0;
}
