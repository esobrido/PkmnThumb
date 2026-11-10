# 05 — Flujo de trabajo y verificación

## Entorno

El workspace es Linux sin root y sin paquetes de sistema. `setup.sh` instala **todo
dentro de `toolsrc/`** (gitignored):

- **cmake 3.30.5** (binario oficial, porque `pip install cmake` no está disponible aquí).
- **SDL2 2.30.9 compilada con drivers `dummy`** — sin X11/Wayland/ALSA/Pulse. Suficiente:
  Mote escribe PPM en modo headless y no abre ventana.
- **ARM GNU toolchain 13.3** (141 MB) para compilar el `.mote` de device sin root.
  `scripts/setup.sh --host-only` se la salta si sólo interesa el emulador.

```sh
scripts/setup.sh    # cmake + SDL2 + mote_host + arm-none-eabi-gcc
```

Ni los paquetes ni los binarios sobreviven al archivado del sandbox; el script es
idempotente, así que se vuelve a ejecutar y listo.

## Ciclo de trabajo

```sh
scripts/test.sh                              # unit + escenarios, sin emulador
python3 tools/extract.py                     # datos de PC -> game/src/gen/world.h
scripts/build.sh                             # .so de host
scripts/build.sh --device                    # + .mote de 48 KB para el handheld
./scripts/shot.sh artifacts/overworld.png 45 "a:2-5"   # build + frame 45 a PNG
./scripts/shot.sh artifacts/walk.png 90 "a:2-5,left:20-70"
MOTE_DT_MS=16 ./scripts/shot.sh artifacts/talk.png 214 \
    "a:2-5,left:20-99,down:110-112,a:125-128"   # walk to the boy and talk
./scripts/shot.sh artifacts/options.png 90 \
    "a:2-5,menu:20-23,down:30-33,down:40-43,down:50-53,down:60-63,a:70-73"
./scripts/perf.sh 900                        # fps / update_us / raster_us
./scripts/run.sh                             # ventana del emulador (requiere display)
```

`shot.sh` recompila antes de capturar: un `.so` viejo produce capturas engañosas.

El extractor emite **un solo** `gen/world.h` con todos los mapas que se le pasen, sus
tilesets (sólo los distintos) y todos los sprites:

```sh
python3 tools/extract.py                                        # NewBarkTown,Route29,CherrygroveCity
python3 tools/extract.py --maps NewBarkTown,ElmsLab,PlayersHouse1F
python3 tools/preview_map.py --map ElmsLab --out /tmp/lab.png    # render de referencia
```

Las puertas llevan a los cinco interiores de New Bark Town; para saltar a cualquier mapa
sin andar está **RB / LB** (atajo de depuración).

## Entradas guionizadas

`MOTE_KEYS="btn:from-to,..."` con `up/down/left/right/a/b/lb/rb/menu` y números de
frame. Ejemplos:

| Objetivo | `MOTE_KEYS` |
|---|---|
| Saltar el launcher de Mote y entrar al juego | `a:2-5` |
| Andar al sur 3 tiles | `a:2-5,down:20-70` |
| Probar colisión contra un edificio | `a:2-5,up:20-120` |
| Girar sin andar | `a:2-5,left:20-22` |
| Abrir el menú de pausa | `a:2-5,menu:20-23` |
| Abrir la pantalla de Option | `a:2-5,menu:20-23,down:30-33,down:40-43,down:50-53,down:60-63,a:70-73` |
| … y cambiar la velocidad de texto | (lo anterior)`,right:84-87` |
| … y bajar el cursor | `a:2-5,menu:20-23,down:40-43` |
| Correr al oeste 40 frames | `a:2-5,left:20-60,b:20-60` (con `MOTE_DT_MS=16`) |
| Girar sin andar (desde el spawn, que mira al sur) | `a:2-5,left:20-25` |
| Empujar una pared y ver la animación | `a:2-5,up:20-99` (el edificio del norte está pegado al spawn; con `MOTE_DT_MS=16`) |
| **Entrar por la puerta** de casa de Lyra | `a:2-5,left:10-20,up:40-260` (una celda al oeste y arriba: la puerta está en (11,13)) |
| Volver a salir | añadir `down:280-700`: pisa la estera hacia abajo y el warp de salida te deja en (11,14) |
| Ver el interior de un mapa sin andar | `a:2-5,rb:10-13,rb:20-23,rb:30-33` (RB cicla: 3 = laboratorio de Elm, 4 = casa del jugador…) |
| **Cruzar andando a la Ruta 29** (salir del pueblo por el oeste) | `a:60-64,left:75-400,up:410-520,right:530-634,up:640-900,left:910-1400` con `MOTE_DT_MS=16`; el log dice `connect: Route29` |
| **New Bark Town → Ruta 29 → Ciudad Cerezo** andando | la anterior más `down:1405-1510,left:1530-1552,down:1572-1594,left:1614-1786,up:1806-1844,left:1864-1969,up:1989-2044,left:2064-2186,up:2206-2228,left:2248-2470,up:2490-2562,left:2582-2704,left:2724-2784`; dos `connect:` (Ruta 29 y Ciudad Cerezo) |

Como el emulador corre sin límite de fps, los frames no equivalen a tiempo real; para
eso está `MOTE_DT_MS`. **Importante para capturas con posición exacta**: sin
`MOTE_DT_MS=16` el host corre a ~500 fps y un fotograma del emulador avanza ~0.12 px, así
que "16 frames = una celda" deja de cumplirse y el jugador se queda a medio camino. Con
16 ms por frame, 1 frame = 1 px = 1/16 de paso.

Trampa — **el número de `MOTE_KEYS` no es un frame de juego, es una lectura del pad**
(`mote_plat_host.c`: *"one frame == one buttons read"*). El juego puede leer el pad
varias veces sin presentar frame, así que una ventana de N lecturas dura unas 4-5 veces
más de lo que parece y la proporción cambia con la velocidad del host: medido hoy,
`left:10-18` da 0 celdas en una ejecución y 2 en otra, y `up:18-...` vs `up:28-...`
decide si el jugador llega a la puerta o se queda en la celda anterior.

Por eso las recetas de captura con posición exacta se escriben **contra algo que
absorbe**: una pared (el `up` largo se para en la fachada), el borde del mapa o la
propia puerta, que al pisarla ya no deja seguir. Contar frames sólo funciona para
distancias cortas y aun así conviene comprobar el resultado con el log.

Girar: pedir una dirección distinta de la que se mira gira en el sitio antes de andar —
8 frames con *Turning Speed* en Slow (el valor por defecto), 4 en Fast — y un toque corto
sólo gira, sin dar el paso. O sea que la primera celda en una dirección nueva cuesta 24
frames: 8 de giro más 16 de paso.

Empujar: si la celda de delante está bloqueada (pared, NPC, borde del mapa) el jugador no
se queda quieto — anda en el sitio, con los mismos dos frames y el mismo ritmo que al andar,
sin mover un píxel. Desde el spawn (mira al sur) el edificio del norte está pegado, así que
`MOTE_DT_MS=16 MOTE_KEYS="a:2-5,up:20-99"` empuja desde el primer frame útil. La prueba está
en `artifacts/bump.png`: grabando 110 frames (`MOTE_REC`/`MOTE_REC_N`) los fotogramas 37 y 46
sólo se diferencian en 74 píxeles dentro de la caja del sprite (59..68 × 81..92), y el 37 con
el 55 son idénticos: el ciclo de dos poses se repite mientras el mapa no se mueve.

Andar y correr: **B** mantenido corre (2 px por frame) mientras se mantiene una
dirección, siempre que el ajuste *Running Shoes* esté en Off, que es su valor por defecto.
Con el ajuste en **On** es al revés — se corre solo y B sirve para andar —, porque el
original decide con un XOR entre el bit y el botón (`.RunCheck`), no con un interruptor
secuencial. Misma entrada de 40 frames en este build: **42 px andando, 80 px corriendo**
(`artifacts/run.png`).

Hablar con un NPC: `A` con el jugador quieto y mirando a la celda del NPC; el guion de
arriba camina 5 celdas al oeste y 1 al sur desde el spawn de New Bark Town hasta el
chaval de (7,15). Los actores **bloquean** su celda, así que hacia ellos primero se gira y después se empuja.

El menú de pausa abre con **MENU** (el botón ≡), no con A: en el original es START. Se
mueve con UP/DOWN, se cierra con B o MENU, y A elige una entrada: **Options** abre su
pantalla, y las otras cuatro todavía sólo escriben el nombre de la entrada en el log,
porque sus pantallas no están portadas.

En la pantalla de Option: UP/DOWN elige ajuste, LEFT/RIGHT cambia el valor (con vuelta),
A cierra sobre `Done`, y B o MENU salen desde cualquier fila. Los ajustes ya aplicados se
quedan: cerrar no los deshace, igual que en el original. La captura de referencia es
`docs/img/options.png`, hecha con el guion de la tabla de arriba.

Para ver que un ajuste llega de verdad al juego, cambia la velocidad de texto y habla con
el chaval; la misma captura, 7 frames después de pulsar A, muestra `W` en Slow, `Wow, your`
en Fast (el valor por defecto) y la página entera en Instant:

```sh
P="a:2-5,menu:20-23,down:30-33,down:40-43,down:50-53,down:60-63,a:70-73"
TAIL="left:120-199,down:205-207,a:215-218"
MOTE_DT_MS=16 ./scripts/shot.sh /tmp/slow.png 222 "$P,right:80-83,right:90-93,b:104-107,$TAIL"
MOTE_DT_MS=16 ./scripts/shot.sh /tmp/fast.png 222 "$P,b:84-87,$TAIL"
MOTE_DT_MS=16 ./scripts/shot.sh /tmp/inst.png 222 "$P,right:80-83,b:94-97,$TAIL"
```

Cuidado con los guiones de teclas: dos pulsaciones seguidas sin hueco (`right:80-83`
y `right:84-87`) son **una sola** pulsación, porque la tecla nunca se suelta entre medias.

## Cómo se verifica

0. **Con tests, primero**: `scripts/test.sh` compila cada test de `tests/unit` contra los
   módulos y ejecuta los escenarios de `tests/scenarios`; además falla si un módulo de
   `game/src` no tiene su test. Es el bucle rápido: sin emulador, sin hardware. Reglas y
   formato de escenarios en [07-conventions.md](07-conventions.md).
1. **Estructural, sin mirar píxeles**: `tools/preview_map.py --map ElmsLab` debe dar
   una sala con paredes, estanterías y mesas. Es la prueba de que el orden de metatiles
   y de tiles es correcto (una sala no admite interpretaciones).
2. **Visual**: `scripts/shot.sh` y comparar con `vendor/polishedcrystal/screenshots/*.png`
   (hay capturas reales del juego en el repo — es el oráculo más barato).
3. **De integración**: capturas en frames distintos con la misma secuencia de teclas
   para comprobar que el módulo real arranca, que el jugador avanza, que la cámara panea
   y que la colisión bloquea. Es lo único que cubre `game.c` (el glue de Mote).
4. **De datos**: `tools/extract.py` falla con mensaje explícito si los tamaños no
   cuadran (`ablk` ≠ ancho×alto, ids de tile fuera de la hoja, colisiones ≠ metatiles).
   Ese chequeo es la red de seguridad al añadir mapas y tilesets.

## Medición (`perf()`)

El módulo loguea una línea por segundo con `mote->perf()`:

```
perf fps=60 update_us=120 raster_us=830 flush_us=210 core0%=42 core1%=38
```

`scripts/perf.sh [frames]` la recoge en el host. Valores medidos aquí (8 mapas, caminando):

| Métrica | Host | Device |
|---|---|---|
| `fps` | ~487 (sin límite de vsync) | **sin medir** |
| `raster_us` | 20 | **sin medir** |
| `update_us` | 0 (por debajo de la resolución del temporizador) | **sin medir** |
| `.mote` | — | 76 700 B, `.bss` 3 384 B |
| Fundido de puerta (16 384 px, nivel 1-9) | 61 µs | **sin medir** (~0,7 ms estimados por reloj) |

El fundido se mide aparte porque sólo ocurre dentro de una puerta: un banco de 300
pasadas sobre un framebuffer de 128×128, `blend()` por canal y sin SIMD. El frame de
nivel 10 no lo paga (es un relleno a `0xFFFF`, 0,4 µs).

El build de device **sí** compila y enlaza en este entorno (`scripts/build.sh --device`),
pero ejecutarlo requiere el handheld: los fps en RP2350 siguen pendientes de medir.

## Empaquetar para el handheld (`.mote`)

```sh
scripts/export.sh                  # -> dist/PolishedCrystal-<versión>.mote
scripts/export.sh /tmp/entrega     # otro destino
```

El script regenera los datos, compila el módulo de device, **valida la cabecera**
(magic `MOTE`, versión embebida, ABI contra el `MOTE_ABI_VERSION` del SDK) y deja en el
destino:

| Fichero | Qué es |
|---|---|
| `PolishedCrystal-0.14.0.mote` | el módulo que se copia al dispositivo (76 700 B) |
| `manifest.json` | versión, ABI, tamaño, `sha256`, commit y si el árbol tenía cambios |
| `PolishedCrystal-0.14.0.png` | captura de referencia a 128×128 para la galería |

La versión la fija `MOTE_GAME_VERSION` en `game/src/game.c` (ahora `0.14.0`), que es lo
que el lanzador y la galería leen **del propio binario** sin ejecutarlo. Se sube al
cerrar cada fase; `export.sh` falla si vale `0` (módulo sin versión = siempre "actualizable").

Para inspeccionar un `.mote` suelto (¿qué versión es? ¿carga en este firmware?):

```sh
python3 tools/moteinfo.py dist/PolishedCrystal-0.14.0.mote
python3 tools/moteinfo.py x.mote --json
```

Instalación: copiar el `.mote` a la carpeta `/mote/` del dispositivo (se monta como
almacenamiento USB y aparece en el lanzador MOTE), o con el dispositivo conectado
`./vendor/mote/tools/mote push game`.

Ese artefacto lleva contenido de Polished Crystal: es para uso personal, `dist/` está en
`.gitignore` y no se redistribuye.

## Pendiente de medir (y cómo)

- **Coste real del raster en device**: objetivo < 16 ms para 60 fps holgado; medir con
  `scripts/perf.sh` en hardware. Si sobra, cachear tiles expandidos.
- **Flash disponible**: el `.mote` con tres mapas, sus textos y los menús pesa 28 KB, pero con 458 mapas y 53
  tilesets el presupuesto de 16 MB (menos firmware y FAT) es el límite duro. Hay que
  decidir si se empaqueta **todo** en el `.mote` o en un archivo de datos aparte leído
  con `kv_*`/`save()` (CON-02, D2).
