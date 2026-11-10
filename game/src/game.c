/*
 * Polished Crystal on the Thumby Color — native Mote port.
 *
 * Slice 1: the overworld. Real Polished Crystal maps (New Bark Town, Route 29,
 * Cherrygrove City) rendered from their own data — map block ids, the
 * johto_traditional metatile table, the 2bpp tileset sheet and the NPC walker
 * sprites — with grid movement and collision read from the game's own `tilecoll`
 * table. No GBZ80 is emulated and no GB ROM is loaded: all of that content was
 * converted to C by tools/extract.py and is drawn by Mote's banded renderer
 * across both cores.
 *
 * This file is only the Mote glue (vtable, arena, input mapping, map switching).
 * The systems it drives live in their own modules and are unit-tested on the host:
 *
 *   ow/map     map geometry + collision      tests/unit/test_ow_map.c
 *   ow/camera  the scrolling window          tests/unit/test_ow_camera.c
 *   ow/player  the walking state machine     tests/unit/test_ow_player.c
 *   ow/actor   NPC facing, spin, Y-sort      tests/unit/test_ow_actor.c
 *   ow/render  the banded raster             tests/unit/test_ow_render.c
 *   ow/connect walking between two maps      tests/unit/test_ow_connect.c
 *   gfx_sprite 2bpp expansion and blitting   tests/unit/test_gfx_sprite.c
 *
 * Geometry (identical to the Game Boy): a map "block" is one metatile, 32x32 px
 * = 4x4 tiles of 8x8; the player walks in 16x16 px steps and collision is stored
 * per 16x16 quadrant. The GB screen is 160x144; the panel here is 128x128, so
 * the view is a 16x16 tile window that pans with the player.
 *
 * Doors work like the original's: a warp event on the cell, triggered when the
 * step onto a door ends or when a doormat's direction is pushed against, then
 * 10 frames fading to white, the map swap, and 10 frames back (ow/warp, ow/fade).
 * Walking off an outdoor map's edge is the other way between maps, and it has no
 * fade at all: the `connection` rows of data/maps/attributes.asm say which map is
 * next door, and the player reappears on the opposite edge (ow/connect). RB/LB
 * still jump between maps in the world table, which is a debug shortcut.
 */
#include "mote_api.h"
#include "mote_build.h"
#include "gen/world.h"
#include "ow/map.h"
#include "ow/camera.h"
#include "ow/player.h"
#include "ow/actor.h"
#include "ow/render.h"
#include "ow/warp.h"
#include "ow/connect.h"
#include "gfx_sprite.h"
#include "gen/text.h"
#include "gen/scripts.h"
#include "bag.h"
#include "script/vm.h"
#include "script/trigger.h"
#include "text/box.h"
#include "ui/menu.h"
#include "ui/options.h"

MOTE_GAME_MODULE();

/* Name shown by the launcher, and the version baked into the .mote so the
 * gallery can tell an installed build from a newer one. Bump both when a
 * phase lands: the artefact name made by scripts/export.sh comes from here. */
MOTE_GAME_META("Polished Crystal", "fan port");
MOTE_GAME_VERSION("0.14.0");
#ifdef MOTE_MODULE_BUILD
#include "mote_module.h"
MOTE_MODULE_HEADER();
#endif

#define SCREEN 128

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

/* Batch of walker sheets: PC_SPRITE_COUNT NPCs at 6 frames of 2x2 tiles each. */
#define SHEET_TILES (SPR_FRAMES * SPR_TILES_PER_FRAME)
#define SHEET_PX (SHEET_TILES * SPR_TILE_PX2)

/* Where the player starts on a map nobody warped into: the RB/LB debug jump, and
 * the fallback when a warp event is missing.
 *
 * New Bark Town keeps a hand-picked start — the road in the south of town, which
 * is where every capture recipe in docs/05 begins. The rest use the game's own
 * respawn point (data/maps/spawn_points.asm; Cherrygrove's is the city square),
 * and indoor maps have none, so they fall back to their first warp event, which
 * is the doormat by the door. Route 29 has no respawn point in the game (it is
 * walked into), so the debug jump lands on the road at its west end, where
 * Cherrygrove arrives. */
static void map_spawn(int index, int *x, int *y) {
    const PcMapDef *d = &pc_maps[index];
    if (index == PC_MAP_NEWBARKTOWN) {
        *x = 6 * 32;                 /* the road, south of the houses */
        *y = 7 * 32;
        return;
    }
    if (d->spawn_x || d->spawn_y) {
        *x = d->spawn_x * OW_STEP_PX;
        *y = d->spawn_y * OW_STEP_PX;
        return;
    }
    if (index == PC_MAP_ROUTE29) {
        *x = 1 * OW_STEP_PX;         /* west end, where Cherrygrove arrives */
        *y = 6 * OW_STEP_PX;
        return;
    }
    if (d->warp_count) {
        *x = d->warps[0].x * OW_STEP_PX;
        *y = d->warps[0].y * OW_STEP_PX;
        return;
    }
    *x = *y = OW_STEP_PX;
}

/* ---- runtime buffers (carved from the Mote arena in init) ---------------- */
static uint16_t *spr_rgb;                  /* PC_SPRITE_COUNT * SHEET_PX */
static uint16_t *player_rgb;               /* SHEET_PX */

/* ---- state --------------------------------------------------------------- */
static OwMap map;                          /* view over the generated tables */
static const PcTileset *tileset;           /* art of the loaded map */
static int map_index;
static OwPlayer player;
static OwCamera cam;
static OwActor actors[OW_MAX_ACTORS];
static int actor_count;
static OwWarp warp;                       /* the door transition in progress */

/* ------------------------------------------------------------------- engine */

/* Put the player at (x, y); if that cell is blocked (content changed), look for
 * the nearest free cell instead of dropping him inside a tree. */
static void place_player(int x, int y, int dir) {
    if (ow_px_blocked(&map, x, y)) {
        for (int r = 1; r <= 4 && ow_px_blocked(&map, x, y); r++) {
            for (int dy = -r; dy <= r && ow_px_blocked(&map, x, y); dy++)
                for (int dx = -r; dx <= r && ow_px_blocked(&map, x, y); dx++) {
                    int nx = x + dx * OW_STEP_PX, ny = y + dy * OW_STEP_PX;
                    if (!ow_px_blocked(&map, nx, ny)) { x = nx; y = ny; }
                }
        }
    }
    ow_player_init(&player, x, y, dir);
    ow_camera_follow(&cam, &map, player.px, player.py, SCREEN);
}

/* ow_warp_landing asks whether a cell is blocked; give it the loaded map. */
static int landing_blocked(int x, int y) {
    return ow_px_blocked(&map, x, y);
}

/* NPCs block their cell: they are not in the tileset collision, but you cannot
 * walk through one, and a text triggers only when you are stopped in front of
 * it. The map therefore gets a second, per-cell blocker layer filled here.
 * Sized for the widest starting map (30 blocks = 60 cells across).
 * Rebuilt on every load; when NPCs start walking (OW-06) this is the table to
 * update as they move. */
/* 64 x 32 cells = 32 x 16 blocks, twice the tallest starting map. */
#define MAX_CELLS (64 * 32)
static uint8_t occupied[MAX_CELLS];

static void mark_actors(void) {
    int cw = map.w * 2;
    int total = cw * (map.h * 2);
    if (total > MAX_CELLS) {     /* map too big: fall back to tile collision
                                    instead of indexing past the table */
        map.cells = 0;
        return;
    }
    for (int i = 0; i < total; i++) occupied[i] = 0;
    for (int i = 0; i < actor_count; i++) {
        int cx = actors[i].px >> 4, cy = actors[i].py >> 4;
        if (cx >= 0 && cx < cw && cy >= 0 && cy < map.h * 2)
            occupied[cy * cw + cx] = 1;
    }
    map.cells = occupied;
}

/* Everything a map load shares: the tables, the NPCs and their blocking layer.
 * Placing the player is the caller's business (spawn or warp destination). */
static uint8_t script_flags[PC_SCRIPT_FLAG_BYTES];
static PcBag bag;                           /* found items; no screen yet */
static ScrVm script;
static const PcScriptMap *map_scripts;      /* the loaded map's bytecode, +-0 */
static int script_active;
static int script_object;                   /* actor index, for the fallback */

static const PcScriptMap *scripts_for(int index) {
    for (int i = 0; i < PC_SCRIPT_MAP_COUNT; i++) {
        if (pc_script_maps[i].map_index == index) return &pc_script_maps[i];
    }
    return 0;
}

static void load_map_tables(int index) {
    if (index < 0) index = PC_MAP_COUNT - 1;
    if (index >= PC_MAP_COUNT) index = 0;
    map_index = index;
    map_scripts = scripts_for(index);
    script_active = 0;              /* walking into a door abandons a script */
    script_object = -1;

    const PcMapDef *d = &pc_maps[index];
    tileset = &pc_tilesets[d->tileset];
    map.w = d->w;
    map.h = d->h;
    map.blocks = d->blocks;
    map.mt_tiles = tileset->mt_tiles;
    map.mt_coll = tileset->mt_coll;
    map.mt_pal = &tileset->mt_pal[0][0];
    map.cells = 0;              /* set by mark_actors(), once the NPCs exist */

    actor_count = d->object_count < OW_MAX_ACTORS ? d->object_count : OW_MAX_ACTORS;
    for (int i = 0; i < actor_count; i++) {
        const PcObject *o = &d->objects[i];
        ow_actor_init(&actors[i], o->x, o->y, o->dir, o->anim);
    }
    mark_actors();      /* before placing the player: never on top of an NPC */
}

static void load_map(int index) {
    load_map_tables(index);
    int x, y;
    map_spawn(map_index, &x, &y);
    place_player(x, y, OW_DIR_DOWN);
}

/* Map connections: the seamless way between two outdoor maps. The original
 * streams the neighbour into the map buffer's border and scrolls it in; there is
 * no border here, so the swap is a cut on the edge cell — the player is placed on
 * the opposite edge of the next map, facing the way he was walking, and the held
 * direction walks him on from there. No fade: the GB does not fade here either.
 *
 * The connection data also says where to land (the strip offset), and
 * tools/extract.py refuses a connection whose two edges share no walkable cell,
 * so a bad offset is a build error rather than an invisible wall. */
static int take_connection(int dir_held) {
    if (dir_held == OW_DIR_NONE || player.step_left) return 0;
    const PcMapDef *d = &pc_maps[map_index];
    int cx = player.px >> 4, cy = player.py >> 4;
    if (!ow_connect_on_edge(cx, cy, d->w, d->h, dir_held)) return 0;
    const PcConnection *c = ow_connect_at(d->conns, d->conn_count, dir_held);
    if (!c || c->map < 0 || c->map >= PC_MAP_COUNT) return 0;
    int px, py;
    if (!ow_connect_land(c, pc_maps[c->map].w, pc_maps[c->map].h, cx, cy, &px, &py))
        return 0;
    load_map_tables(c->map);
    place_player(px, py, dir_held);
    /* The only trace of a cut with no fade: name the map you walked into. */
    {
        char line[48], *w = line;
        for (const char *s = "connect: "; *s; s++) *w++ = *s;
        for (const char *s = pc_maps[c->map].name;
             *s && w < line + (int)sizeof line - 1; s++) *w++ = *s;
        *w = 0;
        mote->log(line);
    }
    return 1;
}

/* Warp arrival: the destination event's own cell, one cell out of the doorway
 * for a door, facing into the room indoors. */
static void load_map_at(int index, int warp_no) {
    load_map_tables(index);
    int x, y, dir;
    ow_warp_landing(pc_maps[map_index].warps, pc_maps[map_index].warp_count, warp_no,
                    landing_blocked, &x, &y, &dir);
    place_player(x, y, dir);
}

/* The textbox is the game's text bytecode, so it lives in one of these. */
static TextBox textbox;

/* The Option screen's state. It is the same data the original keeps in
 * wOptions1/wOptions2/wOptions3, and it outlives the screen on purpose: text
 * speed applies to the next textbox opened. */
static PcOptions options;

/* Text speed is the one option with something to configure today: the textbox. */
static void apply_text_speed(void) {
    textbox_set_speed(&textbox, options_text_ticks(options.v[PC_OPT_TEXT_SPEED]));
}

static void g_init(void) {
    spr_rgb = (uint16_t *)mote->alloc((uint32_t)(PC_SPRITE_COUNT * SHEET_PX) * 2);
    if (spr_rgb) {
        for (int s = 0; s < PC_SPRITE_COUNT; s++)
            for (int t = 0; t < SHEET_TILES; t++)
                sprite_expand_tile(&spr_rgb[s * SHEET_PX + t * SPR_TILE_PX2],
                                   pc_sprites[s].tiles[t], pal_npc);
    }
    player_rgb = (uint16_t *)mote->alloc((uint32_t)SHEET_PX * 2);
    if (player_rgb) {
        for (int t = 0; t < SHEET_TILES; t++)
            sprite_expand_tile(&player_rgb[t * SPR_TILE_PX2], spr_player[t], pal_npc);
    }

    options_defaults(&options);
    bag_init(&bag);

    ow_warp_init(&warp);
    load_map(PC_MAP_NEWBARKTOWN);

    mote->set_fps_limit(60);
    mote->log("polished-crystal: overworld slice ready");
}

static int held_direction(const MoteInput *in) {
    if (mote_pressed(in, MOTE_BTN_UP))    return OW_DIR_UP;
    if (mote_pressed(in, MOTE_BTN_DOWN))  return OW_DIR_DOWN;
    if (mote_pressed(in, MOTE_BTN_LEFT))  return OW_DIR_LEFT;
    if (mote_pressed(in, MOTE_BTN_RIGHT)) return OW_DIR_RIGHT;
    return OW_DIR_NONE;
}

/* ---- minimal u32 -> decimal, to log perf() without pulling in stdio ------ */
static char *u32_str(char *end, uint32_t v) {
    *--end = 0;
    do { *--end = (char)('0' + v % 10); v /= 10; } while (v);
    return end;
}

static void log_perf(void) {
    static const char *const parts[6] = { " fps=", " update_us=", " raster_us=",
                                          " flush_us=", " core0%=", " core1%=" };
    uint32_t p[6];
    mote->perf(p);
    char line[128], *w = line;
    for (const char *s = "perf"; *s; s++) *w++ = *s;
    for (int i = 0; i < 6; i++) {
        for (const char *s = parts[i]; *s; s++) *w++ = *s;
        char num[12];
        char *n = u32_str(num + sizeof num, p[i]);
        while (*n) *w++ = *n++;
    }
    *w = 0;
    mote->log(line);
}


static float walk_acc;      /* fractional pixels, so the speed is fps-independent */
static int perf_timer;

/* ---- talking ------------------------------------------------------------ */

/* What the text substitutes: the names it prints in place of <PLAYER>/<RIVAL>
 * (a save-file name is a phase 3 concern, so the port starts with the Crystal
 * defaults and says so) and, one day, the values <NUM> prints. No string the
 * port has extracted uses <NUM> yet, so the table is empty: an unregistered
 * address prints "?" rather than a made-up number. */
static const TextVars k_vars = { { "CHRIS", "SILVER" }, 0, 0 };

static int dir_dx(int d) {
    return d == OW_DIR_RIGHT ? 1 : d == OW_DIR_LEFT ? -1 : 0;
}

static int dir_dy(int d) {
    return d == OW_DIR_DOWN ? 1 : d == OW_DIR_UP ? -1 : 0;
}

/* The START menu. It is a list of the game's own entries, so it lives in a
 * struct here and the rules (open/close/confirm) in ui/menu. */
static UiMenu menu;

/* ---- running an NPC's script ---------------------------------------------
 * The one talked to runs its own bytecode from gen/scripts.h, in the same order
 * the GB would. A with an NPC in the facing cell starts it: the player must be
 * between cells to talk (mid-step never talks), and the object_event tells which
 * script it is — a label of the map's file, or a single command the engine runs
 * as a script (`OBJECTTYPE_COMMAND, jumptextfaceplayer, Text`).
 *
 * The VM never touches the world: it stops and asks. This is the part that knows
 * what a textbox, an actor and a sound are, so it maps those requests onto the
 * port. It also decides what happens with a command the port cannot run yet
 * (battle, movement of other objects, scenes): the script stops and the NPC
 * falls back to the flat text the extractor read for it, so talking never goes
 * silent halfway. */
/* The object's own text, the one the port showed before it could run scripts. */
static void talk_text(const PcObject *o) {
    if (o->text < 0 || o->text >= PC_TEXT_COUNT) return;
    textbox_open(&textbox, pc_texts[o->text].bytes, pc_texts[o->text].len, &k_vars);
    apply_text_speed();
}

static void script_run(void);

/* A command the port cannot run stops the script, and the NPC says its flat
 * text instead: half a conversation is worse than the old behaviour. The log
 * line is the only way to see it from the outside, so it names the opcode. */
static void script_fallback(unsigned opcode) {
    script_active = 0;
    char line[48], *w = line;
    for (const char *s = "script: stopped at "; *s; s++) *w++ = *s;
    for (const char *s = "0x"; *s; s++) *w++ = *s;
    const char *hex = "0123456789abcdef";
    *w++ = hex[(opcode >> 4) & 15];
    *w++ = hex[opcode & 15];
    *w = 0;
    mote->log(line);
    if (script_object >= 0 && script_object < actor_count) {
        talk_text(&pc_maps[map_index].objects[script_object]);
    }
}

/* One request: do it here and return, or run on to the next one. */
static void script_run(void) {
    for (int guard = 0; guard < 256 && script_active; guard++) {
        ScrOut out;
        ScrRun r = scr_vm_next(&script, &out);
        if (r == SCR_RUN_END) {
            script_active = 0;
            return;
        }
        if (r == SCR_RUN_ERROR) {
            script_fallback(0);       /* bytecode ran off the end, or it is dead */
            return;
        }
        switch (out.kind) {
        case SCR_EV_TEXT:
            if (out.a >= PC_TEXT_COUNT) {
                script_fallback(0xfffe);   /* text index outside pc_texts */
                return;
            }
            textbox_open(&textbox, pc_texts[out.a].bytes, pc_texts[out.a].len, &k_vars);
            apply_text_speed();
            return;                    /* the script waits for the box */
        case SCR_EV_FACE: {
            /* object 0 is the player; LAST_TALKED is the one being talked to. */
            int who = out.a == SCR_OBJECT_LAST_TALKED || out.a == 0 ? script_object : out.a;
            int dir = out.b;
            if (who >= 0 && who < actor_count) {
                if (dir == SCR_FACE_TO_PLAYER) {
                    int dx = (player.px >> 4) - (actors[who].px >> 4);
                    int dy = (player.py >> 4) - (actors[who].py >> 4);
                    dir = dx > 0 ? OW_DIR_RIGHT : dx < 0 ? OW_DIR_LEFT
                        : dy > 0 ? OW_DIR_DOWN : OW_DIR_UP;
                }
                ow_actor_face(&actors[who], dir);
            }
            break;
        }
        case SCR_EV_TEXT_OPEN:
        case SCR_EV_TEXT_CLOSE:
        case SCR_EV_SOUND:             /* no audio yet (TXT-10) */
        case SCR_EV_DELAY:             /* the original waits; nothing blocks here */
        case SCR_EV_SPRITE:            /* appear/disappear is not drawn yet */
            break;
        case SCR_EV_UNSUPPORTED:
            script_fallback(out.a);
            return;
        default:
            script_fallback(0);
            return;
        }
    }
}

static void try_talk(void) {
    if (player.step_left) return;
    int tx = (player.px >> 4) + dir_dx(player.dir);
    int ty = (player.py >> 4) + dir_dy(player.dir);
    for (int i = 0; i < actor_count; i++) {
        if ((actors[i].px >> 4) != tx || (actors[i].py >> 4) != ty) continue;
        const PcObject *o = &pc_maps[map_index].objects[i];
        ow_actor_face(&actors[i], player.dir ^ 1);   /* face the player */
        script_object = i;
        if (map_scripts && map_scripts->objects && i < map_scripts->object_count &&
                map_scripts->objects[i] != 0xffff) {
            scr_vm_start(&script, map_scripts->bytes, map_scripts->len,
                         map_scripts->objects[i], script_flags, PC_SCRIPT_FLAG_BYTES);
            script_active = 1;
            script_run();
            return;
        }
        talk_text(o);
        return;
    }

    /* Nothing of its own there: a sign, a bookshelf, a hidden item. The original
     * looks for an object_event first and a bg_event second, and a bg_event can
     * be one cell or demand a side, so the facing decides. */
    PcTrigger t;
    if (!pc_bg_event(map_scripts, tx, ty, player.dir, script_flags,
                     PC_SCRIPT_FLAG_BYTES, &t))
        return;
    if (t.item != PC_TRIGGER_NONE) {
        /* A hidden item is the engine's own script, not the map's: give the item
         * first (the cell stays readable when the bag is full, as in the
         * original) and only then mark it taken. */
        if (!bag_add(&bag, t.item, 1)) return;
        scr_flag_set(script_flags, PC_SCRIPT_FLAG_BYTES, t.flag, 1);
    }
    script_object = -1;                /* no NPC to fall back to */
    scr_vm_start(&script, map_scripts->bytes, map_scripts->len, t.script,
                 script_flags, PC_SCRIPT_FLAG_BYTES);
    script_active = 1;
    script_run();
}

static void g_update(float dt) {
    const MoteInput *in = mote->input();
    int dir_held = held_direction(in);

    /* A warp freezes the world the way a textbox does: the fade is the door, and
     * the map swaps on the frame the screen is fully white. */
    if (ow_warp_busy(&warp)) {
        ow_warp_update(&warp, dir_held);
        if (ow_warp_take_load(&warp)) load_map_at(warp.map, warp.warp);
        if (++perf_timer >= 60) { perf_timer = 0; log_perf(); }
        return;
    }

    /* Idle: this is where the pad being released re-arms the next warp. */
    ow_warp_update(&warp, dir_held);

    /* While a textbox is open the world stops, as in the original: no walking,
     * no NPC spinning, A only advances the text. */
    if (textbox_is_open(&textbox)) {
        textbox_update(&textbox, 1);
        if (mote_just_pressed(in, MOTE_BTN_A)) textbox_advance(&textbox);
        /* The box closing is the script's cue to carry on from the showtext. */
        if (!textbox_is_open(&textbox) && script_active) script_run();
        if (++perf_timer >= 60) { perf_timer = 0; log_perf(); }
        return;
    }

    /* The Option screen sits over the world like a textbox does, and takes the
     * same four buttons: UP/DOWN pick a row, LEFT/RIGHT change its value. */
    if (options_is_open(&options)) {
        if (mote_just_pressed(in, MOTE_BTN_UP))    options_move(&options, -1);
        if (mote_just_pressed(in, MOTE_BTN_DOWN))  options_move(&options, 1);
        if (mote_just_pressed(in, MOTE_BTN_LEFT))  options_cycle(&options, -1);
        if (mote_just_pressed(in, MOTE_BTN_RIGHT)) options_cycle(&options, 1);
        /* A closes on "Done"; the values already apply as you change them. */
        if (mote_just_pressed(in, MOTE_BTN_A)) options_confirm(&options);
        if (mote_just_pressed(in, MOTE_BTN_B) || mote_just_pressed(in, MOTE_BTN_MENU))
            options_close(&options);
        if (++perf_timer >= 60) { perf_timer = 0; log_perf(); }
        return;
    }

    /* The pause menu freezes the world the same way the textbox does: no
     * walking, no NPCs, and UP/DOWN only move the cursor. */
    if (ui_menu_is_open(&menu)) {
        if (mote_just_pressed(in, MOTE_BTN_UP))   ui_menu_move(&menu, -1);
        if (mote_just_pressed(in, MOTE_BTN_DOWN)) ui_menu_move(&menu, 1);
        if (mote_just_pressed(in, MOTE_BTN_A)) {
            /* A on Options opens its screen; the other entries lead to screens
             * that are not ported yet, so they close the menu and leave a log
             * line (see TASKS.md). */
            int item = ui_menu_confirm(&menu);
            if (item == PC_UI_OPTIONS) {
                options_open(&options);
            } else if (item >= 0) {
                char line[32], *w = line;
                for (const char *s = "menu: "; *s; s++) *w++ = *s;
                for (const char *s = pc_ui_labels[item].name; *s; s++) *w++ = *s;
                *w = 0;
                mote->log(line);
            }
        }
        if (mote_just_pressed(in, MOTE_BTN_B) || mote_just_pressed(in, MOTE_BTN_MENU))
            ui_menu_close(&menu);
        if (++perf_timer >= 60) { perf_timer = 0; log_perf(); }
        return;
    }

    if (mote_just_pressed(in, MOTE_BTN_MENU)) {
        ui_menu_open(&menu);
        if (++perf_timer >= 60) { perf_timer = 0; log_perf(); }
        return;
    }

    if (mote_just_pressed(in, MOTE_BTN_A)) try_talk();

    if (mote_just_pressed(in, MOTE_BTN_RB)) load_map(map_index + 1);
    if (mote_just_pressed(in, MOTE_BTN_LB)) load_map(map_index - 1);

    /* The GB walks one 16 px step in 16 frames at 60 fps, i.e. 60 px/s. Keep the
     * remainder so a fast display does not round the walk away. Running doubles
     * the pixels per frame inside ow_player; which button means "run" is the
     * Running Shoes option's business (options_run). */
    walk_acc += dt * 60.0f;
    int adv = (int)walk_acc;
    walk_acc -= (float)adv;
    int run = options_run(&options, mote_pressed(in, MOTE_BTN_B));
    ow_player_update(&player, &map, dir_held, adv, run, options_turn_frames(&options));
    ow_camera_follow(&cam, &map, player.px, player.py, SCREEN);

    /* Warps, in the original's order: a door fires on the step that ends on it,
     * a doormat when the step towards it is blocked (.CheckWarp runs after
     * TryStep). ow_warp_poll also refuses to fire twice before the pad is
     * released, so arriving on a mat cannot bounce you straight back. */
    {
        const PcMapDef *d = &pc_maps[map_index];
        ow_warp_poll(&warp, d->warps, d->warp_count, player.px >> 4, player.py >> 4,
                     player.dir, player.just_arrived, player.bumping);
        /* The edge, unless a door won the frame. A warp fades; a connection cuts. */
        if (!ow_warp_busy(&warp)) take_connection(dir_held);
    }

    /* Actors animate per frame, not per pixel walked: a spinner keeps turning
     * while the player stands still, exactly like OBJECT_ACTION_SPIN. */
    for (int i = 0; i < actor_count; i++) ow_actor_update(&actors[i], 1);

    /* One perf line per second: fps, update, raster, flush, core0%, core1%. */
    if (++perf_timer >= 60) {
        perf_timer = 0;
        log_perf();
    }
}

static void g_band(uint16_t *fb, int y0, int y1) {
    /* Indoors the game swaps to the `indoor` palette group at any hour. */
    const uint16_t (*pals)[4] = pc_maps[map_index].pal ? bg_pal_indoor : bg_pal;
    ow_render_band(fb, SCREEN, SCREEN, y0, y1, &map, tileset->tiles,
                   pals, cam.x, cam.y);
}

/* ---- sprites (core0, after the bands) ----------------------------------- */
#define MAX_DRAW (OW_MAX_ACTORS + 1)

static void g_overlay(uint16_t *fb) {
    /* NPCs and the player are drawn together, sorted by feet y, so a sprite
     * lower on the map covers one higher up — the GB's OAM ordering. */
    int feet[MAX_DRAW], slot_of[MAX_DRAW], n = 0;
    for (int i = 0; i < actor_count; i++) {
        int sx = actors[i].px - cam.x, sy = actors[i].py - cam.y;
        if (sx <= -16 || sx >= SCREEN || sy <= -16 || sy >= SCREEN) continue;
        feet[n] = actors[i].py;          /* the feet, not the top: that is what
                                            decides who stands in front */
        slot_of[n++] = i;
    }
    int player_slot = -1;
    {
        int sx = player.px - cam.x, sy = player.py - cam.y;
        if (!(sx <= -16 || sx >= SCREEN || sy <= -16 || sy >= SCREEN)) {
            feet[n] = player.py;
            player_slot = n;
            slot_of[n++] = -1;           /* -1 = the player */
        }
    }

    int order[MAX_DRAW];
    ow_actor_depth_sort(feet, n, order);

    for (int k = 0; k < n; k++) {
        int slot = order[k];
        /* On purpose no special case for the NPC being talked to: the textbox is
         * drawn after every sprite, so it covers the bottom of the screen like
         * the original's window does. */
        if (slot == player_slot) {
            if (player_rgb)
                sprite_draw_frame(fb, SCREEN, SCREEN, player_rgb,
                                  ow_player_sheet_frame(&player),
                                  player.px - cam.x, player.py - cam.y,
                                  ow_player_flip(&player));
            continue;
        }
        int idx = slot_of[slot];
        if (spr_rgb && idx >= 0 && idx < PC_SPRITE_COUNT)
            sprite_draw_frame(fb, SCREEN, SCREEN, &spr_rgb[idx * SHEET_PX],
                              ow_actor_sheet_frame(&actors[idx]),
                              actors[idx].px - cam.x, actors[idx].py - cam.y,
                              ow_actor_flip(&actors[idx]));
    }

    /* The window goes last: it is the bottom of the screen, over map and sprites.
     * Palettes 7 (text) are the game's own white-on-black pair. */
    if (textbox_is_open(&textbox))
        textbox_draw(&textbox, fb, SCREEN, 0, SCREEN, 0, SCREEN - TB_H,
                     bg_pal[7][3], bg_pal[7][0]);
    /* The pause menu goes over the world too, on the right like the original. */
    if (ui_menu_is_open(&menu))
        ui_menu_draw(&menu, &k_vars.names, fb, SCREEN, 0, SCREEN,
                     bg_pal[7][3], bg_pal[7][0]);
    /* The option screen covers the whole panel, like the original's, which hides
     * the map behind its own background. */
    if (options_is_open(&options))
        options_draw(&options, fb, SCREEN, 0, SCREEN, bg_pal[7][3], bg_pal[7][0]);

    /* The door transition covers everything drawn this frame, sprites and UI
     * included: the original fades BG and OBJ palettes alike. */
    ow_fade_apply(fb, SCREEN * SCREEN, ow_warp_fade(&warp));
}

static const MoteGameVtbl k_vtbl = {
    .init = g_init,
    .update = g_update,
    .render_band = g_band,
    .overlay = g_overlay,
    .config = { 0 },
};

static const MoteGameVtbl *mote_game_vtbl(void) { return &k_vtbl; }
