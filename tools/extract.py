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


def map_objects(pc, map_name):
    """object_event rows -> (x, y, SPRITE_NAME, facing, anim).

    Coordinates are in 16x16 px tiles (the same grid the player walks on), not
    in 32px map blocks — an object_event y of 15 on a 9-block-tall map only fits
    that way. The movement type (`SPRITEMOVEDATA_*`) decides both the facing the
    NPC stands with and whether it animates in place.
    """
    txt = read(os.path.join(pc, "maps", map_name + ".asm"))
    out = []
    pat = re.compile(r"^\s*object_event\s+(\d+)\s*,\s*(\d+)\s*,\s*(SPRITE_\w+)\s*,"
                     r"\s*(SPRITEMOVEDATA_\w+)", re.M)
    for m in pat.finditer(txt):
        mv = m.group(4)
        out.append((int(m.group(1)), int(m.group(2)), m.group(3),
                    FACING.get(mv, 0), ANIM.get(mv, 0)))
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
    """Yield (label, body lines) for every top-level `Label:` in an asm file."""
    lines = txt.splitlines()
    for i, line in enumerate(lines):
        m = re.match(r"^([A-Za-z_]\w*):\s*$", line)
        if not m:
            continue
        body = []
        for nxt in lines[i + 1:]:
            if re.match(r"^[A-Za-z_]\w*:\s*$", nxt):
                break
            body.append(nxt)
        yield m.group(1), body


def parse_text_block(body, charmap, keys):
    """`text "..."/line/para/done` -> bytes, or None if it is not plain text.

    text_far/text_asm/text_ram/text_decimal hand control to something else (a
    text in another file, or code). Those are phase 2 work; skipping them keeps
    the extractor honest instead of emitting half a line.
    """
    out = bytearray()
    for raw in body:
        s = raw.strip()
        if not s or s.startswith(";"):
            continue
        m = re.match(r"(text|next1|next|line|cont|para|done|prompt)(?:\s+(.*))?$", s)
        if not m:
            if re.match(r"(text_far|text_asm|text_ram|text_decimal|text_sound|"
                        r"text_pause|text_promptbutton|text_today|text_start)\b", s):
                return None
            return None
        op, arg = m.group(1), (m.group(2) or "").strip()
        out.append(TEXT_MACRO[op])
        if op in ("done", "prompt"):
            return bytes(out)
        if arg:
            am = re.match(r'^"((?:[^"\\]|\\.)*)"$', arg)
            if not am:
                return None
            piece = am.group(1).replace('\\"', '"').replace("\\\\", "\\")
            out += encode_text(piece, charmap, keys)
    return None if not out else bytes(out) + b"\x52"


def map_texts(pc, map_name, charmap, keys):
    """Every plain-text label in a map, in file order: {label: bytes}.

    The map file holds the NPC scripts and their text; `text_far` and friends are
    skipped (they live elsewhere). Order is kept because the generated table is
    read side by side with the .asm by a human.
    """
    txt = map_source(pc, map_name)
    out = {}
    for label, body in label_blocks(txt):
        blk = parse_text_block(body, charmap, keys)
        if blk is None:
            continue
        head = next((l.strip() for l in body if l.strip()), "")
        if not re.match(r"text\b", head):
            continue
        out[label] = blk
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
    for i, raw in enumerate(body):
        m = re.match(r"\s*(?:if\w+_)?(showtext|writetext|jumptext\w*)\s+(\w+)", raw)
        if m and m.group(2) in texts:
            return (m.group(2), texts[m.group(2)])
        if re.match(r"\s*jumpthistext\w*\s*$", raw):
            blk = parse_text_block(body[i + 1:], charmap, keys)
            if blk is not None:
                return ("%s_%s_inline" % (map_name, script), blk)
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

    movement = set()
    for line in read(os.path.join(pc, "macros/scripts/movement.asm")).splitlines():
        m = re.match(r"MACRO\s+(\w+)", line.strip())
        if m:
            movement.add(m.group(1))

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
            "text": text_macros, "specials": specials, "consts": consts}


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


def map_scripts(pc, map_name, defs, text_index):
    """Assemble every script label of a map into one blob.

    Each entry point gets its own region, so a script can be run from its
    offset without knowing the map's layout. In-file jumps resolve to offsets
    in the blob; a pointer to a text becomes an index into `pc_texts`, because
    the port has no ROM addresses. Anything else (movement data, another map's
    script) becomes $FFFF and is reported, not silently zeroed.
    """
    blocks = list(asm_label_blocks(map_source(pc, map_name).splitlines()))
    header = next((body for label, body in blocks if label.endswith("_MapScriptHeader")), [])

    objects, scenes, n = {}, {}, 0
    for line in header:
        m = re.match(r"\s*const\s+(\w+)", line)
        if m:
            n += 1
            objects[m.group(1)] = n
            continue
        m = re.match(r"\s*scene_const\s+(\w+)", line)
        if m:
            scenes[m.group(1)] = len(scenes)
    consts = dict(defs["consts"])
    consts.update(scenes)

    def is_script(body):
        for line in body:
            s = line.split(";")[0].strip()
            if not s:
                continue
            name = s.split()[0]
            return name in defs["macros"] and name not in defs["movement"]
        return False

    scripts = [(label, body) for label, body in blocks if is_script(body)]
    if not scripts:
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
        for line in body:
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
            args = split_args(parts[1]) if len(parts) > 1 else []
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

    blob, extern = [], set()
    for label, body in scripts:
        data, ext = assemble(label, body, labels, locals_rel[label])
        if len(data) != sizes[label]:
            die("%s.%s: size changed between passes (%d != %d)"
                % (map_name, label, len(data), sizes[label]))
        blob += data
        extern |= ext
    return {"labels": labels, "bytes": bytes(blob), "extern": sorted(extern),
            "locals": sum(len(v) for v in locals_rel.values())}


def emit_scripts(path, per_map, defs, text_index):
    """One header with the opcode names, the blobs and the label offsets."""
    with open(path, "w") as f:
        f.write("/* generated by tools/extract.py from Polished Crystal — do not edit */\n")
        f.write("#ifndef PC_GEN_SCRIPTS_H\n#define PC_GEN_SCRIPTS_H\n#include <stdint.h>\n\n")
        f.write("/* opcodes: the `const *_command` order of macros/scripts/events.asm */\n")
        for name, code in defs["opcodes"]:
            f.write("#define PCS_%-26s 0x%02x\n" % (name[:-len("_command")].upper(), code))
        f.write("\n/* text pointers inside a script are indexes into pc_texts, not addresses */\n")
        f.write("#define PCS_TEXT_INDEX_COUNT %d\n" % len(text_index))
        f.write("#define PCS_EXTERN 0xffff\n\n")
        for i, (map_name, data) in enumerate(per_map):
            f.write("static const uint8_t scr%d_%s[%d] = {\n" % (i, map_name, len(data["bytes"])))
            for j in range(0, len(data["bytes"]), 16):
                f.write("\t" + ",".join("0x%02x" % b for b in data["bytes"][j:j + 16]) + ",\n")
            f.write("};\n\n")
        f.write("typedef struct { const char *label; uint16_t offset; } PcScriptLabel;\n")
        f.write("typedef struct { const char *map; const uint8_t *bytes; uint16_t len; "
                "const PcScriptLabel *labels; uint16_t label_count; } PcScriptMap;\n\n")
        for i, (map_name, data) in enumerate(per_map):
            f.write("static const PcScriptLabel scr%d_labels[%d] = {\n"
                    % (i, len(data["labels"])))
            for label, off in data["labels"].items():
                f.write('\t{ "%s", %d },\n' % (label, off))
            f.write("};\n\n")
        f.write("#define PC_SCRIPT_MAP_COUNT %d\n" % len(per_map))
        f.write("static const PcScriptMap pc_script_maps[PC_SCRIPT_MAP_COUNT] = {\n")
        for i, (map_name, data) in enumerate(per_map):
            f.write('\t{ "%s", scr%d_%s, %d, scr%d_labels, %d },\n'
                    % (map_name, i, map_name, len(data["bytes"]), i,
                       len(data["labels"])))
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

        f.write("/* ---- object events: pixel coords on the 16px grid ----\n"
                " * dir: 0 down, 1 up, 2 left, 3 right; anim: 0 stand, 1 spin ccw,\n"
                " * 2 spin cw, 3 spin randomly (in place). */\n")
        f.write("typedef struct {\n"
                "    uint16_t x, y;\n"
                "    uint8_t sprite;\n"
                "    uint8_t dir;\n"
                "    uint8_t anim;\n"
                "    int16_t text;              /* index into pc_texts, -1 = none */\n"
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
                for (tx, ty, si, direction, anim), text in zip(m["objects"], m["obj_text"]):
                    f.write("\t{%4d,%4d,%2d,%d,%d,%3d},  /* tile %d,%d */\n"
                            % (tx * 16, ty * 16, si, direction, anim, text, tx, ty))
                f.write("};\n")
            if m["warps"]:
                f.write("static const PcWarp warp_%s[%d] = {\n"
                        % (ident, len(m["warps"])))
                for wp in m["warps"]:
                    f.write("\t{%3d,%3d,%3d,%d,%d,%d},  /* -> %s #%d */\n"
                            % (wp["x"], wp["y"], wp["map"], wp["warp"], wp["kind"],
                               wp["dir"], wp["dest"], wp["warp"]))
                f.write("};\n")
            f.write("\n")

        f.write("#define PC_MAP_COUNT %d\n" % len(maps))
        for mi, m in enumerate(maps):
            f.write("#define PC_MAP_%s %d\n" % (m["name"].upper(), mi))
        f.write("static const PcMapDef pc_maps[PC_MAP_COUNT] = {\n")
        for mi, m in enumerate(maps):
            ident = ctok(m["name"])
            f.write('\t{ "%s", %d, %d, blk_%s, %d, %s, %d, %s, %d, %d, %d, %d },\n'
                    % (m["name"], m["w"], m["h"], ident, m["ts"],
                       "obj_" + ident if m["objects"] else "0", len(m["objects"]),
                       "warp_" + ident if m["warps"] else "0", len(m["warps"]),
                       m["pal"], m["spawn"][0], m["spawn"][1]))
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
        for (bx, by, spr, direction, anim), found in zip(map_objects(pc, map_name),
                                                       obj_labels):
            name = spr[len("SPRITE_"):].lower()
            idx = next((i for i, (n, _) in enumerate(sprites) if n == name), -1)
            if idx < 0:
                frames = sprite_frames(pc, name)
                if not frames:
                    continue
                idx = len(sprites)
                sprites.append((name, frames))
            objects.append((bx, by, idx, direction, anim))
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

        maps.append({"name": map_name, "w": w, "h": h, "blocks": blocks, "ts": ti,
                     "objects": objects, "obj_text": obj_text, "warps": warps,
                     "pal": 1 if env in ("INDOOR", "GATE") else 0,
                     "spawn": map_spawn.get(map_name, (0, 0))})
        print("map      %-20s %2dx%-2d blocks (%3dx%-3d px)  tileset %-20s obj %d warp %d"
              % (map_name, w, h, w * 32, h * 32, ts, len(objects), len(warps)))

    pals = bg_palettes(pc)
    pals_indoor = bg_palettes(pc, "indoor")
    player = sprite_frames(pc, args.player)
    if not player:
        die("player sprite gfx/sprites/%s.png not found" % args.player)

    path = os.path.join(out, "world.h")
    emit_world(path, maps, tilesets, sprites, player, pals, pals_indoor)
    print("tilesets  %d: %s" % (len(tilesets), ", ".join(t["name"] for t in tilesets)))
    print("sprites   %d: %s" % (len(sprites), ", ".join(n for n, _ in sprites)))
    print("wrote     %s (%d KB)" % (path, os.path.getsize(path) // 1024))

    tpath = os.path.join(out, "text.h")
    emit_text(tpath, texts_all, pc)
    print("texts     %d in %s (%d KB)"
          % (len(texts_all), tpath, os.path.getsize(tpath) // 1024))

    # Map scripts: the original's own script bytecode, assembled from the .asm
    # macro calls (never hand-written byte tables).
    defs = script_defs(pc)
    per_map, bytes_total, extern_all = [], 0, set()
    for map_name in wanted:
        try:
            data = map_scripts(pc, map_name, defs, text_index)
        except Unsupported as exc:
            print("scripts   %-22s SKIPPED: %s" % (map_name, exc))
            continue
        per_map.append((map_name, data))
        bytes_total += len(data["bytes"])
        extern_all |= set(data["extern"])
        print("scripts   %-22s %2d labels, %4d B%s"
              % (map_name, len(data["labels"]), len(data["bytes"]),
                 "" if not data["extern"] else
                 ", extern: " + ", ".join(data["extern"])))
    spath = os.path.join(out, "scripts.h")
    emit_scripts(spath, per_map, defs, text_index)
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
