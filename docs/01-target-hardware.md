# 01 — El objetivo: Thumby Color + Mote

## Hardware (TinyCircuits Thumby Color)

| Dato | Valor |
|---|---|
| SoC | RP2350, 2× Arm Cortex-M33 con FPU |
| Reloj | 150 MHz nominal (se puede subir a ~300) |
| SRAM | 520 KB |
| Flash | 16 MB (1 MB firmware, 2 MB scratch, resto FAT ~13 MB) |
| Pantalla | LCD 128×128 RGB565 (GC9107), ~0.85" |
| Entradas | D-pad, A, B, LB, RB, MENU (9 botones) |
| Audio | buzzer magnético de 4 kHz (mono, limitado) |
| Extra | motor de vibración, RTC, USB-C |

Consecuencias para el port:

- **128×128 ≠ 160×144.** El original muestra 20×18 tiles; aquí caben 16×16. La ventana
  del overworld es un *viewport* que sigue al jugador (ya implementado). Las UIs del
  original (textboxes a 20 tiles de ancho) no caben: hay que rediseñarlas a 16 tiles,
  o escalar/recortar. Decisión pendiente en `TASKS.md`.
- **Buzzer de 4 kHz**: la banda sonora sonará a radio AM. Con vendas (2 canales,
  lowpass) puede quedar digna; no esperar fidelidad.
- **520 KB de RAM** para estado de juego + motor. Es el recurso que manda.

## Mote (motor nativo, ABI v47)

Un juego es un módulo `.mote` que **no enlaza** con el motor: recibe un
`const MoteApi *` (una jump table de punteros a función) y devuelve su vtable.

```c
MOTE_GAME_MODULE();                        // exporta mote_game_register + abi
MOTE_GAME_META("Polished Crystal", "port");
MOTE_GAME_VERSION("0.1.0");
// ...
static const MoteGameVtbl k_vtbl = {
    .init = g_init, .update = g_update,
    .render_band = g_band,      // opcional: raster propio por bandas
    .overlay = g_overlay,       // HUD/sprite pass en core0
    .config = { ... },          // pools del motor; 0 = no lo uso
};
```

Datos verificados en `vendor/mote/sdk/`:

| Aspecto | Detalle |
|---|---|
| Enlace | El módulo se enlaza en `MOTE_MODULE_VADDR = 0x10800000` y ejecuta **en sitio (XIP)** desde flash: sin copia ni relocación |
| RAM del módulo | `MOTE_MODULE_RAM = 0x2005E800`, `MOTE_MODULE_RAM_SZ = 0x21800` = **134 KB** para `.data`/`.bss` |
| Heap | **No hay `malloc`.** `mote->alloc(n)` reparte de una arena común (`MOTE_ARENA_SIZE` = 272 KB, 277 KB en la variante con más SRAM, menos lo que declare `config`); `arena_free()` dice cuánto queda |
| Vblank / framing | `render_band(fb, y0, y1)` se llama en **los dos núcleos** con bandas disjuntas; `overlay(fb)` solo en core0 |
| Pantalla | `fb` es `uint16_t*` de 128×128, stride 128 |
| Texto | `text()` (3×5), `text_2x()`, `text_font()` con fuentes horneadas, `ui_font(MOTE_FONT_MED/READ/LARGE)` (Audiowide, v47) |
| Audio | `audio_note(freq, amp)` (8 voces), `audio_play(MoteSound*)` (4 canales PCM), `audio_play_sfx(MoteSfx*)` (recetas SFXR, 4 voces), `audio_set_stream(fill)` (tu propio mezclador) |
| Persistencia | `save(slot)/load(slot)` (slots de flash), `kv_save/kv_load/kv_list` (blobs con nombre bajo la carpeta de saves del juego) |
| Diagnóstico | `log(s)` al host, `perf(uint32_t[6])` = fps, update_us, raster_us, flush_us, %, % |
| Ritmo | `set_fps_limit(fps)`; el dispositivo corre libre por defecto |
| 2D | `scene2d_*` (tilemap + sprites + autotiles) **o** `render_band` propio |
| 3D | pipeline de triángulos con depth, físico, splats, etc. (no usado por este port) |

### Por qué `render_band` y no `scene2d_*`

El modelo del GB (tilemap de 32×32 + ventana + sprites con prioridad y paletas por
tile) no encaja en el modelo de `scene2d` sin traducciones que perderían fidelidad.
Con `render_band` propio replicamos la semántica del GB (tile de 8×8, paleta por tile,
recorte por banda) y además controlamos el coste por píxel, que es lo que hay que
medir en hardware.

## Emulador de host

`vendor/mote/build_host/mote_host` ejecuta un `.so` y es *headless-friendly*:

```sh
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  MOTE_KEYS="a:2-5,left:20-70" MOTE_SHOT=/tmp/s.ppm MOTE_SHOT_FRAME=90 \
  ./vendor/mote/build_host/mote_host game/build/PolishedCrystal.so
```

- `MOTE_KEYS="btn:from-to,..."` — pulsaciones guionizadas por número de frame
  (`up/down/left/right/a/b/lb/rb/menu`). Es la base del test automatizado.
- `MOTE_SHOT` + `MOTE_SHOT_FRAME` — vuelca un PPM de 128×128 y sale.
- `MOTE_PICK` — autoselecciona en el launcher de Mote (evita el `a:2-5`).
- `MOTE_REC`/`MOTE_REC_N` — graba secuencias de frames.

Que exista este modo es lo que permite desarrollar y verificar el port sin el
dispositivo físico.
