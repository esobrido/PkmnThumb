/* What is in front of the player and answers to A.
 *
 * The original tries an object_event first and a bg_event second
 * (`CheckAPressOW` -> `TryObjectEvent` -> `TryBGEvent`), and a bg_event can be
 * one cell (a sign, a bookshelf) or direction-specific (the PC on the lab's
 * desk only answers from the south). This module is that second lookup: given
 * the cell the player faces, the map's bg_event table and the facing, it says
 * what to run and, for a hidden item, whether it is still there.
 */
#ifndef PC_SCRIPT_TRIGGER_H
#define PC_SCRIPT_TRIGGER_H

#include <stdint.h>

#include "gen/scripts.h"

/* "no item", "no flag": the value of a field that does not apply. */
#define PC_TRIGGER_NONE 0xffff

typedef struct {
    uint16_t script;  /* offset of the script to run, inside the map's blob */
    uint16_t item;    /* hidden item to give, or PC_TRIGGER_NONE */
    uint16_t flag;    /* flag that marks it taken, or PC_TRIGGER_NONE */
} PcTrigger;

/* Fills `out` and returns 1 when cell (x, y) answers to A with the player
 * facing `dir` (an OW_DIR_* from ow/dir.h), 0 when nothing is there.
 *
 * `flags` is the game's flag/event bit array (see script/vm.h): a hidden item
 * whose flag is already set is not there any more, which is the check
 * BGEVENT_ITEM does before it reads. A bg_event of a kind the port cannot run
 * yet has no script in the table, so it answers 0 like an empty cell. */
int pc_bg_event(const PcScriptMap *sm, int x, int y, int dir,
                const uint8_t *flags, uint16_t nflags, PcTrigger *out);

#endif
