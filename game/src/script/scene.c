#include "script/scene.h"

/* Nothing in this file names pc_script_maps or pc_scene_maps: they arrive
 * through the struct. A reference here would make the linker keep a second copy
 * of each map's whole bytecode and label table in this translation unit, which
 * cost 4.9 KB of flash the first time it happened (docs/06). */

int pc_scene_active(const ScrScenes *s) {
    if (!s || !s->bytes || s->scene_count == 0) return PC_SCENE_NONE;
    return s->bytes[s->slot];
}

void pc_scene_set(ScrScenes *s, int scene) {
    if (!s || !s->bytes || s->scene_count == 0) return;
    s->bytes[s->slot] = (int8_t)(scene & 0xff);
}

int pc_scene_slot(const ScrScenes *s, int group, int number) {
    if (!s) return -1;
    for (int i = 0; i < s->map_count; i++) {
        if (s->maps[i].group == (uint8_t)group &&
                s->maps[i].number == (uint8_t)number)
            return s->maps[i].slot;
    }
    return -1;
}

int pc_scene_at(const ScrScenes *s, int slot) {
    if (!s || !s->bytes || slot < 0 || slot >= PC_SCRIPT_MAP_COUNT)
        return PC_SCENE_NONE;
    return s->bytes[slot];
}

int pc_coord_event(const PcScriptMap *sm, int x, int y, int scene, uint16_t *script) {
    if (!sm || !sm->coords) return 0;
    for (int i = 0; i < sm->coord_count; i++) {
        const PcCoordEvent *e = &sm->coords[i];
        if (e->x != x || e->y != y) continue;
        /* One cell can hold several events, one per scene: a mismatch is not the
         * end of the search, or an always-fire cell behind a scene cell would
         * never be reached. */
        if (e->scene >= 0 && e->scene != scene) continue;
        if (e->script == 0xffff) return 0;
        if (script) *script = e->script;
        return 1;
    }
    return 0;
}
