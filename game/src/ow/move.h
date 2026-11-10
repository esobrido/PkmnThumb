/*
 * ow/move.h — the movement byte stream, and the walking patterns of an NPC.
 *
 * Polished Crystal's movement data (macros/scripts/movement.asm) is a byte
 * stream just like the script bytecode: `step_left` is one byte, `step_sleep`
 * is two. The extractor keeps those blocks inside the map's own script blob, so
 * an `applymovement` operand is an offset into the blob the game already has —
 * no second table, no pointer table to keep in sync.
 *
 * Two ways an NPC moves, and both live here because both are "what byte/pattern
 * says step next":
 *
 *   ow_move_*    a stream from `applymovement`, run to `step_end`.
 *   ow_wander_*  the `SPRITEMOVEDATA_WALK_*` patterns, which pick their own
 *                direction every so often and walk one cell at a time.
 *
 * Pure logic: no Mote, no globals, no allocation. This module never touches an
 * OwActor's pixels — it says what the caller should do next, and the caller
 * (game.c) owns the collision test and the actor's walk. That is what makes the
 * teacher's four steps testable without a screen.
 */
#ifndef OW_MOVE_H
#define OW_MOVE_H

#include <stdint.h>

#include "ow/actor.h"

/* Frames per 16 px step, by the original's step kinds: StepVectors has a slow
 * row (2 frames per pixel), a normal one (1) and a fast/run one (2 px/frame). */
enum {
    OW_STEP_FAST = 8,
    OW_STEP_NORMAL = 16,
    OW_STEP_SLOW = 32,
};

/* A movement stream being played: a pointer into the map's blob and a cursor. */
typedef struct {
    const uint8_t *code;   /* NULL when nothing is running */
    uint16_t len;
    uint16_t pos;
} OwMove;

typedef enum {
    OW_MOVE_END = 0,      /* reached step_end (or ran off the data) */
    OW_MOVE_TURN,         /* face `dir` where it stands: instant */
    OW_MOVE_STEP,         /* walk one cell towards `dir` over `frames` frames */
    OW_MOVE_WAIT,         /* do nothing for `frames` frames */
    OW_MOVE_SHOW,         /* `b`: 1 shows the object, 0 hides it */
    OW_MOVE_UNSUPPORTED,  /* a step this port cannot run; `opcode` names it */
} OwMoveKind;

typedef struct {
    uint8_t kind;     /* OwMoveKind */
    uint8_t dir;      /* OW_DIR_* for TURN and STEP */
    uint8_t frames;   /* frames of a STEP or a WAIT */
    uint8_t b;        /* SHOW: 1 show, 0 hide */
    uint8_t opcode;   /* UNSUPPORTED: the movement byte */
} OwMoveOut;

/* Point the stream at `code` (a movement label's offset in the map's blob). A
 * NULL or empty stream ends immediately, so a caller never has to test first. */
void ow_move_start(OwMove *m, const uint8_t *code, uint16_t len);

/* Forget the stream: the actor keeps standing. */
void ow_move_reset(OwMove *m);

/* 1 while there is a stream that has not reached step_end. */
int ow_move_running(const OwMove *m);

/* The next thing to do. Turns, facing fixes and show/hide are consumed while
 * looking for something that takes time, so this returns TURN or SHOW only when
 * they are the first byte; the caller does them and calls again. The walker must
 * be standing: one step at a time, like the original's step functions. */
OwMoveKind ow_move_next(OwMove *m, OwMoveOut *out);

/* The step cannot be taken (wall, NPC, the player): the movement ends, as the
 * original's does when a walker bumps, and the script carries on. */
void ow_move_stop(OwMove *m);

/* ---- the SPRITEMOVEDATA_WALK_* patterns --------------------------------- */

/* How long an NPC looks at the world between steps, in frames. The original
 * draws a random duration after every walk (`RandomStepDuration_Slow`), so the
 * port does the same: 16 to 79 frames, from the actor's own seed. */
typedef struct {
    uint8_t timer;   /* frames to wait before the next decision */
} OwWander;

void ow_wander_init(OwWander *w);

/* A direction to try, or OW_DIR_NONE while the NPC waits. `move` is the
 * original's SPRITEMOVEDATA_* number from gen/world.h. WALK_UP_DOWN picks up or
 * down, WALK_LEFT_RIGHT picks left or right, and everything else stands still.
 * Each answer also arms the next wait, so a blocked NPC retries every ~16-79
 * frames instead of every frame. The caller checks the cell: if it is blocked,
 * nothing happens and the pattern simply tries again later. */
int ow_wander_next(OwWander *w, OwActor *a, uint8_t move);

#endif
