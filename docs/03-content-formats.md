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

Trampa — **no todo mapa tiene `.ablk` propio**. `data/maps/blocks.asm` comparte el
bloque de varias casas por etiqueta:

```
PlayersNeighborsHouse_BlockData:
Route30BerrySpeechHouse_BlockData:
ValenciaHouse_BlockData:
	INCBIN "maps/JohtoHouse4.ablk"
```

El extractor (`map_block_path()`) busca `maps/<Map>.ablk`; si no existe, lee el
`INCBIN` de la etiqueta `<Map>_BlockData`. Sin ese respaldo, la casa de los vecinos
salía como mapa vacío.

### Warps

```
; maps/NewBarkTown.asm
def_warp_events
	warp_event 15,  5, PLAYERS_HOUSE_1F, 1   ; x, y, mapa destino, nº de warp destino
	warp_event 10,  2, ELMS_HOUSE, 2
```

`MACRO warp_event` (`macros/scripts/maps.asm`) lo deja en el ROM como
`db y, x, warp_to, map_id`, y **el número de warp es 1-based**: apunta a la fila N del
`def_warp_events` del mapa destino. New Bark Town tiene 5; cuatro son puertas y una
es la estera de la casa de Elm en (10,2).

La clase de warp no está en el evento, se deduce del **tile que hay debajo** (colisión
del cuadrante, `warp_kind()` en el extractor):

| `kind` | Tile | Cuándo dispara |
|---|---|---|
| `PC_WARP_DOOR` (0) | `DOOR` | al **terminar** el paso sobre la celda |
| `PC_WARP_CARPET` (1) | `WARP_CARPET_DOWN/UP/LEFT/RIGHT` | de pie encima, con esa dirección mantenida y el paso bloqueado |
| `PC_WARP_STEP` (2) | `STAIRCASE`, `LADDER`, `CAVE`, `WARP_PANEL`… | como una puerta |

Un warp cuyo destino no está en el build queda con `map = -1`: el tile existe y se
pisa, pero no lleva a ninguna parte (las escaleras a los 2.º pisos, hoy).

El punto de reaparición de cada mapa no está en el mapa, sino en
`data/maps/spawn_points.asm` (`spawn NEW_BARK_TOWN, 15, 6`). Aunque el comentario del
macro diga `map, y, x`, el motor carga los dos bytes en `wXCoord` y `wYCoord`
(`engine/overworld/spawn_points.asm`), así que son `x, y`: Cherrygrove con `29, 4`
sólo encaja así en un mapa de 40×22 celdas. El extractor lo emite como
`PcMapDef.spawn_x / spawn_y`; New Bark Town da (15,6), la celda pegada a la puerta de
casa.

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
- **La colisión viene en orden de pantalla `TL, TR, BL, BR`**, que es también el que
  quiere el render: **no hay que reordenar nada**. Lo confirma
  `GetCoordTileCollision` (`home/map.asm`), que indexa
  `(x & 1) + 2 * (y & 1)` sobre los cuatro bytes del `tilecoll`. Una versión anterior
  del extractor los remapeaba con `order = (1, 3, 0, 2)` y eso giraba cada metatile
  180°: las puertas se leían como muro y el agua como hierba. Ver `docs/06`.
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

Trampa nº 3 — **dentro de un edificio no se usa la franja horaria, sino el grupo
`indoor`**. El original lo elige por la variable de entorno del mapa (`ENV_INDOOR` en
`data/maps/maps.asm`), y es la razón de que el interior de una casa no tenga los
verdes del exterior aunque el arte sea el mismo tileset: `johto_house` sólo aporta las
formas, el color lo pone la paleta. El extractor emite `bg_pal` (exterior) y
`bg_pal_indoor`, y `PcMapDef.pal` dice cuál toca.

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

### Comandos de texto

Los bytes `$00`-`$09` y `$52`-`$5d` no son caracteres: son órdenes que el motor de texto
interpreta (`home/text.asm`, tabla `TextCommands`) y **arrastran argumentos**. Un impresor
que no los conozca no los ignora: imprime el comando *y sus argumentos* como letras.

| Byte | Comando | Argumentos | Qué hace el original |
|---|---|---|---|
| $00 | `<START>` | — | la cadena empieza aquí |
| $01 | `<RAM>` | dirección (2) | imprime la cadena que hay en WRAM |
| $02 | `<WAIT>` | — | cursor parpadeante; espera a A/B |
| $03 | `<ASM>` | — | salta a código encajado en el propio flujo (o lo termina) |
| $04 | `<NUM>` | dir. (2) + formato (1) | imprime el número de esa dirección, con `formato & $f` dígitos |
| $05 | `<PAUSE>` | — | 30 frames de pausa, o A/B |
| $06 | `<SOUND>` | id de efecto (1) | `PlaySFX` + `WaitSFX` |
| $07 | `<DAY>` | — | "Sunday"…"Saturday" |
| $08 | `<FAR>` | banco + dirección (3) | imprime una cadena de otro banco |
| $09 | `<PLURAL>` | — | elige singular o plural |
| $52 | `<DONE>` | — | fin |
| $54 | `<PROMPT>` | — | fin y pregunta sí/no |
| $55 | `<LNBRK>` | — | salto sin espacio |
| $56 | `<NEXT>` | — | página nueva, sin borrar |
| $57 | `<LINE>` | — | línea nueva |
| $58 | `<CONT>` | — | línea nueva sin espacio |
| $59 | `<PARA>` | — | página nueva |
| $5a-$5c | `<TARGET>` `<USER>` `<ENEMY>` | — | nombres de los combatientes |
| $5d | `<CTXT>` | — | marca del decodificador contextual |

El extractor emite esa tabla **parseando `constants/charmap.asm`** (`text_commands()`),
no copiándola: un byte transcrito mal imprimiría basura en vez de fallar al extraer.

De los 23 textos extraídos **ninguno usa comandos** aparte de `<START>`: son diálogos
planos. Los comandos llegan con los scripts de mapa (fase 2), y el impresor del port ya
los consume todos (`text/box.c`), porque un comando desconocido no se ignora solo:
arrastra sus argumentos consigo.

## Scripts de mapa

`macros/scripts/maps.asm` define macros (`def_scene_scripts`, `scene_script`,
`warp_event`, `object_event`, `coord_event`, `bg_event`, `connection`…) que **emiten
bytes** con contadores y cálculos de offset. Es decir: un mapa es una estructura de
datos y sus scripts son **bytecode** que interpreta `engine/overworld/scripting.asm`.

Plan del port: no traducir los scripts a mano ni reescribirlos; extraer el bytecode y
escribir un **intérprete nuevo** en C con la misma semántica de comandos. Eso da acceso
gratis a los 458 mapas con sus eventos, textos, warps y escenas.

### Cómo se ensambla ese bytecode

`macros/scripts/events.asm` es la tabla de comandos: un `const X_command` por comando, en
el mismo orden que el `jumptable` de `RunScriptCommand`, y debajo el macro que lo emite.

```asm
	const showtext_command          ; 188 = $bc
MACRO showtext
	db showtext_command
	dw \1 ; text_pointer
ENDM
```

Los cuerpos son casi todos `db`/`dw`/`dba`/`dp`/`dr` con `\1`..`\9`, así que el
extractor **parsea la definición en vez de copiarla** (`script_defs`) y ensambla las llamadas
del fichero del mapa (`map_scripts`), en dos pasadas porque un `sjump` puede saltar hacia
adelante. Siete detalles que no son evidentes:

- **`dp` no son dos bytes sueltos.** `macros/data.asm` lo define como
  `db LOW(especie), HIGH(especie) << MON_EXTSPECIES_F | forma`: el segundo byte es la
  forma (bits bajos) con los bits altos de la especie desplazados. `setmonval TOGEKISS`
  (especie `$10f`, forma `PLAIN_FORM` = 1) emite `$0f $21`.
- **`dr` es una distancia relativa** (16 bits, big-endian) y solo existe en las variantes
  `sjumpfwd`/`iftruefwd`/`iffalsefwd`/`ifequalfwd`. El extractor **emite la forma absoluta**
  equivalente: un formato de salto es suficiente en el port y el cálculo de distancias deja
  de ser una fuente de errores de un byte.
- **Los punteros a texto se reescriben a índices** de `pc_texts`: el port no tiene
  direcciones de ROM. Los que apuntan fuera del fichero (datos de movimiento, scripts de
  otro mapa) quedan en `$FFFF` y se **listan** al extraer, no se rellenan con ceros.
- **Dentro de una etiqueta de script puede haber datos**: cadenas (`text "..."`) o pasos
  de movimiento (`step_*`). El ensamblador los salta hasta la siguiente etiqueta `.local`,
  que vuelve a ser código; sin eso, media docena de scripts se cortan en el primer `text`.
- **Un macro puede inyectar código como argumento**, y de hecho lo hacen los normales:
  `giveitem ITEM, iffalse_endtext` emite los tres bytes del objeto y después escribe `\#`,
  que es "los argumentos que `shift` no ha consumido". Cada uno de ésos se ensambla como
  un comando más en la misma posición; el fallback del inventario lleno es, literalmente,
  el siguiente comando del script.
- **Las tablas de saltos (`scalltable`)** son `dw Script1` / `dw Script2` dentro de una
  etiqueta de script: se emiten como palabras LE con el offset de cada etiqueta, el mismo
  formato que un `sjump`.
- **`if DEF(DEBUG)` también está en el `.asm` del mapa**, envolviendo una escena y trozos
  de texto. El extractor evalúa `if`/`elif`/`else`/`endc` como una compilación normal
  (`DEBUG=0`, `FAITHFUL=0`) y descarta la rama que no se ensambla; si un `DEF()` no está
  declarado, falla en voz alta.

### De un NPC a su script

`object_event` guarda en su noveno campo **cómo responde a A**, y hay dos formas:

```asm
	object_event 29, 12, SPRITE_POKEFAN_F, SPRITEMOVEDATA_SPINRANDOM_SLOW, 0, 0, -1, \
		PAL_NPC_PURPLE, OBJECTTYPE_SCRIPT, 0, TuscanyScript, EVENT_ROUTE_29_TUSCANY_OF_TUESDAY
	object_event 27, 16, SPRITE_SCHOOLBOY, SPRITEMOVEDATA_WALK_UP_DOWN, 1, 0, -1, \
		PAL_NPC_GREEN, OBJECTTYPE_COMMAND, jumptextfaceplayer, Route29YoungsterText, -1
```

- `OBJECTTYPE_SCRIPT` es una **etiqueta de script** del mismo fichero, que ya está en el
  blob: el objeto guarda su offset (`PcObject.script`).
- `OBJECTTYPE_COMMAND` es **un comando con su argumento**. El motor lo "ejecuta" copiando
  ese campo a `wScriptPointer`, así que el extractor emite ese comando como una región de
  tres bytes al final del blob: el objeto apunta a un script de una línea, exactamente como
  el original. `ObjectEvent` en ese campo es el valor nulo (NPC de escena, sin diálogo).
- `OBJECTTYPE_ITEMBALL`/`TRAINER`/`POKEMON`/`GENERICTRAINER` leen una ficha propia (el
  objeto, la party) en vez de un script: `tools/extract.py` los nombra en la salida y deja
  el objeto sin script.

Cobertura actual (`gen/scripts.h`): 224 opcodes, **2 473 B de bytecode** y 79 etiquetas en
los ocho mapas del build, más **32 scripts de objeto** (de 35 `object_event`) y **28
de 40 celdas `bg_event`**. Los dos mapas sin etiquetas de script
(`PlayersNeighborsHouse`, `ElmsHouse`) tienen blob igualmente: solo los comandos de sus
NPCs y sus carteles, 9 B y 12 B. Lo que el ensamblador no puede resolver — datos
de movimiento en otro fichero, un `dw` de otro banco, un entrenador — no se adivina: queda
en `$FFFF` y se lista en la salida del extractor.

### De un cartel a su script

`bg_event` es la segunda capa de "lo que hay delante": el motor busca un
`object_event` en la celda y, si no hay, un `bg_event` (`CheckAPressOW` →
`TryObjectEvent` → `TryBGEvent`). La fila lleva la celda, el tipo y el puntero:

```asm
	bg_event  8,  8, BGEVENT_JUMPTEXT, NewBarkTownSignText
	bg_event  6,  1, BGEVENT_UP,       PlayersHouse1FTVScript
	bg_event  2,  1, BGEVENT_READ,     ElmsLabHealingMachine
	bg_event  6,  1, BGEVENT_JUMPSTD,  difficultbookshelf
	bg_event  3,  2, BGEVENT_ITEM + POTION, EVENT_NEW_BARK_TOWN_HIDDEN_POTION
```

- `BGEVENT_JUMPTEXT` no apunta a un script: el motor **construye** uno
  (`jumptext <texto>`) y lo ejecuta. El extractor emite esos mismos tres bytes al final
del blob, así que el port no distingue un cartel de un NPC.
- `BGEVENT_READ` (3 celdas en el build, una de ellas apuntando a `InitialRadio`, que vive
  en otro fichero) y `BGEVENT_UP/DOWN/RIGHT/LEFT` (5 celdas) apuntan a un script del
  propio mapa, que ya está en el blob: la celda guarda su offset.
- `BGEVENT_JUMPSTD` (9 celdas, las estanterías del laboratorio) necesita la tabla de
  *std scripts*, y `BGEVENT_ITEM + <objeto>` (2 celdas) esconde un objeto bajo el suelo:
  ninguna se extrae todavía, y la celda queda a `$FFFF`.

El tipo es el byte original (`PC_BGEVENT_*` en `gen/scripts.h`), no un enum nuevo: la
celda dice `BGEVENT_UP` y el port lo compara con la dirección del jugador en
`script/trigger`.

### Texto que no tiene etiqueta

`jumpthistext`, `jumpthistextfaceplayer` y `jumpthisopenedtext` imprimen **dónde está el
script**, no una etiqueta. Hay dos sitios donde eso cae:

- dentro del mismo bloque, después del comando (`jumpthistext` + `text "..."`);
- en la **etiqueta siguiente** del fichero, que es lo normal cuando el comando es la última
  línea del script (así termina `MysticWaterGuy` en Ciudad Cerezo, imprimiendo
  `MysticWaterGuyTextAfter`).

El extractor distingue los dos y emite el índice de `pc_texts` como operando de 2 bytes, que
no es lo que dice el `.asm` (allí no hay operando) pero sí lo que el port necesita: la VM no
tiene que adivinar dónde acaba un texto dentro del blob.

## Texto

El texto del mapa **ya es bytecode**, no una cadena: `text "..."` emite `$00`
(`<START>`) y luego los bytes del charmap; `line`/`cont` emiten `$57`/`$58`,
`para`/`next` emiten `$59`/`$56` y `done` emite `$52`. Los textos son además el
punto donde el juego mete nombres y símbolos, con "n-gramas" (una ranura que
imprime varias letras).

`constants/charmap.asm` define tres charmaps; el de los textos es `compressing`, que
añade a los caracteres normales:

| Fuente | Byte | Imprime |
|---|---|---|
| `#` | `$4d` | "Poké" (cuatro glifos) |
| `#mon` | `$4e` | "Pokémon" |
| `<PLAYER>` | `$4f` | el nombre del jugador |
| `<RIVAL>` | `$50` | el del rival |
| `<TRENDY>` | `$51` | la frase de moda (aún no portada) |

Como el ensamblador ya resolvió el charmap, el extractor sólo tiene que hacer el
mapeo inverso (`maximal munch`, para que `#mon` gane a `#`) y emitir los bytes tal
cual: no hay que imitar el compresor Huffman del ROM, porque los seis mapas de
arranque suman **170 textos / 90 KB de header**, nada frente a los 16 MB de flash.

`text_far`, `text_asm`, `text_ram` y `text_decimal` delegan en otro fichero o en
código: el extractor los **salta** en vez de emitir media frase, y quedarán para
el intérprete de scripts (SCR-02).

### De un NPC a su texto

El `object_event` no lleva el texto, así que hay que resolver un salto:

```
object_event ..., OBJECTTYPE_COMMAND, jumptextfaceplayer, Text_GearIsImpressive
object_event ..., OBJECTTYPE_SCRIPT, 0, NewBarkTownTeacherScript
```

Con `OBJECTTYPE_COMMAND` el texto es el propio argumento. Con `OBJECTTYPE_SCRIPT`
se busca en el script la primera referencia directa (`showtext`, `writetext`,
`jumptext*`, `iftrue_jumptext*`) o el texto **inline** que sigue a
`jumpthistextfaceplayer`. No se siguen saltos a otros scripts: eso es flujo de
control y lo ejecutará la VM de scripts, no el extractor.

### La fuente

`gfx/font/normal.png` (128×64, 16 columnas de 8×8) y `common.png` (48×8) son las
vistas previas del 1bpp que genera `rgbgfx -d1`: fondo blanco, **tinta negra**, tiles
en orden de fila. El juego carga 114 tiles de `normal` en el tile `$80` (así que 'A'
es el tile 0) y 6 de `common` en `$f2` (↑ ↓ ′ ″ <PHONE> <BLACK>). La regla
`--trim-whitespace` de las fuentes sólo recorta tiles vacíos **del final**, de modo
que no hay desplazamiento que compensar.

`$7f` (espacio) no tiene arte: en el juego se carga un tile **opaco** desde
`space.png` para tapar texto viejo; aquí el fondo del textbox ya es papel, así que
una celda de espacio se queda vacía.

### Las entradas del menú de pausa

No están en `data/text`: son cadenas inline en el propio menú, porque se imprimen
directamente en su caja (`engine/menus/start_menu.asm`):

```
.PokedexString:  db "#dex@"
.PartyString:    db "#mon@"
.PackString:     db "Bag@"
.StatusString:   db "<PLAYER>@"
```

El `@` es el **terminador de cadena**, no un carácter (va detrás de la comilla). El
extractor las pasa por el mismo charmap, así que `#dex` son **cuatro bytes** (`$4d`,
`d`, `e`, `x`) y el juego imprime "POKédex" al expandir el n-grama: no hay una segunda
ruta de texto para los menús.

El original decide **qué** entradas mostrar en tiempo de ejecución (`wStatusFlags` bit
0 para la Pokédex, `wPartyCount` para Pokémon, `wLinkMode` y el concurso de bichos para
la bolsa); eso es estado de partida, que aún no existe. El port muestra las cuatro que
siempre aplican, en el orden del juego: `#dex`, `#mon`, `Bag`, `<PLAYER>`. Save,
Options, Exit, PokéGear y Quit vuelven con sus pantallas.

### La pantalla de Option

Tres ficheros, y ninguno es un texto suelto:

- `engine/menus/options_menu.asm` — la lista de 13 ajustes y el `Done` final, cada uno con
  sus valores (`"Slow   @"`, `"Switch @"`, `"12-hour@"`). Los valores están **padded a una
  columna fija**, porque el original los imprime en la misma línea que el nombre; aquí van
  debajo, así que el extractor les quita el relleno.
- `data/options/option_names.asm` — los nombres (`"Text Autoscroll@"`, `"#dex Units@"`), en
  el orden de su tabla `dw`, que es el orden del menú.
- `data/options/default_options.asm` — los valores iniciales:

```
; wOptions1   BATTLE_EFFECTS | STEREO | FAST_TEXT
; wOptions2   0   (zapatillas off, 12-hour, imperial, battle style Set)
; wOptions3   0   (nicknames Ask, teclado ABCDEF)
; wTextboxFrame 0 (Frame 01)
```

No todos los valores son cadenas: **Frame** es un número. `Options_Frame` guarda
`wTextboxFrame` (0..19) y lo imprime con `PrintNum` como `+1` en dos columnas, así que el
port guarda el índice 0..19 y dibuja "01".."20" en el sitio. Es el único sitio del port
que formatea un número en vez de copiar bytes.

Los tres bytes de opciones son bits repartidos (`wOptions1` tiene la velocidad de texto en
los bits 0-1, el autoscroll en 2-3, el estéreo en 6...). El port los guarda como un índice
por ajuste: la misma información, sin máscaras, y `LEFT/RIGHT` es un módulo.

## Música

`audio/music/*.asm` (100 canciones) es un lenguaje de macros sobre 4 canales
(`audio/` incluye el motor de sonido y los instrumentos). No hay forma barata de
reutilizarlo: hay que re-expresarlo como notas/stream para Mote (`audio_note`,
`audio_play_sfx`, o un mezclador propio vía `audio_set_stream`). Prioridad baja.

## Lo que emite el extractor

Un solo header, `game/src/gen/world.h`, con cinco grupos de tablas:

```c
bg_pal[8][4]                // paletas BG del grupo `day` (exterior)
bg_pal_indoor[8][4]         // paletas BG del grupo `indoor` (interiores)
pc_tilesets[PC_TS_COUNT]    // por tileset: tiles 2bpp, mt_tiles, mt_pal, mt_coll
pc_sprites[PC_SPRITE_COUNT] // hojas 16x96 (6 frames de 16x16), compartidas por mapas
pc_maps[PC_MAP_COUNT]       // { nombre, ancho, alto, bloques, tileset, objetos, n,
                            //   warps, n_warps, pal, spawn_x, spawn_y }
PcWarp warp_<mapa>[N]       // { x, y, mapa destino, nº de warp, kind, dir }
```

Los tilesets se deduplican: New Bark Town, Ruta 29 y Ciudad Cerezo usan
`johto_traditional` y las cuatro casas `johto_house`, así que el arte de cada uno
aparece una vez. Los objetos salen con coordenadas en píxeles (rejilla de 16),
la **cara** (`0` abajo, `1` arriba, `2` izquierda, `3` derecha) tomada del
`SPRITEMOVEDATA_STANDING_*`, y el **tipo de animación** (`0` quieto, `1` gira antihorario,
`2` horario, `3` aleatorio) tomado de los `SPRITEMOVEDATA_SPIN*`.

## Cómo se valida todo esto sin hardware

`tools/preview_map.py` renderiza un mapa en Python desde los mismos datos, con
`--model A` y `--order row|col|quad|quadT`, y paletas `town|gray`. Sirve para separar
"problema de datos" de "problema del render C".

La prueba estructural más útil: **Elm's Lab** (`maps/ElmsLab.ablk`, tileset
`johto_house`) y **PlayersHouse1F** deben salir como salas con paredes, estanterías,
mesas y ventanas. Si salen así, el orden de metatiles y de tiles es correcto — porque
una sala tiene estructura inequívoca, mientras que un terreno ditherado admite
interpretaciones.
