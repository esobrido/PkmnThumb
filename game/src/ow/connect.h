/*
 * ow/connect.h — map connections: walking off one map and into the next.
 *
 * Pure logic: no Mote, no globals, no allocation. The connection table comes
 * from the map's own `connection` rows (data/maps/attributes.asm) through
 * gen/world.h; the caller passes the destination's size, so this module never
 * needs to know which map is which.
 *
 * A connection is a shared edge, not a door: the two maps are laid out next to
 * each other and the edge cells touch. The original streams the neighbour into
 * the invisible border of the map buffer (wOverworldMapBlocks) and scrolls it
 * in, so there is no fade and no special tile — you simply keep walking. The
 * port has no shared border buffer (each map is drawn on its own), so the swap is
 * a cut: the player is placed on the opposite edge cell of the next map, facing
 * the same way, and the walk carries on from there.
 *
 * The offset is the one number that makes the two edges line up. The macro turns
 * `connection west, Route29, ROUTE_29, 0` into a strip offset of `offset * -2`
 * cells, and the engine adds it to the player's coordinate when the edge is
 * crossed (EnterWestConnection and friends in engine/overworld/warp_connection.asm).
 * So it is measured in *blocks* even though the result is in cells: a connection
 * with offset 4 lands the player 8 cells along the destination's edge.
 */
#ifndef OW_CONNECT_H
#define OW_CONNECT_H

#include "gen/world.h"
#include "ow/dir.h"
#include "ow/map.h"

/* The connection that leaves this map in `dir`, or NULL. */
const PcConnection *ow_connect_at(const PcConnection *conns, int count, int dir);

/* 1 when (cx, cy) is the cell of the edge `dir` leaves through. `w` and `h` are
 * the map's size in blocks, as PcMapDef has it. Standing in the middle of the
 * map and pushing right does nothing: the edge has to be reached first. */
int ow_connect_on_edge(int cx, int cy, int w, int h, int dir);

/* Where the player lands, in pixels, when the step out of (cx, cy) in `dir` goes
 * through `c`. `dest_w`/`dest_h` are the destination's size in blocks. Returns 0
 * for a shut connection (map < 0) or a landing cell outside the destination, so
 * the caller can leave the edge solid instead of reading past the map. */
int ow_connect_land(const PcConnection *c, int dest_w, int dest_h, int cx, int cy,
                    int *px, int *py);

#endif
