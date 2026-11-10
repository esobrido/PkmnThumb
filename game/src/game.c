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
 *   ow/actor   NPC facing, walk, spin, Y-sort tests/unit/test_ow_actor.c
 *   ow/move    movement streams and walking patterns tests/unit/test_ow_move.c
 *   ow/render  the banded raster             tests/unit/test_ow_render.c
 *   ow/connect walking between two maps      tests/unit/test_ow_connect.c
 *   ow/clock   the in-game clock and the palette group an hour picks
 *                                            tests/unit/test_ow_clock.c
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
#include "ow/move.h"
#include "ow/render.h"
#include "ow/warp.h"
#include "ow/connect.h"
#include "ow/clock.h"
#include "gfx_sprite.h"
#include "gen/text.h"
#include "gen/scripts.h"
#include "bag.h"
#include "script/vm.h"
#include "script/trigger.h"
#include "script/scene.h"
#include "text/box.h"
#include "ui/menu.h"
#include "ui/options.h"

MOTE_GAME_MODULE();

/* Name shown by the launcher, and the version baked into the .mote so the
 * gallery can tell an installed build from a newer one. Bump both when a
 * phase lands: the artefact name made by scripts/export.sh comes from here. */
MOTE_GAME_META("Polished Crystal", "fan port");
MOTE_GAME_VERSION("0.21.0");
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
/* How each NPC moves when no script is driving it, and the scripted movement in
 * progress. `move_wait` is the object a running script is waiting on: -1 nobody,
 * MOVER_PLAYER the player, else the actor index. Only one scripted movement runs
 * at a time, because the VM does not advance until it ends. */
static OwMove movers[OW_MAX_ACTORS];        /* applymovement, per NPC */
static OwWander wanders[OW_MAX_ACTORS];     /* SPRITEMOVEDATA_WALK_*, per NPC */
static int follow_of[OW_MAX_ACTORS];        /* leader actor index, or -1 */
static OwMove player_move;                  /* applymovement PLAYER */
static int moving_player;                   /* the player's step came from a script */
static int player_follower_of = -1;         /* `follow X, PLAYER` */
static int move_wait = -1;
static int move_timer;                      /* frames left of a step_sleep */
static OwWarp warp;                       /* the door transition in progress */

/* The clock: the original's RTC has no counterpart on the Thumby (no battery, and
 * Mote's API gives a frame delta and nothing else), so the time of day is the time
 * played, at the no-RTC patch's 6x. It starts at 10:00, inside DAY, because there
 * is no time-set screen to ask. */
static OwClock clock_state;
static float clock_acc;                   /* fractional milliseconds carried over */
static int tod_shown = -1;                /* the phase the palettes are showing */
/* An object_event's time mask hides an NPC outside its hours (a schoolboy who is
 * only out in the morning); the index stays put so the scripts keep pointing at
 * the right object. */
static uint8_t actor_hidden[OW_MAX_ACTORS];

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
        if (actor_hidden[i]) continue;
        int cx = actors[i].px >> 4, cy = actors[i].py >> 4;
        if (cx >= 0 && cx < cw && cy >= 0 && cy < map.h * 2)
            occupied[cy * cw + cx] = 1;
    }
    map.cells = occupied;
}

/* Which NPCs are out at this hour, from each object's time-of-day bits
 * (home/map_objects.asm CheckObjectTime: one bit per phase). The blocking layer
 * follows, so a hidden NPC blocks nothing. */
static void refresh_actors(void) {
    int tod = ow_clock_tod(clock_state.hour);
    for (int i = 0; i < actor_count; i++)
        actor_hidden[i] = !ow_clock_object_out(pc_maps[map_index].objects[i].time_mask,
                                               tod);
    for (int i = actor_count; i < OW_MAX_ACTORS; i++) actor_hidden[i] = 0;
    mark_actors();
}

/* Everything a map load shares: the tables, the NPCs and their blocking layer.
 * Placing the player is the caller's business (spawn or warp destination). */
static uint8_t script_flags[PC_SCRIPT_FLAG_BYTES];
static PcBag bag;                           /* found items; no screen yet */
static ScrVm script;
static const PcScriptMap *map_scripts;      /* the loaded map's bytecode, +-0 */
static int script_active;
static int script_object;                   /* actor index, for the fallback */
static int script_slot = -1;                /* map_scripts' place in pc_script_maps */
/* One scene byte per map, as the original keeps them in WRAM (data/maps/scenes.asm).
 * A new game starts with every map in scene 0, which is the scene a map's first
 * entry is, and `setscene`/`setmapscene` move it on from a script. map_scene is
 * the loaded map's view of that array, handed to the VM and to the coord_event
 * lookup; the tables are passed by pointer, never copied. */
static int8_t scenes[PC_SCRIPT_MAP_COUNT];
static ScrScenes map_scene;

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
    script_slot = map_scripts ? (int)(map_scripts - pc_script_maps) : -1;
    map_scene.bytes = scenes;
    map_scene.slot = (uint8_t)(script_slot < 0 ? 0 : script_slot);
    map_scene.scene_count = map_scripts ? map_scripts->scene_count : 0;
    map_scene.maps = pc_scene_maps;
    map_scene.map_count = PC_SCENE_MAP_COUNT;
    script_active = 0;              /* walking into a door abandons a script */
    script_object = -1;
    /* Scripted movement does not survive a map change: the blob it points into
     * is the one being replaced. */
    ow_move_reset(&player_move);
    moving_player = 0;
    player_follower_of = -1;
    move_wait = -1;
    move_timer = 0;
    for (int i = 0; i < OW_MAX_ACTORS; i++) {
        ow_move_reset(&movers[i]);
        ow_wander_init(&wanders[i]);
        follow_of[i] = -1;
    }

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
    refresh_actors();   /* before placing the player: never on top of an NPC */
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
    ow_clock_init(&clock_state, 10, 0);     /* the port has no time-set screen */

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
static void script_begin(uint16_t offset, int obj);

/* Scripts here nest only through `sdefer`; four levels is far more than the
 * original's one deferred slot and stops a script that defers itself. */
static int script_depth;

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

/* ---- scripted movement ---------------------------------------------------
 * `applymovement` hands the VM's caller a stream of steps, and the script does
 * not advance until the last `step_end`: the GB waits the same way (wScriptMode
 * stays in SCRIPT_READ_COMMANDS and the object's step function drives the walk).
 * The stream lives inside the map's own script blob, so there is nothing to
 * load or free — only a cursor (ow/move).
 *
 * While a movement runs the pad does nothing: the world is frozen exactly like an
 * open textbox, which is also what makes the recipes in docs/05 deterministic.
 */
#define MOVER_PLAYER (-2)

/* Can an NPC step onto this cell? Its own cell is in `occupied`, but the
 * destination is not, so this is the tiles, the other NPCs and the player. */
static int actor_cell_blocked(int px, int py) {
    if (px < 0 || py < 0 || (px >> 4) >= map.w * 2 || (py >> 4) >= map.h * 2)
        return 1;
    if (ow_px_blocked(&map, px, py)) return 1;
    return player.px == px && player.py == py;
}

/* The log line is the only way to see an unsupported step from the outside. */
static void move_log_opcode(unsigned opcode) {
    char line[40], *w = line;
    for (const char *s = "move: stopped at "; *s; s++) *w++ = *s;
    for (const char *s = "0x"; *s; s++) *w++ = *s;
    const char *hex = "0123456789abcdef";
    *w++ = hex[(opcode >> 4) & 15];
    *w++ = hex[opcode & 15];
    *w = 0;
    mote->log(line);
}

/* `follow leader, follower`: the follower walks into the cell the leader has just
 * left, once the leader lands there. Blocked means it lags a step behind, which
 * is what the original's FOLLOWNOTEXACT does when it cannot keep up. */
static void follow_arrivals(int leader) {
    int dir = actors[leader].dir;
    int dx = dir_dx(dir) * OW_STEP_PX, dy = dir_dy(dir) * OW_STEP_PX;
    for (int i = 0; i < actor_count; i++) {
        if (follow_of[i] != leader || actors[i].step_left) continue;
        if (actor_cell_blocked(actors[i].px + dx, actors[i].py + dy)) continue;
        ow_actor_start_step(&actors[i], dir, OW_STEP_NORMAL);
    }
    if (player_follower_of == leader && !player.step_left &&
            !ow_px_blocked(&map, player.px + dx, player.py + dy)) {
        player.dir = dir;
        ow_player_try_step(&player, &map, dir);
    }
}

/* Which object a script means: 0 is the player in the original's numbering, and
 * the object ids of object_const_def count from 1, so actor N is object N + 1. */
static int object_actor(int obj) {
    if (obj == 0) return MOVER_PLAYER;
    if (obj == SCR_OBJECT_LAST_TALKED) return script_object;
    return obj - 1;
}

/* Start a movement stream for `obj` and park the script on it. */
static void script_move_start(int obj, uint16_t offset) {
    if (!map_scripts || offset >= map_scripts->len) {
        script_fallback(0xfffd);        /* offset outside the map's bytecode */
        return;
    }
    int who = object_actor(obj);
    if (who != MOVER_PLAYER && (who < 0 || who >= actor_count)) {
        script_fallback(0xfffc);        /* no such object on this map */
        return;
    }
    if (who == MOVER_PLAYER) {
        ow_move_start(&player_move, map_scripts->bytes + offset,
                      (uint16_t)(map_scripts->len - offset));
    } else {
        ow_move_start(&movers[who], map_scripts->bytes + offset,
                      (uint16_t)(map_scripts->len - offset));
    }
    move_wait = who;
    move_timer = 0;
}

/* Do one frame of the movement the script is waiting for. Returns 1 when it is
 * over, so the caller can let the script run on. */
static int movement_step(int adv) {
    if (adv < 1) adv = 1;

    /* A walk already under way finishes first, whichever object owns it: the
     * player may still be following the NPC the stream is moving. */
    if (player.step_left) {
        ow_player_update(&player, &map, OW_DIR_NONE, adv, 0,
                         options_turn_frames(&options));
        if (player.step_left) return 0;
        moving_player = 0;
        mark_actors();
    }
    if (move_wait != MOVER_PLAYER) {
        OwActor *a = &actors[move_wait];
        if (a->step_left) {
            ow_actor_update(a, 1);
            if (a->step_left) return 0;
            mark_actors();
            follow_arrivals(move_wait);
        }
    }

    if (move_timer > 0) {
        move_timer -= adv;
        return 0;
    }

    OwMove *m = move_wait == MOVER_PLAYER ? &player_move : &movers[move_wait];
    OwActor *a = move_wait == MOVER_PLAYER ? 0 : &actors[move_wait];
    OwMoveOut out;
    for (;;) {
        switch (ow_move_next(m, &out)) {
        case OW_MOVE_END:
            return player.step_left ? 0 : 1;
        case OW_MOVE_TURN:
            if (a) ow_actor_face(a, out.dir);
            else player.dir = out.dir;
            break;
        case OW_MOVE_SHOW:
            /* show_object/hide_object: the port has no sprite visibility yet, and
             * a scene that needs it (Lyra's entrance) is SCR-07. */
            break;
        case OW_MOVE_WAIT:
            move_timer = out.frames ? out.frames : 1;
            return 0;
        case OW_MOVE_STEP: {
            int dx = dir_dx(out.dir) * OW_STEP_PX;
            int dy = dir_dy(out.dir) * OW_STEP_PX;
            if (!a) {
                player.dir = out.dir;
                if (!ow_player_try_step(&player, &map, out.dir)) {
                    ow_move_stop(m);        /* blocked: the movement is over */
                    return 1;
                }
                moving_player = 1;
                return 0;
            }
            if (actor_cell_blocked(a->px + dx, a->py + dy)) {
                ow_move_stop(m);
                return 1;
            }
            ow_actor_start_step(a, out.dir, out.frames);
            mark_actors();
            return 0;
        }
        case OW_MOVE_UNSUPPORTED:
            move_log_opcode(out.opcode);
            ow_move_stop(m);
            return 1;
        default:
            ow_move_stop(m);
            return 1;
        }
    }
}

/* One request: do it here and return, or run on to the next one. */
static void script_run(void) {
    for (int guard = 0; guard < 256 && script_active; guard++) {
        ScrOut out;
        ScrRun r = scr_vm_next(&script, &out);
        if (r == SCR_RUN_END) {
            script_active = 0;
            /* `sdefer` queued a follow-up script: it runs now, as the original's
             * deferred script does between script frames. */
            uint16_t next;
            if (script_depth < 4 && scr_vm_deferred(&script, &next)) {
                script_begin(next, script_object);
            }
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
        case SCR_EV_MOVE:
            script_move_start(out.b, out.a);
            return;                    /* the script waits for the last step */
        case SCR_EV_FOLLOW: {
            /* out.a is the leader, out.b the follower (Script_follow). */
            int leader = object_actor(out.a);
            int follower = object_actor(out.b);
            if (leader == MOVER_PLAYER) break;      /* the player leads: no follower */
            if (follower == MOVER_PLAYER) player_follower_of = leader;
            else if (follower >= 0 && follower < actor_count) follow_of[follower] = leader;
            break;
        }
        case SCR_EV_STOPFOLLOW:
            for (int i = 0; i < OW_MAX_ACTORS; i++) follow_of[i] = -1;
            player_follower_of = -1;
            break;
        /* `writetext`: the box goes up and the script keeps running behind it. */
        case SCR_EV_TEXT_NOWAIT:
            if (out.a >= PC_TEXT_COUNT) {
                script_fallback(0xfffe);
                return;
            }
            textbox_open(&textbox, pc_texts[out.a].bytes, pc_texts[out.a].len, &k_vars);
            apply_text_speed();
            break;

        /* `waitbutton`: the script waits for the player to read the box out. The
         * box closes itself on the last A (see the textbox branch of g_update),
         * which is the same signal `showtext` waits for; a wait with no box up is
         * already satisfied — the original would still ask for a button there,
         * but a script cannot reach a wait without having printed something. */
        case SCR_EV_TEXT_WAIT:
            if (textbox_is_open(&textbox)) return;
            break;

        case SCR_EV_TEXT_END:          /* `endtext`: closetext, then end */
            textbox_close(&textbox);
            break;

        /* `moveobject`: an object changes cell without walking there. */
        case SCR_EV_PLACE: {
            int who = object_actor(out.b);
            int cx = (int)(out.a & 0xff) << 4;
            int cy = (int)(out.a >> 8) << 4;
            if (who == MOVER_PLAYER) {
                place_player(cx >> 4, cy >> 4, player.dir);
            } else if (who >= 0 && who < actor_count) {
                ow_actor_init(&actors[who], (uint8_t)cx, (uint8_t)cy,
                              actors[who].dir, pc_maps[map_index].objects[who].anim);
                mark_actors();
            }
            break;
        }

        /* `reloadmap`: rebuild the map the player is standing on, in place. */
        case SCR_EV_RELOADMAP: {
            int cx = player.px >> 4, cy = player.py >> 4;
            int dir = player.dir;
            load_map_tables(map_index);
            place_player(cx, cy, dir);
            break;
        }

        case SCR_EV_TEXT_OPEN:
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

/* Runs a script of the loaded map from `offset`, with the map's scene state and
 * `obj` as the object under discussion (an actor index, or -1 when the trigger
 * came from a cell and there is nobody to fall back to). */
static void script_begin(uint16_t offset, int obj) {
    script_object = obj;
    scr_vm_start(&script, map_scripts->bytes, map_scripts->len, offset,
                 script_flags, PC_SCRIPT_FLAG_BYTES);
    scr_vm_set_scene(&script, &map_scene);
    script_active = 1;
    script_depth++;
    script_run();
    script_depth--;
}

static void try_talk(void) {
    if (player.step_left) return;
    int tx = (player.px >> 4) + dir_dx(player.dir);
    int ty = (player.py >> 4) + dir_dy(player.dir);
    for (int i = 0; i < actor_count; i++) {
        if (actor_hidden[i]) continue;
        if ((actors[i].px >> 4) != tx || (actors[i].py >> 4) != ty) continue;
        const PcObject *o = &pc_maps[map_index].objects[i];
        ow_actor_face(&actors[i], player.dir ^ 1);   /* face the player */
        if (map_scripts && map_scripts->objects && i < map_scripts->object_count &&
                map_scripts->objects[i] != 0xffff) {
            script_begin(map_scripts->objects[i], i);
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
    script_begin(t.script, -1);        /* no NPC to fall back to */
}

static void g_update(float dt) {
    const MoteInput *in = mote->input();
    int dir_held = held_direction(in);

    /* Time passes whatever is on screen — the RTC kept running through a textbox
     * or a menu in the original too. A phase change re-picks the palette group (see
     * g_band) and re-checks which NPCs are out, and is logged once so a capture
     * shows which hour it was taken at. */
    clock_acc += dt * 1000.0f;
    int cms = (int)clock_acc;
    clock_acc -= (float)cms;
    ow_clock_advance(&clock_state, (uint32_t)cms);
    {
        int tod = ow_clock_tod(clock_state.hour);
        if (tod != tod_shown) {
            static const char *const names[4] = { "morn", "day", "nite", "eve" };
            tod_shown = tod;
            refresh_actors();
            char line[24], *w = line;
            for (const char *p = "tod: "; *p; p++) *w++ = *p;
            for (const char *p = names[tod]; *p; p++) *w++ = *p;
            *w = 0;
            mote->log(line);
        }
    }

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

    /* A movement a script is waiting for freezes the world like an open textbox:
     * the pad walks nobody, a follower steps when its leader lands, and the
     * script resumes on the frame the movement ends. */
    if (script_active && move_wait != -1) {
        walk_acc += dt * 60.0f;
        int adv = (int)walk_acc;
        walk_acc -= (float)adv;
        if (movement_step(adv)) {
            move_wait = -1;
            move_timer = 0;
            script_run();
        }
        ow_camera_follow(&cam, &map, player.px, player.py, SCREEN);
        for (int i = 0; i < actor_count; i++) ow_actor_update(&actors[i], 1);
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

    /* A coord_event, last in the step's order: it fires on the frame the step
     * ends, on the cell the player just reached, and only while the map is in
     * the scene it names. A door on the same cell wins — the screen is already
     * fading out — and a running script is never cut in by a new trigger. */
    if (map_scripts && player.just_arrived && !ow_warp_busy(&warp) && !script_active) {
        uint16_t at;
        int scene = pc_scene_active(&map_scene);
        if (pc_coord_event(map_scripts, player.px >> 4, player.py >> 4, scene, &at))
            script_begin(at, -1);
    }

    /* Actors animate per frame, not per pixel walked: a spinner keeps turning
     * while the player stands still, exactly like OBJECT_ACTION_SPIN. An NPC with
     * a walking pattern takes one step when its own timer says so, and waits
     * again if the cell is taken — the original's RandomWalkContinue. */
    for (int i = 0; i < actor_count; i++) {
        int was_walking = ow_actor_walking(&actors[i]);
        ow_actor_update(&actors[i], 1);
        /* An NPC that lands moves its blocking cell: rebuild the grid on every
         * arrival, or its old cell stays occupied and it can never walk back
         * into it (nor can anyone else use it). */
        if (was_walking && !ow_actor_walking(&actors[i])) mark_actors();
        if (actor_hidden[i] || actors[i].step_left || ow_move_running(&movers[i]))
            continue;
        int d = ow_wander_next(&wanders[i], &actors[i],
                               pc_maps[map_index].objects[i].move);
        if (d == OW_DIR_NONE) continue;
        int nx = actors[i].px + dir_dx(d) * OW_STEP_PX;
        int ny = actors[i].py + dir_dy(d) * OW_STEP_PX;
        if (actor_cell_blocked(nx, ny)) continue;
        ow_actor_start_step(&actors[i], d, OW_STEP_NORMAL);
        mark_actors();
    }

    /* One perf line per second: fps, update, raster, flush, core0%, core1%. */
    if (++perf_timer >= 60) {
        perf_timer = 0;
        log_perf();
    }
}

/* The palette group the loaded map shows at this hour: the map's own time field
 * and environment decide (ow/clock.h), which is why a house is bright by day and
 * a route is not. */
static const uint16_t (*map_pals(void))[4] {
    int g = ow_clock_group(pc_maps[map_index].pal, pc_maps[map_index].indoor,
                           clock_state.hour);
    return g == OW_TOD_INDOOR ? bg_pal_indoor : bg_pal[g];
}

static void g_band(uint16_t *fb, int y0, int y1) {
    ow_render_band(fb, SCREEN, SCREEN, y0, y1, &map, tileset->tiles,
                   map_pals(), cam.x, cam.y);
}

/* ---- sprites (core0, after the bands) ----------------------------------- */
#define MAX_DRAW (OW_MAX_ACTORS + 1)

static void g_overlay(uint16_t *fb) {
    /* NPCs and the player are drawn together, sorted by feet y, so a sprite
     * lower on the map covers one higher up — the GB's OAM ordering. */
    int feet[MAX_DRAW], slot_of[MAX_DRAW], n = 0;
    for (int i = 0; i < actor_count; i++) {
        if (actor_hidden[i]) continue;
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
     * Slot 7 is the game's text palette, the same white-on-black in all four
     * groups, so the menus are pinned to `day` and never flicker with the hour. */
    if (textbox_is_open(&textbox))
        textbox_draw(&textbox, fb, SCREEN, 0, SCREEN, 0, SCREEN - TB_H,
                     bg_pal[OW_TOD_DAY][7][3], bg_pal[OW_TOD_DAY][7][0]);
    /* The pause menu goes over the world too, on the right like the original. */
    if (ui_menu_is_open(&menu))
        ui_menu_draw(&menu, &k_vars.names, fb, SCREEN, 0, SCREEN,
                     bg_pal[OW_TOD_DAY][7][3], bg_pal[OW_TOD_DAY][7][0]);
    /* The option screen covers the whole panel, like the original's, which hides
     * the map behind its own background. */
    if (options_is_open(&options))
        options_draw(&options, fb, SCREEN, 0, SCREEN,
                     bg_pal[OW_TOD_DAY][7][3], bg_pal[OW_TOD_DAY][7][0]);

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
