# 03 — Formatos de contenido (y sus trampas)

Todo lo que sigue está **verificado ejecutando el extractor** (`tools/extract.py`) y
comparando el resultado con material del propio repo. Las rutas son relativas a
`vendor/polishedcrystal`.

## Geometría: la base de todo

```
bloque (map "block")  = 32×32 px = metatile = 4×4 tiles de 8×8
paso del jugador      = 16×16 px
colisión              = por cuadrante de 16×16 dentro del bloque (4 por bloque)
object_event (x, y)   = rejilla de 16 px, NO de 32
```

Errores que provoca confundir esto:

- Leer los `object_event` como si fueran de 32 px deja NPCs en coordenadas fuera del
  mapa (`y=15` en un mapa de 9 bloques es imposible… salvo en rejilla de 16 px).
- Tratar el bloque como 16×16 descoloca todo el mapa.

## Mapa

| Dato | Dónde |
|---|---|
| Ancho/alto en bloques | `constants/map_constants.asm`: `map_const NEW_BARK_TOWN, 12, 9` |
| Id del mapa (`NEW_BARK_TOWN`) | `data/maps/attributes.asm`: `map_attributes <Map>, <MAP_ID>, <border>` |
| Nombre del mapa ⇄ id | `map <Map>, <TILESET>, <ENV>, <SIGN>, <LANDMARK>, <MUSIC>, <PHONE>, <PALETTE>` en `data/maps/maps.asm`; el id está en la línea de `map_attributes` de `attributes.asm` |
| Tileset | 2.º campo de esa misma línea `map` (`TILESET_JOHTO_TRADITIONAL`) |
| Bloques | `maps/<Map>.ablk` — **crudo**, `ancho*alto` bytes, uno por bloque |

`.ablk` es el dato sin comprimir: el build genera `maps/<Map>.ablk.lzp` para el ROM
(`data/maps/blocks.asm` hace `INCBIN` del `.lzp`), pero el `.ablk` del repo es crudo y
se lee directamente. New Bark Town: 12×9 = 108 bytes → 384×288 px.

## Metatiles y atributos

```
data/tilesets/<ts>_metatiles.bin    16 bytes por metatile = 16 ids de tile, ROW-MAJOR
                                    (fila 0: TL TR BL BR de la mitad superior)
data/tilesets/<ts>_attributes.bin   16 bytes por metatile = atributo por tile;
                                    bits 0-2 = ranura de paleta BG (0..7)
data/tilesets/<ts>_collision.asm    `tilecoll BL, TL, BR, TR` por metatile
```

Las tres tablas se enlazan juntas: `data/tilesets.asm` declara
`MACRO tileset` → `dbas \1Meta, \1Coll, \1Attr` + `dba \1GFX0, \1GFX1, \1GFX2`.
La colisión llega al ROM como `_collision.bin` (regla de build que ensambla el `.asm`),
pero la fuente legible es el `.asm`: es la que parsea el extractor.

- `johto_traditional`: 4048 B ⇒ 253 metatiles, ids de tile 0..160.
- Ambas tablas tienen el mismo tamaño (253×16) pero significados distintos: no
  confundirlas.
- **La colisión viene en orden `BL, TL, BR, TR`** y el render la quiere en orden de
  pantalla `TL, TR, BL, BR`. El extractor reordena (`order = (1, 3, 0, 2)`).
- Caminable = `FLOOR`, `TALL_GRASS`, `LADDER`, `WARP_CARPET_*`, `WATER`, `DOOR`… el
  resto bloquea. Está en `COLL_WALKABLE` de `tools/extract.py`; conviene revisarla
  contra `constants/collision_constants.asm` si aparece algún tile raro.

## Tilesets: arte 2bpp

```
gfx/tilesets/<ts>.<common>.png   ← hoja combinada (la que usa el build)
gfx/tilesets/<common>.png        ← sólo la parte común
```

Cada tileset aporta **tres** bancos de gráficos:

```
data/tilesets.asm:  TilesetJohtoTraditionalGFX0::  gfx/tilesets/johto_common.2bpp.lzp                  (128 tiles, común)
                    TilesetJohtoTraditionalGFX1::  gfx/tilesets/johto_traditional.johto_common.2bpp.vram0.lzp
                    TilesetJohtoTraditionalGFX2::  gfx/tilesets/johto_traditional.johto_common.2bpp.vram1.lzp
```

La hoja combinada que usa el extractor (`<ts>.<common>.png`) para `johto_traditional`
es 128×88 = 16 tiles de ancho × 11 filas = **176 tiles**. Ese 88 no es casual: el
Makefile parte el `.2bpp` con `tools/sub_2bpp.sh <file> 128` → `vram0` (tiles 0..127) y
`128 128` → `vram1` (tiles 128+). 8 filas (64 px) + 3 filas (24 px) = 88 px.
**Orden = fila a fila.**

Cómo se usa en el port: la hoja combinada se toma entera como tabla plana de 176 tiles,
y los ids de los metatiles de New Bark Town llegan a 160, así que se resuelven todos
(verificado visualmente). Aviso para otros tilesets: existe además una hoja **común
compartida** (`johto_common.png`, 128 tiles) que el motor carga aparte como GFX0. Si en
un tileset aparecen ids que se salen de la hoja combinada o el arte se ve desplazado,
la primera sospecha es que haya que concatenar/mapear esa hoja común.

Trampa nº 1 — **la rampa de grises está invertida**: el PNG guarda 4 niveles con
`0 = negro`, pero en 2bpp de GB el índice **0 es el color más claro** (la entrada 0 de
la paleta es casi blanca). Hay que invertir al extraer; si no, cada tile sale con
luces y sombras cambiadas.

```python
def _png_shade(v):     # 43/128/213 separan 0, 85, 170, 255
    return 3 if v < 43 else 2 if v < 128 else 1 if v < 213 else 0
```

Empaquetado por fila: `lo` (bit 0) y luego `hi` (bit 1), MSB = píxel izquierdo.

## Paletas

```
gfx/tilesets/bg_tiles.pal   ← las paletas BG del juego (5 bits por componente)
data/tilesets/<ts>_attributes.bin  ← qué ranura usa cada tile (bits 0-2)
```

`bg_tiles.pal` contiene 44 paletas: 4 franjas horarias (`morn`, `day`, `night`, `eve`)
× 8 ranuras, más `indoor` (8) y `water` (4). Las 8 ranuras son:

```
0 gray · 1 red · 2 green · 3 water · 4 yellow · 5 brown · 6 roof · 7 text
```

Trampa nº 2 — **la paleta no es una, son ocho, y cada tile elige la suya**. Con una
paleta única inventada, el pueblo entero se ve como rayas verdes: la acera (ranura
`gray`), la hierba (`green`) y los tejados (`roof`) comparten los mismos 4 índices con
significados de color distintos. Comparación de los grises de `day`:

```
day gray : (222,255,222) (172,172,172) (106,106,106) (57,57,57)
day green: (180,255,82)  (98,205,8)    (41,115,0)    (57,57,57)
day roof : (222,255,222) (123,255,255) (41,139,255)  (57,57,57)
```

(valores del archivo, 5 bits escalados a 8; los de `screenshots/` del repo coinciden
a ±1)

El índice 0 y 1 de `gray` están mucho más cerca entre sí que los de `green`: eso es lo
que hace que una textura de 1 px de alto se lea como relieve suave y no como rayas.

## Sprites de overworld

```
gfx/sprites/<nombre>.png   16×96 = 6 frames de 16×16 (2×2 tiles), 2bpp
```

- Nombre = `SPRITE_<X>` del `object_event` en minúsculas (`SPRITE_POKEFAN_F` →
  `pokefan_f.png`). 217 sprites disponibles.
- Orden de frames: `down` (quieto, paso), `up` (quieto, paso), `left` (quieto, paso);
  `right` = `left` espejado.
- El índice 0 del sprite es **transparente** (no es blanco): el renderizador debe
  saltarlo. En la práctica se usa `0` como clave de transparencia al expandir a RGB565.
- Paleta: `gfx/overworld/npc_sprites.pal` (variantes `npc_sprites_darkness.pal` y
  `npc_sprites_overcast.pal` para día/noche y tiempo).

Comprobado: `gfx/sprites/rival.png` es 16×96 en escala de grises de 4 niveles, igual
que la hoja de tilesets (misma inversión de rampa).

## Compresión: "Polished LZ"

Documentada en `vendor/polishedcrystal/docs/lz_format.md` y resumida aquí porque
**todo** lo que sea gráfico en el ROM pasa por ella (`.lzp`). Para el port sólo hace
falta si se decide extraer del ROM en vez de las fuentes, pero es la referencia si
algún día se quiere leer SRAM/saves o assets ya comprimidos.

- Cabecera corta `CCCLLLLL` (3 bits comando + 5 de longitud) o larga
  `111CCCL0 LLLLLLLL` (9 bits de longitud, bit 1 siempre 0).
- Comandos: `0 DATA`, `1 REPEAT`, `2 ALTERNATE`, `3 ZERO`, `4 COPY`, `5 COPY_FLIPPED`,
  `6 COPY_REVERSED`. **La longitud es `encoded + mínimo del comando`**, donde el mínimo
  es 1 para casi todos pero **2 para REPEAT y 3 para ALTERNATE**.
- Offsets: 1 byte con bit 7 = negativo (`0x80` = −1); 2 bytes = absoluto desde el
  inicio de la salida.
- Opcodes especiales propios `0xfc` (nibbles altos), `0xfd` (índices en tabla de 16),
  `0xfe` (nibbles bajos); `0xff` = fin.
- Copias con solapamiento (estilo LZ77) permitidas.

## Texto

```
constants/charmap.asm        257 líneas: caracteres → bytes, y comandos de control
macros/scripts/text.asm      macros `text`, `line`, `para`, `cont`, `done`, `text_ram`…
```

Trampa nº 3 — **el texto se comprime al ensamblar** (n-gramas: `$4d-$51`; el macro
`text` decide si comprime según ahorre espacio, y añade terminador `@`). Por eso *no*
conviene extraer el texto del ROM: es mucho mejor **parsear las cadenas fuente** de los
`.asm` (`text "..."`, `line "..."`, con el charmap aplicado para las comillas y
escapes) y generar texto plano en C. Los comandos (`<PLAYER>`, `<RAM>`, `<NUM>`,
`<PAUSE>`, `<SOUND>`…) son código de presentación, no contenido.

Fuente: `gfx/font/*.png` — `normal.png` es 128×64 = 16×8 celdas de 8×8 (fuente
principal); también `bold`, `italic`, `narrow`, `serif`, `chicago`, `micr`, `space`.

## Scripts de mapa

`macros/scripts/maps.asm` define macros (`def_scene_scripts`, `scene_script`,
`warp_event`, `object_event`, `coord_event`, `bg_event`, `connection`…) que **emiten
bytes** con contadores y cálculos de offset. Es decir: un mapa es una estructura de
datos y sus scripts son **bytecode** que interpreta `engine/overworld/scripting.asm`.

Plan del port: no traducir los scripts a mano ni reescribirlos; extraer el bytecode y
escribir un **intérprete nuevo** en C con la misma semántica de comandos. Eso da acceso
gratis a los 458 mapas con sus eventos, textos, warps y escenas.

## Música

`audio/music/*.asm` (100 canciones) es un lenguaje de macros sobre 4 canales
(`audio/` incluye el motor de sonido y los instrumentos). No hay forma barata de
reutilizarlo: hay que re-expresarlo como notas/stream para Mote (`audio_note`,
`audio_play_sfx`, o un mezclador propio vía `audio_set_stream`). Prioridad baja.

## Cómo se valida todo esto sin hardware

`tools/preview_map.py` renderiza un mapa en Python desde los mismos datos, con
`--model A` y `--order row|col|quad|quadT`, y paletas `town|gray`. Sirve para separar
"problema de datos" de "problema del render C".

La prueba estructural más útil: **Elm's Lab** (`maps/ElmsLab.ablk`, tileset
`johto_house`) y **PlayersHouse1F** deben salir como salas con paredes, estanterías,
mesas y ventanas. Si salen así, el orden de metatiles y de tiles es correcto — porque
una sala tiene estructura inequívoca, mientras que un terreno ditherado admite
interpretaciones.
