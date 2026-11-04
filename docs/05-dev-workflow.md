# 05 — Flujo de trabajo y verificación

## Entorno

El workspace es Linux sin root y sin paquetes de sistema. `setup.sh` instala **todo
dentro de `toolsrc/`** (gitignored):

- **cmake 3.30.5** (binario oficial, porque `pip install cmake` no está disponible aquí).
- **SDL2 2.30.9 compilada con drivers `dummy`** — sin X11/Wayland/ALSA/Pulse. Suficiente:
  Mote escribe PPM en modo headless y no abre ventana.

```sh
./setup.sh          # cmake + SDL2 + vendor/mote/build_host/mote_host
```

Ni los paquetes ni los binarios sobreviven al archivado del sandbox; el script es
idempotente, así que se vuelve a ejecutar y listo.

## Ciclo de trabajo

```sh
python3 tools/extract.py                     # datos de PC -> game/src/gen/newbarktown.h
./shot.sh artifacts/overworld.png 45 "a:2-5" # build + frame 45 a PNG
./shot.sh artifacts/walk.png 90 "a:2-5,left:20-70"
./run.sh                                     # ventana del emulador (requiere display)
```

`shot.sh` recompila antes de capturar: un `.so` viejo produce capturas engañosas.

Para otro mapa: `--map <Nombre> --out <dir>` escribe `<nombre>.h` en minúsculas
(`--map CherrygroveCity` → `cherrygrovecity.h`). Ojo: `game.c` incluye un header fijo
(`#include "gen/newbarktown.h"`), así que hoy sólo se juega el mapa extraído con ese
nombre. Generalizarlo es la tarea **FND-06**.

```sh
python3 tools/extract.py --map ElmsLab --out /tmp/gen   # prueba de otro mapa sin tocar el juego
python3 tools/preview_map.py --map ElmsLab --out /tmp/lab.png
```

## Entradas guionizadas

`MOTE_KEYS="btn:from-to,..."` con `up/down/left/right/a/b/lb/rb/menu` y números de
frame. Ejemplos:

| Objetivo | `MOTE_KEYS` |
|---|---|
| Saltar el launcher de Mote y entrar al juego | `a:2-5` |
| Andar al sur 3 tiles | `a:2-5,down:20-70` |
| Probar colisión contra un edificio | `a:2-5,up:20-120` |
| Girar sin andar | `a:2-5,left:20-22` |

Como el emulador corre sin límite de fps, los frames no equivalen a tiempo real; para
eso está `MOTE_DT_MS`.

## Cómo se verifica

1. **Estructural, sin mirar píxeles**: `tools/preview_map.py --map ElmsLab` debe dar
   una sala con paredes, estanterías y mesas. Es la prueba de que el orden de metatiles
   y de tiles es correcto (una sala no admite interpretaciones).
2. **Visual**: `shot.sh` y comparar con `vendor/polishedcrystal/screenshots/*.png`
   (hay capturas reales del juego en el repo — es el oráculo más barato).
3. **De comportamiento**: capturas en frames distintos con la misma secuencia de teclas
   para comprobar que el jugador avanza, que la cámara panea y que la colisión bloquea.
4. **De datos**: `tools/extract.py` falla con mensaje explícito si los tamaños no
   cuadran (`ablk` ≠ ancho×alto, ids de tile fuera de la hoja, colisiones ≠ metatiles).
   Ese chequeo es la red de seguridad al añadir mapas y tilesets.

## Pendiente de medir (y cómo)

- **Coste real del raster**: en device, `mote->perf(out)` da `raster_us` por frame.
  Objetivo: < 16 ms para 60 fps holgado. Si no, cachear tiles expandidos.
- **Flash disponible**: el `.mote` con un mapa pesa poco, pero con 458 mapas y 53
  tilesets el presupuesto de 16 MB de flash (menos firmware y FAT) es el límite duro.
  Hay que decidir si se empaqueta **todo** en el `.mote` o en un archivo de datos
  aparte leído con `kv_*`/`save()`.
- **Compilación a device**: falta `arm-none-eabi-gcc` + Pico SDK en este entorno;
  `mote build --device` no se ha ejecutado todavía.
