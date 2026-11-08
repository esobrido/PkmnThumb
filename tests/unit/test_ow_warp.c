/* ow_warp: the door/carpet trigger rules and the fade-swap-fade transition. */
#include "ow/warp.h"
#include "ow/map.h"    /* OW_STEP_PX */
#include "test_util.h"

/* A tiny two-map world. Warp events are 1-based in the original (`warp_event`
 * numbers its rows from 1), which is why `warp` is stored that way. */
static const PcWarp events[3] = {
    /* x,  y, map, warp, kind,              dir */
    {  3,  4,   1,   1,   PC_WARP_DOOR,     OW_DIR_DOWN },
    {  6,  7,   0,   2,   PC_WARP_CARPET,   OW_DIR_DOWN },
    {  9,  9,  -1,   1,   PC_WARP_STEP,     OW_DIR_UP   },   /* shut: not built */
};
enum { N_EVENTS = 3 };

/* The landing test wants a collision answer for "one cell below the door"; the
 * map is irrelevant to the warp code, so a bitmap stands in for it. The callback
 * is asked in pixels, like ow_map's own test, hence the shift. */
static int blocked_cells[16 * 16];
static int blocked(int px, int py) {
    return blocked_cells[(py >> 4) * 16 + (px >> 4)];
}

/* Run a whole transition, reporting the sequence of fade levels and the frame
 * the map got swapped on. Both are what the renderer and the glue consume. */
static void run_transition(OwWarp *w, int *frames, int *peak, int *loads, int *load_at) {
    *frames = *peak = *loads = *load_at = 0;
    while (ow_warp_busy(w) && *frames < 100) {
        ow_warp_update(w, OW_DIR_NONE);
        (*frames)++;
        if (ow_warp_take_load(w)) { (*loads)++; *load_at = *frames; }
        if (ow_warp_fade(w) > *peak) *peak = ow_warp_fade(w);
    }
}

int main(void) {
    OwWarp w;
    ow_warp_init(&w);
    CHECK_EQ(w.phase, OW_WARP_IDLE);
    CHECK_EQ(ow_warp_busy(&w), 0);
    CHECK_EQ(w.armed, 1);
    CHECK_EQ(ow_warp_fade(&w), 0);

    /* ---- the trigger rule ------------------------------------------------ */
    /* standing on a door does nothing: arrival is not a step */
    CHECK_EQ(ow_warp_trigger(events, N_EVENTS, 3, 4, OW_DIR_DOWN, 0, 0), -1);
    /* a step that ends on the door fires it */
    CHECK_EQ(ow_warp_trigger(events, N_EVENTS, 3, 4, OW_DIR_DOWN, 1, 0), 0);
    /* a bump alone does not: CheckWarpTile is only reached once a step has
     * finished, so pushing against something while standing in a doorway
     * (an NPC in the way, say) leaves the door shut */
    CHECK_EQ(ow_warp_trigger(events, N_EVENTS, 3, 4, OW_DIR_DOWN, 0, 1), -1);
    /* ...but not from a cell with no event */
    CHECK_EQ(ow_warp_trigger(events, N_EVENTS, 4, 4, OW_DIR_DOWN, 1, 1), -1);
    CHECK_EQ(ow_warp_trigger(0, 0, 3, 4, OW_DIR_DOWN, 1, 1), -1);

    /* a carpet is the other way round: it never fires on arrival, only when the
     * held direction cannot start a step and the player faces the carpet's way */
    CHECK_EQ(ow_warp_trigger(events, N_EVENTS, 6, 7, OW_DIR_DOWN, 1, 0), -1);
    CHECK_EQ(ow_warp_trigger(events, N_EVENTS, 6, 7, OW_DIR_DOWN, 0, 1), 1);
    /* facing up on a down carpet is the classic mistake — it does nothing */
    CHECK_EQ(ow_warp_trigger(events, N_EVENTS, 6, 7, OW_DIR_UP, 0, 1), -1);
    /* arrival *and* a bump still only counts as the carpet's rule */
    CHECK_EQ(ow_warp_trigger(events, N_EVENTS, 6, 7, OW_DIR_LEFT, 1, 1), -1);

    /* a warp whose destination is not in the build is a shut door: no trigger */
    CHECK_EQ(ow_warp_trigger(events, N_EVENTS, 9, 9, OW_DIR_UP, 1, 1), -1);

    /* lookups */
    CHECK_EQ(ow_warp_at(events, N_EVENTS, 3, 4), 0);
    CHECK_EQ(ow_warp_at(events, N_EVENTS, 6, 7), 1);
    CHECK_EQ(ow_warp_at(events, N_EVENTS, 3, 5), -1);
    CHECK_EQ(ow_warp_at(0, 3, 3, 4), -1);

    /* ---- starting one ---------------------------------------------------- */
    ow_warp_init(&w);
    ow_warp_start(&w, -1, 1);                  /* a shut door never starts */
    CHECK_EQ(ow_warp_busy(&w), 0);
    ow_warp_start(&w, 1, 2);
    CHECK_EQ(ow_warp_busy(&w), 1);
    CHECK_EQ(w.map, 1);
    CHECK_EQ(w.warp, 2);
    CHECK_EQ(w.armed, 0);                      /* must be re-armed by release */

    /* ---- the 10 + 10 frame transition ----------------------------------- */
    int frames, peak, loads, load_at;
    run_transition(&w, &frames, &peak, &loads, &load_at);
    CHECK_EQ(frames, 2 * OW_WARP_FADE);        /* 10 out, 10 in */
    CHECK_EQ(peak, OW_WARP_FADE);              /* the screen goes fully white */
    CHECK_EQ(loads, 1);                        /* one map swap, never two */
    CHECK_EQ(load_at, OW_WARP_FADE);           /* at the white frame, not before */
    CHECK_EQ(ow_warp_busy(&w), 0);
    CHECK_EQ(ow_warp_fade(&w), 0);             /* fully back in */
    CHECK_EQ(ow_warp_take_load(&w), 0);        /* the swap is not offered twice */

    /* the fade goes up, then down, one level per frame: record it */
    ow_warp_init(&w);
    ow_warp_start(&w, 1, 1);
    int levels[2 * OW_WARP_FADE + 4], n = 0;
    while (ow_warp_busy(&w) && n < (int)(sizeof levels / sizeof *levels)) {
        ow_warp_update(&w, OW_DIR_NONE);
        levels[n++] = ow_warp_fade(&w);
    }
    CHECK_EQ(n, 2 * OW_WARP_FADE);
    for (int i = 0; i < n; i++) {
        int want = i < OW_WARP_FADE ? i + 1 : 2 * OW_WARP_FADE - i - 1;
        CHECK_EQ(levels[i], want);
    }
    CHECK_EQ(levels[0], 1);                     /* one step in, not straight to 10 */
    CHECK_EQ(levels[OW_WARP_FADE - 1], OW_WARP_FADE);
    CHECK_EQ(levels[OW_WARP_FADE], OW_WARP_FADE - 1);

    /* ---- arming: no bounce on the mat you land on ----------------------- */
    ow_warp_init(&w);
    /* standing on the carpet bumping down: it fires */
    CHECK_EQ(ow_warp_poll(&w, events, N_EVENTS, 6, 7, OW_DIR_DOWN, 0, 1), 1);
    /* the pad is still held while the transition runs: no second trigger */
    CHECK_EQ(ow_warp_poll(&w, events, N_EVENTS, 6, 7, OW_DIR_DOWN, 0, 1), 0);
    run_transition(&w, &frames, &peak, &loads, &load_at);
    /* arrived in the house, standing on the mat, still holding down: dead */
    CHECK_EQ(ow_warp_poll(&w, events, N_EVENTS, 6, 7, OW_DIR_DOWN, 0, 1), 0);
    ow_warp_update(&w, OW_DIR_DOWN);           /* still held: stays disarmed */
    CHECK_EQ(w.armed, 0);
    CHECK_EQ(ow_warp_poll(&w, events, N_EVENTS, 6, 7, OW_DIR_DOWN, 0, 1), 0);
    /* letting go re-arms it, and then the same push works again */
    ow_warp_update(&w, OW_DIR_NONE);
    CHECK_EQ(w.armed, 1);
    CHECK_EQ(ow_warp_poll(&w, events, N_EVENTS, 6, 7, OW_DIR_DOWN, 0, 1), 1);

    /* an idle warp while armed keeps drawing no fade at all */
    ow_warp_init(&w);
    ow_warp_update(&w, OW_DIR_NONE);
    CHECK_EQ(ow_warp_fade(&w), 0);
    CHECK_EQ(ow_warp_busy(&w), 0);

    /* ---- where the player lands ----------------------------------------- */
    int px, py, dir;
    for (int i = 0; i < 16 * 16; i++) blocked_cells[i] = 0;

    /* a door: on its own cell would be inside the wall, so one cell below, and
     * facing down — the original's forced step out of COLL_DOOR */
    ow_warp_landing(events, N_EVENTS, 1, blocked, &px, &py, &dir);
    CHECK_EQ(px, 3 * OW_STEP_PX);
    CHECK_EQ(py, 5 * OW_STEP_PX);
    CHECK_EQ(dir, OW_DIR_DOWN);

    /* if that cell is taken the player stays in the doorway rather than
     * standing inside an NPC and never moving again */
    blocked_cells[5 * 16 + 3] = 1;
    ow_warp_landing(events, N_EVENTS, 1, blocked, &px, &py, &dir);
    CHECK_EQ(py, 4 * OW_STEP_PX);
    CHECK_EQ(dir, OW_DIR_DOWN);

    /* a carpet: on its own cell, facing up into the room, no push */
    ow_warp_landing(events, N_EVENTS, 2, blocked, &px, &py, &dir);
    CHECK_EQ(px, 6 * OW_STEP_PX);
    CHECK_EQ(py, 7 * OW_STEP_PX);
    CHECK_EQ(dir, OW_DIR_UP);

    /* a step warp (stairs) keeps its cell and faces down */
    ow_warp_landing(events, N_EVENTS, 3, blocked, &px, &py, &dir);
    CHECK_EQ(px, 9 * OW_STEP_PX);
    CHECK_EQ(py, 9 * OW_STEP_PX);
    CHECK_EQ(dir, OW_DIR_DOWN);

    /* a warp number out of range falls back to the top-left cell instead of
     * reading past the table */
    ow_warp_landing(events, N_EVENTS, 0, blocked, &px, &py, &dir);
    CHECK_EQ(px, OW_STEP_PX);
    CHECK_EQ(py, OW_STEP_PX);
    ow_warp_landing(events, N_EVENTS, 99, blocked, &px, &py, &dir);
    CHECK_EQ(px, OW_STEP_PX);
    CHECK_EQ(py, OW_STEP_PX);
    ow_warp_landing(0, 0, 1, blocked, &px, &py, &dir);
    CHECK_EQ(py, OW_STEP_PX);

    return test_report("test_ow_warp");
}
