# AGENTS.md — how to work in this repo

Instructions for an AI agent (or a new human) picking this project up. Read this first,
then `docs/07-conventions.md` before writing any code, and `TASKS.md` for what to do next.

## What this is

A **native port of Polished Crystal to the Thumby Color**, built as a
[Mote](https://github.com/austinio7116/mote) game module. Not an emulator: game
*content* (maps, tilesets, collision, sprites, text, data tables) is re-used as C
tables generated from the original repo, and the *engine* is rewritten in C.

- Source of the port: `game/`, `tools/`, `tests/`, `scripts/`, `docs/`
- Third-party checkouts (**gitignored, not part of the port**): `vendor/polishedcrystal`,
  `vendor/mote`, `vendor/thumbyone`

## Commands

Run from the repo root. Nothing outside `vendor/` needs root; this VM has no sudo.

```sh
scripts/setup.sh                  # cmake + SDL2(dummy) + Mote's host emulator. Idempotent.
                                # Re-run after the sandbox is restored: build output does not survive.
python3 tools/extract.py          # game data -> game/src/gen/*.h
scripts/build.sh                  # build the module -> game/build/PolishedCrystal.so
scripts/build.sh --device         # also the .mote (ARM toolchain from setup.sh)
scripts/test.sh                   # unit tests + scenarios (no emulator)
scripts/test.sh unit              # only tests/unit
scripts/test.sh scen              # only tests/scenarios
scripts/shot.sh out.png 45 "a:2-5,down:20-70"    # build + headless screenshot
scripts/perf.sh 900               # fps / update_us / raster_us from mote->perf()
scripts/export.sh                 # package dist/PolishedCrystal-<version>.mote + manifest
./scripts/run.sh                  # build + emulator window (needs a display)
```

**Always build with `scripts/build.sh`, never `mote build game` directly.** Mote's tool
only compiles `.c` files at the top level of `game/src`; the wrapper mirrors the tree
flat (with one-line `#include` shims for modules in folders) and then calls the official
tool, so folders work for the host and the device build alike.

In game: doors work — walk into a doorway and the screen fades to white for 10 frames,
the interior loads, and it fades back in over 10 more; stand on the doormat at the bottom
of a house and push down to leave. RB / LB switch maps (debug shortcut, not the way to
travel); A in front of
an NPC opens its dialogue, A again turns pages / closes it; MENU (≡) opens the pause
menu (UP/DOWN move, B or MENU closes, A records the entry and closes); A on Options
opens the settings screen (UP/DOWN pick, LEFT/RIGHT change the value, A exits on Done).
In the overworld, holding B runs (2 px/frame) while a direction is held — unless the
Running Shoes option is On, which inverts it, as in the original. Asking for a direction the
player is not facing turns in place first (8 frames at the default Turning Speed of Slow, 4
at Fast) before the step starts, so a tap only turns. Holding a direction into a wall, an
NPC or the map edge walks in place: the legs cycle, the player does not move.

`MOTE_KEYS="btn:from-to,..."` scripts input (`up/down/left/right/a/b/lb/rb/menu`), and
`MOTE_SHOT`/`MOTE_SHOT_FRAME` take the screenshot. Without `a:2-5` you only see Mote's
launcher. `MOTE_DT_MS` gives a fixed timestep.

## Non-negotiable conventions

1. **Organise by complexity, not by a fixed rule.** Simple code (one idea, ~200 lines or
   less) is a single `game/src/<name>.c` + `.h` — e.g. `gfx_sprite.c`. Complex code, or
   code that will grow, gets its own folder `game/src/<system>/` with one file per piece
   — e.g. `ow/map.c`, `ow/camera.c`, `ow/player.c`, `ow/render.c`. At most one level of
   nesting, and files inside a folder do not repeat the folder name. `game.c` (Mote
   glue) and `gen/` (generated data) stay at the top. Promoting a file to a folder or
   collapsing a folder back to a file is a normal, cheap refactor.
2. **Every module (every `.c`) has a unit test** named after its path with `/` → `_`:
   `ow/map.c` → `tests/unit/test_ow_map.c`. `scripts/test.sh` fails if one is missing.
   Write the test in the same change as the module.
3. **Behaviour tests are scenarios** (`tests/scenarios/*.txt`): initial state → steps →
   final state, in a plain-text format documented in `docs/07-conventions.md`. They must
   be writable by someone who cannot see the implementation — no internal fields, no
   frame-level magic numbers beyond "1 frame = 1 px".
4. **Modules are pure.** No `mote->` calls, no Mote headers, no globals, no `malloc`.
   State lives in a struct owned by the caller; big buffers come from `mote->alloc` in
   `game/src/game.c`, which is the only file allowed to talk to Mote.
5. **`game/src/gen/*.h` is generated.** Never edit it; change `tools/extract.py` instead.
   Make the extractor fail loudly on inconsistent data (sizes, id ranges).
6. **No new dependencies** without a reason recorded in `TASKS.md`.

## Definition of done for a change

- `scripts/test.sh` passes (including the module-has-a-test check).
- It builds through the wrapper: `scripts/build.sh` succeeds (not `mote build` directly).
- If it touches rendering or data, take a screenshot and compare it with a known-good
  one (`docs/img/overworld.png`); mention the command you used.
- If it changes a documented fact, update `docs/` in the same change. The docs are
  measurements, not prose: if a number or a path in them is wrong, fix it.

## Things that will waste your time (already learned the hard way)

- **The 2bpp shade ramp is inverted** in the PNGs: PNG value 0 is black, but GB colour
  index 0 is the *lightest* shade. `tools/extract.py` inverts it.
- **BG palettes are per tile, not per map.** Each tile's attribute byte picks 1 of 8
  palette slots (gray/red/green/water/yellow/brown/roof/text). One global palette gives
  green stripes where pavement should be.
- **`object_event` coordinates are on a 16 px grid**, not the 32 px block grid.
- **Collision is stored in screen order `TL, TR, BL, BR`** and needs no remapping; in
  this port the mask bits are TL=0, TR=1, BL=2, BR=3. `GetCoordTileCollision`
  (`home/map.asm`) indexes `(x & 1) + 2 * (y & 1)` over the four `tilecoll` bytes. An
  earlier remap with `order = (1, 3, 0, 2)` rotated every metatile 180° and made doors
  read as walls — see `docs/06`.
- **Doors and warp carpets fire at different moments.** A door (`DOOR`) fires when the
  step onto its cell *ends*; a directional carpet fires while standing on it with that
  direction held and the step blocked. Arriving is not a step, which is why landing on
  the doormat does not bounce you back out.
- **There is no door-tile animation in the original.** A door transition is
  `MAPSETUP_DOOR`: FadeOutPalettes, load, FadeInPalettes, both with c = 10. Indoor maps
  also use the `indoor` palette group, not the time-of-day one.
- **`MOTE_KEYS` windows count button reads, not presented frames**
  (`mote_plat_host.c`). The ratio drifts with host speed, so a recipe for an exact
  position should end against something that absorbs the player (a wall, the map edge,
  or the door itself) instead of counting frames.
- **C truncates toward zero**: `-1 / 2 == 0`, so any coordinate test must check the
  negative case *before* dividing. This already caused a walk-through-the-map-edge bug.
- A stale `.so` screenshots the previous build. `scripts/shot.sh` rebuilds first — keep
  it that way.
- `mote build game` silently skips `.c` files inside folders (`undefined symbol` at load).
  Use `scripts/build.sh`; see `docs/07-conventions.md`.
- `MOTE_KEYS` timing is in button reads (not milliseconds, and not exactly presented
  frames); the emulator does not run at 60 fps unless `MOTE_DT_MS` is set.
- A screenshot of a *specific position* needs `MOTE_DT_MS=16`: the host runs at ~500 fps,
  so one emulator frame is ~0.12 px and "16 frames per cell" silently becomes 2 cells.
- Text bytes are the game's charmap, **not ASCII** ('Y' is 0x98; 0x59 is `<PARA>`). Never
  put a C character literal into a text stream.
- The text commands (`$00`-`$09`, `$52`-`$5d`) carry **arguments** (`<NUM>` 3 bytes,
  `<SOUND>` 1, `<FAR>` 3), so a printer that does not know one prints the command *and its
  arguments* as glyphs. `text/box` consumes all of them; `<WAIT>` is an input gate that
  resumes on the same page and `<PAUSE>` is a 30 frame delay, not a page break. The codes
  are parsed out of `constants/charmap.asm`, never retyped.
- NPCs are not in the tileset collision. `OwMap.cells` is the second layer that makes
  them block; without it the player walks through an NPC and can never talk to it.
- Menu labels are inline in `engine/menus/start_menu.asm` as `db "#dex@"` — the `@`
  terminator is *after* the closing quote, and they are not in `data/text`.
- `ui_list_draw` indexes `items[0..count-1]`: pass a table at least that long, or it
  reads past the end (the 10-item scrolling test segfaulted on 4 labels).
- **The charmap's scroll arrows are `↑ $f2` and `↓ $f3`.** `ui/list` drew `$f3`/`$f4`
  (`↓` and `′`) for weeks because the pause menu never scrolls; a test that only asserts
  "some ink is here" cannot tell one glyph from another — compare cells pixel for pixel.
- The option screen's rows are two lines (name, value) because the GB's 20-tile row does
  not fit in 16: "Text Autoscroll" alone is 15 tiles, which leaves no room for a ▶. The
  selected row is an inverted bar instead.
- Option **values are padded to a fixed column** in the original (`"Slow   @"`); the
  extractor strips that, since the port prints them on their own line.
- **Running Shoes is an XOR of the option bit with the B button**, not "if the option,
  run": with it On you run all the time and B walks. See `.RunCheck` in
  `engine/overworld/player_movement.asm`, or `options_run` in `ui/options.c`.
- Running needs no separate animation state: the walker sheet advances every 8 px walked,
  so doubling the pixels per frame doubles the leg cycle for free.
- Turning is a movement, not a facing assignment: the original runs `StepFunction_Turn` for
  two halves of `GetTurningSpeed()` frames (4 Slow / 2 Fast) and the player cannot walk
  until it ends. A tap therefore turns without stepping.
- A step under way cannot be redirected. Accepting a new direction mid-cell walks the player
  off the 16 px grid; input only counts while standing.
- The push/bump animation is the *step* action: `SetFacingBumpAction` falls through into
  `SetFacingStepAction` and increments the same `OBJECT_STEP_FRAME`, so walking into a wall
  reuses the walk frames with no displacement. It is not a zero-pixel step — nothing is
  committed and it stops the frame the button is released — and running does not speed it up
  (the GB counts frames there, not pixels).
- `MOTE_KEYS` counts **button reads, not presented frames**, and Mote's launcher reads
  buttons before the module draws: `left:20-25` lands 4-5 frames earlier than the numbers
  suggest. To see something a few frames long, record with `MOTE_REC` and pick frames after.
- Screenshots do not measure distance. With `MOTE_DT_MS=16` the glue adds 0.96 px per
  frame, and the camera clamps at the map edge; use a scenario (`run right 32`) for numbers.
- NPC/actor animation counts **frames, not pixels walked**. Passing the player's pixel
  delta looks right (1 px per frame) but freezes every NPC the moment the player stops.
- `WATER` counts as walkable in the collision table, so "nearest free cell" can drop the
  player into the sea. Spawns go on road/ground blocks, not merely unblocked cells.
- `perf()` numbers from the host are host numbers (240 MHz + XIP on device): compare
  builds with them, never claim 60 fps on hardware from them.

## Where to look

| Need | File |
|---|---|
| Current state and next tasks | `TASKS.md` |
| Hardware/engine limits and API | `docs/01-target-hardware.md` |
| Data formats of Polished Crystal | `docs/03-content-formats.md` |
| Why the code looks like this | `docs/04-port-architecture.md` |
| Build/verify loop, headless shots | `docs/05-dev-workflow.md` |
| Dead ends, do not repeat | `docs/06-findings-log.md` |
| Module and test rules | `docs/07-conventions.md` |

## Scope and legal

Personal fan project: Pokémon is Nintendo/Game Freak IP and Polished Crystal is a hack.
Do not publish builds bundling the original content. Do not commit `vendor/`,
`toolsrc/`, `game/build/`, `tests/build/`, `artifacts/` or `dist/` (all gitignored).

Do not run `git push`, deploys or destructive commands unless the user asks.
