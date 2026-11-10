/*
 * script/scene.h — a map's scene, and the cells that fire on it.
 *
 * Every map may keep one byte of "scene" state, and that byte is an index into
 * the map's `def_scene_scripts` table (`scene_const`/`scene_script`, in file
 * order). The original keeps those bytes in WRAM, one per map, in the MapScenes
 * table (data/maps/scenes.asm) — a map not listed there has no scene at all and
 * every scene test answers -1. The bytes belong to the caller; this module is
 * the only thing that reads or writes them, so "which map's scene is this?" has
 * a single answer, and the generated tables are only ever pointed at, never
 * copied (a second copy of pc_script_maps would drag the whole script blob with
 * it — see docs/06).
 *
 * A coord event is a cell that runs a script when a step *ends* on it while the
 * map is in a given scene. That is the whole trigger: the original's
 * CheckCurrentMapCoordEvents (home/map.asm) walks the table comparing the cell
 * and the scene, and SCENE_ALWAYS (-1) means the scene does not matter. Three of
 * the eight maps in the build have no scene table at all (the neighbours'
 * house, Lyra's house and Elm's house), so on those only a SCENE_ALWAYS cell
 * could ever fire.
 */
#ifndef PC_SCRIPT_SCENE_H
#define PC_SCRIPT_SCENE_H

#include <stdint.h>

#include "gen/scripts.h"

/* What a scene test answers when the map has no scene table: SCENE_ALWAYS is
 * -1 as well, but that value lives in a coord event's own field, never here. */
#define PC_SCENE_NONE (-1)

/* The scene state of one map, plus the map-id table that setmapscene and
 * checkmapscene need to reach another map. `bytes` has one entry per map, in
 * pc_script_maps order; `slot` and `scene_count` are the running map's. */
typedef struct {
    int8_t *bytes;             /* one scene byte per map, owned by the caller */
    uint8_t slot;              /* the running map's position in pc_script_maps */
    uint8_t scene_count;      /* and its scene table size (0: it has none) */
    const PcSceneMap *maps;    /* the maps that do have one (pc_scene_maps) */
    uint8_t map_count;         /* how many of those there are */
} ScrScenes;

/* The running map's scene, or PC_SCENE_NONE. `s` may be NULL. */
int pc_scene_active(const ScrScenes *s);

/* Writes the running map's scene. A map with no scene table is not an error but
 * a no-op, exactly like GetMapSceneID failing in the original: `setscene` there
 * does nothing rather than growing state out of nowhere. */
void pc_scene_set(ScrScenes *s, int scene);

/* The slot for the map id `map_id MAP` writes into a script (its group and map
 * number), or -1 when that map has no scene table — which is what makes
 * `setmapscene` of a map outside the build harmless. */
int pc_scene_slot(const ScrScenes *s, int group, int number);

/* The scene byte of `slot`, which is taken from `s->maps`, so it always has a
 * scene table. */
int pc_scene_at(const ScrScenes *s, int slot);

/* 1 and `*script` filled when ending a step on (x, y) runs a script in scene
 * `scene`; 0 when nothing fires there. A cell whose script the extractor could
 * not resolve ($ffff) fires nothing. */
int pc_coord_event(const PcScriptMap *sm, int x, int y, int scene, uint16_t *script);

#endif
