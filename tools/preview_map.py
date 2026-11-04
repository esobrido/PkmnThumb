#!/usr/bin/env python3
"""Quick reference renderer: draw a Polished Crystal map straight from the
original data to a PNG, to validate the extraction assumptions independently of
the C renderer.

  python3 tools/preview_map.py --map NewBarkTown --out /tmp/ref.png --model A
"""
import argparse
import os
import sys

from PIL import Image

sys.path.insert(0, os.path.dirname(__file__))
import extract as X  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--map", default="NewBarkTown")
    ap.add_argument("--model", default="A", choices="ABC",
                    help="A: 32px block = one 16-tile entry; "
                         "B: 16px cell = one 4-tile entry; "
                         "C: 16px cell = one of 4 entries per block")
    ap.add_argument("--order", default="row", choices="row,col,quad,quadT",
                    help="tile order inside the 16-tile entry (model A)")
    ap.add_argument("--out", default="/tmp/ref.png")
    ap.add_argument("--scale", type=int, default=2)
    ap.add_argument("--palette", default="town")
    args = ap.parse_args()

    pc = os.path.join(os.path.dirname(__file__), "..", "vendor", "polishedcrystal")
    pc = os.path.abspath(pc)
    w, h = X.map_dims(pc, args.map)
    ts = X.map_tileset(pc, args.map)
    blocks = X.map_blocks(pc, args.map, w, h)
    tiles = X.load_2bpp_sheet(X.tileset_sheet(pc, ts))
    mts = X.metatiles(pc, ts)

    pals = {
        "town": [(0xF8, 0xF8, 0xE0), (0xA8, 0xD8, 0x88), (0x60, 0x98, 0x50), (0x38, 0x58, 0x40)],
        "gray": [(0xFF, 0xFF, 0xFF), (0xAA, 0xAA, 0xAA), (0x55, 0x55, 0x55), (0x00, 0x00, 0x00)],
    }
    pal = pals[args.palette]

    def tile_px(idx, x, y):
        t = tiles[idx]
        lo, hi = t[y * 2], t[y * 2 + 1]
        bit = 7 - x
        return ((lo >> bit) & 1) | (((hi >> bit) & 1) << 1)

    cell = 32 if args.model == "A" else 16
    W, H = w * cell, h * cell
    img = Image.new("RGB", (W, H))
    px = img.load()

    for by in range(h):
        for bx in range(w):
            b = blocks[by * w + bx]
            for my in range(cell):
                for mx in range(cell):
                    cx, cy = mx >> 3, my >> 3        # 8px tile inside the cell
                    if args.model == "A":
                        # one 16-tile metatile per map block, 4x4 tiles
                        o = args.order
                        if o == "row":
                            i = cy * 4 + cx
                        elif o == "col":
                            i = cx * 4 + cy
                        elif o == "quad":
                            i = ((cy >> 1) * 2 + (cx >> 1)) * 4 + ((cy & 1) * 2 + (cx & 1))
                        else:
                            i = ((cy & 1) * 2 + (cx & 1)) * 4 + ((cy >> 1) * 2 + (cx >> 1))
                        tid = mts[b][i]
                    else:
                        q = 0
                        if args.model == "C":
                            q = (cy >> 1) * 2 + (cx >> 1)          # TL,TR,BL,BR
                        inner = ((cy & 1) << 1) | (cx & 1)     # TL,TR,BL,BR
                        tid = mts[b * (4 if args.model == "C" else 1) + q][inner]
                    px[bx * cell + mx, by * cell + my] = pal[tile_px(tid, mx & 7, my & 7)]

    if args.scale != 1:
        img = img.resize((W * args.scale, H * args.scale), Image.NEAREST)
    img.save(args.out)
    print("%s model=%s -> %s (%dx%d px, %dx%d cells of %dpx)"
          % (args.map, args.model, args.out, W, H, w, h, cell))


if __name__ == "__main__":
    main()
