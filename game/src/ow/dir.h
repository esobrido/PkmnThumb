/*
 * ow/dir.h — facing directions and the walker-sheet frame layout.
 *
 * Header-only because it is pure vocabulary shared by the player, the NPCs and
 * the renderer: no state, no code that can fail.
 *
 * A walker sheet (player and NPCs alike) is 16x96 = 6 frames of 16x16:
 *   frames 0-1  facing down   (standing, one walk step)
 *   frames 2-3  facing up
 *   frames 4-5  facing left   (facing right reuses these, mirrored)
 */
#ifndef OW_DIR_H
#define OW_DIR_H

#include <stdint.h>

enum { OW_DIR_DOWN, OW_DIR_UP, OW_DIR_LEFT, OW_DIR_RIGHT, OW_DIR_NONE = -1 };

/* Which of the 6 sheet frames to draw. `cycling` is 1 while the walker is moving
 * its legs — a step under way, or the bump animation when a held direction is
 * blocked — and 0 while it stands. The cycle alternates every 8 units of
 * `walk_timer` (the GB increments that counter once per frame), so a 16 px step
 * shows both frames. */
static inline int ow_sheet_frame(int dir, int cycling, int walk_timer) {
    int base = dir == OW_DIR_DOWN ? 0 : dir == OW_DIR_UP ? 2 : 4;
    int step = (cycling && ((walk_timer / 8) & 1)) ? 1 : 0;
    return base + step;
}

/* 1 when the sprite must be drawn mirrored (facing right). */
static inline int ow_sheet_flip(int dir) {
    return dir == OW_DIR_RIGHT;
}

#endif
