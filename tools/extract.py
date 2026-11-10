#!/usr/bin/env python3
"""
extract.py — Polished Crystal -> C header extraction for the Mote (Thumby Color) port.

This is the "reuse the content, rewrite the engine" pipeline: everything the
Game Boy ROM ships as *data* (map block ids, metatile tables, collision lists,
2bpp tile graphics, overworld sprite frames) is converted here into plain C
tables that the native Mote renderer draws. No GBZ80 code is translated.

Formats (see the Polished Crystal sources under vendor/polishedcrystal):

  maps/<Map>.ablk                     width*height raw block ids (one byte per block)
  data/maps/maps.asm                  `map <Map>, TILESET_<X>, ...` -> tileset name
  constants/map_constants.asm         `map_const <MAP>, <w>, <h>` (blocks)
  data/tilesets/<ts>_metatiles.bin    16 tile ids per metatile (4x4 8x8 tiles)
  data/tilesets/<ts>_collision.asm    `tilecoll BL, TL, BR, TR` per metatile
  gfx/tilesets/<ts>.<common>.png      the combined 16-tiles-wide 2bpp sheet
  gfx/sprites/<name>.png              16x96 = 6 walk frames of 16x16, 2bpp

A block is 32x32 px = 2x2 metatiles; a metatile is 16x16 px = 2x2 tiles.
The GB screen is 160x144 px (20x18 tiles); the Thumby Color panel is 128x128.

Usage:
  python3 tools/extract.py --map NewBarkTown --out game/src/gen
  python3 tools/extract.py --map NewBarkTown --out game/src/gen --pc vendor/polishedcrystal
"""

import argparse
import glob
import os
import re
import sys

from PIL import Image

COLL_WALKABLE = {
    "FLOOR", "TALL_GRASS", "LONG_GRASS", "LADDER", "WARP_CARPET_UP",
    "WARP_CARPET_DOWN", "WARP_CARPET_LEFT", "WARP_CARPET_RIGHT", "WATER",
    "ICE", "HOP_UP", "HOP_DOWN", "HOP_LEFT", "HOP_RIGHT", "HOP_UP_DOWN",
    "HOP_LEFT_RIGHT", "CURRENT_UP", "CURRENT_DOWN", "CURRENT_LEFT",
    "CURRENT_RIGHT", "SLIDE_UP", "SLIDE_DOWN", "SLIDE_LEFT", "SLIDE_RIGHT",
    "WHIRLPOOL", "STAIRS", "DOOR", "CAVE", "STAIRCASE", "STOP",
}


def die(msg):
    sys.stderr.write("extract: " + msg + "\n")
    sys.exit(1)


def read(path, mode="r"):
    with open(path, mode) as f:
        return f.read()


# --------------------------------------------------------------------------- map
def map_id(pc, map_name):
    """`map_attributes <Map>, <MAP_ID>, <border>` -> MAP_ID."""
    txt = read(os.path.join(pc, "data/maps/attributes.asm"))
    pat = re.compile(r"^\s*map_attributes\s+%s\s*,\s*(\w+)" % map_name, re.M)
    m = pat.search(txt)
    if not m:
        die("map_attributes for %s not found" % map_name)
    return m.group(1)


def map_dims(pc, map_name):
    mid = map_id(pc, map_name)
    txt = read(os.path.join(pc, "constants/map_constants.asm"))
    pat = re.compile(r"^\s*map_const\s+%s\s*,\s*(\d+)\s*,\s*(\d+)" % mid, re.M)
    m = pat.search(txt)
    if not m:
        die("map_const for %s (%s) not found" % (map_name, mid))
    return int(m.group(1)), int(m.group(2))


def map_tileset(pc, map_name):
    txt = read(os.path.join(pc, "data/maps/maps.asm"))
    pat = re.compile(r"^\s*map\s+%s\s*,\s*TILESET_(\w+)" % map_name, re.M)
    m = pat.search(txt)
    if not m:
        die("`map %s` row not found" % map_name)
    return m.group(1).lower()


def map_block_path(pc, map_name):
    """The .ablk holding this map's blocks.

    Most maps have their own maps/<Name>.ablk, but some share a layout: their
    data/maps/blocks.asm label carries a single INCBIN of another map's .ablk
    (four Johto houses share JohtoHouse4.ablk, for instance). The build turns
    those into <name>.ablk.lzp; the plain .ablk is what we read.
    """
    path = os.path.join(pc, "maps", map_name + ".ablk")
    if os.path.exists(path):
        return path
    txt = read(os.path.join(pc, "data/maps/blocks.asm"))
    m = re.search(r"^%s_BlockData:\s*$" % map_name, txt, re.M)
    if not m:
        die("no maps/%s.ablk and no %s_BlockData label" % (map_name, map_name))
    inc = re.search(r'INCBIN\s+"maps/(\w+)\.ablk(?:\.lzp)?"', txt[m.end():])
    if not inc:
        die("%s_BlockData shares no .ablk" % map_name)
    return os.path.join(pc, "maps", inc.group(1) + ".ablk")


def map_blocks(pc, map_name, w, h):
    path = map_block_path(pc, map_name)
    raw = read(path, "rb")
    if len(raw) != w * h:
        die("%s is %d bytes, expected %d (%dx%d blocks)"
            % (path, len(raw), w * h, w, h))
    return list(raw)


def map_warps(pc, map_name):
    """`def_warp_events` block -> [(x, y, DEST_MAP_CONST, warp number)].

    `warp_event x, y, MAP, n` lists the cell in *this* map and the 1-based
    number of the warp event to land on in the destination map. That is why a
    two-tile-wide doormat needs two events pointing at the same place.
    """
    txt = read(os.path.join(pc, "maps", map_name + ".asm"))
    m = re.search(r"^\s*def_warp_events\s*$(.*?)(?=^\s*def_|\Z)", txt, re.M | re.S)
    if not m:
        return []
    out = []
    for w in re.finditer(r"^\s*warp_event\s+(\d+)\s*,\s*(\d+)\s*,\s*(\w+)\s*,\s*(\d+)",
                         m.group(1), re.M):
        out.append((int(w.group(1)), int(w.group(2)), w.group(3), int(w.group(4))))
    return out


# The `connection` macro's direction -> the ow/dir.h direction the player walks to
# leave through that edge (ow/dir.h: 0 down, 1 up, 2 left, 3 right).
CONNECTION_DIRS = {"north": 1, "south": 0, "west": 2, "east": 3}
CONNECTION_OPPOSITE = {"north": "south", "south": "north", "west": "east",
                       "east": "west"}


def map_connections(pc, map_name, const_name):
    """`connection <dir>, <Map>, <MAP_ID>, <offset>` rows of this map.

    They sit right after the map's `map_attributes` line and run to the next one.
    The offset shifts the destination along the shared edge, in blocks: the macro
    turns it into the strip offsets with `_x = offset * -2` (north/south) and
    `_y = offset * -2` (east/west), in 16 px cells, and the engine adds them to
    the player's coordinate when the edge is crossed (home/map.asm, the
    StripXOffset/StripYOffset of the connection struct).
    """
    txt = read(os.path.join(pc, "data/maps/attributes.asm"))
    m = re.search(r"^\s*map_attributes\s+%s\s*,.*?$"
                  r"(.*?)(?=^\s*map_attributes\s+\w+\s*,|\Z)" % map_name,
                  txt, re.M | re.S)
    if not m:
        die("map_attributes block for %s not found" % map_name)
    out = []
    seen = set()
    for c in re.finditer(r"^\s*connection\s+(\w+)\s*,\s*(\w+)\s*,\s*(\w+)\s*,"
                         r"\s*(-?\d+)", m.group(1), re.M):
        cdir, cname, cconst, coff = c.group(1), c.group(2), c.group(3), int(c.group(4))
        if cdir not in CONNECTION_DIRS:
            die("%s: connection direction %r is not north/south/west/east"
                % (map_name, cdir))
        if cdir in seen:
            die("%s: two %s connections" % (map_name, cdir))
        seen.add(cdir)
        if const_name.get(cconst) != cname:
            die("%s: connection %s names %s, but that map const is %s"
                % (map_name, cdir, cname, const_name.get(cconst)))
        if not -127 <= coff <= 127:
            die("%s: connection %s offset %d does not fit the emitted int8_t"
                % (map_name, cdir, coff))
        out.append({"dir": cdir, "name": cname, "offset": coff})
    return out


_edge_cells_cache = {}


def map_edge_cells(pc, map_name, side):
    """The walkable cells on one edge of a map, as the coordinate that varies.

    West/east edges answer with the set of walkable y cells, north/south with the
    set of walkable x cells (in 16 px cells, 2 per block). Two connected maps have
    to agree on that set once the offset is applied, which is what
    map_connections checks: a connection that leads into a wall is a data bug the
    extractor should catch, not something the player discovers by walking into an
    invisible edge.
    """
    key = (map_name, side)
    if key in _edge_cells_cache:
        return _edge_cells_cache[key]
    w, h = map_dims(pc, map_name)
    ts = map_tileset(pc, map_name)
    blocks = map_blocks(pc, map_name, w, h)
    colls = collisions(pc, ts)
    cw, ch = w * 2, h * 2

    def walkable(cx, cy):
        quad = (cx & 1) + 2 * (cy & 1)
        return not ((colls[blocks[(cy // 2) * w + cx // 2]] >> quad) & 1)

    if side in ("west", "east"):
        x = 0 if side == "west" else cw - 1
        cells = {y for y in range(ch) if walkable(x, y)}
    else:
        y = 0 if side == "north" else ch - 1
        cells = {x for x in range(cw) if walkable(x, y)}
    _edge_cells_cache[key] = cells
    return cells


def map_const_to_name(pc):
    """PLAYERS_HOUSE_1F -> PlayersHouse1F, from data/maps/attributes.asm."""
    txt = read(os.path.join(pc, "data/maps/attributes.asm"))
    out = {}
    for m in re.finditer(r"^\s*map_attributes\s+(\w+)\s*,\s*(\w+)", txt, re.M):
        out[m.group(2)] = m.group(1)
    return out


def map_spawns(pc):
    """`spawn MAP, x, y` table -> {map name: (x, y)}, the game's own respawn points.

    The macro's comment says "map, y, x" but the engine reads the two bytes into
    wXCoord then wYCoord (engine/overworld/spawn_points.asm), and Cherrygrove's
    29,4 only fits a 40x22-cell map as (x, y). So the arguments are x, y.
    """
    txt = read(os.path.join(pc, "data/maps/spawn_points.asm"))
    const_name = map_const_to_name(pc)
    out = {}
    for m in re.finditer(r"^\s*spawn\s+(\w+)\s*,\s*(\d+)\s*,\s*(\d+)", txt, re.M):
        name = const_name.get(m.group(1))
        if name:
            out[name] = (int(m.group(2)), int(m.group(3)))
    return out


def map_environment(pc, map_name):
    """The 3rd field of `map <Name>, TILESET_X, <ENV>, ...` (INDOOR, TOWN, ...)."""
    txt = read(os.path.join(pc, "data/maps/maps.asm"))
    m = re.search(r"^\s*map\s+%s\s*,\s*TILESET_\w+\s*,\s*(\w+)" % map_name, txt, re.M)
    if not m:
        die("no environment for %s" % map_name)
    return m.group(1)


# ---------------------------------------------------------------------- tileset
def tileset_sheet(pc, ts):
    """The combined 2bpp sheet: <ts>.<common>.png if present, else <ts>.png."""
    cands = sorted(glob.glob(os.path.join(pc, "gfx/tilesets", ts + ".*.png")))
    if not cands:
        cands = [os.path.join(pc, "gfx/tilesets", ts + ".png")]
    if not os.path.exists(cands[0]):
        die("no tileset sheet for %s" % ts)
    return cands[0]


def _png_shade(v):
    """PNG grey -> GB 2bpp colour index.

    The art is stored as 4 grey levels with 0 = black; the GB's index 0 is the
    LIGHTEST shade (palette entry 0 = the near-white), so the ramp is inverted
    here. Getting this wrong swaps highlights and shadows on every tile.
    """
    if v < 43:
        return 3
    if v < 128:
        return 2
    if v < 213:
        return 1
    return 0


def load_2bpp_sheet(path):
    """PNG (grayscale 2bpp) -> list of 8x8 tiles, each 16 bytes GB-planar."""
    im = Image.open(path)
    if im.mode != "L":
        im = im.convert("L")
    w, h = im.size
    if w % 8 or h % 8:
        die("%s is not a multiple of 8x8" % path)
    px = im.load()
    tiles = []
    for ty in range(h // 8):
        for tx in range(w // 8):
            data = []
            for y in range(8):
                lo = hi = 0
                for x in range(8):
                    idx = _png_shade(px[tx * 8 + x, ty * 8 + y])
                    bit = 7 - x
                    lo |= (idx & 1) << bit
                    hi |= (idx >> 1) << bit
                data += [lo, hi]
            tiles.append(bytes(data))
    return tiles


def metatiles(pc, ts):
    raw = read(os.path.join(pc, "data/tilesets", ts + "_metatiles.bin"), "rb")
    if len(raw) % 16:
        die("%s_metatiles.bin size not a multiple of 16" % ts)
    return [list(raw[i:i + 16]) for i in range(0, len(raw), 16)]


def metatile_palettes(pc, ts):
    """Per-tile GBC BG palette index (0..7): the low 3 bits of the tile's
    attribute byte in <tileset>_attributes.bin."""
    raw = read(os.path.join(pc, "data/tilesets", ts + "_attributes.bin"), "rb")
    if len(raw) != 16 * (len(raw) // 16):
        die("%s_attributes.bin size not a multiple of 16" % ts)
    return [[b & 7 for b in raw[i:i + 16]] for i in range(0, len(raw), 16)]


def bg_palettes(pc, tod="day"):
    """gfx/tilesets/bg_tiles.pal -> 8 palettes of 4 RGB565 colours.

    The table is `morn, day, night, eve, indoor, water` x 8 palette slots
    (gray, red, green, water, yellow, brown, roof, text). Colours are 5-bit.
    Outdoor maps use one group per time of day (the port stays on `day`), and
    indoor maps use `indoor` at any hour, which is why both are extracted.
    """
    groups = ["morn", "day", "night", "eve", "indoor", "water"]
    txt = read(os.path.join(pc, "gfx/tilesets/bg_tiles.pal"))
    rows = []
    for line in txt.splitlines():
        s = line.split(";")[0].strip()
        m = re.match(r"RGB\s+(.+)$", s)
        if not m:
            continue
        nums = [int(v) for v in re.findall(r"\d+", m.group(1))]
        if len(nums) != 12:
            die("bad RGB line in bg_tiles.pal: " + line)
        rows.append([nums[i:i + 3] for i in range(0, 12, 3)])
    start = groups.index(tod) * 8
    if len(rows) < start + 8:
        die("bg_tiles.pal has only %d palettes" % len(rows))

    def to565(c):
        r, g, b = [v * 255 // 31 for v in c]
        return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

    return [[to565(c) for c in pal] for pal in rows[start:start + 8]]


def collision_names(pc, ts):
    """Per metatile: the 4 collision mnemonics, in the engine's byte order.

    `tilecoll a, b, c, d` stores a, b, c, d straight into the metatile's four
    quadrant bytes, and GetCoordTileCollision (home/map.asm) indexes them as
    (x & 1) + 2 * (y & 1): byte 0 is the top-left 16x16 quadrant, then
    top-right, bottom-left, bottom-right. Any other order silently rotates the
    collision map — which is exactly what hid the doors for a while.
    """
    path = os.path.join(pc, "data/tilesets", ts + "_collision.asm")
    if not os.path.exists(path):
        die("no collision table for %s" % ts)
    out = []
    for line in read(path).splitlines():
        line = line.split(";")[0].strip()
        m = re.match(r"tilecoll\s+(.+)$", line)
        if not m:
            continue
        codes = [c.strip() for c in m.group(1).split(",")]
        if len(codes) != 4:
            die("bad tilecoll line: " + line)
        out.append(codes)
    return out


def collisions(pc, ts):
    """Per metatile: one blocked mask, bits TL,TR,BL,BR (of the 32x32 block)."""
    out = []
    for codes in collision_names(pc, ts):
        mask = 0
        for bit, code in enumerate(codes):
            if code not in COLL_WALKABLE:
                mask |= 1 << bit
        out.append(mask)
    return out


# Warp cells, keyed by the collision mnemonic the tile carries. A warp always
# needs its event cell to be one of these (CheckWarpCollision tests the tile), so
# finding the kind here doubles as a validation of the collision order.
WARP_CARPET_DIRS = {
    "WARP_CARPET_DOWN": 0, "WARP_CARPET_UP": 1,
    "WARP_CARPET_LEFT": 2, "WARP_CARPET_RIGHT": 3,
}
WARP_OTHER = ("STAIRCASE", "STAIRS", "CAVE", "LADDER", "WARP_PANEL", "AWNING",
              "DOOR", "HOP_UP", "HOP_DOWN")


def warp_kind(code):
    """Collision mnemonic -> (kind, direction).

    kind 0 = door: you warp as the step onto it ends (and the original then
              pushes the player one cell further, out of the doorway).
    kind 1 = carpet: a directional warp. It fires while standing on it with the
              matching direction held, and needs no step.
    kind 2 = anything else that warps (stairs, cave mouths, panels).
    """
    if code in WARP_CARPET_DIRS:
        return 1, WARP_CARPET_DIRS[code]
    if code == "DOOR":
        return 0, 0
    return 2, 0


# ----------------------------------------------------------------------- sprite
def sprite_frames(pc, name):
    """gfx/sprites/<name>.png (16x96, 6 frames of 16x16, 2bpp) -> 6*4 tiles."""
    path = os.path.join(pc, "gfx/sprites", name + ".png")
    if not os.path.exists(path):
        return None
    im = Image.open(path)
    if im.mode != "L":
        im = im.convert("L")
    w, h = im.size
    if w != 16 or h % 16:
        return None
    px = im.load()
    frames = []
    for f in range(h // 16):
        for ty in range(2):
            for tx in range(2):
                data = []
                for y in range(8):
                    lo = hi = 0
                    for x in range(8):
                        idx = _png_shade(px[tx * 8 + x, f * 16 + ty * 8 + y])
                        bit = 7 - x
                        lo |= (idx & 1) << bit
                        hi |= (idx >> 1) << bit
                    data += [lo, hi]
                frames.append(bytes(data))
    return frames


# Facing encoded by the movement type, i.e. what the NPC looks like when it is
# not walking. Everything else faces down until NPC movement lands (OW-06);
# STANDING_* is the common case for static NPCs.
FACING = {
    "SPRITEMOVEDATA_STANDING_DOWN": 0,
    "SPRITEMOVEDATA_STANDING_UP": 1,
    "SPRITEMOVEDATA_STANDING_LEFT": 2,
    "SPRITEMOVEDATA_STANDING_RIGHT": 3,
}

# Visible animation kind. Spinners rotate in place (they need no pathfinding), so
# they are the part of NPC animation that works before OW-06.
ANIM = {
    "SPRITEMOVEDATA_SPINRANDOM_SLOW": 3,
    "SPRITEMOVEDATA_SPINRANDOM_FAST": 3,
    "SPRITEMOVEDATA_SPINCOUNTERCLOCKWISE": 1,
    "SPRITEMOVEDATA_SPINCLOCKWISE": 2,
}


def map_objects(pc, map_name, consts):
    """object_event rows -> (x, y, SPRITE_NAME, facing, anim, move, script).

    Coordinates are in 16x16 px tiles (the same grid the player walks on), not
    in 32px map blocks — an object_event y of 15 on a 9-block-tall map only fits
    that way. The movement type (`SPRITEMOVEDATA_*`) decides the facing the
    NPC stands with, whether it animates in place, and how it walks on its own:
    `move` is the original's number, which ow/move reads for the patterns.

    `script` is how the object answers to A, in the original's two shapes:
    (`label`, name) is an OBJECTTYPE_SCRIPT, a label of this same file, and
    (`command`, macro, arg) is an OBJECTTYPE_COMMAND, which the engine runs as a
    script of one command (`jumptextfaceplayer MyText`). None is an object with
    no script: the null `ObjectEvent` field of a scene-only NPC.
    """
    txt = map_source(pc, map_name)
    out = []
    for line in txt.splitlines():
        m = re.match(r"^\s*object_event\s+(.+)$", line.split(";")[0])
        if not m:
            continue
        a = [x.strip() for x in split_args(m.group(1))]
        if len(a) < 11:
            die("%s: object_event with %d fields: %r" % (map_name, len(a), line.strip()))
        kind, spec = a[8], None
        if kind == "OBJECTTYPE_SCRIPT":
            if a[10] not in ("ObjectEvent", "0", "-1"):
                spec = ("label", a[10])
        elif kind == "OBJECTTYPE_COMMAND":
            spec = ("command", a[9], a[10])
        elif kind in ("OBJECTTYPE_ITEMBALL", "OBJECTTYPE_TRAINER",
                      "OBJECTTYPE_GENERICTRAINER", "OBJECTTYPE_POKEMON"):
            # Their script is not a label or a command but a record the game reads
            # itself (the item, the trainer party); nothing to assemble yet.
            print("object   %-18s %s not ported" % (map_name, kind))
        else:
            die("%s: unknown object type %r" % (map_name, kind))
        if a[3] not in consts:
            die("%s: %r is not a SPRITEMOVEDATA_* constant" % (map_name, a[3]))
        out.append((int(a[0]), int(a[1]), a[2], FACING.get(a[3], 0),
                    ANIM.get(a[3], 0), consts[a[3]], spec))
    return out


def std_scripts(pc, consts):
    """engine/events/std_scripts.asm -> ({name: body}, [name in table order]).

    `StdScripts::` is a table of `dw Name`, and the order *is* the numbering of
    constants/std_constants.asm: a BGEVENT_JUMPSTD cell stores an index into it.
    The port inlines the script the index names, so the byte only ever has to
    make sense to the extractor, never to a pointer.
    """
    txt = read(os.path.join(pc, "engine", "events", "std_scripts.asm"))
    blocks = dict(asm_label_blocks(txt.splitlines()))
    order, in_table = [], False
    for line in txt.splitlines():
        s = line.split(";")[0].strip()
        if not s:
            continue
        if s.startswith("StdScripts::"):
            in_table = True
            continue
        if not in_table:
            continue
        m = re.match(r"^dw\s+(\w+)$", s)
        if m:
            order.append(m.group(1))
            continue
        in_table = False
    if not order:
        die("engine/events/std_scripts.asm: no StdScripts table found")
    by_const = {}
    for i, name in enumerate(order):
        if name not in blocks:
            die("engine/events/std_scripts.asm: %s is in the table but has no block"
                % name)
        short = re.sub(r"Script$", "", name).lower()
        # The constants are the table's order; a `const` that disagrees means the
        # two files drifted apart and every BGEVENT_JUMPSTD byte is off by one.
        if short in consts and consts[short] != i:
            die("constants/std_constants.asm: %s is %d, but it is entry %d of the table"
                % (short, consts[short], i))
        by_const[short] = blocks[name]
    return by_const, [re.sub(r"Script$", "", n).lower() for n in order]


def item_names(pc):
    """data/items/names.asm -> [display name], indexed by item id.

    `ItemNames::` is a `list_start` + `li "Park Ball"` list in item-id order
    (constants/item_constants.asm), with blank lines between categories. The
    count has to cover every id the extractor uses, or a hidden item would be
    announced as a different one.
    """
    txt = map_build_text(read(os.path.join(pc, "data", "items", "names.asm")),
                         "data/items/names.asm")
    start = txt.find("ItemNames::")
    if start < 0:
        die("data/items/names.asm: no ItemNames label")
    names = []
    for line in txt[start:].splitlines()[1:]:
        s = line.split(";")[0].strip()
        if not s:
            continue
        m = re.match(r'^li\s+"(.*)"$', s)
        if m:
            names.append(m.group(1))
        elif not s.startswith("list_"):
            break
    if not names:
        die("data/items/names.asm: no `li` names under ItemNames")
    return names


_TEXT_FILE_CACHE = {}


def text_file(pc, rel, charmap, keys):
    """A plain-text file of `data/text` as {label: bytes}, cached."""
    if rel in _TEXT_FILE_CACHE:
        return _TEXT_FILE_CACHE[rel]
    blocks = list(label_blocks(read(os.path.join(pc, rel))))
    out = {}
    for label, body in blocks:
        head = next((l.strip() for l in body if l.strip()), "")
        blk = parse_text_block(body, charmap, keys)
        if blk is not None and re.match(r"text\b", head):
            out[label] = blk
    _TEXT_FILE_CACHE[rel] = out
    return out


def bg_event_texts(pc, bg_events, item_names_, std_blocks, charmap, keys):
    """{text key: bytes} the bg_event cells of one map need.

    Two kinds of bg_event print a text the map file does not hold:

    - BGEVENT_JUMPSTD runs a std script, which lives in
      engine/events/std_scripts.asm with its text in data/text/std_text.asm.
      The text is loaded by the label the script jumps to, not by name.
    - BGEVENT_ITEM is a hidden item: the engine falls into HiddenItemScript,
      which prints `_FoundItemText` after filling wStringBuffer3 with the item's
      name (`getitemname`). The port has no string buffer, so the name is
      substituted into the same _FoundItemText here, once per item.
    """
    out = {}
    for spec in bg_events:
        spec = spec[3]
        if spec is None:
            continue
        if spec[0] == "std":
            for raw in std_blocks[spec[1]]:
                s = raw.split(";")[0].strip()
                m = re.match(r"^\w*text\w*\s+(\w+)$", s)
                if not m:
                    continue
                label = m.group(1)
                for rel in ("data/text/std_text.asm", "data/text/common.asm"):
                    tables = text_file(pc, rel, charmap, keys)
                    if label in tables:
                        out[label] = tables[label]
                        break
                else:
                    die("bg_event text %s is in neither std_text.asm nor common.asm"
                        % label)
            continue
        if spec[0] != "item":
            continue
        _, item, flag, key = spec
        if item >= len(item_names_):
            die("item id %d has no name in data/items/names.asm" % item)
        body = list(label_blocks(read(os.path.join(pc, "data/text/common.asm"))))
        found = dict(body).get("_FoundItemText")
        if found is None:
            die("data/text/common.asm: no _FoundItemText")
        blk = parse_text_block(found, charmap, keys,
                               ram={"wStringBuffer3":
                                    encode_text(item_names_[item], charmap, keys)})
        if blk is None:
            die("_FoundItemText is not plain text")
        out[key] = blk
    return out


def map_bg_events(pc, map_name, consts, std_order):
    """bg_event rows -> (x, y, kind, spec), in file order.

    `bg_event x, y, FUNCTION, arg` is a cell that answers to A: a sign, a
    bookshelf, a healing machine, a trash can. The engine looks it up by the
    tile the player faces (`CheckFacingBGEvent`) after no object_event matched,
    so these act as the second layer of "what is in front of me".

    `spec` is what the port has to run:
    - (`label`, name) for BGEVENT_READ and the four directional kinds: an
      ordinary script of this same map file.
    - (`text`, label) for BGEVENT_JUMPTEXT, where the engine builds the script
      itself (`jumptext <pointer>`), so the port emits that same one-command
      script instead of the text.
    - (`std`, name) for BGEVENT_JUMPSTD: the std script is inlined as a region
      of this map's blob, so no std table is needed at runtime.
    - (`item`, item id, flag, text key) for a hidden item (BGEVENT_ITEM and up):
      the type byte minus BGEVENT_ITEM is the item, the fourth field is the flag
      that marks it taken, and the engine's HiddenItemScript text is built here.
    - None for the kinds it cannot run yet: BGEVENT_IFSET/IFNOTSET keep a flag
      in front of their pointer, and GrottoItem is a flag plus a location.
    """
    names = ("BGEVENT_READ", "BGEVENT_UP", "BGEVENT_DOWN", "BGEVENT_RIGHT",
             "BGEVENT_LEFT", "BGEVENT_IFSET", "BGEVENT_IFNOTSET",
             "BGEVENT_JUMPTEXT", "BGEVENT_JUMPSTD")
    missing = [n for n in names if n not in consts]
    if missing:
        die("constants/script_constants.asm: no %s" % ", ".join(missing))
    out = []
    for line in map_source(pc, map_name).splitlines():
        m = re.match(r"^\s*bg_event\s+(.+)$", line.split(";")[0])
        if not m:
            continue
        a = [x.strip() for x in split_args(m.group(1))]
        if len(a) < 4:
            die("%s: bg_event with %d fields: %r" % (map_name, len(a), line.strip()))
        kindtxt, item = a[2], ""
        if kindtxt in consts:
            kind = consts[kindtxt]
            base = kindtxt
        else:
            # `BGEVENT_ITEM + POTION`: the item id is added to the type byte, and
            # BGEVENT_ITEM itself is `DEF ... EQU NUM_BGEVENTS`, not a `const`.
            head, _, tail = kindtxt.partition("+")
            base, item = head.strip(), tail.strip()
            first = consts.get(base)
            if first is None and base == "BGEVENT_ITEM":
                first = max(consts["BGEVENT_GROTTOITEM"], consts["BGEVENT_JUMPSTD"]) + 1
            if not item or first is None or item not in consts:
                die("%s: unknown bg event type %r" % (map_name, kindtxt))
            kind = first + consts[item]
        spec = None
        if base == "BGEVENT_JUMPTEXT":
            spec = ("text", a[3])
        elif base == "BGEVENT_JUMPSTD":
            if len(a) > 4:
                # `treegrotto, HIDDENGROTTO_X`: the std script takes an argument,
                # which the engine passes through the script variable.
                raise Unsupported("%s: JUMPSTD with an argument (%s)"
                                  % (map_name, ", ".join(a[3:])))
            if a[3] not in std_order:
                die("%s: %r is not in StdScripts" % (map_name, a[3]))
            spec = ("std", a[3])
        elif base in ("BGEVENT_READ", "BGEVENT_UP", "BGEVENT_DOWN",
                      "BGEVENT_RIGHT", "BGEVENT_LEFT"):
            spec = ("label", a[3])
        elif base == "BGEVENT_ITEM":
            if item not in consts or a[3] not in consts:
                die("%s: hidden item %r needs an item id and a flag in the constants"
                    % (map_name, kindtxt))
            spec = ("item", consts[item], consts[a[3]], "_FoundItemText_" + item)
        else:
            print("bg       %-18s %d,%d %s not ported" % (map_name, int(a[0]), int(a[1]), base))
        out.append((int(a[0]), int(a[1]), kind, spec))
    return out


def map_scenes(pc, map_name):
    """The map's scene script table, in order -> [name or None].

    `def_scene_scripts` opens the table and two macros fill it: `scene_const
    NAME` (a scene with no script) and `scene_script Label, NAME` (a script).
    Both take the next id, and an id *is* the index in this table: the wram byte
    a map keeps its scene in starts at 0, so the first scene is the one a new
    game is in. That is why `setscene 0` and "no scene yet" look the same to
    checkscene (see script/vm.c).
    """
    names = []
    inside = False
    for line in map_source(pc, map_name).splitlines():
        s = line.split(";")[0].strip()
        if not s:
            continue
        if s.startswith("def_scene_scripts"):
            inside = True
            continue
        if not inside:
            continue
        word = s.split(None, 1)[0]
        if word not in ("scene_const", "scene_script"):
            break
        args = [x.strip() for x in split_args(s.split(None, 1)[1])]
        if word == "scene_const":
            if len(args) != 1:
                die("%s: scene_const with %d fields: %r" % (map_name, len(args), s))
            name = args[0]
        else:
            # `scene_script Label, NAME`: the second field is optional (the
            # original's const_skip), and the id counts either way.
            name = args[1] if len(args) > 1 else None
        if name is not None and name in names:
            die("%s: scene %s declared twice" % (map_name, name))
        names.append(name)
    return names


def map_coord_events(pc, map_name, scenes, w=0, h=0):
    """coord_event rows -> (x, y, scene id, script label), in file order.

    A coord event fires when a step *ends* on its cell and the map's active
    scene is the one it names: the original calls CheckCurrentMapCoordEvents
    from DoPlayerEvent, which is the step's last frame. `SCENE_ALWAYS` (-1)
    skips the scene test, and it is the only way a cell can fire on a map with
    no scene table at all.
    """
    index = {n: i for i, n in enumerate(scenes) if n is not None}
    out = []
    for line in map_source(pc, map_name).splitlines():
        m = re.match(r"^\s*coord_event\s+(.+)$", line.split(";")[0])
        if not m:
            continue
        a = [x.strip() for x in split_args(m.group(1))]
        if len(a) != 4:
            die("%s: coord_event with %d fields: %r" % (map_name, len(a), line.strip()))
        try:
            x, y = int(a[0]), int(a[1])
        except ValueError:
            die("%s: coord_event at %r has no cell" % (map_name, line.strip()))
        if w and h and (x >= w or y >= h):
            die("%s: coord_event %d,%d is outside the %dx%d-cell map"
                % (map_name, x, y, w, h))
        if a[2] == "SCENE_ALWAYS":
            scene = -1
        elif a[2] in index:
            scene = index[a[2]]
        else:
            die("%s: coord_event names scene %s, which is not in its scene table %s"
                % (map_name, a[2], [n for n in scenes if n is not None]))
        out.append((x, y, scene, a[3]))
    return out


def map_ids(pc):
    """{MAP_ID const: (group, number)} from constants/map_constants.asm.

    `map_id MAP` emits exactly these two bytes, and `setmapscene`/`checkmapscene`
    carry them in the script, so the runtime needs the same table to find a
    map's scene byte.
    """
    txt = read(os.path.join(pc, "constants/map_constants.asm"))
    group, number, out = 0, 0, {}
    for line in txt.splitlines():
        s = line.split(";")[0].strip()
        if not s:
            continue
        if s.startswith("newgroup"):
            group += 1
            number = 0
            continue
        m = re.match(r"map_const\s+(\w+)\s*,", s)
        if m:
            number += 1
            out[m.group(1)] = (group, number)
    return out


# ---------------------------------------------------------------------- text
# The game's text is already a bytecode: `text` starts it ($00), then the
# charmap bytes, with $56/$57/$58/$59 for next/line/cont/para and $52 for done.
# Polished Crystal compresses the text at build time, but here the alternative
# would be reimplementing the Huffman decoder; the maps below carry ~4 KB of
# text, so the bytes are stored verbatim and `text/box` interprets them.
TEXT_MACRO = {
    "text": 0x00,     # <START>
    "next1": 0x55,    # <LNBRK>  one screen row down
    "next": 0x56,     # <NEXT>   new page
    "line": 0x57,     # <LINE>   write on the bottom line
    "cont": 0x58,     # <CONT>   scroll to the next line
    "para": 0x59,     # <PARA>   new page
    "done": 0x52,     # <DONE>
    "prompt": 0x54,   # <PROMPT>
}


def load_charmap(pc):
    """constants/charmap.asm -> {source text: byte}.

    The file defines several charmaps (no_ngrams, compressing, default). Only the
    compressing one is what the text sources are written in, and it is a superset
    of the plain characters: it adds `#` -> "Poké" and `#mon`, `<PLAYER>`,
    `<RIVAL>`, `<TRENDY>`. Merging them all is safe because the extra keys are
    what the other charmap cannot express.
    """
    out = {}
    pat = re.compile(r'^\s*charmap\s+"((?:[^"\\]|\\.)*)"\s*,\s*\$([0-9A-Fa-f]+)')
    for line in read(os.path.join(pc, "constants", "charmap.asm")).splitlines():
        m = pat.match(line)
        if not m:
            continue
        s = m.group(1).replace('\\"', '"').replace("\\\\", "\\")
        out[s] = int(m.group(2), 16)
    return out


def encode_text(s, charmap, keys):
    """Source string -> charmap bytes, longest key first (`#mon` before `#`)."""
    out = bytearray()
    i = 0
    while i < len(s):
        for k in keys:
            if s.startswith(k, i):
                out.append(charmap[k])
                i += len(k)
                break
        else:
            die("text: no charmap entry for %r in %r" % (s[i], s))
    return out


# The port is a normal build of the original: `make` defaults to DEBUG=0 and
# FAITHFUL=0. Map files put a whole scene (and its texts) behind
# `if DEF(DEBUG)`/`if !DEF(DEBUG)`, so the other branch has to be dropped here.
BUILD_FLAGS = {"DEBUG": False, "FAITHFUL": False}


def _def_cond(expr, where):
    m = re.match(r"(!?)DEF\((\w+)\)$", expr.strip())
    if not m:
        die("%s: unsupported build condition %r" % (where, expr))
    if m.group(2) not in BUILD_FLAGS:
        die("%s: unknown build flag %s" % (where, m.group(2)))
    return BUILD_FLAGS[m.group(2)] != bool(m.group(1))


def map_build_text(txt, where):
    """The lines a release build assembles: `if`/`elif`/`else`/`endc` resolved."""
    out, stack, active = [], [], True
    for line in txt.splitlines():
        code = line.split(";")[0].strip()
        if re.match(r"if\s", code):
            parent = active
            active = parent and _def_cond(code[2:], where)
            stack.append([parent, active])
            continue
        if re.match(r"elif\s", code):
            if not stack:
                die("%s: `elif` without `if`" % where)
            parent, taken = stack[-1]
            active = parent and not taken and _def_cond(code[4:], where)
            stack[-1][1] = taken or active
            continue
        if code == "else":
            if not stack:
                die("%s: `else` without `if`" % where)
            parent, taken = stack[-1]
            active = parent and not taken
            stack[-1][1] = True
            continue
        if code == "endc":
            if not stack:
                die("%s: `endc` without `if`" % where)
            active = stack.pop()[0]
            continue
        if active:
            out.append(line)
    if stack:
        die("%s: unclosed `if`" % where)
    return "\n".join(out)


def map_source(pc, map_name):
    """A map's .asm with the build-time conditionals already resolved."""
    return map_build_text(read(os.path.join(pc, "maps", map_name + ".asm")), map_name)


def label_blocks(txt):
    """Yield (label, body lines) for every top-level `Label:` in an asm file.

    `::` is a global label: data/text uses it for the texts other files reach,
    which is exactly the `_FoundItemText` of a hidden item.
    """
    lines = txt.splitlines()
    for i, line in enumerate(lines):
        m = re.match(r"^([A-Za-z_]\w*)::?\s*$", line)
        if not m:
            continue
        body = []
        for nxt in lines[i + 1:]:
            if re.match(r"^[A-Za-z_]\w*::?\s*$", nxt):
                break
            body.append(nxt)
        yield m.group(1), body


def parse_text_block(body, charmap, keys, ram=None):
    """`text "..."/line/para/done` -> bytes, or None if it is not plain text.

    text_far/text_asm/text_decimal hand control to something else (a text in
    another file, or code). Those are phase 2 work; skipping them keeps the
    extractor honest instead of emitting half a line.

    `text_ram` prints a buffer the script has just filled. The port has no string
    buffer, so the caller may pass `ram` — {buffer name: glyph bytes} — and the
    bytes are written here as if they had been the line's text.
    """
    out = bytearray()
    started = False
    for raw in body:
        s = raw.strip()
        if not s or s.startswith(";"):
            continue
        m = re.match(r"text_ram\s+(\w+)$", s)
        if m and ram and m.group(1) in ram:
            out += ram[m.group(1)]
            continue
        m = re.match(r"(text|next1|next|line|cont|para|done|prompt)(?:\s+(.*))?$", s)
        if not m:
            if re.match(r"(text_far|text_asm|text_ram|text_decimal|text_sound|"
                        r"text_pause|text_promptbutton|text_today|text_start)\b", s):
                return None
            return None
        op, arg = m.group(1), (m.group(2) or "").strip()
        # `<START>` goes out once, at the block's first `text`; a later `text`
        # only adds characters (macros/scripts/text.asm, `_dtxt`'s compressible
        # branch). A hidden item's _FoundItemText is the case that shows it.
        if op != "text" or not started:
            out.append(TEXT_MACRO[op])
        started = True
        if op in ("done", "prompt"):
            return bytes(out)
        if arg:
            am = re.match(r'^"((?:[^"\\]|\\.)*)"$', arg)
            if not am:
                return None
            piece = am.group(1).replace('\\"', '"').replace("\\\\", "\\")
            out += encode_text(piece, charmap, keys)
    return None if not out else bytes(out) + b"\x52"


# `jumpthistext` and its two siblings print "the text where the script is
# standing", which is either the block that follows inside the same script or,
# when the command is the script's last line (`jumpthisopenedtext` often is), the
# text label that comes next in the file.
INLINE_TEXT = ("jumpthistext", "jumpthistextfaceplayer", "jumpthisopenedtext")


def inline_keys(map_name, blocks):
    """{script label: [(text key, inline_here), ...]} in script order.

    `inline_here` is True when the text follows the command inside the same
    block, so its bytes have to be pulled out of `pc_texts` under a made-up name;
    otherwise the key is the label that comes next, which is already a text.
    """
    out = {}
    for i, (label, body) in enumerate(blocks):
        n = 0
        for j, raw in enumerate(body):
            if raw.split(";")[0].strip() not in INLINE_TEXT:
                continue
            rest = [l for l in body[j + 1:] if l.split(";")[0].strip()]
            if rest:
                key = "%s_%s_inline%s" % (map_name, label,
                                          "" if n == 0 else str(n + 1))
            else:
                key = blocks[i + 1][0] if i + 1 < len(blocks) else None
            out.setdefault(label, []).append((key, bool(rest)))
            n += 1
    return out


def map_texts(pc, map_name, charmap, keys):
    """Every plain-text label in a map, in file order: {label: bytes}.

    The map file holds the NPC scripts and their text; `text_far` and friends are
    skipped (they live elsewhere). Order is kept because the generated table is
    read side by side with the .asm by a human. The texts a script prints inline
    (`jumpthistext`) have no label of their own, so they are named here too.
    """
    txt = map_source(pc, map_name)
    blocks = list(label_blocks(txt))
    out = {}
    for label, body in blocks:
        blk = parse_text_block(body, charmap, keys)
        if blk is not None:
            head = next((l.strip() for l in body if l.strip()), "")
            if re.match(r"text\b", head):
                out[label] = blk
    for label, keys_in in inline_keys(map_name, blocks).items():
        body = dict(blocks)[label]
        n = 0
        for j, raw in enumerate(body):
            if raw.split(";")[0].strip() not in INLINE_TEXT:
                continue
            key, here = keys_in[n]
            n += 1
            if not here:
                continue
            blk = parse_text_block(body[j + 1:], charmap, keys)
            if blk is None:
                die("%s.%s: inline text after %s is not plain text"
                    % (map_name, label, raw.strip()))
            out[key] = blk
    return out


TEXT_COMMANDS = {"jumptext", "jumptextfaceplayer", "jumptexttoplayer", "showtext"}


def split_args(s):
    """Split by commas at paren depth 0 (macro args can contain `(1 << MORN)`)."""
    out, depth, cur = [], 0, ""
    for ch in s:
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        if ch == "," and depth == 0:
            out.append(cur.strip())
            cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out


def first_text_ref(texts, script, blocks, charmap, keys, map_name):
    """The first text an NPC script shows: (key, bytes), or None.

    Two shapes cover almost every NPC in the starting maps:

        showtext SomeLabel          /  iftrue_jumptextfaceplayer SomeLabel
        jumpthistextfaceplayer      /  jumpthistext
        (blank)                     /  (blank)
        text "..."                  /  text "..."     <- inline, no label

    Jumps to *other scripts* are deliberately not followed: that is control flow,
    and phase 2 (the script VM) will run it for real instead of the extractor
    guessing.
    """
    body = blocks.get(script)
    if body is None:
        return None
    blocks_list = list(blocks.items())
    for raw in body:
        m = re.match(r"\s*(?:if\w+_)?(showtext|writetext|jumptext\w*)\s+(\w+)", raw)
        if m and m.group(2) in texts:
            return (m.group(2), texts[m.group(2)])
    keys_in = inline_keys(map_name, blocks_list)
    for key, _ in keys_in.get(script, []):
        if key in texts:
            return (key, texts[key])
    return None


def map_object_texts(pc, map_name, texts, charmap, keys):
    """One text label per object_event, or None, in object order.

    An object_event ends in `..., TYPE, command, label, flag`. With
    OBJECTTYPE_COMMAND the label is the text itself (jumptextfaceplayer); with
    OBJECTTYPE_SCRIPT it is a script whose first text reference is taken. The
    index of the object_event here matches the order map_objects() emits, which
    is what PcObject.text indexes.
    """
    txt = map_source(pc, map_name)
    blocks = dict(label_blocks(txt))
    out = []
    for line in txt.splitlines():
        s = line.strip()
        if not s.startswith("object_event"):
            continue
        args = split_args(s[len("object_event"):].strip())
        if len(args) < 11:
            out.append(None)
            continue
        kind, command, label = args[8], args[9], args[10]
        if kind == "OBJECTTYPE_COMMAND" and command in TEXT_COMMANDS and label in texts:
            out.append((label, texts[label]))
        elif kind == "OBJECTTYPE_SCRIPT":
            out.append(first_text_ref(texts, label, blocks, charmap, keys, map_name))
        else:
            out.append(None)
    return out


def menu_labels(pc):
    """`engine/menus/start_menu.asm` -> {label name: source string}.

    The pause menu's entries are not in data/text: they are `db "#dex@"` strings
    inline in the menu's own asm, printed straight into the menu box. `@` is the
    string terminator, not a character.
    """
    path = os.path.join(pc, "engine", "menus", "start_menu.asm")
    out = {}
    for line in read(path).splitlines():
        t = line.strip()
        if not t.startswith(".") or ":" not in t:
            continue
        name, rest = t[1:].split(":", 1)
        if not name.endswith("String"):
            continue
        rest = rest.strip()
        if not rest.startswith("db "):
            continue
        rest = rest[3:].strip()
        if not (rest.startswith('"') and rest.endswith('@"')):
            continue
        out[name] = rest[1:-2]
    if not out:
        die("start_menu.asm: no `.<Name>String: db \"...\"@` labels found")
    return out


def asm_db_strings(txt):
    """`Label:` + `db "..."@` -> {label: string}, for a whole asm file.

    Two spellings occur: start_menu.asm keeps the string on the label's line, the
    option tables put it on the next one, so both are handled here. `@` is the
    terminator, not a character.
    """
    out = {}
    pending = None
    for line in txt.splitlines():
        t = line.strip()
        if not t or t.startswith(";"):
            continue
        m = re.match(r"^(\.[\w]+|\w+):(.*)$", t)
        label, body = (m.group(1), m.group(2).strip()) if m else (pending, t)
        if m and not body:
            pending = label
            continue
        pending = None
        if label is None:
            continue
        if body.startswith("db ") and body.endswith('@"') and '"' in body:
            out[label] = body[body.index('"') + 1:-2]
    return out


def option_names(pc):
    """`data/options/option_names.asm` -> the name labels, in the game's order.

    The order is the `dw .Label` list in the OptionNames table, not the order the
    strings happen to appear in the file.
    """
    path = os.path.join(pc, "data", "options", "option_names.asm")
    txt = read(path)
    order = []
    inside = False
    for line in txt.splitlines():
        t = line.strip()
        if t.startswith("OptionNames:"):
            inside = True
            continue
        if not inside:
            continue
        if t.startswith("assert_table_length"):
            break
        m = re.match(r"^dw \.(\w+)$", t)
        if m:
            order.append("." + m.group(1))
    if not order:
        die("option_names.asm: empty OptionNames table")
    strings = asm_db_strings(txt)
    for lbl in order:
        if lbl not in strings:
            die("option_names.asm: no db string for %s" % lbl)
    return [(lbl, strings[lbl]) for lbl in order]


def option_value_tables(txt):
    """`Options_<Name>:` routines that build a `.Strings` table -> {label: labels}.

    Some options point at one of two shared strings instead (`ld de, .Mono`), so
    they have no table here and are listed by hand in OPTION_VALUES.
    """
    tables = {}
    cur = None
    inside = False
    for line in txt.splitlines():
        t = line.strip()
        m = re.match(r"^(Options_\w+):$", t)
        if m:
            cur, inside = m.group(1), False
            continue
        if t.startswith(".Strings:"):
            inside = True
            tables[cur] = []
            continue
        if not inside:
            continue
        if t.startswith("assert_table_length") or t.startswith("."):
            inside = False
            continue
        m = re.match(r"^dw (\.\w+)$", t)
        if m:
            tables[cur].append(m.group(1))
    return tables


def num_frames(pc):
    """How many textbox frames the game has (NUM_FRAMES in ram_constants.asm).

    The Frame option prints `wTextboxFrame + 1` with two digits at run time, so
    the port needs the count, not a string table.
    """
    txt = read(os.path.join(pc, "constants", "ram_constants.asm"))
    n = len(re.findall(r"^\s*const FRAME_\d+", txt, re.M))
    if n < 1:
        die("ram_constants.asm: no FRAME_n constants")
    return n


def emit_ui(path, labels):
    """The pause menu's labels, as charmap bytes like every other text.

    `#dex` is one byte ($4d, the n-gram) followed by `dex`: the menu draws it
    through the same font expansion as the textbox, so it prints "POKédex"
    without the extractor or the game knowing how long that is.
    """
    with open(path, "w") as f:
        f.write("/* generated by tools/extract.py from Polished Crystal — do not edit */\n")
        f.write("#ifndef PC_GEN_UI_H\n#define PC_GEN_UI_H\n#include <stdint.h>\n\n")
        for i, (name, data) in enumerate(labels):
            f.write("static const uint8_t ui%d_%s[%d] = {\n" % (i, name, len(data)))
            f.write("\t" + ",".join("0x%02x" % b for b in data) + ",\n};\n")
        f.write("typedef struct { const char *name; const uint8_t *bytes; "
                "uint8_t len; } PcUiLabel;\n")
        f.write("#define PC_UI_LABEL_COUNT %d\n" % len(labels))
        f.write("enum { " + ", ".join("PC_UI_%s" % n for n, _ in labels) + " };\n")
        f.write("static const PcUiLabel pc_ui_labels[PC_UI_LABEL_COUNT] = {\n")
        for i, (name, data) in enumerate(labels):
            f.write('\t{ "%s", ui%d_%s, %d },\n' % (name, i, name, len(data)))
        f.write("};\n\n#endif\n")


# The option screen, in the order of the original's menu (engine/menus/
# options_menu.asm): (C name, routine, value labels). "frame" is the one option
# whose value is a number (wTextboxFrame + 1), not a string.
def option_table(pc):
    menu = read(os.path.join(pc, "engine", "menus", "options_menu.asm"))
    tables = option_value_tables(menu)
    strings = asm_db_strings(menu)
    names = dict(option_names(pc))

    wanted = (
        ("TEXT_SPEED", "TextSpeed", tables.get("Options_TextSpeed")),
        ("TEXT_AUTOSCROLL", "TextAutoscroll", tables.get("Options_TextAutoscroll")),
        ("FRAME", "Frame", None),
        ("TYPEFACE", "Typeface", tables.get("Options_Typeface")),
        ("KEYBOARD", "Keyboard", (".ABC", ".QWERTY")),
        ("SOUND", "Sound", (".Mono", ".Stereo")),
        ("BATTLE_EFFECTS", "BattleEffects", ("OffString", "OnString")),
        ("BATTLE_STYLE", "BattleStyle", (".Set", ".Switch", ".Predict")),
        ("NICKNAMES", "Nicknames", (".Ask", ".Always", ".Never")),
        ("RUNNING_SHOES", "RunningShoes", ("OffString", "OnString")),
        ("TURNING_SPEED", "TurningSpeed", tables.get("Options_TurningSpeed")),
        ("CLOCK_FORMAT", "ClockFormat", (".Twelve", ".TwentyFour")),
        ("POKEDEX_UNITS", "PokedexUnits", (".Imperial", ".Metric")),
    )
    if len(names) != len(wanted):
        die("option_names.asm has %d names, the port knows %d"
            % (len(names), len(wanted)))

    out = []
    for cname, routine, values in wanted:
        lbl = "." + routine
        if lbl not in names:
            die("option_names.asm: no name for %s" % routine)
        if values is None:
            out.append((cname, names[lbl], None, num_frames(pc)))
            continue
        if not values:
            die("options_menu.asm: no value table for Options_%s" % routine)
        vals = []
        for v in values:
            if v not in strings:
                die("options_menu.asm: no db string for %s (Options_%s)"
                    % (v, routine))
            vals.append(strings[v].rstrip())   # the original pads to a column
        out.append((cname, names[lbl], vals, len(vals)))

    done = asm_db_strings(read(os.path.join(pc, "engine", "menus",
                                            "options_menu_shared.asm")))
    if ".Done" not in done:
        die("options_menu_shared.asm: no .Done string")
    return out, done[".Done"].rstrip()


def emit_options(path, table, done, charmap, keys):
    """`gen/options.h`: the 13 options with their names and values, as charmap
    bytes, plus `Done` (the list terminator). Values are printed through the same
    font expansion as every other string, so nothing here is ASCII."""
    maxvals = max(n for _, _, _, n in table)

    def blob(f, name, s):
        data = encode_text(s, charmap, keys)
        f.write("static const uint8_t %s[%d] = {\n" % (name, len(data)))
        f.write("\t" + ",".join("0x%02x" % b for b in data) + ",\n};\n")
        return len(data)

    with open(path, "w") as f:
        f.write("/* generated by tools/extract.py from Polished Crystal — do not edit */\n")
        f.write("#ifndef PC_GEN_OPTIONS_H\n#define PC_GEN_OPTIONS_H\n#include <stdint.h>\n\n")

        lens = {}
        for i, (_, name, vals, _n) in enumerate(table):
            lens["name%d" % i] = blob(f, "opt_name_%d" % i, name)
            for k, v in enumerate(vals or []):
                lens["val%d_%d" % (i, k)] = blob(f, "opt_val_%d_%d" % (i, k), v)
        lens["done"] = blob(f, "opt_done", done)

        f.write("/* A string as the game stores it: charmap bytes, not ASCII. */\n")
        f.write("typedef struct { const uint8_t *bytes; uint8_t len; } "
                "PcOptionString;\n")
        f.write("/* One option: its values, in the original's order. `values` is NULL\n"
                " * for Frame, whose value is a number (wTextboxFrame + 1) printed at\n"
                " * run time; `count` is how many there are either way. */\n")
        f.write("typedef struct { const PcOptionString *values; uint8_t count; } "
                "PcOptionDef;\n")
        f.write("#define PC_OPTION_COUNT %d\n" % len(table))
        f.write("#define PC_OPTION_MAX_VALUES %d\n" % maxvals)
        f.write("enum { " + ", ".join("PC_OPT_%s" % c for c, _, _, _ in table) + " };\n\n")

        f.write("static const PcOptionString pc_option_names[PC_OPTION_COUNT] = {\n")
        for i, (cname, _name, _v, _n) in enumerate(table):
            f.write("\t{ opt_name_%d, %d },   /* %s */\n"
                    % (i, lens["name%d" % i], cname))
        f.write("};\n\n")

        f.write("/* One flat table of every value, in option order: `pc_options[i]\n"
                " * .values` points into it, so no row is padded out to the longest. */\n")
        f.write("static const PcOptionString pc_option_value_list[] = {\n")
        first = []
        at = 0
        for i, (_, _, vals, n) in enumerate(table):
            first.append(at if vals else -1)
            for k in range(n if vals else 0):
                f.write("\t{ opt_val_%d_%d, %d },\n"
                        % (i, k, lens["val%d_%d" % (i, k)]))
            at += n if vals else 0
        f.write("};\n\n")

        f.write("static const PcOptionDef pc_options[PC_OPTION_COUNT] = {\n")
        for i, (_, _, vals, n) in enumerate(table):
            src = "pc_option_value_list + %d" % first[i] if vals else "0"
            f.write("\t{ %s, %d },\n" % (src, n))
        f.write("};\n\n")

        f.write("static const PcOptionString pc_option_done = { opt_done, %d };\n"
                % lens["done"])
        f.write("\n#endif\n")


def font_tiles(pc, name, count):
    """gfx/font/<name>.png -> `count` tiles of 8 rows, 1 bit each, 1 = ink.

    The PNG is the preview of the 1bpp the game builds (rgbgfx -d1), so black is
    ink and tiles are row-major. Fonts are built with --trim-whitespace, which
    only drops empty tiles at the END, so no offset is involved.
    """
    path = os.path.join(pc, "gfx", "font", name + ".png")
    im = Image.open(path).convert("L")
    if im.width % 8 or im.height % 8:
        die("font %s: %dx%d is not a whole number of 8x8 tiles"
            % (name, im.width, im.height))
    px = im.load()
    tiles = []
    for ty in range(im.height // 8):
        for tx in range(im.width // 8):
            rows = []
            for y in range(8):
                bits = 0
                for x in range(8):
                    if px[tx * 8 + x, ty * 8 + y] < 128:
                        bits |= 1 << (7 - x)
                rows.append(bits)
            tiles.append(bytes(rows))
    if count > len(tiles):
        die("font %s: asked for %d tiles, the sheet has %d" % (name, count, len(tiles)))
    return tiles[:count]


# ---------------------------------------------------------------------- emitter
def ctok(s):
    """UPPER_SNAKE name for macros; plain lower name for C identifiers."""
    out = re.sub(r"(?<!^)(?=[A-Z])", "_", s)
    return out.lower()


def text_commands(pc):
    """Every `charmap "<X>", $nn` in constants/charmap.asm, as (NAME, code).

    The control bytes are the game's, not the port's: <START> $00, <NUM> $04,
    <PAUSE> $05, <WAIT> $02, and the page breaks at $52-$59. Reading them from
    the charmap instead of retyping the table means the port cannot drift from
    the original by one code.
    """
    path = os.path.join(pc, "constants/charmap.asm")
    out = []
    for line in open(path, encoding="utf-8"):
        m = re.match(r'\s*charmap\s+"<([A-Z0-9]+)>"\s*,\s*\$([0-9a-fA-F]{2})', line)
        if m:
            out.append((m.group(1), int(m.group(2), 16)))
    if not out:
        die("charmap: no <...> control codes found in %s" % path)
    return out


def emit_text(path, texts, pc):
    """One header with the map text bytecode and an index the maps point into.

    Each entry is the game's own text stream: $00 <START>, charmap bytes, then
    $56/$57/$58/$59 for next/line/cont/para and $52 <DONE>. Nothing is decoded to
    UTF-8, so `é`, `…`, `“` and the dynamic <PLAYER> stay exactly what the game
    would print.
    """
    cmds = text_commands(pc)
    with open(path, "w") as f:
        f.write("/* generated by tools/extract.py from Polished Crystal — do not edit */\n")
        f.write("#ifndef PC_GEN_TEXT_H\n#define PC_GEN_TEXT_H\n#include <stdint.h>\n\n")
        f.write("/* text control codes, straight from constants/charmap.asm */\n")
        for name, code in cmds:
            f.write("#define PCT_%-7s 0x%02x\n" % (name, code))
        f.write(
                "#define PCT_SPACE  0x7f\n"
                "#define PCT_POKE   0x4d   /* n-gram: prints \"Poké\" */\n"
                "#define PCT_POKEMON 0x4e  /* n-gram: prints \"Pokémon\" */\n"
                "#define PCT_PLAYER 0x4f\n"
                "#define PCT_RIVAL  0x50\n"
                "#define PCT_TRENDY 0x51\n\n")
        for i, (label, data) in enumerate(texts):
            f.write("static const uint8_t txt%d_%s[%d] = {\n" % (i, label, len(data)))
            for j in range(0, len(data), 16):
                f.write("\t" + ",".join("0x%02x" % b for b in data[j:j + 16]) + ",\n")
            f.write("};\n")
        f.write("typedef struct { const char *label; const uint8_t *bytes; "
                "uint16_t len; } PcText;\n")
        f.write("#define PC_TEXT_COUNT %d\n" % len(texts))
        f.write("static const PcText pc_texts[PC_TEXT_COUNT] = {\n")
        for i, (label, data) in enumerate(texts):
            f.write('\t{ "%s", txt%d_%s, %d },\n' % (label, i, label, len(data)))
        f.write("};\n\n#endif\n")


def movement_defs(pc):
    """macros/scripts/movement.asm -> ({const name: byte}, {macro: [bytes]}).

    A movement step is one byte and `step_sleep` is two (the step plus how long
    to wait). Nothing is counted or retyped here: every macro body reads
    `db movement_x ; $hh`, so the comment is the value, and a `db \\1` line is
    the macro's own argument (the duration of `step_sleep`).
    """
    consts, macros = {}, {}
    txt = read(os.path.join(pc, "macros/scripts/movement.asm")).splitlines()
    i = 0
    while i < len(txt):
        m = re.match(r"\s*MACRO\s+(\w+)\s*$", txt[i])
        if not m:
            i += 1
            continue
        name = m.group(1)
        i += 1
        steps = []
        while i < len(txt) and txt[i].strip() != "ENDM":
            line = txt[i]
            i += 1
            m = re.match(r"\s*db\s+(\w+)\s*;\s*\$([0-9a-fA-F]+)\s*$", line)
            if m:
                consts[m.group(1)] = int(m.group(2), 16)
                steps.append(consts[m.group(1)])
                continue
            m = re.match(r"\s*db\s+\\(\d)\s*(?:;.*)?$", line)
            if m:
                steps.append(("arg", int(m.group(1))))
                continue
            s = line.split(";")[0].strip()
            if not s:
                continue
            die("movement macro %s: cannot assemble %r" % (name, s))
        i += 1
        macros[name] = steps
    if len(macros) < 50 or len(consts) < 50:
        die("macros/scripts/movement.asm: only %d macros, %d constants found"
            % (len(macros), len(consts)))
    return consts, macros


def script_defs(pc):
    """Everything the map scripts need that lives outside a map file.

    `opcodes` is the `const X_command` sequence of `macros/scripts/events.asm`
    (the same order as RunScriptCommand's jump table, so index == opcode),
    `macros` are those macro bodies, `movement` the names that belong to
    movement data (not scripts), `specials` the index of each entry of
    SpecialsPointers, and `consts` every `const`/`DEF ... EQU n` under
    constants/ plus the map groups. Nothing here is retyped by hand.
    """
    txt = read(os.path.join(pc, "macros/scripts/events.asm")).splitlines()
    opcodes, macros = [], {}
    n, i = 0, 0
    while i < len(txt):
        line = txt[i].strip()
        m = re.match(r"const_def\s*(\d+)?", line)
        if m:
            n = int(m.group(1) or 0)
            i += 1
            continue
        m = re.match(r"const\s+(\w+)", line)
        if m:
            if m.group(1) != "_skip":
                opcodes.append((m.group(1), n))
            n += 1
            i += 1
            continue
        m = re.match(r"MACRO\s+(\w+)", line)
        if m:
            body = []
            i += 1
            while i < len(txt) and txt[i].strip() != "ENDM":
                body.append(txt[i])
                i += 1
            i += 1
            macros[m.group(1)] = body
            continue
        i += 1
    if len(opcodes) < 200:
        die("events.asm: only %d script opcodes found" % len(opcodes))

    mconsts, mmacros = movement_defs(pc)
    movement = set(mmacros)

    # Text macros end a script block: the strings sit right after `end` in the
    # same label, and map_texts has already extracted them.
    text_macros = set()
    for line in read(os.path.join(pc, "macros/scripts/text.asm")).splitlines():
        m = re.match(r"MACRO\s+(\w+)", line.strip())
        if m:
            text_macros.add(m.group(1))

    specials = {}
    for line in read(os.path.join(pc, "data/events/special_pointers.asm")).splitlines():
        m = re.match(r"\s*add_special\s+(\w+)", line)
        if m:
            specials[m.group(1)] = len(specials)

    consts = {name: code for name, code in opcodes}
    for path in sorted(glob.glob(os.path.join(pc, "constants", "*.asm"))):
        n = 0
        for line in read(path).splitlines():
            s = line.split(";")[0].strip()
            m = re.match(r"const_def\s*(-?\d+)?$", s)
            if m:
                n = int(m.group(1) or 0)
                continue
            m = re.match(r"const\s+(\w+)\s*(?:=\s*(\d+))?$", s)
            if m:
                if m.group(1) != "_skip":
                    consts.setdefault(m.group(1),
                                      int(m.group(2)) if m.group(2) else n)
                n += 1
                continue
            m = re.match(r"const_skip\s*(\d+)?$", s)
            if m:
                n += int(m.group(1) or 1)
                continue
            m = re.match(r"(?:DEF\s+)?(\w+)\s+EQU\s+(-?\d+|\$[0-9a-fA-F]+)$", s)
            if m:
                v = m.group(2)
                consts.setdefault(m.group(1), int(v[1:], 16) if v[0] == "$" else int(v))
    if len(consts) < 1000:
        die("constants: only %d values found" % len(consts))
    # The movement steps are `const movement_*` in macros/scripts/movement.asm
    # itself, not under constants/, and a script can name one:
    # `applyonemovement PLAYER, step_up` emits `db movement_step_up`.
    for name, code in mconsts.items():
        consts.setdefault(name, code)

    group, order = -1, 0
    for line in read(os.path.join(pc, "constants/map_constants.asm")).splitlines():
        s = line.split(";")[0].strip()
        if re.match(r"newgroup$", s):
            group += 1
            order = 0
            continue
        m = re.match(r"map_const\s+(\w+)", s)
        if m:
            consts["GROUP_" + m.group(1)] = group
            consts["MAP_" + m.group(1)] = order
            order += 1
    return {"opcodes": opcodes, "macros": macros, "movement": movement,
            "mmacros": mmacros, "text": text_macros, "specials": specials,
            "consts": consts}


def asm_label_blocks(txt):
    """(label, lines) for `Name:` and bare-`Name` labels (RGBDS allows both)."""
    cur, body = None, []
    for line in txt:
        m = re.match(r"^(\w+):\s*(?:;.*)?$", line.rstrip())
        if not m:
            m = re.match(r"^(\w+)$", line.rstrip())
        if m:
            if cur is not None:
                yield cur, body
            cur, body = m.group(1), []
            continue
        if cur is not None:
            body.append(line)
    if cur is not None:
        yield cur, body


class Unsupported(Exception):
    """A macro this extractor cannot assemble yet (nested code fragments)."""


def _compare(op, a, b):
    return {"==": a == b, "!=": a != b, ">=": a >= b,
            "<=": a <= b, ">": a > b, "<": a < b}[op]


def script_sizes(defs, name, args):
    """Bytes one script macro emits, as [(value | symbol, width)].

    The relative forms (`sjumpfwd`, `iftruefwd`, `iffalsefwd`, `ifequalfwd`) are
    rewritten to their absolute twins: they exist only to save bytes, and a
    distance measured from the operand is one more thing to get wrong.
    """
    if name.endswith("fwd") and name[:-3] in defs["macros"]:
        name = name[:-3]
    out = []
    # `\#` is "the arguments `shift` has not eaten yet", so a macro that takes
    # an item plus a fail-safe handler (`giveitem POTION, .full`) is a call to a
    # byte-emitting twin followed by the remaining arguments as script code.
    args = list(args)
    first = 0
    def arg(mo):
        i = first + int(mo.group(1)) - 1
        if i >= len(args):
            raise Unsupported("%s: argument %d missing" % (name, i + 1))
        return args[i]

    def cond(expr):
        """`_NARG == 2`, `\1 == 0`: both sides are numbers or `_NARG`."""
        expr = re.sub(r"\\(\d)", arg, expr).strip()
        m = re.match(r"(.+?)\s*(==|!=|>=|<=|>|<)\s*(.+)$", expr)
        if not m:
            raise Unsupported("%s: cannot evaluate %r" % (name, expr))

        def val(s):
            s = s.strip()
            return len(args) - first if s == "_NARG" else _number(s, defs)
        return _compare(m.group(2), val(m.group(1)), val(m.group(3)))

    # `if` guards matter because both branches are emitted otherwise, and one
    # of them then references an argument that is not there. `elif` is real in
    # `pokepic`/`cry` (`if \1 == 0` ... `elif _NARG == 2`).
    active, stack = True, []
    for line in defs["macros"][name]:
        code = line.split(";")[0].strip()
        if (not code or code.startswith("assert")
                or code.startswith("rept") or code.startswith("endr")):
            continue
        m = re.match(r"shift\s*(\d+)?$", code)
        if m:
            first = min(first + int(m.group(1) or 1), len(args))
            continue
        m = re.match(r"if\s+(.*)$", code)
        if m:
            parent = active
            active = parent and cond(m.group(1))
            stack.append({"parent": parent, "taken": active})
            continue
        m = re.match(r"elif\s+(.*)$", code)
        if m:
            if not stack:
                die("script macro %s: `elif` without `if`" % name)
            top = stack[-1]
            active = top["parent"] and not top["taken"] and cond(m.group(1))
            top["taken"] = top["taken"] or active
            continue
        if re.match(r"else$", code):
            if not stack:
                die("script macro %s: `else` without `if`" % name)
            top = stack[-1]
            active = top["parent"] and not top["taken"]
            top["taken"] = True
            continue
        if re.match(r"endc$", code):
            if not stack:
                die("script macro %s: `endc` without `if`" % name)
            active = stack.pop()["parent"]
            continue
        if not active:
            continue

        if code == "\\#":
            # The rest of the arguments *are* script code, one command each.
            for frag in args[first:]:
                head = frag.split(None, 1)[0]
                if head not in defs["macros"]:
                    raise Unsupported("%s: %r is not a script command" % (name, frag))
                rest = frag.split(None, 1)[1] if len(frag.split(None, 1)) > 1 else ""
                out += script_sizes(defs, head, split_args(rest))
            continue

        m = re.match(r"(db|dp|dw|dba|dr|map_id)\s+(.*)$", code)
        if not m:
            # a nested macro call (`giveitem_unsafe` -> `giveitems_unsafe`)
            head = code.split(None, 1)[0]
            if head in defs["macros"]:
                inner = code.split(None, 1)[1] if len(code.split(None, 1)) > 1 else ""
                out += script_sizes(defs, head,
                                    split_args(re.sub(r"\\(\d)", arg, inner)))
                continue
            raise Unsupported("%s: cannot assemble %r" % (name, code))
        kind, expr = m.group(1), m.group(2).strip()
        expr = re.sub(r"\\(\d)", arg, expr.replace("\\#", ", ".join(args[first:])))
        if "\\" in expr:
            raise Unsupported("%s: cannot expand %r" % (name, expr))
        if kind == "map_id":
            out += [("GROUP_" + expr, 1), ("MAP_" + expr, 1)]
            continue
        m = re.match(r"^\((\w+)Special\s*-\s*SpecialsPointers\)\s*/\s*3$", expr)
        if m:
            if m.group(1) not in defs["specials"]:
                die("special %s: not in SpecialsPointers" % m.group(1))
            out.append((defs["specials"][m.group(1)], 1))
            continue
        if kind == "dp":
            # macros/data.asm: `dp species, form` is LOW(species) and
            # HIGH(species) << MON_EXTSPECIES_F | form, not two loose bytes.
            vals = split_args(expr)
            if not 1 <= len(vals) <= 2:
                die("%s.dp: %d arguments" % (name, len(vals)))
            species = _number(vals[0], defs)
            form = _number(vals[1], defs) if len(vals) == 2 else \
                defs["consts"]["PLAIN_FORM"]
            flag = defs["consts"]["MON_EXTSPECIES_F"]
            high = ((species >> 8) << flag) | form
            out.append((species & 0xff, 1))
            out.append((high & 0xff, 1))
            continue
        width = {"db": 1, "dw": 2, "dba": 3, "dr": 2}[kind]
        if re.fullmatch(r"-?\d+", expr):
            out.append((int(expr), width))
        elif expr in defs["consts"]:
            out.append((defs["consts"][expr], width))
        else:
            n = const_expr(expr, defs["consts"])
            out.append((n if n is not None else expr, width))
    return out


def _number(sym, defs):
    """A literal, a known constant or simple arithmetic; dies if it is neither."""
    if re.fullmatch(r"-?\d+", sym):
        return int(sym)
    if sym in defs["consts"]:
        return defs["consts"][sym]
    n = const_expr(sym, defs["consts"])
    if n is None:
        die("script data: cannot evaluate %r" % sym)
    return n


def const_expr(expr, consts):
    """`(1 << MORN) | (1 << DAY)` -> a number, or None if it is not arithmetic.

    Only shifts, ors and the constants themselves are allowed: if the string is
    a symbol or anything else, the caller keeps it as a name.
    """
    if not re.fullmatch(r"[-\w\s()<>|+*/&]+$", expr):
        return None
    names = set(re.findall(r"[A-Za-z_]\w*", expr))
    if not names or not names <= set(consts):
        return None
    try:
        return int(eval(expr.replace("$", "0x"),  # noqa: S307 - digits and consts only
                        {"__builtins__": {}},
                        {n: consts[n] for n in names}))
    except Exception:
        return None


def map_movements(pc, map_name, defs):
    """All movement data of a map file as one region: (bytes, {label: offset}).

    The blocks are fallthrough data — `Movement_A:` can run straight into
    `Movement_B:` — so they have to stay contiguous and in file order, and a
    label is an offset into the one region, exactly like a script label is an
    offset into the script blob. Every stream has to reach a `step_end` inside
    the region: a movement that ran off the end would read script bytes as steps.
    """
    steps, at = [], {}
    for label, body in asm_label_blocks(map_source(pc, map_name).splitlines()):
        lines = [l.split(";")[0].strip() for l in body]
        lines = [l for l in lines if l]
        if not lines or lines[0].split()[0] not in defs["mmacros"]:
            continue
        if label in at:
            die("%s: movement label %s appears twice" % (map_name, label))
        at[label] = len(steps)
        for line in lines:
            parts = line.split(None, 1)
            macro = parts[0]
            if macro not in defs["mmacros"]:
                die("%s.%s: %r is not a movement step" % (map_name, label, line))
            args = split_args(parts[1]) if len(parts) > 1 else []
            for b in defs["mmacros"][macro]:
                if not isinstance(b, tuple):
                    steps.append(b)
                    continue
                k = b[1] - 1
                if k >= len(args):
                    die("%s.%s: %s needs %d argument(s)"
                        % (map_name, label, macro, k + 1))
                v = _number(args[k], defs)
                if not 0 <= v <= 255:
                    die("%s.%s: %s duration %d does not fit in a byte"
                        % (map_name, label, macro, v))
                steps.append(v)
    end = defs["consts"]["movement_step_end"]
    sleep = defs["consts"]["movement_step_sleep"]
    for label, start in at.items():
        i = start
        while i < len(steps) and steps[i] != end:
            i += 2 if steps[i] == sleep else 1
        if i >= len(steps):
            die("%s.%s: the movement never reaches step_end" % (map_name, label))
    return steps, at


def map_scripts(pc, map_name, defs, text_index, obj_specs=(), bg_events=(),
                std_blocks=None, scenes=(), coords=(), scene_consts=None,
                movements=None):
    """Assemble every script label of a map into one blob.

    Each entry point gets its own region, so a script can be run from its
    offset without knowing the map's layout. In-file jumps resolve to offsets
    in the blob; a pointer to a text becomes an index into `pc_texts`, because
    the port has no ROM addresses. Anything else (movement data, another map's
    script) becomes $FFFF and is reported, not silently zeroed.

    `obj_specs` are the objects' scripts (see map_objects). An OBJECTTYPE_COMMAND
    becomes a one-command tail of the same blob, which is exactly what the engine
    does with it (it copies the field into wScriptPointer and runs it), so every
    object ends up pointing at a script offset and nowhere else.

    `bg_events` are the bg_event cells (see map_bg_events), appended after the
    objects for the same reason. BGEVENT_JUMPTEXT is the same trick as an
    OBJECTTYPE_COMMAND: the engine builds `jumptext <text>` and runs it, so the
    port emits those three bytes instead of the text.

    `std_blocks` are engine/events/std_scripts.asm's scripts (see
    std_scripts): a BGEVENT_JUMPSTD cell gets its std script assembled into the
    blob as one more region, and a hidden item gets the two commands the
    engine's HiddenItemScript reduces to here (`opentext` then `jumptext`; the
    item and the flag are handled by the game, which sees them in PcBgEvent).

    `scenes` are the map's scene names (see map_scenes) and `coords` its coord
    events (see map_coord_events): the names become the constants `setscene`
    takes, and each coord event's script is one more offset of this blob.

    `movements` is (bytes, {label: offset}) from map_movements: the region goes
    in right after the scripts, so `applymovement` can point at it by offset and
    the port never needs a second table (see docs/06 on what that costs).
    """
    stats = {"event": -1, "flag": -1}
    blocks = list(asm_label_blocks(map_source(pc, map_name).splitlines()))
    header = next((body for label, body in blocks if label.endswith("_MapScriptHeader")), [])

    objects, n = {}, 0
    for line in header:
        m = re.match(r"\s*const\s+(\w+)", line)
        if m:
            n += 1
            objects[m.group(1)] = n
    consts = dict(defs["consts"])
    # Scene ids are per map but their names are global constants (scene_const
    # exports them), so a script may name another map's scene — Elms Lab sets
    # Route 29's tutorial with `setmapscene ROUTE_29, SCENE_ROUTE29_...`.
    consts.update(scene_consts or {})
    consts.update({name: i for i, name in enumerate(scenes) if name is not None})

    def is_script(body):
        for line in body:
            s = line.split(";")[0].strip()
            if not s:
                continue
            name = s.split()[0]
            return name in defs["macros"] and name not in defs["movement"]
        return False

    scripts = [(label, body) for label, body in blocks if is_script(body)]
    ikeys = inline_keys(map_name, blocks)
    if (not scripts and not any(spec for spec in obj_specs)
            and not any(e[3] for e in bg_events) and not coords):
        raise Unsupported("no script labels in the map file")

    def assemble(label, body, labels, locals_rel):
        """One script block.

        `locals_rel` maps `.label` -> offset inside the block. Two passes are
        unavoidable: a script jumps forward over its own `.foo` labels, so a
        single pass would see them as unknown.
        """
        out, extern = [], set()

        def value(sym):
            if sym.startswith("."):
                key = label + sym
                if key in locals_rel:
                    return labels.get(label, 0) + locals_rel[key]
                extern.add(sym)
                return None
            if sym in labels:
                return labels[sym]
            if sym in text_index:
                return text_index[sym]
            if sym in objects:
                return objects[sym]
            if sym in consts:
                return consts[sym]
            extern.add(sym)
            return None

        skipping = False
        inline_n = 0
        i = 0
        while i < len(body):
            line = body[i]
            i += 1
            s = line.split(";")[0].strip()
            if not s:
                continue
            m = re.match(r"^(\.\w+):?$", s)
            if m:
                if not skipping:
                    locals_rel[label + m.group(1).rstrip(":")] = len(out)
                else:
                    skipping = False
                    locals_rel[label + m.group(1).rstrip(":")] = len(out)
                continue
            if skipping:
                continue
            parts = s.split(None, 1)
            if parts[0] in defs["text"] or parts[0] in defs["movement"]:
                # Data, not code: text strings or movement steps can sit inside a
                # script label, and both have their own extractor. Skip to the
                # next label, which is a script again.
                skipping = True
                continue
            m = re.match(r"(db|dw)\s+(.*)$", s)
            if m:
                # A `scalltable` jump table: `dw Script1`, `dw Script2` in file
                # order. Its entries are script offsets, so they resolve the
                # same way a `sjump` target does.
                width = 1 if m.group(1) == "db" else 2
                for sym in split_args(m.group(2)):
                    v = value(sym.strip())
                    if v is None:
                        out += [0xff] * width
                        continue
                    out += [v & 0xff, (v >> 8) & 0xff][:width]
                continue
            if parts[0] not in defs["macros"]:
                die("%s.%s: %r is not a script macro" % (map_name, label, s))
            if parts[0] in INLINE_TEXT:
                # These print "the text where the script is": an index like any
                # other command's, except the extractor is the one that knows
                # which text that is (see inline_keys).
                for val, width in script_sizes(defs, parts[0], []):
                    out += [val & 0xff, (val >> 8) & 0xff][:width]
                refs = ikeys.get(label, [])
                if inline_n >= len(refs) or not refs[inline_n][0]:
                    die("%s.%s: no text after %s" % (map_name, label, parts[0]))
                key, here = refs[inline_n]
                inline_n += 1
                v = value(key)
                if v is None:
                    die("%s.%s: %s prints %s, which is not a text"
                        % (map_name, label, parts[0], key))
                out += [v & 0xff, (v >> 8) & 0xff]
                if here:
                    # Skip exactly those bytes, up to their `done`: the next line
                    # may be another command, not more text.
                    while i < len(body):
                        head = body[i].split(";")[0].strip().split(None, 1)
                        i += 1
                        if head and head[0] in ("done", "prompt"):
                            break
                continue
            if parts[0] == "farjumptext":
                # There are no banks in the port: a far text pointer and a near
                # one are both an index into pc_texts, so one opcode serves both.
                parts[0] = "jumptext"
            args = split_args(parts[1]) if len(parts) > 1 else []
            # How many bits of flag/event state the whole build has to hold: the
            # VM shares one bit array, so the largest index wins.
            key = {"checkevent": "event", "setevent": "event",
                   "clearevent": "event", "checkflag": "flag",
                   "setflag": "flag", "clearflag": "flag"}.get(parts[0])
            if key and args:
                v = value(args[0].strip())
                if isinstance(v, int):
                    stats[key] = max(stats[key], v)
                else:
                    extern.discard(args[0].strip())
            for val, width in script_sizes(defs, parts[0], args):
                if not isinstance(val, str):
                    out += [val & 0xff, (val >> 8) & 0xff][:width]
                    continue
                v = value(val)
                if v is None:
                    out += [0xff] * width
                    continue
                if width == 1:
                    out.append(v & 0xff)
                else:
                    out += [v & 0xff, (v >> 8) & 0xff][:width]
        return out, extern

    sizes, locals_rel, off = {}, {}, 0
    for label, body in scripts:
        locals_rel[label] = {}
        try:
            sizes[label] = len(assemble(label, body, {}, locals_rel[label])[0])
        except Unsupported as exc:
            raise Unsupported("%s: %s" % (label, exc))
    labels = {}
    for label, _ in scripts:
        labels[label] = off
        off += sizes[label]
    # Movement data starts where the scripts end; its labels are offsets into
    # this same blob, which is what an `applymovement` operand becomes.
    mov_bytes, mov_at = movements or ([], {})
    for label, here in mov_at.items():
        labels.setdefault(label, off + here)

    blob, extern = [], set()
    for label, body in scripts:
        data, ext = assemble(label, body, labels, locals_rel[label])
        if len(data) != sizes[label]:
            die("%s.%s: size changed between passes (%d != %d)"
                % (map_name, label, len(data), sizes[label]))
        blob += data
        extern |= ext
    blob += list(mov_bytes)

    # The objects last, so every label offset above keeps its value: an object's
    # script is a one-command region appended after the last script.
    obj_offsets, missing = [], []
    for i, spec in enumerate(obj_specs):
        if spec is None:
            obj_offsets.append(0xffff)
            continue
        if spec[0] == "label":
            if spec[1] not in labels:
                missing.append(spec[1])
                obj_offsets.append(0xffff)
                continue
            obj_offsets.append(labels[spec[1]])
            continue
        _, macro, arg = spec
        name = "_obj%d" % i
        data, ext = assemble(name, ["%s %s" % (macro, arg)], labels, {})
        obj_offsets.append(len(blob))
        blob += data
        extern |= ext
    # And then the bg_event cells, in the same blob: a sign's script is an offset
    # of the same map, a jump-text sign is a three byte script of its own, and a
    # bookshelf's std script is copied in whole.
    bg_offsets, bg_flags = [], []
    jump = dict(defs["opcodes"])["jumptext_command"]
    for i, (_, _, _, spec) in enumerate(bg_events):
        if spec is None:
            bg_offsets.append(0xffff)
            bg_flags.append(0xffff)
            continue
        kind = spec[0]
        if kind == "label":
            if spec[1] not in labels:
                missing.append(spec[1])
                bg_offsets.append(0xffff)
            else:
                bg_offsets.append(labels[spec[1]])
            bg_flags.append(0xffff)
            continue
        if kind == "text":
            if spec[1] not in text_index:
                missing.append(spec[1])
                bg_offsets.append(0xffff)
                bg_flags.append(0xffff)
                continue
            t = text_index[spec[1]]
            bg_offsets.append(len(blob))
            bg_flags.append(0xffff)
            blob += [jump, t & 0xff, (t >> 8) & 0xff]
            continue
        if kind == "std":
            if spec[1] not in (std_blocks or {}):
                die("%s: std script %s is not in engine/events/std_scripts.asm"
                    % (map_name, spec[1]))
            data, ext = assemble("%s_std%d" % (map_name, i), std_blocks[spec[1]],
                                 labels, {})
            bg_offsets.append(len(blob))
            bg_flags.append(0xffff)
            blob += data
            extern |= ext
            continue
        # A hidden item: the game gives the item and sets the flag before it
        # runs this, so the script is only the message.
        _, item, flag, key = spec
        # game.c writes this flag, so the shared bit array has to hold it.
        stats["flag"] = max(stats["flag"], flag)
        data, ext = assemble("%s_item%d" % (map_name, i),
                             ["opentext", "jumptext %s" % key], labels, {})
        bg_offsets.append(len(blob))
        bg_flags.append(flag)
        blob += data
        extern |= ext
    # And the coord events: each one names a script of this same file, which is
    # already a labelled region of the blob.
    coord_offsets = []
    for (_, _, _, label) in coords:
        if label not in labels:
            missing.append(label)
            coord_offsets.append(0xffff)
        else:
            coord_offsets.append(labels[label])
    if missing:
        print("scripts   %-22s script(s) not in this file: %s"
              % (map_name, ", ".join(missing)))
    return {"labels": labels, "bytes": bytes(blob), "extern": sorted(extern),
            "objects": obj_offsets, "bg": bg_offsets, "bg_flags": bg_flags,
            "bg_events": bg_events, "coords": coord_offsets,
            "coord_events": coords, "scene_count": len(scenes), "stats": stats,
            "movements": len(mov_at), "movement_bytes": len(mov_bytes),
            "locals": sum(len(v) for v in locals_rel.values())}


def emit_move(path, consts):
    """The movement step numbers as C names, in their own small header.

    ow/move.c has to name the step bytes, and it must not include gen/scripts.h:
    that header carries the blobs, and naming them from a second translation unit
    makes the linker keep a full copy of each one (see docs/06). A header of
    #defines costs nothing until it is used.
    """
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write("/* generated by tools/extract.py from Polished Crystal — do not edit */\n")
        f.write("#ifndef PC_GEN_MOVE_H\n#define PC_GEN_MOVE_H\n\n")
        f.write("/* movement steps: the `const movement_*` values of\n"
                " * macros/scripts/movement.asm, one byte each (step_sleep two) */\n")
        for name in sorted(consts):
            if name.startswith("movement_"):
                f.write("#define PCM_%-28s 0x%02x\n"
                        % (name[len("movement_"):].upper(), consts[name]))
        f.write("\n/* the movement types of an object_event (constants/map_object_constants.asm) */\n")
        for name in sorted(consts):
            if name.startswith("SPRITEMOVEDATA_"):
                f.write("#define PC_%s %d\n" % (name, consts[name]))
        f.write("\n#endif\n")


def emit_scripts(path, per_map, defs, text_index, consts):
    """One header with the opcode names, the blobs and the label offsets."""
    with open(path, "w") as f:
        f.write("/* generated by tools/extract.py from Polished Crystal — do not edit */\n")
        f.write("#ifndef PC_GEN_SCRIPTS_H\n#define PC_GEN_SCRIPTS_H\n#include <stdint.h>\n\n")
        f.write("/* opcodes: the `const *_command` order of macros/scripts/events.asm */\n")
        for name, code in defs["opcodes"]:
            f.write("#define PCS_%-26s 0x%02x\n" % (name[:-len("_command")].upper(), code))
        f.write("/* bg_event types, in the original's order (constants/script_constants.asm) */\n")
        for name in ("BGEVENT_READ", "BGEVENT_UP", "BGEVENT_DOWN", "BGEVENT_RIGHT",
                     "BGEVENT_LEFT", "BGEVENT_IFSET", "BGEVENT_IFNOTSET",
                     "BGEVENT_JUMPTEXT", "BGEVENT_JUMPSTD", "BGEVENT_GROTTOITEM"):
            if name not in consts:
                die("constants/script_constants.asm: no %s" % name)
            f.write("#define PC_%s %d\n" % (name, consts[name]))
        item_base = max(consts["BGEVENT_GROTTOITEM"], consts["BGEVENT_JUMPSTD"]) + 1
        f.write("/* hidden item: `kind - PC_BGEVENT_ITEM` is the item id */\n")
        f.write("#define PC_BGEVENT_ITEM %d\n" % item_base)
        f.write("\n/* text pointers inside a script are indexes into pc_texts, not addresses */\n")
        f.write("#define PCS_TEXT_INDEX_COUNT %d\n" % len(text_index))
        f.write("#define PCS_EXTERN 0xffff\n\n")
        # Engine flags and map events share one bit array in the VM, so the size
        # is the largest index any assembled script touches, plus one.
        top = max([1] + [d["stats"][k] + 1 for _, d in per_map
                         for k in ("event", "flag")])
        f.write("/* bits of flag/event state the scripts of this build use */\n")
        f.write("#define PC_SCRIPT_FLAG_BYTES %d\n\n" % ((top + 7) // 8))
        for i, (map_name, data) in enumerate(per_map):
            f.write("static const uint8_t scr%d_%s[%d] = {\n" % (i, map_name, len(data["bytes"])))
            for j in range(0, len(data["bytes"]), 16):
                f.write("\t" + ",".join("0x%02x" % b for b in data["bytes"][j:j + 16]) + ",\n")
            f.write("};\n\n")
        f.write("typedef struct { const char *label; uint16_t offset; } PcScriptLabel;\n")
        f.write("/* A bg_event: the cell (x, y) answers to A. `kind` is the original's\n"
                " * BGEVENT_* type byte; READ, the four directional kinds, JUMPTEXT,\n"
                " * JUMPSTD and the hidden items have a script here ($ffff otherwise).\n"
                " * `flag` hides a hidden item once it has been taken ($ffff: none),\n"
                " * which is the test BGEVENT_ITEM does before it reads. */\n")
        f.write("typedef struct { uint8_t x, y, kind; uint16_t script;\n"
                "    uint16_t flag; } PcBgEvent;\n")
        f.write("/* A coord_event: ending a step on (x, y) runs `script` while the map's\n"
                " * scene is `scene`, or from any scene when it is SCENE_ALWAYS (-1).\n"
                " * `script` is $ffff when the extractor could not find the label. */\n")
        f.write("typedef struct { uint8_t x, y; int8_t scene; uint16_t script;\n"
                "    } PcCoordEvent;\n")
        f.write("/* `map_id` (GROUP_x, MAP_x): what setmapscene/checkmapscene put in a\n"
                " * script, and the only way to find another map's scene byte. `slot`\n"
                " * is the position in pc_script_maps. */\n")
        f.write("typedef struct { uint8_t group, number, slot; } PcSceneMap;\n")
        f.write("/* map_index is the position in pc_maps, so the game can find a map's\n"
                " * bytecode by the map it loaded. objects[] is one script offset per\n"
                " * object_event, bg[] one per bg_event, coords[] one per coord_event.\n"
                " * scene_count is the map's scene table size (0: it has none). */\n")
        f.write("typedef struct { const char *map; int8_t map_index; const uint8_t *bytes;\n"
                "    uint16_t len; const PcScriptLabel *labels; uint16_t label_count;\n"
                "    const uint16_t *objects; uint8_t object_count;\n"
                "    const PcBgEvent *bg; uint8_t bg_count;\n"
                "    const PcCoordEvent *coords; uint8_t coord_count;\n"
                "    uint8_t scene_count; } PcScriptMap;\n\n")
        for i, (map_name, data) in enumerate(per_map):
            f.write("static const PcScriptLabel scr%d_labels[%d] = {\n"
                    % (i, len(data["labels"])))
            for label, off in data["labels"].items():
                f.write('\t{ "%s", %d },\n' % (label, off))
            f.write("};\n\n")
        for i, (map_name, data) in enumerate(per_map):
            if not data["objects"]:
                continue
            f.write("static const uint16_t scr%d_objects[%d] = {\n"
                    % (i, len(data["objects"])))
            for j in range(0, len(data["objects"]), 8):
                f.write("\t" + ",".join("%5d" % o if o != 0xffff else "0xffff"
                                       for o in data["objects"][j:j + 8]) + ",\n")
            f.write("};\n\n")
        for i, (map_name, data) in enumerate(per_map):
            if not data["bg"]:
                continue
            f.write("static const PcBgEvent scr%d_bg[%d] = {\n" % (i, len(data["bg"])))
            for (x, y, kind, _), off, fl in zip(data["bg_events"], data["bg"],
                                               data["bg_flags"]):
                f.write("\t{%3d, %3d, %2d, %s, %s},\n"
                        % (x, y, kind, "%5d" % off if off != 0xffff else "0xffff",
                           "%5d" % fl if fl != 0xffff else "0xffff"))
            f.write("};\n\n")
        for i, (map_name, data) in enumerate(per_map):
            if not data["coords"]:
                continue
            f.write("static const PcCoordEvent scr%d_coords[%d] = {\n"
                    % (i, len(data["coords"])))
            for (x, y, scene, _), off in zip(data["coord_events"], data["coords"]):
                f.write("\t{%3d, %3d, %3d, %s},\n"
                        % (x, y, scene,
                           "%5d" % off if off != 0xffff else "0xffff"))
            f.write("};\n\n")
        scene_maps = [(i, d) for i, (_, d) in enumerate(per_map) if d["scene_count"]]
        f.write("/* the maps with a scene table, as MapScenes lists them */\n")
        f.write("#define PC_SCENE_MAP_COUNT %d\n" % len(scene_maps))
        f.write("static const PcSceneMap pc_scene_maps[PC_SCENE_MAP_COUNT] = {\n")
        for i, d in scene_maps:
            f.write("\t{%3d, %3d, %2d},\n" % (d["group"], d["number"], i))
        f.write("};\n\n")
        f.write("#define PC_SCRIPT_MAP_COUNT %d\n" % len(per_map))
        f.write("static const PcScriptMap pc_script_maps[PC_SCRIPT_MAP_COUNT] = {\n")
        for i, (map_name, data) in enumerate(per_map):
            f.write('\t{ "%s", %d, scr%d_%s, %d, scr%d_labels, %d, %s, %d, %s, %d, '
                    '%s, %d, %d },\n'
                    % (map_name, data["map_index"], i, map_name, len(data["bytes"]), i,
                       len(data["labels"]),
                       "scr%d_objects" % i if data["objects"] else "0",
                       len(data["objects"]),
                       "scr%d_bg" % i if data["bg"] else "0",
                       len(data["bg"]),
                       "scr%d_coords" % i if data["coords"] else "0",
                       len(data["coords"]), data["scene_count"]))
        f.write("};\n\n#endif\n")


def emit_font(path, primary, common):
    """The 8x8 font as 1bpp rows, 1 = ink, bit 7 = leftmost pixel.

    The game loads 114 tiles from normal.png at tile $80 (so 'A' is tile 0) and 6
    from common.png at $f2. Code $7f (space) is not art: the textbox is already
    paper, so the renderer leaves the cell empty.
    """
    with open(path, "w") as f:
        f.write("/* generated by tools/extract.py from Polished Crystal — do not edit */\n")
        f.write("#ifndef PC_GEN_FONT_H\n#define PC_GEN_FONT_H\n#include <stdint.h>\n\n")
        f.write("#define PC_FONT_FIRST   0x80   /* first code in pc_font */\n")
        f.write("#define PC_FONT_TILES   %d\n" % len(primary))
        f.write("static const uint8_t pc_font[PC_FONT_TILES][8] = {\n")
        for i, t in enumerate(primary):
            f.write("\t{" + ",".join("0x%02x" % b for b in t) + "}, /* %02x */\n"
                    % (0x80 + i))
        f.write("};\n\n")
        f.write("#define PC_FONT_COMMON_FIRST 0xf2\n")
        f.write("#define PC_FONT_COMMON_TILES %d\n" % len(common))
        f.write("static const uint8_t pc_font_common[PC_FONT_COMMON_TILES][8] = {\n")
        for i, t in enumerate(common):
            f.write("\t{" + ",".join("0x%02x" % b for b in t) + "}, /* %02x */\n"
                    % (0xf2 + i))
        f.write("};\n\n#endif\n")


def emit_world(path, maps, tilesets, sprites, player, pals, pals_indoor):
    """One header for the whole world: tilesets, sprites, maps, warps, objects.

    Maps share tilesets (Johto's three starting maps all use
    johto_traditional, and four houses share johto_house), so a tileset is
    emitted once and referenced by index; that keeps flash use proportional to
    the distinct art, not to the map count.
    """
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write("/* generated by tools/extract.py from Polished Crystal — do not edit */\n")
        f.write("#ifndef PC_GEN_WORLD_H\n#define PC_GEN_WORLD_H\n#include <stdint.h>\n\n")

        names = ["gray", "red", "green", "water", "yellow", "brown", "roof", "text"]
        f.write("#define BG_PAL_COUNT %d\n" % len(pals))
        f.write("/* BG palettes, straight from the game: 8 slots of 4 RGB565 colours.\n"
                " * Outdoor maps use the `day` group (the port has no clock yet);\n"
                " * indoor maps use `indoor`, which the original picks at any hour. */\n")
        for label, table in (("bg_pal", pals), ("bg_pal_indoor", pals_indoor)):
            f.write("static const uint16_t %s[BG_PAL_COUNT][4] = {\n" % label)
            for i, p in enumerate(table):
                f.write("\t{" + ",".join("0x%04x" % c for c in p) + "}, /* %s */\n"
                        % names[i])
            f.write("};\n")
        f.write("\n")

        f.write("/* ---- tilesets ---- */\n")
        f.write("#define PC_TS_COUNT %d\n" % len(tilesets))
        for ti, ts in enumerate(tilesets):
            f.write("/* %s: %d tiles, %d metatiles, 2bpp planar */\n"
                    % (ts["name"], len(ts["tiles"]), len(ts["mts"])))
            f.write("#define PC_TS_%s %d\n" % (ts["name"].upper(), ti))
            f.write("static const uint8_t ts%d_tiles[%d][16] = {\n"
                    % (ti, len(ts["tiles"])))
            for t in ts["tiles"]:
                f.write("\t{" + ",".join("0x%02x" % b for b in t) + "},\n")
            f.write("};\n")
            f.write("static const uint8_t ts%d_mt_tiles[%d][16] = {\n"
                    % (ti, len(ts["mts"])))
            for m in ts["mts"]:
                f.write("\t{" + ",".join("%3d" % b for b in m) + "},\n")
            f.write("};\n")
            f.write("/* per-tile BG palette index, same 4x4 order as mt_tiles */\n")
            f.write("static const uint8_t ts%d_mt_pal[%d][16] = {\n"
                    % (ti, len(ts["mts"])))
            for m in ts["mtpal"]:
                f.write("\t{" + ",".join("%d" % b for b in m) + "},\n")
            f.write("};\n")
            f.write("static const uint8_t ts%d_mt_coll[%d] = {\n" % (ti, len(ts["colls"])))
            for i in range(0, len(ts["colls"]), 20):
                f.write("\t" + ",".join("0x%02x" % c for c in ts["colls"][i:i + 20])
                        + ",\n")
            f.write("};\n\n")

        f.write("typedef struct {\n"
                "    const uint8_t (*tiles)[16];      /* 2bpp sheet */\n"
                "    const uint8_t (*mt_tiles)[16];   /* 16 tile ids per metatile */\n"
                "    const uint8_t (*mt_pal)[16];     /* BG palette slot per tile */\n"
                "    const uint8_t *mt_coll;          /* blocked-quadrant mask per metatile */\n"
                "} PcTileset;\n\n")
        f.write("static const PcTileset pc_tilesets[PC_TS_COUNT] = {\n")
        for ti, ts in enumerate(tilesets):
            f.write('\t{ ts%d_tiles, ts%d_mt_tiles, ts%d_mt_pal, ts%d_mt_coll }, /* %s */\n'
                    % (ti, ti, ti, ti, ts["name"]))
        f.write("};\n\n")

        f.write("/* ---- overworld sprites (16x96: 6 frames of 16x16 = 4 tiles) ---- */\n")
        f.write("#define SPR_FRAMES 6\n#define SPR_TILES_PER_FRAME 4\n")
        f.write("typedef struct { const char *name; const uint8_t (*tiles)[16]; } PcSprite;\n")
        for name, frames in sprites:
            f.write("static const uint8_t spr_%s[%d][16] = {\n" % (name, len(frames)))
            for t in frames:
                f.write("\t{" + ",".join("0x%02x" % b for b in t) + "},\n")
            f.write("};\n")
        f.write("static const uint8_t spr_player[%d][16] = {\n" % len(player))
        for t in player:
            f.write("\t{" + ",".join("0x%02x" % b for b in t) + "},\n")
        f.write("};\n")
        f.write("#define PC_SPRITE_COUNT %d\n" % len(sprites))
        f.write("static const PcSprite pc_sprites[PC_SPRITE_COUNT] = {\n")
        for name, _ in sprites:
            f.write('\t{ "%s", spr_%s },\n' % (name, name))
        f.write("};\n\n")

        f.write("/* ---- warps ----\n"
                " * x,y is the cell in this map; map/warp say where it leads (map = -1:\n"
                " * the destination is not part of this build, so the door is shut).\n"
                " * kind 0 = door: warps when the step onto it ends, and the player is\n"
                " * pushed one cell out of the doorway on arrival; kind 1 = warp carpet:\n"
                " * fires while standing on it facing `dir` (an ow/dir.h direction);\n"
                " * kind 2 = everything else that warps (stairs, cave mouths, panels). */\n"
                "enum { PC_WARP_DOOR, PC_WARP_CARPET, PC_WARP_STEP };\n"
                "typedef struct {\n"
                "    uint8_t x, y;\n"
                "    int8_t map;                /* index into pc_maps, -1 = not in this build */\n"
                "    uint8_t warp;              /* 1-based event number in the destination */\n"
                "    uint8_t kind;\n"
                "    uint8_t dir;               /* carpet direction that triggers it */\n"
                "} PcWarp;\n\n")

        f.write("/* ---- connections ----\n"
                " * A shared edge, not a door: walking into the edge cell of this map\n"
                " * puts the player on the opposite edge of `map`, shifted `offset`\n"
                " * blocks along it (the original's strip offsets, -2 cells per block).\n"
                " * `dir` is the ow/dir.h direction you walk to leave this map;\n"
                " * map = -1 means the other side is not in this build, so the edge\n"
                " * stays solid. */\n"
                "typedef struct {\n"
                "    uint8_t dir;\n"
                "    int8_t map;\n"
                "    int8_t offset;\n"
                "} PcConnection;\n\n")

        f.write("/* ---- object events: pixel coords on the 16px grid ----\n"
                " * dir: 0 down, 1 up, 2 left, 3 right; anim: 0 stand, 1 spin ccw,\n"
                " * 2 spin cw, 3 spin randomly (in place). */\n")
        f.write("typedef struct {\n"
                "    uint16_t x, y;\n"
                "    uint8_t sprite;\n"
                "    uint8_t dir;\n"
                "    uint8_t anim;\n"
                "    uint8_t move;              /* SPRITEMOVEDATA_*, the original's number */\n"
                "    int16_t text;              /* index into pc_texts, -1 = none */\n"
                "    uint16_t script;           /* offset in the map's script blob, $ffff = none */\n"
                "} PcObject;\n\n")
        f.write("typedef struct {\n"
                "    const char *name;\n"
                "    uint8_t w, h;               /* size in blocks (32x32 px) */\n"
                "    const uint8_t *blocks;\n"
                "    uint8_t tileset;            /* index into pc_tilesets */\n"
                "    const PcObject *objects;\n"
                "    uint8_t object_count;\n"
                "    const PcWarp *warps;\n"
                "    uint8_t warp_count;\n"
                "    uint8_t pal;                /* 0 = bg_pal (outdoor), 1 = bg_pal_indoor */\n"
                "    uint8_t spawn_x, spawn_y;   /* the game's respawn point, 0 = none */\n"
                "    const PcConnection *conns;\n"
                "    uint8_t conn_count;\n"
                "} PcMapDef;\n\n")

        for m in maps:
            ident = ctok(m["name"])
            w, h = m["w"], m["h"]
            f.write("/* ---- %s: %d x %d blocks (%d x %d px) ---- */\n"
                    % (m["name"], w, h, w * 32, h * 32))
            f.write("static const uint8_t blk_%s[%d] = {\n" % (ident, len(m["blocks"])))
            for i in range(0, len(m["blocks"]), 16):
                f.write("\t" + ",".join("%3d" % b for b in m["blocks"][i:i + 16]) + ",\n")
            f.write("};\n")
            if m["objects"]:
                f.write("static const PcObject obj_%s[%d] = {\n"
                        % (ident, len(m["objects"])))
                for (tx, ty, si, direction, anim, move, script), text in zip(
                        m["objects"], m["obj_text"]):
                    f.write("\t{%4d,%4d,%2d,%d,%d,%2d,%3d,%s},  /* tile %d,%d */\n"
                            % (tx * 16, ty * 16, si, direction, anim, move, text,
                               "%5d" % script if script != 0xffff else "0xffff",
                               tx, ty))
                f.write("};\n")
            if m["warps"]:
                f.write("static const PcWarp warp_%s[%d] = {\n"
                        % (ident, len(m["warps"])))
                for wp in m["warps"]:
                    f.write("\t{%3d,%3d,%3d,%d,%d,%d},  /* -> %s #%d */\n"
                            % (wp["x"], wp["y"], wp["map"], wp["warp"], wp["kind"],
                               wp["dir"], wp["dest"], wp["warp"]))
                f.write("};\n")
            if m["conns"]:
                f.write("static const PcConnection conn_%s[%d] = {\n"
                        % (ident, len(m["conns"])))
                for cn in m["conns"]:
                    f.write("\t{%d,%3d,%3d},  /* %s -> %s, offset %d */\n"
                            % (cn["dir"], cn["map"], cn["offset"], cn["side"],
                               cn["dest"], cn["offset"]))
                f.write("};\n")
            f.write("\n")

        f.write("#define PC_MAP_COUNT %d\n" % len(maps))
        for mi, m in enumerate(maps):
            f.write("#define PC_MAP_%s %d\n" % (m["name"].upper(), mi))
        f.write("static const PcMapDef pc_maps[PC_MAP_COUNT] = {\n")
        for mi, m in enumerate(maps):
            ident = ctok(m["name"])
            f.write('\t{ "%s", %d, %d, blk_%s, %d, %s, %d, %s, %d, %d, %d, %d, %s, %d },\n'
                    % (m["name"], m["w"], m["h"], ident, m["ts"],
                       "obj_" + ident if m["objects"] else "0", len(m["objects"]),
                       "warp_" + ident if m["warps"] else "0", len(m["warps"]),
                       m["pal"], m["spawn"][0], m["spawn"][1],
                       "conn_" + ident if m["conns"] else "0", len(m["conns"])))
        f.write("};\n\n#endif\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--maps",
                    default="NewBarkTown,Route29,CherrygroveCity,ElmsLab,"
                            "PlayersHouse1F,PlayersNeighborsHouse,LyrasHouse1F,"
                            "ElmsHouse",
                    help="comma-separated map names, in map-slot order")
    ap.add_argument("--player", default="chris", help="player walker sheet name")
    ap.add_argument("--pc", default=os.path.join(os.path.dirname(__file__), "..",
                                                 "vendor", "polishedcrystal"))
    ap.add_argument("--out", default=os.path.join(os.path.dirname(__file__), "..",
                                                  "game", "src", "gen"))
    args = ap.parse_args()
    pc = os.path.abspath(args.pc)
    out = os.path.abspath(args.out)

    tilesets = []          # name -> tables, in first-use order
    ts_index = {}
    sprites = []           # (name, frames), shared by every map
    maps = []
    charmap = load_charmap(pc)
    ckeys = sorted(charmap, key=len, reverse=True)   # maximal-munch order
    texts_all = []         # (label, bytes), deduplicated across maps
    text_index = {}

    wanted = [n.strip() for n in args.maps.split(",") if n.strip()]
    map_slot = {name: i for i, name in enumerate(wanted)}
    const_name = map_const_to_name(pc)
    map_spawn = map_spawns(pc)
    coll_name_cache = {}

    # Map scripts: the original's own script bytecode, assembled from the .asm
    # macro calls (never hand-written byte tables). They come before world.h
    # because every object_event needs the offset of its own script.
    defs = script_defs(pc)
    per_map, bytes_total, extern_all = [], 0, set()
    # BGEVENT_JUMPSTD cells name a script of engine/events/std_scripts.asm by its
    # table index; the port inlines it, so the table is read once here.
    std_blocks, std_order = std_scripts(pc, defs["consts"])
    item_names_all = item_names(pc)
    map_ids_all = map_ids(pc)
    # Scene constants are global in the original, and a script may set another
    # map's scene, so every map's names resolve everywhere.
    scene_consts = {}
    for name in wanted:
        for i, sn in enumerate(map_scenes(pc, name)):
            if sn is not None:
                scene_consts[sn] = i
    # Movement data lives inside the map's own blob (it is fallthrough data, not
    # a table of pointers), so a script may only apply movement defined in the
    # same file. Mapping label -> map lets a cross-file reference fail here
    # instead of becoming a $ffff the game would report at run time.
    movement_owner = {}
    for name in wanted:
        for label, _ in map_movements(pc, name, defs)[1].items():
            movement_owner.setdefault(label, name)

    for map_name in wanted:
        w, h = map_dims(pc, map_name)
        ts = map_tileset(pc, map_name)
        blocks = map_blocks(pc, map_name, w, h)
        env = map_environment(pc, map_name)

        if ts not in ts_index:
            tiles = load_2bpp_sheet(tileset_sheet(pc, ts))
            mts = metatiles(pc, ts)
            mtpal = metatile_palettes(pc, ts)
            colls = collisions(pc, ts)
            if len(colls) != len(mts) or len(mtpal) != len(mts):
                die("%s: collision/attribute count %d/%d != metatile count %d"
                    % (ts, len(colls), len(mtpal), len(mts)))
            if max(max(m) for m in mts) >= len(tiles):
                die("%s: metatile references tile %d but the sheet has %d"
                    % (ts, max(max(m) for m in mts), len(tiles)))
            ts_index[ts] = len(tilesets)
            tilesets.append({"name": ts, "tiles": tiles, "mts": mts,
                             "mtpal": mtpal, "colls": colls})
        ti = ts_index[ts]
        if max(blocks) >= len(tilesets[ti]["mts"]):
            die("%s: block id %d out of range (%d metatiles)"
                % (map_name, max(blocks), len(tilesets[ti]["mts"])))

        texts = map_texts(pc, map_name, charmap, ckeys)
        # Every text of the map goes in the table, not only the ones an object
        # points at: scripts reach for the rest with `showtext`.
        for label, data in texts.items():
            if label not in text_index:
                text_index[label] = len(texts_all)
                texts_all.append((label, data))
        obj_labels = map_object_texts(pc, map_name, texts, charmap, ckeys)

        objects = []
        obj_text = []
        obj_specs = []
        for (bx, by, spr, direction, anim, move, spec), found in zip(
                map_objects(pc, map_name, defs["consts"]), obj_labels):
            obj_specs.append(spec)
            name = spr[len("SPRITE_"):].lower()
            idx = next((i for i, (n, _) in enumerate(sprites) if n == name), -1)
            if idx < 0:
                frames = sprite_frames(pc, name)
                if not frames:
                    continue
                idx = len(sprites)
                sprites.append((name, frames))
            objects.append((bx, by, idx, direction, anim, move))
            if found is None:
                obj_text.append(-1)
            else:
                label, data = found
                if label not in text_index:
                    text_index[label] = len(texts_all)
                    texts_all.append((label, data))
                obj_text.append(text_index[label])

        # Warp events: the cell has to be a warp tile as well, or the original
        # would never fire it (CheckWarpCollision tests the collision byte).
        if ts not in coll_name_cache:
            coll_name_cache[ts] = collision_names(pc, ts)
        cnames = coll_name_cache[ts]
        warps = []
        for (wx, wy, dest_const, dest_warp) in map_warps(pc, map_name):
            if wx >= w * 2 or wy >= h * 2:
                die("%s: warp at %d,%d is outside the %dx%d-cell map"
                    % (map_name, wx, wy, w * 2, h * 2))
            mt = blocks[(wy // 2) * w + wx // 2]
            code = cnames[mt][(wx & 1) + 2 * (wy & 1)]
            kind, cdir = warp_kind(code)
            if code == "FLOOR":
                die("%s: warp %d,%d sits on a FLOOR tile, so it could never fire"
                    % (map_name, wx, wy))
            dest = const_name.get(dest_const)
            if dest is None:
                die("%s: warp destination %s is not a map name"
                    % (map_name, dest_const))
            slot = map_slot.get(dest, -1)
            warps.append({"x": wx, "y": wy, "map": slot, "warp": dest_warp,
                          "kind": kind, "dir": cdir, "dest": dest})
            if slot < 0:
                print("warp     %-18s %d,%d -> %s #%d NOT IN BUILD (shut)"
                      % (map_name, wx, wy, dest, dest_warp))

        # Map connections: a shared edge with the map next door. The two edges have
        # to line up once the offset is applied, or the player would walk into an
        # invisible wall (or onto a wall) on the other side.
        conns = []
        for cn in map_connections(pc, map_name, const_name):
            slot = map_slot.get(cn["name"], -1)
            side = cn["dir"]
            if slot >= 0:
                src = map_edge_cells(pc, map_name, side)
                dst = map_edge_cells(pc, cn["name"], CONNECTION_OPPOSITE[side])
                shift = 2 * cn["offset"]
                if side in ("west", "east"):
                    shared = {y for y in src if (y - shift) in dst}
                else:
                    shared = {x for x in src if (x - shift) in dst}
                if not shared:
                    die("%s: %s connection to %s (offset %d) shares no walkable "
                        "edge cell (source %s, destination %s)"
                        % (map_name, side, cn["name"], cn["offset"],
                           sorted(src), sorted(dst)))
            else:
                print("connect  %-18s %-5s -> %-18s NOT IN BUILD (solid edge)"
                      % (map_name, side, cn["name"]))
            conns.append({"dir": CONNECTION_DIRS[side], "map": slot,
                          "offset": cn["offset"], "side": side, "dest": cn["name"]})

        # The objects' scripts: an OBJECTTYPE_SCRIPT points at a label already in
        # the blob, an OBJECTTYPE_COMMAND becomes a one-command region at the end.
        # The bg_event cells go in the same blob for the same reason: a sign's
        # script is a script of this map, and a jump-text sign is `jumptext text`.
        sconsts = dict(defs["consts"])
        try:
            bg_events = map_bg_events(pc, map_name, sconsts, std_order)
        except Unsupported as exc:
            print("bg        %-22s SKIPPED: %s" % (map_name, exc))
            bg_events = []
        # A bookshelf's text is in data/text/std_text.asm, not in the map file, and
        # a hidden item's is _FoundItemText with the item's name filled in: both
        # have to be in the table before the scripts can point at them.
        for label, data in bg_event_texts(pc, bg_events, item_names_all, std_blocks,
                                          charmap, ckeys).items():
            if label not in text_index:
                text_index[label] = len(texts_all)
                texts_all.append((label, data))
        # Scenes and coord events. A scene is named by the map file (`scene_const`
        # or `scene_script`), and a coord event fires when a step ends on its cell
        # while that scene is active; both are checked here so a typo in the .asm
        # is an extraction error, not a trigger that silently never happens.
        scenes = map_scenes(pc, map_name)
        coords = map_coord_events(pc, map_name, scenes, w * 2, h * 2)
        movements = map_movements(pc, map_name, defs)
        try:
            sdata = map_scripts(pc, map_name, defs, text_index, obj_specs, bg_events,
                                std_blocks, scenes, coords, scene_consts, movements)
        except Unsupported as exc:
            print("scripts   %-22s SKIPPED: %s" % (map_name, exc))
            sdata = None
        if sdata is not None:
            for sym in sdata["extern"]:
                owner = movement_owner.get(sym)
                if owner and owner != map_name:
                    die("%s: a script applies %s, but that movement data is in "
                        "%s; movement stays in the map's own blob"
                        % (map_name, sym, owner))
            sdata["map_index"] = map_slot[map_name]
            sdata["group"], sdata["number"] = map_ids_all[map_id(pc, map_name)]
            per_map.append((map_name, sdata))
            bytes_total += len(sdata["bytes"])
            extern_all |= set(sdata["extern"])
            print("scripts   %-22s %2d labels, %4d B%s"
                  % (map_name, len(sdata["labels"]), len(sdata["bytes"]),
                     "" if not sdata["extern"] else
                     ", extern: " + ", ".join(sdata["extern"])))
            ran = sum(1 for o in sdata["bg"] if o != 0xffff)
            print("bg        %-22s %2d cells, %d with a script"
                  % (map_name, len(bg_events), ran))
            if scenes or coords:
                print("scene     %-22s %2d scenes, %2d coord event(s), %d with a script"
                      % (map_name, len(scenes), len(coords),
                         sum(1 for o in sdata["coords"] if o != 0xffff)))
            if sdata["movements"]:
                print("movement  %-22s %2d blocks, %3d B"
                      % (map_name, sdata["movements"], sdata["movement_bytes"]))
        scripts_for = sdata["objects"] if sdata is not None else [0xffff] * len(objects)
        objects = [o + (sc,) for o, sc in zip(objects, scripts_for)]

        maps.append({"name": map_name, "w": w, "h": h, "blocks": blocks, "ts": ti,
                     "objects": objects, "obj_text": obj_text, "warps": warps,
                     "conns": conns,
                     "pal": 1 if env in ("INDOOR", "GATE") else 0,
                     "spawn": map_spawn.get(map_name, (0, 0))})
        print("map      %-20s %2dx%-2d blocks (%3dx%-3d px)  tileset %-20s obj %d warp %d conn %d"
              % (map_name, w, h, w * 32, h * 32, ts, len(objects), len(warps),
                 len(conns)))

    pals = bg_palettes(pc)
    pals_indoor = bg_palettes(pc, "indoor")
    player = sprite_frames(pc, args.player)
    if not player:
        die("player sprite gfx/sprites/%s.png not found" % args.player)

    if len(maps) > 127:
        die("PcConnection.map is an int8_t: at most 127 maps fit in one build")
    path = os.path.join(out, "world.h")
    emit_world(path, maps, tilesets, sprites, player, pals, pals_indoor)
    print("tilesets  %d: %s" % (len(tilesets), ", ".join(t["name"] for t in tilesets)))
    print("sprites   %d: %s" % (len(sprites), ", ".join(n for n, _ in sprites)))
    print("wrote     %s (%d KB)" % (path, os.path.getsize(path) // 1024))

    tpath = os.path.join(out, "text.h")
    emit_text(tpath, texts_all, pc)
    print("texts     %d in %s (%d KB)"
          % (len(texts_all), tpath, os.path.getsize(tpath) // 1024))

    mpath = os.path.join(out, "move.h")
    emit_move(mpath, dict(defs["consts"]))
    print("movement  %d steps, %d types in %s"
          % (sum(1 for n in defs["consts"] if n.startswith("movement_")),
             sum(1 for n in defs["consts"] if n.startswith("SPRITEMOVEDATA_")), mpath))

    spath = os.path.join(out, "scripts.h")
    emit_scripts(spath, per_map, defs, text_index, dict(defs["consts"]))
    print("scriptsvm %d opcodes, %d B of bytecode in %s (%d KB)"
          % (len(defs["opcodes"]), bytes_total, spath,
             os.path.getsize(spath) // 1024))

    fpath = os.path.join(out, "font.h")
    emit_font(fpath, font_tiles(pc, "normal", 114), font_tiles(pc, "common", 6))
    print("font      114 + 6 tiles in %s (%d KB)"
          % (fpath, os.path.getsize(fpath) // 1024))

    # The four entries the port can show today, in the original's order (it
    # builds the list from wStatusFlags/wPartyCount at runtime, which is a save
    # concern). Save/Options/Exit/PokéGear/Quit come back with their screens.
    ui_src = menu_labels(pc)
    ui_wanted = (("POKEDEX", "PokedexString"), ("POKEMON", "PartyString"),
                 ("BAG", "PackString"), ("PLAYER", "StatusString"),
                 ("OPTIONS", "OptionString"))
    ui_labels = []
    for name, label in ui_wanted:
        if label not in ui_src:
            die("start_menu.asm: .%s not found" % label)
        ui_labels.append((name, encode_text(ui_src[label], charmap, ckeys)))
    upath = os.path.join(out, "ui.h")
    emit_ui(upath, ui_labels)
    print("menu      %d labels in %s: %s" % (len(ui_labels), upath,
          ", ".join(ui_src[l] for _, l in ui_wanted)))

    # The option screen: 13 options, 4/4/20/8/2/2/2/3/3/2/2/2/2 values.
    opts, done = option_table(pc)
    opath = os.path.join(out, "options.h")
    emit_options(opath, opts, done, charmap, ckeys)
    print("options   %d in %s: %s" % (len(opts), opath,
          ", ".join(n for _, n, _, _ in opts)))


if __name__ == "__main__":
    main()
