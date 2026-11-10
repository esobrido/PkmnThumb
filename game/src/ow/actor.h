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
#include "ow/map.h"

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
    int step_frames;   /* frames the whole 16 px step takes (see ow/move.h) */
    int step_acc;      /* frames walked of it so far: px = 16 * acc / frames */
    int walk_timer;    /* pixels walked, drives the 2-frame walk cycle */
    int spin_timer;    /* frames since the last in-place turn */
    uint32_t rng;      /* seeded from the position: screenshots stay reproducible */
} OwActor;

void ow_actor_init(OwActor *a, int px, int py, int dir, int anim);

/* Start a 16 px step towards `dir` over `frames` frames (OW_STEP_* in ow/move.h).
 * The facing changes with the step, as the original's step functions do. */
void ow_actor_start_step(OwActor *a, int dir, int frames);

/* 1 while a step is under way, i.e. the actor is between two cells. */
int ow_actor_walking(const OwActor *a);

/* The next number of the actor's own generator: deterministic for a position, so
 * a screenshot of frame N is the same frame every run. */
uint16_t ow_actor_rand(OwActor *a);

/* Advance `frames` updates, walking a step and/or spinning in place. Returns 1
 * if the NPC turned. A spinner that has been told to walk stops turning until it
 * stands again: the walk owns the facing while it lasts. */
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
