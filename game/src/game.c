/*
 * Polished Crystal on the Thumby Color — native Mote port.
 *
 * Slice 1: the overworld. A real Polished Crystal map (New Bark Town) rendered
 * from its own data — map block ids, the johto_traditional metatile table, the
 * 2bpp tileset sheet and the NPC walker sprites — with grid movement and
 * collision read from the game's own `tilecoll` table. No GBZ80 is emulated and
 * no GB ROM is loaded: all of that content was converted to C by
 * tools/extract.py and is drawn by Mote's banded renderer across both cores.
 *
 * Geometry (identical to the Game Boy): a map "block" is one metatile, 32x32 px
 * = 4x4 tiles of 8x8; the player walks in 16x16 px steps and collision is stored
 * per 16x16 quadrant. The GB screen is 160x144; the panel here is 128x128, so
 * the view is a 16x16 tile window that pans with the player.
 */
#include "mote_api.h"
#include "mote_build.h"
#include "gen/newbarktown.h"
#include <math.h>

MOTE_GAME_MODULE();
MOTE_GAME_META("Polished Crystal", "port");
MOTE_GAME_VERSION("0.1.0");
#ifdef MOTE_MODULE_BUILD
#include "mote_module.h"
MOTE_MODULE_HEADER();
#endif

#define SCREEN 128
#define BLOCK_PX 32          /* one map metatile = 32x32 px */
#define STEP_PX 16           /* walking step = 16x16 px */
#define WALK_FRAMES 16       /* frames per step at 60 fps, as on the GB */

/* ---- palettes ------------------------------------------------------------
 * Both tilesets and overworld sprites are 4-shade 2bpp art. The BG palettes
 * (bg_pal[8][4], day set) come from the game's own gfx/tilesets/bg_tiles.pal and
 * each tile picks one by the low 3 bits of its attribute byte; sprite art uses
 * the NPC palette, whose shade 0 is transparent.
 */
static const uint16_t pal_npc[4] = {
    0,                          /* 0 = transparent in sprite art */
    MOTE_RGB565(0xF8, 0xE0, 0xC0),
    MOTE_RGB565(0xD8, 0x78, 0x38),
    MOTE_RGB565(0x48, 0x48, 0x78),
};

/* ---- runtime buffers (carved from the Mote arena in init) ---------------- */
static uint16_t *spr_rgb;                  /* PC_SPRITE_COUNT * SPR_FRAMES * 4 * 64 */
static uint16_t *player_rgb;               /* SPR_FRAMES * 4 * 64 */

/* ---- overworld state ---------------------------------------------------- */
enum { DIR_DOWN, DIR_UP, DIR_LEFT, DIR_RIGHT };

static int px, py;            /* player pixel position, 16x16 grid aligned */
static int dir;               /* facing */
static int step_left;         /* pixels left in the current step */
static int walk_timer;        /* frames walked, for the 2-frame animation */
static int cam_x, cam_y;

/* start: the south side of town, on the road */
#define START_X (6 * BLOCK_PX)
#define START_Y (7 * BLOCK_PX)

/* ------------------------------------------------------------------ helpers */
/* A 2bpp GB tile is 16 bytes: per row, a low bitplane then a high bitplane,
 * MSB = leftmost pixel. Expand once into RGB565 so the render loop is a blit. */
static void expand_tile(uint16_t *dst, const uint8_t *src, const uint16_t *pal) {
    for (int y = 0; y < 8; y++) {
        uint8_t lo = src[y * 2], hi = src[y * 2 + 1];
        for (int x = 0; x < 8; x++) {
            int bit = 7 - x;
            int idx = ((lo >> bit) & 1) | (((hi >> bit) & 1) << 1);
            dst[y * 8 + x] = pal[idx];
        }
    }
}

static int block_at(int bx, int by) {
    if (bx < 0 || bx >= MAP_BLOCK_W || by < 0 || by >= MAP_BLOCK_H) return 0;
    return map_blocks[by * MAP_BLOCK_W + bx];
}

/* Collision for one 16x16 walking cell, from the map's own tilecoll table.
 * mt_coll bits are TL,TR,BL,BR of the 32x32 block (tools/extract.py remaps the
 * GB's BL,TL,BR,TR argument order). */
static int cell_blocked(int cx, int cy) {
    int bx = cx / 2, by = cy / 2;                 /* 16px cell -> 32px block */
    if (bx < 0 || bx >= MAP_BLOCK_W || by < 0 || by >= MAP_BLOCK_H) return 1;
    int quad = (cy & 1) * 2 + (cx & 1);
    return (mt_coll[block_at(bx, by)] >> quad) & 1;
}

static void clamp_camera(void) {
    int max_x = MAP_BLOCK_W * BLOCK_PX - SCREEN;
    int max_y = MAP_BLOCK_H * BLOCK_PX - SCREEN;
    cam_x = px + STEP_PX / 2 - SCREEN / 2;
    cam_y = py + STEP_PX / 2 - SCREEN / 2;
    if (cam_x > max_x) cam_x = max_x;
    if (cam_y > max_y) cam_y = max_y;
    if (cam_x < 0) cam_x = 0;
    if (cam_y < 0) cam_y = 0;
}

/* ------------------------------------------------------------------- engine */
static void g_init(void) {
    spr_rgb = (uint16_t *)mote->alloc((uint32_t)(PC_SPRITE_COUNT * SPR_FRAMES * 4 * 64) * 2);
    if (spr_rgb) {
        for (int s = 0; s < PC_SPRITE_COUNT; s++)
            for (int t = 0; t < SPR_FRAMES * 4; t++)
                expand_tile(&spr_rgb[((s * SPR_FRAMES * 4) + t) * 64],
                            pc_sprites[s].tiles[t], pal_npc);
    }
    player_rgb = (uint16_t *)mote->alloc((uint32_t)(SPR_FRAMES * 4 * 64) * 2);
    if (player_rgb) {
        for (int t = 0; t < SPR_FRAMES * 4; t++)
            expand_tile(&player_rgb[t * 64], spr_player[t], pal_npc);
    }

    px = START_X;
    py = START_Y;
    dir = DIR_DOWN;
    step_left = 0;
    clamp_camera();
    mote->set_fps_limit(60);
    mote->log("polished-crystal: overworld slice ready");
}

/* Try to start a step in `d`. Returns 1 if the step began. */
static int try_step(int d) {
    int tx = px, ty = py;
    switch (d) {
        case DIR_UP:    ty -= STEP_PX; break;
        case DIR_DOWN:  ty += STEP_PX; break;
        case DIR_LEFT:  tx -= STEP_PX; break;
        default:        tx += STEP_PX; break;
    }
    if (cell_blocked(tx / STEP_PX, ty / STEP_PX)) return 0;
    step_left = WALK_FRAMES;
    walk_timer = 0;
    return 1;
}

static void g_update(float dt) {
    const MoteInput *in = mote->input();
    int d = -1;

    if (mote_pressed(in, MOTE_BTN_UP))    d = DIR_UP;
    else if (mote_pressed(in, MOTE_BTN_DOWN))  d = DIR_DOWN;
    else if (mote_pressed(in, MOTE_BTN_LEFT))  d = DIR_LEFT;
    else if (mote_pressed(in, MOTE_BTN_RIGHT)) d = DIR_RIGHT;

    if (d >= 0 && d != dir) {
        dir = d;                       /* turn in place, like the original */
        if (step_left == 0) try_step(d);
    }

    if (step_left > 0) {
        /* advance at the GB's walking speed: one 16px step per 16 frames */
        int adv = (int)(dt * 60.0f + 0.5f);
        if (adv < 1) adv = 1;
        if (adv > step_left) adv = step_left;
        switch (dir) {
            case DIR_UP:    py -= adv; break;
            case DIR_DOWN:  py += adv; break;
            case DIR_LEFT:  px -= adv; break;
            default:        px += adv; break;
        }
        step_left -= adv;
        walk_timer += adv;
        clamp_camera();
        /* keep walking while the direction is still held */
        if (step_left == 0 && d >= 0) try_step(d);
    }
}

/* ---- the renderer: one band of the map, called on both cores -------------
 * Draws straight from the extracted 2bpp art with the tile's own BG palette, so
 * the four shades keep the exact spacing the game gives them (a floor reads as a
 * subtle rib, not as stripes). 8x8 tiles are clipped to this core's row band. */
static void g_band(uint16_t *fb, int y0, int y1) {
    int by0 = (y0 + cam_y) >> 3, by1 = ((y1 - 1) + cam_y) >> 3;
    for (int ty = by0; ty <= by1; ty++) {
        int my = ty * 8;
        int sy0 = my - cam_y;
        if (sy0 < y0) sy0 = y0;
        int sy1 = my + 8 - cam_y;
        if (sy1 > y1) sy1 = y1;
        if (sy1 <= sy0) continue;
        for (int tx = 0; tx < SCREEN / 8; tx++) {
            int mx = tx * 8 + cam_x;
            int b = block_at(mx >> 5, my >> 5);
            int ti = (((my >> 3) & 3) * 4) + ((mx >> 3) & 3);
            const uint8_t *src = ts_tiles[mt_tiles[b][ti]];
            const uint16_t *pal = bg_pal[mt_pal[b][ti]];
            for (int sy = sy0; sy < sy1; sy++) {
                int row = (sy + cam_y) - my;
                uint16_t *dst = &fb[sy * SCREEN + tx * 8];
                uint8_t lo = src[row * 2], hi = src[row * 2 + 1];
                for (int x = 0; x < 8; x++) {
                    int bit = 7 - x;
                    dst[x] = pal[((lo >> bit) & 1) | (((hi >> bit) & 1) << 1)];
                }
            }
        }
    }
}

/* ---- sprites (core0, after the bands) ----------------------------------- */
static void draw_sheet(uint16_t *fb, const uint16_t *sheet, int frame,
                       int x, int y, int flip) {
    sheet += frame * 4 * 64;
    for (int ty = 0; ty < 2; ty++) {
        for (int tx = 0; tx < 2; tx++) {
            const uint16_t *tile = &sheet[(ty * 2 + tx) * 64];
            for (int yy = 0; yy < 8; yy++) {
                int sy = y + ty * 8 + yy;
                if (sy < 0 || sy >= SCREEN) continue;
                for (int xx = 0; xx < 8; xx++) {
                    int sx = x + tx * 8 + (flip ? (7 - xx) : xx);
                    if (sx < 0 || sx >= SCREEN) continue;
                    uint16_t c = tile[yy * 8 + xx];
                    if (c) fb[sy * SCREEN + sx] = c;   /* 0 = transparent */
                }
            }
        }
    }
}

static void g_overlay(uint16_t *fb) {
    /* NPCs stand on their 32px block, facing down (frame 0), as on the GB. */
    for (int i = 0; spr_rgb && i < PC_OBJECT_COUNT; i++) {
        int x = pc_objects[i][0] - cam_x;
        int y = pc_objects[i][1] - cam_y;
        if (x <= -16 || x >= SCREEN || y <= -16 || y >= SCREEN) continue;
        draw_sheet(fb, &spr_rgb[pc_objects[i][2] * SPR_FRAMES * 4 * 64],
                   0, x, y, 0);
    }

    /* The walker sheet is 6 frames: down, up, left (standing + one walk frame);
     * facing right reuses the left frames mirrored. */
    if (player_rgb) {
        int stand = (step_left > 0 && ((walk_timer / 8) & 1)) ? 1 : 0;
        int base = dir == DIR_DOWN ? 0 : dir == DIR_UP ? 2 : 4;
        draw_sheet(fb, player_rgb, base + stand, px - cam_x, py - cam_y,
                   dir == DIR_RIGHT);
    }
}

static const MoteGameVtbl k_vtbl = {
    .init = g_init,
    .update = g_update,
    .render_band = g_band,
    .overlay = g_overlay,
    .config = { 0 },
};

static const MoteGameVtbl *mote_game_vtbl(void) { return &k_vtbl; }
