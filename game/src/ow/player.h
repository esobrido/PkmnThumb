/*
 * ow/player.h — the walking state machine.
 *
 * Pure logic: it takes a direction and a number of pixels to advance, so tests
 * can drive it deterministically instead of depending on frame timing. The Mote
 * glue converts `dt` into pixels (60 px/s, the GB's one 16 px step per 16
 * frames) and maps buttons to directions.
 *
 * Running is the same step taken faster: the original's StepVectors table has a
 * walking row of 1 px/frame over 16 frames and a running-shoes row of 2 px/frame
 * over 8 frames, both of which land on the same 16 px cell. So `run` only
 * doubles the pixels per frame — the animation needs no separate case, because
 * the walker-sheet frame advances every 8 *pixels* walked (see ow/dir.h) and the
 * pixels now come twice as fast.
 *
 * The move model mirrors the original: pressing a direction turns in place and,
 * if the next cell is free, starts a step; holding it keeps walking.
 *
 * Turning has its own clock. StepFunction_Turn (map_objects.asm) shows the old
 * facing for `turn_frames` frames, swaps, shows the new facing for another
 * `turn_frames`, and only then lets the player walk; .GetTurningSpeed picks 4
 * frames per half with Turning Speed = Slow and 2 with Fast. So a full turn
 * costs 8 or 4 frames, and a tap turns you without taking a step.
 *
 * Walking into something is not "nothing happens". The original's .NotMoving
 * (player_movement.asm) plays SFX_BUMP and calls ._WalkInPlace, whose bump
 * action keeps the walker's step-frame counter running, so the legs cycle in
 * place while the player stays on its cell and keeps facing the obstacle — the
 * same action code as a step (SetFacingBumpAction falls through into
 * SetFacingStepAction). It is not a step: nothing is committed, and it stops on
 * the frame the direction is released.
 */
#ifndef OW_PLAYER_H
#define OW_PLAYER_H

#include "ow/dir.h"
#include "ow/map.h"

enum {
    OW_WALK_FRAMES = 16,   /* frames (== pixels) per 16 px step */
    OW_RUN_SPEED = 2,      /* px per frame with the running shoes */
    OW_RUN_FRAMES = 8,     /* OW_RUN_SPEED px/frame over 8 frames = 16 px */
};

typedef struct {
    int px, py;        /* pixel position, aligned to the 16 px grid */
    int dir;           /* OW_DIR_* */
    int step_left;     /* pixels still to walk in the current step, 0 = standing */
    int walk_timer;    /* pixels walked, drives the 2-frame animation */
    int turn_left;     /* pixels left in a turn in place, 0 = facing settled */
    int turn_to;       /* the direction a turn is heading for */
    int bumping;       /* 1 while a held direction is blocked: legs walk in place */
    int just_arrived;  /* 1 on the frame a step finished; warps fire on it */
} OwPlayer;

void ow_player_init(OwPlayer *p, int px, int py, int dir);

/* Start a step in `dir` if the destination cell is free. Returns 1 if it began. */
int ow_player_try_step(OwPlayer *p, const OwMap *m, int dir);

/* One update. `in_dir` is OW_DIR_NONE or a held direction; `adv_px` is how many
 * pixels to advance; `run` doubles them; `turn_frames` is how long each half of
 * a turn in place lasts — 4 at Turning Speed = Slow, 2 at Fast, see
 * options_turn_frames. Running does not shorten the turn, nor speed up the bump
 * animation: the original counts frames in both, not pixels. A blocked step
 * sets `bumping` for the renderer and moves nothing, and a step that finishes
 * sets `just_arrived` for one frame — ow/warp fires doors on that frame. */
void ow_player_update(OwPlayer *p, const OwMap *m, int in_dir, int adv_px, int run,
                      int turn_frames);

/* Which of the 6 walker frames to draw: down 0-1, up 2-3, left 4-5. Facing
 * right reuses the left frames mirrored. The bump animation reuses the walk
 * frames, so pushing a wall looks like walking on the spot. */
int ow_player_sheet_frame(const OwPlayer *p);

/* 1 when the player is facing right and the sprite must be drawn mirrored. */
int ow_player_flip(const OwPlayer *p);

#endif
