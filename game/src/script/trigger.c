#include "script/trigger.h"

#include "ow/dir.h"

/* The direction a directional bg_event demands, or OW_DIR_NONE for the kinds
 * that read from any side. The original compares the player's direction with
 * `BGEVENT_UP`..`BGEVENT_LEFT` in its own numbering (events.asm's .checkdir),
 * where READ and JUMPTEXT always read. */
static int wanted_dir(uint8_t kind) {
    switch (kind) {
    case PC_BGEVENT_UP:    return OW_DIR_UP;
    case PC_BGEVENT_DOWN:  return OW_DIR_DOWN;
    case PC_BGEVENT_RIGHT: return OW_DIR_RIGHT;
    case PC_BGEVENT_LEFT:  return OW_DIR_LEFT;
    default:               return OW_DIR_NONE;
    }
}

uint16_t pc_bg_event_script(const PcScriptMap *sm, int x, int y, int dir) {
    if (!sm || !sm->bg) return 0xffff;
    for (int i = 0; i < sm->bg_count; i++) {
        const PcBgEvent *e = &sm->bg[i];
        if (e->x != x || e->y != y) continue;
        if (e->kind == PC_BGEVENT_READ || e->kind == PC_BGEVENT_JUMPTEXT)
            return e->script;
        int want = wanted_dir(e->kind);
        if (want != OW_DIR_NONE && want == dir) return e->script;
        /* Right cell, wrong side: the original stops here (`.dontread`) instead
         * of falling through to a tile collision event. */
        if (want != OW_DIR_NONE) return 0xffff;
    }
    return 0xffff;
}
