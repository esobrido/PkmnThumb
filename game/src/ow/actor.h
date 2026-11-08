/*
 * ow/actor.h — NPC state: facing, in-place animation and draw order.
 *
 * Pure logic: no Mote, no globals, no allocation. NPCs whose movement type is a
 * spin rotate in place — that needs no pathfinding, so it is the part of NPC
 * animation that works before the movement system (OW-06) lands. Walking NPCs
 * will extend the same struct (step_left/walk_timer are already here, and the
 * frame maths is shared with the player through ow/dir.h).
 */
#ifndef OW_ACTOR_H
#define OW_ACTOR_H

#include "ow/dir.h"

enum {
    OW_ANIM_STAND = 0,
    OW_ANIM_SPIN_CCW,       /* down -> right -> up -> left, like OBJECT_ACTION_SPIN */
    OW_ANIM_SPIN_CW,
    OW_ANIM_SPIN_RANDOM,    /* SPRITEMOVEDATA_SPINRANDOM_* */
};

/* The original rotates one facing every 3 object updates (the low nibble of
 * OBJECT_STEP_FRAME counts 0..2 and the high nibble is the facing index). */
enum { OW_SPIN_FRAMES = 3 };

/* Capacity of a map's actor array; the busiest Johto maps fit well inside it. */
enum { OW_MAX_ACTORS = 16 };

typedef struct {
    int px, py;        /* pixel position, aligned to the 16 px grid */
    int dir;           /* OW_DIR_* */
    int anim;          /* OW_ANIM_* */
    int step_left;     /* pixels still to walk in the current step, 0 = standing */
    int walk_timer;    /* pixels walked, drives the 2-frame walk cycle */
    int spin_timer;    /* frames since the last in-place turn */
    uint32_t rng;      /* seeded from the position: screenshots stay reproducible */
} OwActor;

void ow_actor_init(OwActor *a, int px, int py, int dir, int anim);

/* Advance `frames` updates. Returns 1 if the NPC turned. */
int ow_actor_update(OwActor *a, int frames);

/* Turn to a direction and stand still: what talking to an NPC does. */
static inline void ow_actor_face(OwActor *a, int dir) {
    a->dir = dir;
    a->anim = OW_ANIM_STAND;
    a->spin_timer = 0;
}

/* Which of the 6 walker frames to draw, and whether to mirror it. */
int ow_actor_sheet_frame(const OwActor *a);
int ow_actor_flip(const OwActor *a);

/* Fills `order` (n ints) with sprite indices sorted by feet y, so a sprite that
 * is lower on the map is drawn over one that is higher. Stable for equal y, so
 * the result is deterministic. */
void ow_actor_depth_sort(const int *feet_y, int n, int *order);

#endif
