#include "script/trigger.h"

#include "ow/dir.h"
#include "script/vm.h"

/* The direction a directional bg_event demands, or OW_DIR_NONE for the kinds
 * that read from any side. The original compares the player's direction with
 * `BGEVENT_UP`..`BGEVENT_LEFT` in its own numbering (events.asm's .checkdir),
 * where READ, JUMPTEXT, JUMPSTD and the hidden items always read. */
static int wanted_dir(uint8_t kind) {
    switch (kind) {
    case PC_BGEVENT_UP:    return OW_DIR_UP;
    case PC_BGEVENT_DOWN:  return OW_DIR_DOWN;
    case PC_BGEVENT_RIGHT: return OW_DIR_RIGHT;
    case PC_BGEVENT_LEFT:  return OW_DIR_LEFT;
    default:               return OW_DIR_NONE;
    }
}

int pc_bg_event(const PcScriptMap *sm, int x, int y, int dir,
                const uint8_t *flags, uint16_t nflags, PcTrigger *out) {
    if (!sm || !sm->bg || !out) return 0;
    for (int i = 0; i < sm->bg_count; i++) {
        const PcBgEvent *e = &sm->bg[i];
        if (e->x != x || e->y != y) continue;
        int want = wanted_dir(e->kind);
        if (want != OW_DIR_NONE && want != dir) {
            /* Right cell, wrong side: the original stops here (`.dontread`)
             * instead of falling through to a tile collision event. */
            return 0;
        }
        if (e->script == 0xffff) return 0;
        /* A hidden item that is already in the bag is not there any more: the
         * engine tests its flag before running HiddenItemScript. */
        if (e->flag != 0xffff && scr_flag_get(flags, nflags, e->flag)) return 0;
        out->script = e->script;
        out->flag = e->flag;
        out->item = e->kind >= PC_BGEVENT_ITEM
            ? (uint16_t)(e->kind - PC_BGEVENT_ITEM) : PC_TRIGGER_NONE;
        return 1;
    }
    return 0;
}
