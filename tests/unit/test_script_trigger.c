/* test_script_trigger — what a bg_event cell answers, and from where.
 *
 * The lookup is the second half of A in front of something (the first is an
 * object_event, which is game.c's business). These cases pin the extracted
 * table: every cell that has a script points inside its map's blob, a
 * BGEVENT_JUMPTEXT cell really is `jumptext <text>`, a bookshelf starts with an
 * inlined std script, a hidden item carries its item id and its flag, and a
 * directional cell only answers from the side it names.
 */
#include <stdio.h>
#include <string.h>

#include "gen/scripts.h"
#include "ow/dir.h"
#include "script/trigger.h"
#include "script/vm.h"

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
/* Every cell but one now has a script: the radio in the neighbours' house is a
 * READ whose script (InitialRadio) lives in another file, so it stays $ffff and
 * the extractor says so. */
static const int scripts_expected[PC_SCRIPT_MAP_COUNT] = { 5, 3, 4, 16, 4, 0, 4, 3 };

static uint8_t flags[PC_SCRIPT_FLAG_BYTES];

static const PcBgEvent *cell_at(int m, int x, int y) {
    const PcScriptMap *sm = &pc_script_maps[m];
    for (int i = 0; i < sm->bg_count; i++)
        if (sm->bg[i].x == x && sm->bg[i].y == y) return &sm->bg[i];
    return 0;
}

static void test_table(void) {
    for (int m = 0; m < PC_SCRIPT_MAP_COUNT; m++) {
        const PcScriptMap *sm = &pc_script_maps[m];
        CHECK(sm->bg_count == cells_expected[m],
              "the map's bg cell count is the measured one");
        if (sm->bg_count != cells_expected[m]) {
            continue;
        }
        int with = 0;
        for (int i = 0; i < sm->bg_count; i++) {
            const PcBgEvent *e = &sm->bg[i];
            CHECK(e->x < 64 && e->y < 64, "cell is inside a plausible map");
            if (e->script == 0xffff) {
                CHECK(e->kind == PC_BGEVENT_READ,
                      "the only cell without a script is a READ that lives elsewhere");
                continue;
            }
            with++;
            CHECK(e->script < sm->len, "script offset is inside the map's blob");
            if (e->kind == PC_BGEVENT_JUMPTEXT) {
                CHECK(sm->bytes[e->script] == PCS_JUMPTEXT,
                      "a jump-text cell starts with jumptext");
                int t = sm->bytes[e->script + 1] | (sm->bytes[e->script + 2] << 8);
                CHECK(t < PCS_TEXT_INDEX_COUNT, "and points at a text inside pc_texts");
                CHECK(e->flag == 0xffff, "a sign has no flag");
            } else if (e->kind == PC_BGEVENT_JUMPSTD) {
                /* DifficultBookshelfScript is `farjumptext DifficultBookshelfText`:
                 * the port has no banks, so it is a plain jumptext here. */
                CHECK(sm->bytes[e->script] == PCS_JUMPTEXT,
                      "a bookshelf starts with the std script's jumptext");
                int t = sm->bytes[e->script + 1] | (sm->bytes[e->script + 2] << 8);
                CHECK(t < PCS_TEXT_INDEX_COUNT, "and points at its std text");
                CHECK(e->flag == 0xffff, "a bookshelf has no flag");
            } else if (e->kind >= PC_BGEVENT_ITEM) {
                /* The hidden item's script is `opentext` + `jumptext <found text>`. */
                CHECK(sm->bytes[e->script] == PCS_OPENTEXT,
                      "a hidden item opens the box");
                CHECK(sm->bytes[e->script + 1] == PCS_JUMPTEXT,
                      "and prints the found text");
                int t = sm->bytes[e->script + 2] | (sm->bytes[e->script + 3] << 8);
                CHECK(t < PCS_TEXT_INDEX_COUNT, "which is in pc_texts");
                CHECK(e->flag != 0xffff, "and it has the flag that marks it taken");
            }
        }
        CHECK(with == scripts_expected[m], "cell count with a script is the measured one");
    }
}

static void test_lookup(void) {
    PcTrigger t;
    for (int m = 0; m < PC_SCRIPT_MAP_COUNT; m++) {
        const PcScriptMap *sm = &pc_script_maps[m];
        for (int i = 0; i < sm->bg_count; i++) {
            const PcBgEvent *e = &sm->bg[i];
            if (e->script == 0xffff) {
                CHECK(!pc_bg_event(sm, e->x, e->y, OW_DIR_DOWN, 0, 0, &t),
                      "a cell without a script answers nothing");
                continue;
            }
            int any_side = e->kind == PC_BGEVENT_READ || e->kind == PC_BGEVENT_JUMPTEXT ||
                e->kind == PC_BGEVENT_JUMPSTD || e->kind >= PC_BGEVENT_ITEM;
            if (any_side) {
                for (int d = 0; d < 4; d++) {
                    CHECK(pc_bg_event(sm, e->x, e->y, d, 0, 0, &t) &&
                          t.script == e->script,
                          "the cell answers from every side");
                }
                continue;
            }
            int own = e->kind == PC_BGEVENT_UP ? OW_DIR_UP
                : e->kind == PC_BGEVENT_DOWN ? OW_DIR_DOWN
                : e->kind == PC_BGEVENT_RIGHT ? OW_DIR_RIGHT : OW_DIR_LEFT;
            CHECK(pc_bg_event(sm, e->x, e->y, own, 0, 0, &t) && t.script == e->script,
                  "a directional cell answers from its own side");
            CHECK(!pc_bg_event(sm, e->x, e->y, own ^ 1, 0, 0, &t),
                  "and not from the opposite side");
        }
    }

    /* Nothing where there is nothing: an empty cell, and no map at all. */
    CHECK(!pc_bg_event(&pc_script_maps[0], 250, 250, OW_DIR_DOWN, 0, 0, &t),
          "an empty cell answers nothing");
    CHECK(!pc_bg_event(0, 0, 0, OW_DIR_DOWN, 0, 0, &t),
          "no map answers nothing");
    CHECK(!pc_bg_event(&pc_script_maps[0], 8, 8, OW_DIR_DOWN, 0, 0, 0),
          "no place to write the answer is nothing");
}

/* The two hidden items of the build: New Bark Town's Potion and Cherrygrove
 * City's Nugget, with the ids of constants/item_constants.asm. */
static void test_hidden_items(void) {
    static const struct { int map, x, y, item; } items[] = {
        { 0, 3, 2, 25 },     /* POTION */
        { 2, 35, 2, 229 },   /* NUGGET */
    };
    for (unsigned i = 0; i < sizeof items / sizeof items[0]; i++) {
        const PcBgEvent *e = cell_at(items[i].map, items[i].x, items[i].y);
        const PcScriptMap *sm = &pc_script_maps[items[i].map];
        PcTrigger t;
        CHECK(e != 0, "the hidden item cell exists");
        if (!e) continue;
        CHECK(e->kind == PC_BGEVENT_ITEM + items[i].item,
              "the type byte is BGEVENT_ITEM plus the item id");

        memset(flags, 0, sizeof flags);
        CHECK(pc_bg_event(sm, e->x, e->y, OW_DIR_DOWN, flags, sizeof flags, &t),
              "an item not taken yet answers");
        CHECK(t.item == items[i].item, "and names the item to give");
        CHECK(t.flag == e->flag && t.flag != 0xffff, "and the flag that marks it taken");

        /* Taken: the cell is empty, exactly as BGEVENT_ITEM's flag test does. */
        scr_flag_set(flags, sizeof flags, e->flag, 1);
        CHECK(!pc_bg_event(sm, e->x, e->y, OW_DIR_DOWN, flags, sizeof flags, &t),
              "a taken item answers nothing");
        /* ...and only that one: the other hidden item is still there. */
        for (unsigned k = 0; k < sizeof items / sizeof items[0]; k++) {
            if (k == i) continue;
            const PcBgEvent *o = cell_at(items[k].map, items[k].x, items[k].y);
            if (!o) continue;
            CHECK(pc_bg_event(&pc_script_maps[items[k].map], o->x, o->y, OW_DIR_DOWN,
                              flags, sizeof flags, &t),
                  "the other map's hidden item is untouched");
        }
        /* Back to not taken: it answers again (a reset flag is a reset world). */
        scr_flag_set(flags, sizeof flags, e->flag, 0);
        CHECK(pc_bg_event(sm, e->x, e->y, OW_DIR_DOWN, flags, sizeof flags, &t),
              "clearing the flag makes it readable again");

        /* A sign in the same map has no flag and is not affected by any of it. */
        const PcBgEvent *sign = cell_at(0, 8, 8);
        CHECK(sign && sign->flag == 0xffff, "the New Bark Town sign has no flag");

        /* And a NULL flag array reads as "nothing taken". */
        CHECK(pc_bg_event(sm, e->x, e->y, OW_DIR_DOWN, 0, 0, &t),
              "no flag array means every item is still there");
    }
}

int main(void) {
    test_table();
    test_lookup();
    test_hidden_items();
    if (failures) {
        printf("script/trigger: %d failure(s)\n", failures);
        return 1;
    }
    printf("script/trigger: ok\n");
    return 0;
}
