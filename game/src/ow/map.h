/*
 * ow/map.h — map geometry and collision queries.
 *
 * Pure logic: no Mote, no globals, no allocation. The data tables are passed in
 * by the caller (in the game they are the generated gen/ tables; in tests
 * they are small synthetic ones), so this module is unit-testable on the host
 * without the engine or the emulator.
 *
 * Units, identical to the Game Boy:
 *   block ("metatile") = 32x32 px = 4x4 tiles of 8x8
 *   walking cell       = 16x16 px; four of them per block (TL, TR, BL, BR)
 */
#ifndef OW_MAP_H
#define OW_MAP_H

#include <stdint.h>

#define OW_BLOCK_PX 32
#define OW_STEP_PX  16

/* A map view: the block grid plus the tileset's metatile tables. */
typedef struct {
    int w, h;                      /* size in blocks */
    const uint8_t *blocks;         /* w*h block ids, row-major */
    const uint8_t (*mt_tiles)[16]; /* per metatile: 16 tile ids */
    const uint8_t *mt_coll;        /* per metatile: 4 bits, TL=0 TR=1 BL=2 BR=3 */
    const uint8_t *mt_grass;       /* per metatile: tall/long grass, same 4 bits
                                      (may be NULL: no map here has grass) */
    const uint8_t *mt_pal;         /* per metatile: 16 palette slots (may be NULL) */
    const uint8_t *cells;          /* optional per-cell blockers, row-major over
                                      (w*2) x (h*2); NULL = tiles only. NPCs
                                      live here: they are not in the tileset's
                                      collision, but you cannot walk through one. */
} OwMap;

/* Block id at (bx, by); out-of-bounds reads as 0 (the maps' own border block). */
int ow_block_at(const OwMap *m, int bx, int by);

/* Is the 16x16 walking cell (cx, cy) blocked? Out of bounds counts as blocked. */
int ow_cell_blocked(const OwMap *m, int cx, int cy);

/* Same query in pixels; (px, py) must be aligned to the 16 px grid. */
int ow_px_blocked(const OwMap *m, int px, int py);

/* Is the 16x16 walking cell (cx, cy) tall or long grass? A grass cell is
 * walkable, so this reads the collision *code* the tileset carries, not the
 * blocked mask. Out of bounds, or a map with no grass table, is "no". */
int ow_cell_grass(const OwMap *m, int cx, int cy);

/* Same query in pixels. */
int ow_px_grass(const OwMap *m, int px, int py);

#endif
