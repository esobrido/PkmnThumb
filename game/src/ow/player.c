#include "ow/player.h"

void ow_player_init(OwPlayer *p, int px, int py, int dir) {
    p->px = px;
    p->py = py;
    p->dir = dir;
    p->step_left = 0;
    p->walk_timer = 0;
    p->turn_left = 0;
    p->turn_to = dir;
    p->bumping = 0;
    p->just_arrived = 0;
}

int ow_player_try_step(OwPlayer *p, const OwMap *m, int dir) {
    int tx = p->px, ty = p->py;
    switch (dir) {
        case OW_DIR_UP:    ty -= OW_STEP_PX; break;
        case OW_DIR_DOWN:  ty += OW_STEP_PX; break;
        case OW_DIR_LEFT:  tx -= OW_STEP_PX; break;
        case OW_DIR_RIGHT: tx += OW_STEP_PX; break;
        default: return 0;
    }
    if (ow_px_blocked(m, tx, ty)) return 0;
    p->step_left = OW_WALK_FRAMES;
    p->walk_timer = 0;
    return 1;
}

void ow_player_update(OwPlayer *p, const OwMap *m, int in_dir, int adv_px, int run,
                      int turn_frames) {
    if (turn_frames < 1) turn_frames = 1;
    p->bumping = 0;       /* only a blocked direction below turns it back on */
    p->just_arrived = 0;  /* only a step that finishes below turns it back on */

    /* Turning comes first. Asking for a direction the player is not facing
     * starts a turn in place; until it ends the player neither walks nor
     * listens to the pad, exactly like a movement that is already running. */
    if (p->turn_left == 0 && p->step_left == 0 && in_dir != OW_DIR_NONE &&
        in_dir != p->dir) {
        p->turn_left = 2 * turn_frames;
        p->turn_to = in_dir;
    }
    if (p->turn_left > 0) {
        p->turn_left -= adv_px;
        if (p->turn_left <= turn_frames) p->dir = p->turn_to;   /* halfway */
        if (p->turn_left < 0) p->turn_left = 0;
        return;
    }

    /* A step already under way is committed: a new direction only counts once
     * the player is standing on the grid again. A direction equal to the
     * current facing must start a step: at spawn the player faces down, so
     * pressing down has to walk, not freeze.
     *
     * If the step cannot start, the direction is held against a wall, an NPC or
     * the map edge: that is the original's .NotMoving -> ._WalkInPlace, i.e. the
     * walk cycle in place. The legs keep the cadence they had (the bump action
     * runs the same step-frame counter as a step), but running does not speed
     * them up — the GB counts frames there, not pixels. */
    if (p->step_left == 0 && in_dir != OW_DIR_NONE &&
        !ow_player_try_step(p, m, in_dir)) {
        p->bumping = 1;
        if (adv_px > 0) {
            p->walk_timer += adv_px;
            if (p->walk_timer >= OW_WALK_FRAMES) p->walk_timer -= OW_WALK_FRAMES;
        }
        return;
    }

    if (p->step_left <= 0 || adv_px <= 0) return;
    if (run) adv_px *= OW_RUN_SPEED;
    if (adv_px > p->step_left) adv_px = p->step_left;   /* never overshoot the cell */

    switch (p->dir) {
        case OW_DIR_UP:    p->py -= adv_px; break;
        case OW_DIR_DOWN:  p->py += adv_px; break;
        case OW_DIR_LEFT:  p->px -= adv_px; break;
        default:           p->px += adv_px; break;
    }
    p->step_left -= adv_px;
    p->walk_timer += adv_px;
    if (p->step_left == 0) p->just_arrived = 1;   /* the cell is reached */

    /* keep walking while the direction is still held */
    if (p->step_left == 0 && in_dir != OW_DIR_NONE)
        ow_player_try_step(p, m, in_dir);
}

int ow_player_sheet_frame(const OwPlayer *p) {
    return ow_sheet_frame(p->dir, p->step_left > 0 || p->bumping, p->walk_timer);
}

int ow_player_flip(const OwPlayer *p) {
    return ow_sheet_flip(p->dir);
}
