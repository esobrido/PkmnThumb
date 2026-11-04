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


def map_blocks(pc, map_name, w, h):
    raw = read(os.path.join(pc, "maps", map_name + ".ablk"), "rb")
    if len(raw) != w * h:
        die("%s.ablk is %d bytes, expected %d (%dx%d blocks)"
            % (map_name, len(raw), w * h, w, h))
    return list(raw)


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


def collisions(pc, ts):
    """Per metatile: one blocked mask, bits TL,TR,BL,BR (of the 32x32 block).

    The asm lists the four 16x16 quadrants as `tilecoll BL, TL, BR, TR`, so the
    argument order is remapped here to the screen order the renderer wants.
    """
    path = os.path.join(pc, "data/tilesets", ts + "_collision.asm")
    if not os.path.exists(path):
        die("no collision table for %s" % ts)
    order = (1, 3, 0, 2)          # asm arg index for TL, TR, BL, BR
    out = []
    for line in read(path).splitlines():
        line = line.split(";")[0].strip()
        m = re.match(r"tilecoll\s+(.+)$", line)
        if not m:
            continue
        codes = [c.strip() for c in m.group(1).split(",")]
        if len(codes) != 4:
            die("bad tilecoll line: " + line)
        mask = 0
        for bit, arg in enumerate(order):
            if codes[arg] not in COLL_WALKABLE:
                mask |= 1 << bit
        out.append(mask)
    return out


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


def map_objects(pc, map_name):
    """object_event rows -> (x, y, SPRITE_NAME).

    Coordinates are in 16x16 px tiles (the same grid the player walks on), not
    in 32px map blocks — an object_event y of 15 on a 9-block-tall map only fits
    that way.
    """
    txt = read(os.path.join(pc, "maps", map_name + ".asm"))
    out = []
    pat = re.compile(r"^\s*object_event\s+(\d+)\s*,\s*(\d+)\s*,\s*(SPRITE_\w+)", re.M)
    for m in pat.finditer(txt):
        out.append((int(m.group(1)), int(m.group(2)), m.group(3)))
    return out


# ---------------------------------------------------------------------- emitter
def emit_header(path, map_name, w, h, blocks, ts, tiles, mts, mtpal, pals, colls,
                sprites, objects, player):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write("/* generated by tools/extract.py from Polished Crystal — do not edit */\n")
        f.write("#ifndef PC_GEN_%s_H\n#define PC_GEN_%s_H\n#include <stdint.h>\n\n"
                % (map_name.upper(), map_name.upper()))

        f.write("/* ---- map: %s (%d x %d blocks, 32x32 px each) ---- */\n" % (map_name, w, h))
        f.write("#define MAP_BLOCK_W %d\n#define MAP_BLOCK_H %d\n" % (w, h))
        f.write("static const uint8_t map_blocks[%d] = {\n" % len(blocks))
        for i in range(0, len(blocks), 16):
            f.write("\t" + ",".join("%3d" % b for b in blocks[i:i + 16]) + ",\n")
        f.write("};\n\n")

        f.write("/* ---- tileset %s: %d tiles, 2bpp planar ---- */\n" % (ts, len(tiles)))
        f.write("#define TS_TILE_COUNT %d\n" % len(tiles))
        f.write("static const uint8_t ts_tiles[%d][16] = {\n" % len(tiles))
        for t in tiles:
            f.write("\t{" + ",".join("0x%02x" % b for b in t) + "},\n")
        f.write("};\n\n")

        f.write("/* ---- BG palettes (day): 8 x 4 RGB565, straight from the game ---- */\n")
        f.write("#define BG_PAL_COUNT %d\n" % len(pals))
        f.write("static const uint16_t bg_pal[BG_PAL_COUNT][4] = {\n")
        names = ["gray", "red", "green", "water", "yellow", "brown", "roof", "text"]
        for i, p in enumerate(pals):
            f.write("\t{" + ",".join("0x%04x" % c for c in p) + "}, /* %s */\n"
                    % names[i])
        f.write("};\n\n")

        f.write("/* ---- metatiles: 16 tile ids + 1 collision mask (bit=blocked) ---- */\n")
        f.write("#define MT_COUNT %d\n" % len(mts))
        f.write("static const uint8_t mt_tiles[%d][16] = {\n" % len(mts))
        for m in mts:
            f.write("\t{" + ",".join("%3d" % b for b in m) + "},\n")
        f.write("};\n")
        f.write("/* per-tile BG palette index, same 4x4 order as mt_tiles */\n")
        f.write("static const uint8_t mt_pal[%d][16] = {\n" % len(mtpal))
        for m in mtpal:
            f.write("\t{" + ",".join("%d" % b for b in m) + "},\n")
        f.write("};\n")
        f.write("static const uint8_t mt_coll[%d] = {\n" % len(colls))
        for i in range(0, len(colls), 20):
            f.write("\t" + ",".join("0x%02x" % c for c in colls[i:i + 20]) + ",\n")
        f.write("};\n\n")

        f.write("/* ---- overworld sprites (16x96: 6 frames of 16x16 = 4 tiles) ---- */\n")
        f.write("#define SPR_FRAMES 6\n#define SPR_TILES_PER_FRAME 4\n")
        f.write("typedef struct { const char *name; const uint8_t (*tiles)[16]; } PcSprite;\n")
        for name, frames in sprites:
            f.write("static const uint8_t spr_%s[%d][16] = {\n" % (name, len(frames)))
            for t in frames:
                f.write("\t{" + ",".join("0x%02x" % b for b in t) + "},\n")
            f.write("};\n")
        f.write("#define PC_SPRITE_COUNT %d\n" % len(sprites))
        f.write("static const PcSprite pc_sprites[PC_SPRITE_COUNT] = {\n")
        for name, _ in sprites:
            f.write('\t{ "%s", spr_%s },\n' % (name, name))
        f.write("};\n\n")

        f.write("/* ---- the player's own walker sheet ---- */\n")
        f.write("static const uint8_t spr_player[%d][16] = {\n" % len(player))
        for t in player:
            f.write("\t{" + ",".join("0x%02x" % b for b in t) + "},\n")
        f.write("};\n\n")

        f.write("/* ---- object events: pixel coords on the 16px grid ---- */\n")
        f.write("#define PC_OBJECT_COUNT %d\n" % len(objects))
        f.write("static const uint8_t pc_objects[PC_OBJECT_COUNT][3] = {\n")
        for tx, ty, si in objects:
            f.write("\t{%3d,%3d,%3d},  /* tile %d,%d */\n"
                    % (tx * 16, ty * 16, si, tx, ty))
        f.write("};\n\n#endif\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--map", default="NewBarkTown")
    ap.add_argument("--player", default="chris", help="player walker sheet name")
    ap.add_argument("--pc", default=os.path.join(os.path.dirname(__file__), "..",
                                                 "vendor", "polishedcrystal"))
    ap.add_argument("--out", default=os.path.join(os.path.dirname(__file__), "..",
                                                  "game", "src", "gen"))
    args = ap.parse_args()
    pc = os.path.abspath(args.pc)
    out = os.path.abspath(args.out)

    w, h = map_dims(pc, args.map)
    ts = map_tileset(pc, args.map)
    blocks = map_blocks(pc, args.map, w, h)
    sheet = tileset_sheet(pc, ts)
    tiles = load_2bpp_sheet(sheet)
    mts = metatiles(pc, ts)
    mtpal = metatile_palettes(pc, ts)
    pals = bg_palettes(pc)
    colls = collisions(pc, ts)
    if len(colls) != len(mts) or len(mtpal) != len(mts):
        die("collision/attribute count %d/%d != metatile count %d"
            % (len(colls), len(mtpal), len(mts)))

    tiles_in_map = set()
    for b in blocks:
        tiles_in_map.add(b)
    if max(blocks) >= len(mts):
        die("block id %d out of range (%d metatiles)" % (max(blocks), len(mts)))
    if max(max(m) for m in mts) >= len(tiles):
        die("metatile references tile %d but the sheet has %d"
            % (max(max(m) for m in mts), len(tiles)))

    sprites = []
    objects = []
    for bx, by, spr in map_objects(pc, args.map):
        name = spr[len("SPRITE_"):].lower()
        idx = next((i for i, (n, _) in enumerate(sprites) if n == name), -1)
        if idx < 0:
            frames = sprite_frames(pc, name)
            if not frames:
                continue
            idx = len(sprites)
            sprites.append((name, frames))
        objects.append((bx, by, idx))

    path = os.path.join(out, args.map.lower() + ".h")
    player = sprite_frames(pc, args.player)
    if not player:
        die("player sprite gfx/sprites/%s.png not found" % args.player)
    emit_header(path, args.map, w, h, blocks, ts, tiles, mts, mtpal, pals, colls,
                sprites, objects, player)
    print("map      %s  %dx%d blocks (%dx%d px)" % (args.map, w, h, w * 32, h * 32))
    print("tileset  %s  %s  %d tiles, %d metatiles"
          % (ts, os.path.basename(sheet), len(tiles), len(mts)))
    print("blocks    %d distinct, ids %d..%d" % (len(tiles_in_map), min(blocks), max(blocks)))
    print("sprites   %d: %s" % (len(sprites), ", ".join(n for n, _ in sprites)))
    print("objects   %d placed" % len(objects))
    print("wrote     %s" % path)


if __name__ == "__main__":
    main()
