/*
 * ow/warp.h — doors, warp carpets and the transition between maps.
 *
 * Pure logic: the warp events arrive as the generated PcWarp table, and the
 * transition only counts frames and reports a fade level. Swapping the map and
 * placing the player are the caller's job, because that is the one part that
 * needs the engine (and, in a test, a stub).
 *
 * There are two ways to warp, and the original takes them at different points of
 * a frame's movement code:
 *
 *  - Doors and stairs (PC_WARP_DOOR, PC_WARP_STEP) fire when the step onto their
 *    cell *ends*. CheckTileEvent runs once a step completes (player_step.asm) and
 *    asks CheckWarpTile about the tile just landed on (home/map.asm). Nothing
 *    fires from standing still on them, which is what stops a warp from bouncing
 *    the player straight back: arriving is not a step.
 *
 *    A door pulls the player in, too. COLL_DOOR is in the .CheckTile list of
 *    tiles that force a step down (player_movement.asm), which is why leaving a
 *    building leaves you one cell below the doorway. The port applies that once,
 *    on arrival, while the screen is still white.
 *
 *  - Warp carpets (PC_WARP_CARPET) are directional and never fire on their own:
 *    .CheckWarp runs *after* TryStep, and only when the step in the held
 *    direction could not start, the player is facing that way, and the tile
 *    under him is the matching carpet. That is what makes the doormat at the
 *    bottom of every house work: you stand on it, push down, the cell below is
 *    out of the map, so "no step" becomes the warp. It is also why the same mat
 *    does nothing while you face up into the room.
 *
 * The transition is the original's MAPSETUP_DOOR script: FadeOutPalettes (10
 * frames to white), load the destination at the target warp's own cell,
 * FadeInPalettes (10 frames back). There is no door-tile animation anywhere in
 * Polished Crystal: the fade is the door opening, and the step into the doorway
 * is the other half of the effect.
 */
#ifndef OW_WARP_H
#define OW_WARP_H

#include "gen/world.h"
#include "ow/dir.h"
#include "ow/fade.h"

/* Frames of fade-out and of fade-in: the original's two FadeOutPalettes /
 * FadeInPalettes calls with c = 10. */
enum { OW_WARP_FADE = OW_FADE_STEPS };

enum {
    OW_WARP_IDLE,   /* no transition running */
    OW_WARP_OUT,    /* fading to white before the map swap */
    OW_WARP_IN,     /* fading back in on the new map */
};

typedef struct {
    int phase;      /* OW_WARP_* */
    int level;      /* fade level to draw this frame, 0..OW_WARP_FADE */
    int t;          /* frames spent in the current phase */
    int map;        /* destination map index, -1 = shut */
    int warp;       /* 1-based destination warp number */
    int load;       /* 1 on the single frame the map must be swapped */
    int armed;      /* 0 after a warp until the pad is released once */
} OwWarp;

void ow_warp_init(OwWarp *w);

/* Index of the warp event on this cell, or -1. */
int ow_warp_at(const PcWarp *warps, int count, int cx, int cy);

/* Should the player warp right now? `arrived` is "a step ended on this cell this
 * frame" (ow_player.just_arrived), `bumping` is "the held direction could not
 * start a step" (ow_player.bumping). Returns the event index or -1. */
int ow_warp_trigger(const PcWarp *warps, int count, int cx, int cy, int facing,
                    int arrived, int bumping);

/* Begin the transition. Nothing happens for a shut warp (map < 0). */
void ow_warp_start(OwWarp *w, int map, int warp);

/* Look for a warp under the player and start its transition: the whole trigger
 * rule, including "do not fire twice until the pad is released". Returns 1 if a
 * transition began. */
int ow_warp_poll(OwWarp *w, const PcWarp *warps, int count, int cx, int cy,
                 int facing, int arrived, int bumping);

/* Advance one frame. `dir_held` is the pad, so the transition can tell when the
 * player has let go — the only way to re-arm a warp and avoid bouncing. */
void ow_warp_update(OwWarp *w, int dir_held);

/* 1 while the world must stand still and the fade must be drawn. */
int ow_warp_busy(const OwWarp *w);

/* 1 on the frame the destination map has to be loaded (once per warp). */
int ow_warp_take_load(OwWarp *w);

/* The fade level to apply to this frame's framebuffer, 0..OW_WARP_FADE. */
int ow_warp_fade(const OwWarp *w);

/* Where the player ends up after a warp: the destination event's own cell, or
 * one cell below it for a door, which is where the original's forced step down
 * leaves him. `blocked` is the caller's collision test for that cell. */
void ow_warp_landing(const PcWarp *warps, int count, int warp, int (*blocked)(int x, int y),
                     int *px, int *py, int *dir);

#endif
