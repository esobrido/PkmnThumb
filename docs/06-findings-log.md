# 06 — Bitácora de hallazgos (y callejones sin salida)

Registro de lo aprendido construyendo el slice de overworld. Incluye las hipótesis que
**fallaron**, porque repetirlas cuesta horas.

## Decisiones de fondo

1. **Camino elegido: reescritura nativa sobre Mote** (no emulación). El motor GBZ80 se
   sustituye por C; el contenido se reutiliza como datos. La alternativa (emular el ROM
   en el slot GB/GBC de ThumbyOne) era mucho más barata pero no es un port.
2. **`render_band` propio** en vez de `scene2d_*`: el modelo del GB (paleta por tile,
   prioridad sprite-fondo, ventana) se reproduce con menos fricción a mano.
3. **Extraer de las fuentes del repo, no del ROM.** No hace falta RGBDS ni el `.gbc`:
   los `.ablk`, `.bin`, `.asm` y `.png` del repo bastan. Esto elimina la dependencia
   de la toolchain de GB para el 90 % del trabajo.

## Descubrimientos operativos

- **Mote es un repo aparte** (`austinio7116/mote`). ThumbyOne lo busca en `../mote`
  (`add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../mote/os/device ...)`), así que el
  clon tiene que estar en esa ruta relativa para que el firmware de ThumbyOne compile.
- **`mote_host` ejecuta el launcher de Mote**, no directamente el juego. En headless
  hay que enviar una pulsación de A (`MOTE_KEYS="a:2-5"`) o usar `MOTE_PICK`.
- El emulador tiene tres modos de automatización poco documentados y muy útiles:
  `MOTE_KEYS` (input guionizado), `MOTE_SHOT`+`MOTE_SHOT_FRAME` (captura y salida) y
  `MOTE_REC`/`MOTE_REC_N` (grabación de frames).
- Sin root no hay paquetes: `pip install cmake` falla y `apt` no se puede usar. La vía
  que funciona es el **tarball oficial de cmake** + **compilar SDL2** con los drivers
  `dummy` desactivando X11/Wayland/ALSA. Queda encapsulado en `setup.sh`.

## Hallazgos de formato (los caros)

| # | Hallazgo | Síntoma si se ignora |
|---|---|---|
| 1 | `.ablk` es crudo; el `.lzp` lo genera el build | buscar un descompresor LZ que no hace falta |
| 2 | Las dimensiones vienen de `map_const <MAP_ID>`, y el `MAP_ID` no es el nombre del mapa | "map_const for NewBarkTown not found" |
| 3 | Bloque = 32×32 px (4×4 tiles); metatile = bloque, no 16×16 | mapas descolocados y 253 metatiles mal interpretados |
| 4 | `object_event` está en rejilla de 16 px | NPCs con coordenadas fuera del mapa (`y=15` en 9 bloques) |
| 5 | `tilecoll` va en orden `BL,TL,BR,TR` | colisiones giradas 90°/espejadas |
| 6 | La hoja de tileset es `vram0` (128 tiles) + `vram1` en orden de fila (altura 88 = 8+3 filas) | ids de tile desplazados y arte mezclado |
| 7 | **El gris 0 del PNG es el color más oscuro, pero el índice 0 de GB es el más claro** | cada tile con luces y sombras invertidas |
| 8 | **Cada tile elige una de 8 paletas BG** (bits 0-2 del byte de atributo) | todo el mapa con una paleta inventada: rayas verdes donde debería haber acera |
| 9 | El texto se **comprime al ensamblar** (n-gramas) | intentar extraerlo del ROM es mucho peor que parsear las cadenas fuente |

Los hallazgos 7 y 8 son los que explican el 90 % del tiempo perdido en esta fase.

## Callejones sin salida (no repetir)

- **"El mapa debe ser de celdas de 16 px"** (modelo B: 4 bytes por metatile,
  `ablk*4`): revienta con `IndexError` porque los ids llegan a 129 y sólo habría 253
  entradas para 1012 necesarias. Descartado por aritmética, no por estética.
- **Buscar un tablero de ajedrez (checkerboard) en la hoja de tiles** para localizar el
  suelo: no existe ninguno a ningún offset de 8×8. El suelo "de hierba" del original no
  es un tile de tablero; es una textura ditherada de 4 tiles que sólo se lee al
  ensamblarse, y con la paleta correcta.
- **Hipótesis del bit de banco de VRAM en el atributo**: los atributos tienen valores
  0..12 y el bit 3 parece banco, pero aplicarlo no cambia nada en los bloques que
  fallaban (el banco ya viene codificado en el propio id, 128+).
- **Hipótesis del `dedupe` de `tools/gfx`** reordenando tiles: si reordenara, el juego
  original no compilaría correctamente, y PC es un hack que funciona. Descartada.
- **Transposición de la hoja (tile → `(t/11, t%11)`)** y variantes de orden dentro del
  metatile (`col`, `quad`, `quadT`): todas producían resultados igual de incoherentes.
  El orden correcto era fila a fila desde el principio.
- **Adivinar la paleta a ojo**: la paleta "sampleada" a mano de una captura acertaba los
  4 colores de la hierba y aun así el pueblo se veía mal, porque los otros tiles usan
  otras ranuras. Hay que leer `bg_tiles.pal`.

## Lo que sí funcionó como oráculo

- **Elm's Lab / PlayersHouse1F**: una sala tiene estructura inequívoca (paredes,
  estanterías, mesas), así que valida el orden de metatiles y de tiles de un vistazo.
  Esto es lo que permitió separar "datos mal" de "render mal".
- **`screenshots/*.png` del repo** dan colores reales (por ejemplo, la hierba de
  Cherrygrove = `(181,255,82)`/`(99,206,8)`, que coincide exactamente con la ranura
  `green` de `bg_tiles.pal` en 5 bits).
- **`tools/preview_map.py`**: render de referencia en Python, independiente del C. Cada
  vez que el C y el Python coincidían, el problema estaba en los datos; cuando
  diferían, estaba en el C. Ahorra depurar dos capas a la vez.

## Estado al cerrar esta fase

- `tools/extract.py` extrae mapa, metatiles, atributos, colisión, tiles, paletas BG,
  sprites y objetos, y **falla con mensaje claro** si los tamaños no cuadran.
- `game/src/game.c` renderiza el mapa con paleta por tile, mueve al jugador en pasos de
  16 px con colisión real, panea la cámara y dibuja los 5 NPCs del mapa.
- Verificado: build host, captura headless, movimiento con `MOTE_KEYS`, colisión y
  compilación sin warnings propios (`-Wall -Wextra`; los avisos que salen son de las
  cabeceras de Mote).
- No verificado: nada en hardware, ni el build de device, ni el coste de raster.
