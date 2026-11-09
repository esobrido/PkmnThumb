/* What is in front of the player and answers to A.
 *
 * The original tries an object_event first and a bg_event second
 * (`CheckAPressOW` -> `TryObjectEvent` -> `TryBGEvent`), and a bg_event can be
 * one cell (a sign, a bookshelf) or direction-specific (the PC on the lab's
 * desk only answers from the south). This module is that second lookup: given
 * the cell the player faces, the map's bg_event table and the facing, it
 * returns the offset of the script to run, or 0xffff for nothing.
 */
#ifndef PC_SCRIPT_TRIGGER_H
#define PC_SCRIPT_TRIGGER_H

#include <stdint.h>

#include "gen/scripts.h"

/* The script that cell (x, y) runs when the player faces `dir` (an OW_DIR_*
 * from ow/dir.h), or 0xffff. A bg_event of a kind the port cannot run yet has
 * no script in the table, so it answers 0xffff like an empty cell. */
uint16_t pc_bg_event_script(const PcScriptMap *sm, int x, int y, int dir);

#endif
