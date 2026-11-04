# Polished Crystal → Thumby Color (native Mote port)

A native port of [Polished Crystal](https://github.com/Rangi42/polishedcrystal) to the
TinyCircuits **Thumby Color**, built as a [Mote](https://github.com/austinio7116/mote)
game module. Not an emulator: the GBZ80 engine is replaced by C, while the game's
*content* (maps, tilesets, collision, sprite art, palettes) is re-used as-is.

Status: **slice 1 — the overworld**. New Bark Town renders from the original data,
the player walks in 16 px grid steps with the map's own collision, the camera pans,
and the five NPCs from the map's `object_event`s stand where the asm puts them.

Documentation (findings, data formats, architecture, dev workflow): [`docs/`](docs/README.md).
Full component inventory and phased work plan: [`TASKS.md`](TASKS.md).

![New Bark Town on the Thumby Color](docs/img/overworld.png)

Walking west — camera panned, collision live: [docs/img/walk_left.png](docs/img/walk_left.png)

## Why this shape

Polished Crystal is ~11.7 MB of GBZ80 assembly, but almost everything that took
years to author is **data**:

| Content | In the repo | In this port |
|---|---|---|
| Maps | `maps/<Map>.ablk` + `data/maps/maps.asm` | `map_blocks[]` |
| Metatiles | `data/tilesets/<ts>_metatiles.bin` | `mt_tiles[253][16]` |
| Collision | `data/tilesets/<ts>_collision.asm` | `mt_coll[253]` (bitmask) |
| Tile art | `gfx/tilesets/<ts>.<common>.png` (2bpp) | `ts_tiles[176][16]` |
| Tile palettes | `data/tilesets/<ts>_attributes.bin` | `mt_pal[253][16]` |
| BG palettes | `gfx/tilesets/bg_tiles.pal` | `bg_pal[8][4]` (RGB565) |
| NPC art | `gfx/sprites/<name>.png` (16x96) | `spr_*[24][16]` |

So only the engine has to be rewritten. `tools/extract.py` does the conversion;
`game/src/game.c` is the engine (so far: map raster, camera, movement, collision,
sprite blitting).

Geometry is kept identical to the Game Boy, so all the content lines up without
transformation: a map **block** is a 32×32 px metatile (4×4 tiles of 8×8), the
player walks in **16×16 px** steps, and collision is stored per 16×16 quadrant.
`object_event` coordinates are on that same 16 px grid (an object at y=15 on a
9-block-tall map only fits that way).

## Layout

```
tools/extract.py      Polished Crystal -> C headers (the content pipeline)
tools/preview_map.py  reference renderer in Python, to validate the pipeline
                      independently of the C code (also tests tile/palette order)
game/src/game.c        the engine: Mote module, banded map raster, movement
game/src/gen/*.h       generated data (do not edit)
docs/                  findings, data formats, architecture, dev workflow
TASKS.md               every system left to port, and the phased plan
setup.sh               build cmake + SDL2 locally, then Mote's host emulator
run.sh / shot.sh       run it, or grab a headless screenshot
vendor/                clones of polishedcrystal, mote, ThumbyOne (gitignored)
```

## Build and run

The sandbox has no root and no system SDL2, so `setup.sh` installs cmake and a
**dummy-video/dummy-audio SDL2** under `toolsrc/` — enough for the emulator to
render frames headlessly. Package installs and build output do not survive
archiving; the script is idempotent, so just re-run it.

```sh
./setup.sh                                  # cmake + SDL2 + mote_host
python3 tools/extract.py                    # regenerate game/src/gen/*.h
LD_LIBRARY_PATH=$PWD/toolsrc/sdl-install/lib ./vendor/mote/tools/mote build game
./shot.sh artifacts/overworld.png 45 "a:2-5,down:20-60"
./shot.sh artifacts/left.png 90 "a:2-5,left:20-70"
```

`MOTE_KEYS` scripts scripted input: `<button>:<from>-<to>` frames, e.g.
`a:2-5` presses A for frames 2–5 (which is how you get past the Mote launcher),
then `down:20-60` walks south. `MOTE_SHOT_FRAME` picks the captured frame.

## What the extraction relies on

Verified against the repo and against Elm's Lab / PlayersHouse1F, which render as
coherent rooms (a good structural check — see `tools/preview_map.py`):

- `.ablk` block ids index the tileset's `_metatiles.bin`, 16 tile ids each,
  **row-major** (TL,TR,BL,BR).
- The tileset sheet is VRAM-bank order: tiles 0–127 then 128+ (the 128×88 sheet is
  exactly 8 rows + 3 rows of 16 tiles, which is the `_metatiles`/`vram0`/`vram1`
  split the Makefile uses).
- Tile attribute low 3 bits = BG palette slot (0 gray … 6 roof …), which is what
  makes pavement gray, grass green and buildings "roof" coloured.
- `tilecoll BL, TL, BR, TR` is remapped to screen order TL,TR,BL,BR.
- 2bpp index 0 is the **lightest** shade, while the PNG stores black as 0 — the
  ramp is inverted during extraction. Skipping that inversion swaps every tile's
  highlights and shadows.

## Known gaps (in rough order of value)

1. **Text.** No dialog, signs or menus: needs `charmap.asm` + `gfx/font/font.png`
   (8×8 glyphs) and a textbox renderer. This is the next slice.
2. **First-person interaction.** `A` facing an NPC should run its script; scripts
   are compiled bytecode for `engine/overworld/scripting.asm`, so this is the
   interpreter to port (also unlocks warps/signs/scripted events).
3. **Warps and connections.** `warp_event`/`connection` rows are parsed by nothing
   yet, so the town is an island; the map change path doesn't exist.
4. **NPC animation.** NPCs are drawn static on frame 0; walk frames exist in the
   data (`SPRITEMOVEDATA_*` drives them) and the player already animates.
5. **Palettes:** only the *day* set. Night/morning/eve and indoor/water sets are in
   the same table; time-of-day switching would also need the RTC.
6. **Audio.** Nothing — `audio/` is macro language for the GB's 4 channels; it has
   to be re-expressed through `audio_note`/`audio_stream`.
7. **Battle engine, party, menus, save.** Untouched.

## Legal

Polished Crystal is a fan hack; Pokémon is Nintendo/Game Freak IP. Keep this a
personal project — don't publish builds with the original content.
