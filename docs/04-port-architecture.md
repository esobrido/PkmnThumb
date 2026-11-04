# 04 — Arquitectura del port

## Qué se reutiliza y qué se reescribe

```
REUTILIZADO (datos)                       REESCRITO (motor, en C)
────────────────────────────────────      ──────────────────────────────────────
mapas (.ablk)                             PPU / raster de tiles + sprites
metatiles + atributos + colisión          cámara, movimiento, colisiones
tilesets (2bpp) y paletas BG              VM de scripts
sprites de overworld                      motor de batalla y animaciones
textos (desde las fuentes .asm)           menús, UI, Pokédex, PC, Pokégear
base_stats / moves / items                audio y mezcla
música (re-expresada)                     guardado, RTC/día-noche
```

## Forma del módulo

Un único módulo `.mote` ("PolishedCrystal") con:

- `render_band` propio → mapa (y, más adelante, sprites de campo).
- `overlay` → sprites de overworld y, más adelante, HUD/textboxes.
- `update(dt)` → input, máquina de estados de campo.
- `config = {0}` → no se declaran pools del motor 3D/2D; todo el presupuesto va a la
  arena del juego vía `mote->alloc`.

Motivo de hacer el raster a mano: el GB pinta tiles de 8×8 con una paleta de 4 colores
**por tile**, y sprites de 8×8/8×16 con prioridad y flips. Reproducir esa semántica
sobre `scene2d` requeriría traducciones y perdería detalles (prioridad sprite-fondo,
paletas por tile). 16×16 = 256 tiles por frame es barato incluso decodificando 2bpp al
vuelo.

## Geometría y viewport

```
bloque 32×32 px · tile 8×8 · paso 16 px
pantalla 128×128 ⇒ 16×16 tiles visibles (el GB mostraba 20×18)
cámara = clamp(jugador + 8 - 64, 0, mapa - 128)
```

El mapa de New Bark Town mide 384×288 px, así que la cámara se mueve de verdad. En
mapas más pequeños que la pantalla, la cámara se queda en 0.

## Presupuesto actual (medido)

| Recurso | Uso |
|---|---|
| Flash (datos de un mapa + tileset) | ~12 KB (tiles 2.8 KB, metatiles 4 KB, paletas de tile 4 KB, mapa 108 B, sprites ~4 KB) |
| RAM de arena | ~19 KB (sprites + jugador expandidos a RGB565) |
| RAM de módulo (`.bss`) | < 1 KB (estado de juego) |
| Coste de raster | 256 tiles/frame, decodificación 2bpp inline; **sin medir en device** |

Los tiles se leen de flash en el bucle de raster (XIP): no se copian a RAM. Sólo los
sprites se expanden a RGB565 (16×16 con transparencia), porque se dibujan con
prioridad y flips.

## Modelo de movimiento

- Paso de 16 px, 16 frames a 60 fps (la velocidad del original), con `dt` para no
  depender de los fps.
- Pedir dirección gira en el sitio y arranca el paso si la celda destino es libre —
  igual que en el GB.
- La colisión consulta el bitmask del metatile para el cuadrante de 16 px que ocupa el
  jugador.
- Animación: 2 frames (quieto / paso) alternando cada 8 px recorridos.

## Convenciones de código

- `game/src/gen/*.h` es **generado**: no se edita (`tools/extract.py` lo reescribe).
- Nada de `malloc`: todo buffer sale de `mote->alloc` en `init`.
- Los datos grandes son `static const` (flash/XIP); sólo lo que se transforma se copia.
- El raster no asigna ni llama al motor: sólo escribe en `fb` (seguro en ambos núcleos).
- Nombres de datos espejo del original (`mt_tiles`, `mt_pal`, `mt_coll`, `map_blocks`)
  para poder contrastar con la fuente asm en cualquier momento.

## Riesgos abiertos

| Riesgo | Detalle | Mitigación |
|---|---|---|
| Coste de raster en device | 128×128 a 60 fps con decodificación por píxel; sin medir en RP2350 | Medir con `perf()` en hardware; si sobra, cachear tiles expandidos por paleta (8 paletas) |
| 16 tiles de ancho de UI | los textboxes del original son de 20 tiles | rediseñar a 16, o marco con scroll de texto |
| Memoria en batalla | el motor de batalla es el trozo más grande (31 K líneas asm) | por fases: primero una batalla salvaje con 1 Pokémon, sin animaciones |
| Audio | buzzer 4 kHz | 2 canales + lowpass; aceptar pérdida de fidelidad |
| Licencia | IP de Nintendo/Game Freak | proyecto personal, sin builds públicas |
