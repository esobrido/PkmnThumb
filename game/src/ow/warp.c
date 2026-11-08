#include "ow/warp.h"

#include "ow/map.h"

void ow_warp_init(OwWarp *w) {
    w->phase = OW_WARP_IDLE;
    w->level = 0;
    w->t = 0;
    w->map = -1;
    w->warp = 0;
    w->load = 0;
    w->armed = 1;
}

int ow_warp_at(const PcWarp *warps, int count, int cx, int cy) {
    if (!warps) return -1;
    for (int i = 0; i < count; i++)
        if (warps[i].x == cx && warps[i].y == cy) return i;
    return -1;
}

int ow_warp_trigger(const PcWarp *warps, int count, int cx, int cy, int facing,
                    int arrived, int bumping) {
    int i = ow_warp_at(warps, count, cx, cy);
    if (i < 0) return -1;
    const PcWarp *w = &warps[i];
    if (w->map < 0) return -1;              /* destination not in this build */
    if (w->kind == PC_WARP_CARPET)
        return (bumping && facing == (int)w->dir) ? i : -1;
    return arrived ? i : -1;
}

void ow_warp_start(OwWarp *w, int map, int warp) {
    if (map < 0) return;
    w->phase = OW_WARP_OUT;
    w->level = 0;
    w->t = 0;
    w->map = map;
    w->warp = warp;
    w->load = 0;
    w->armed = 0;
}

int ow_warp_poll(OwWarp *w, const PcWarp *warps, int count, int cx, int cy,
                 int facing, int arrived, int bumping) {
    if (ow_warp_busy(w) || !w->armed) return 0;
    int i = ow_warp_trigger(warps, count, cx, cy, facing, arrived, bumping);
    if (i < 0) return 0;
    ow_warp_start(w, warps[i].map, warps[i].warp);
    return 1;
}

void ow_warp_update(OwWarp *w, int dir_held) {
    if (w->phase == OW_WARP_IDLE) {
        /* Letting go of the pad re-arms the warps under the player. Without
         * that, walking into a house with the direction still held would land
         * you on the doormat and fire the exit warp on the next frame. */
        if (dir_held == OW_DIR_NONE) w->armed = 1;
        w->level = 0;
        return;
    }

    w->t++;
    if (w->phase == OW_WARP_OUT) {
        w->level = w->t >= OW_WARP_FADE ? OW_WARP_FADE : w->t;
        if (w->t >= OW_WARP_FADE) {
            w->phase = OW_WARP_IN;
            w->t = 0;
            w->load = 1;            /* swap the map while the screen is white */
        }
        return;
    }

    if (w->t >= OW_WARP_FADE) {
        w->phase = OW_WARP_IDLE;
        w->level = 0;
        return;
    }
    w->level = OW_WARP_FADE - w->t;
}

int ow_warp_busy(const OwWarp *w) {
    return w->phase != OW_WARP_IDLE;
}

int ow_warp_take_load(OwWarp *w) {
    int load = w->load;
    w->load = 0;
    return load;
}

int ow_warp_fade(const OwWarp *w) {
    return w->level;
}

void ow_warp_landing(const PcWarp *warps, int count, int warp,
                     int (*blocked)(int x, int y), int *px, int *py, int *dir) {
    int x = OW_STEP_PX, y = OW_STEP_PX, d = OW_DIR_DOWN;
    int i = warp - 1;
    if (warps && i >= 0 && i < count) {
        x = warps[i].x * OW_STEP_PX;
        y = warps[i].y * OW_STEP_PX;
        /* Inside a building you arrive on the mat facing up, into the room;
         * outside, on the doorstep, facing down — the direction you were
         * walking when you went in. */
        d = warps[i].kind == PC_WARP_CARPET ? OW_DIR_UP : OW_DIR_DOWN;
        /* A door forces a step down out of the doorway (.CheckTile), so the
         * player really ends up one cell below it. Do it during the white
         * frame instead of animating a step nobody can see. */
        if (warps[i].kind == PC_WARP_DOOR && blocked &&
            !blocked(x, y + OW_STEP_PX))
            y += OW_STEP_PX;
    }
    *px = x;
    *py = y;
    *dir = d;
}
