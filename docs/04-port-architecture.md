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

## El mundo: varios mapas en tiempo de ejecución

`game/src/gen/world.h` no contiene "un mapa": contiene **tilesets, sprites, mapas y
objetos**, todo en tablas con índice.

```c
pc_tilesets[PC_TS_COUNT]   // arte + metatiles + paletas + colisión, por tileset
pc_sprites[PC_SPRITE_COUNT] // hojas de 6 frames, compartidas por todos los mapas
pc_maps[PC_MAP_COUNT]       // { ancho, alto, bloques, tileset, objetos, n objetos }
```

Los mapas comparten tileset (New Bark Town, Ruta 29 y Ciudad Cerezo usan todos
`johto_traditional`), así que el arte se emite **una vez** y los mapas lo referencian por
índice: el flash crece con el arte distinto, no con el número de mapas.

`load_map(i)` en `game.c` rehace la vista `OwMap`, carga los NPCs del mapa en el array de
actores y recoloca al jugador. `load_map_at(i, warp)` es lo mismo pero dejando al
jugador en el warp N del destino: es lo que usan las puertas.

Dónde aparece el jugador en un mapa al que no ha entrado por una puerta (el salto de
depuración RB/LB) lo decide `map_spawn()`: el punto de reaparición del original
(`spawn_x/spawn_y` de `gen/world.h`) si el mapa tiene uno, el primer warp si no (los
interiores, que así empiezan en la estera), y para New Bark Town una celda elegida a
mano en la carretera del sur, que es donde arrancan todos los guiones de captura de
`docs/05`. Si la celda está bloqueada, `place_player()` busca la libre más cercana en
vez de dejar al jugador dentro de un árbol.

El cambio de mapa también se puede forzar con **RB (siguiente) / LB (anterior)**, un
atajo de depuración, no la forma de viajar del juego.

## Puertas: disparo, fundido y vuelta afuera

Dos módulos puros y nada más:

```
ow/fade.c   ow_fade_apply(fb, px, level)   rgb565 -> blanco, nivel 0..10
ow/warp.c   OwWarp                      máquina de estados de la transición
```

`ow/warp` no carga mapas ni sabe de Mote: recibe la tabla de warps generada y devuelve
"toca cambiar al mapa X, warp N" durante exactamente un frame.

### Cuándo se dispara

Las dos formas del original, que no son la misma:

| | Dispara | Por qué |
|---|---|---|
| Puerta / escalera | al **terminar** el paso sobre su celda | `CheckTileEvent` corre una vez completado el paso (`player_step.asm`) |
| Alfombra direccional | de pie encima, con la dirección mantenida, **sin poder dar el paso** | `.CheckWarp` va después de `TryStep` en `player_movement.asm` |

Lo que hace falta para distinguirlas en el port es que `ow_player_update()` marque dos
cosas: `just_arrived` (un paso acaba de terminar) y `bumping` (la dirección mantenida no
pudo empezar un paso). Estar quieto sobre una puerta no dispara nada — por eso entrar no
rebota de vuelta.

### La transición

`MAPSETUP_DOOR` del original es `FadeOutPalettes` → cargar el mapa → `FadeInPalettes`, y
los dos fundidos llevan `c = 10` (`timeofday_pals.asm`). **No hay animación de tile de
puerta en Polished Crystal**: el fundido es la puerta, más el paso al umbral.

```
OUT  10 frames   nivel 1..10   (al llegar a 10 se pide la carga del mapa)
IN   10 frames   nivel 9..0
```

El fundido se aplica **en `g_overlay`, el último paso del frame**, sobre el framebuffer
ya compuesto: con la arte en RGB565 no queda ninguna paleta que oscurecer, y así el
fade cubre también los sprites, como el original. Mientras dura, `g_update` no deja
mover al jugador.

### Dónde aterriza

- Puerta: en su celda **y una más abajo**, de cara al sur. Es el empujón de `COLL_DOOR`
  (`.CheckTile`): salir de un edificio te deja un paso por delante de la puerta. Se
  aplica en el frame blanco en lugar de animar un paso que nadie vería.
- Alfombra: en su celda, de cara **arriba**, hacia el interior.
- Si la celda de destino está ocupada (un NPC en la puerta), se queda en el umbral en vez
  de solaparse con él.

### `armed`: el rebote que no ocurre

Al entrar en una casa aterrizas en la estera, que es el warp de salida, y muchas veces
con la dirección todavía pulsada. El original no rebota porque aterrizar no es un paso;
el port tampoco, por dos motivos a la vez: la estera sólo dispara con el pad apretado y
el paso bloqueado, y además `OwWarp.armed` queda a 0 hasta que se suelte el pad.

### Paleta del interior

`PcMapDef.pal` elige `bg_pal` (exterior, franja horaria) o `bg_pal_indoor` (grupo
`indoor` de `bg_tiles.pal`), y `g_band` cambia de tabla en cada mapa. Sin eso el
interior de una casa sale con los verdes del pueblo.

## Actores: orientación, animación y orden de dibujado

`ow/actor` guarda un NPC: posición, dirección, tipo de animación y estado de paso. Las
tres cosas que resuelve:

- **Orientación de partida**, tomada del `SPRITEMOVEDATA_STANDING_*` del `object_event`.
  El resto de tipos mira al sur hasta que exista el movimiento de NPCs (OW-06).
- **Animación en el sitio**: los `SPRITEMOVEDATA_SPIN*` giran una cara cada 3 frames (el
  contador de `OBJECT_STEP_FRAME` del original). El `SPINRANDOM` usa un LCG sembrado con
  la posición del NPC, así que el frame N de una captura sale siempre igual.
- **Orden de dibujado**: jugador y NPCs se ordenan por la **y de los pies**, en el mismo
  array, con `ow_actor_depth_sort` (inserción, estable).

Los frames de andar usan la misma tabla de 6 frames que el jugador (`ow/dir.h`), así que
un NPC que ande más adelante no necesita código nuevo de dibujo.

## Texto

Dos piezas, ambas puras y con test:

- `text/font` — busca el glifo 1bpp de un código y expande lo que no es un glifo:
  `#` → "Poké", `#mon` → "Pokémon", `<PLAYER>`/`<RIVAL>` → el nombre del jugador o del
  rival. Los nombres se pasan al abrir la caja, no son globales.
- `text/box` — interpreta el bytecode de texto y lo convierte en una página de 15×3
glifos: corta por palabras, pagina en `$59`/`$56`, revela un glifo cada 2 frames, espera
A entre páginas (`A` a media página la completa) y pinta el ▼ parpadeante en la esquina.

**Los comandos se consumen todos**, no sólo los saltos de página (lista completa en
`docs/03`). Un comando desconocido no se ignora: arrastra sus argumentos, así que
imprimirlo como letras saca basura en medio del diálogo. Los que necesitan algo que el
port aún no tiene se saltan a conciencia y con un comentario que lo dice: `<SOUND>` (no
hay audio), `<RAM>` y `<FAR>` (las cadenas viven en WRAM o en otro banco, que el
extractor no emite), `<DAY>` (no hay reloj, SYS-05) y los nombres de combate. `<ASM>`
termina el texto, que es la mitad segura del comportamiento del original.

Dos comandos sí hacen algo y son **paradas dentro de la página**, no saltos:

- `<WAIT>` para el impresor en seco (el ▼ parpadea) y **A sigue imprimiendo en la misma
  página**; sólo después A pasa de página. Es lo que hace el cursor del original.
- `<PAUSE>` es sólo un retardo de 30 frames, sin botón.

La parada se guarda por glifo, así que cae en el sitio exacto del flujo, incluso a mitad
de palabra ("are you`<WAIT>`ready?").

`<NUM>` imprime el número que el llamante haya registrado para esa dirección
(`TextVars.nums`): no hay WRAM que leer, y una dirección no registrada imprime "?" en
lugar de un número inventado.

**Reflow.** El original parte las líneas a 20 columnas y aquí caben 15, así que la caja
**ignora** los `$57`/`$58` y re-envuelve por palabras; `$59`/`$56` sí son página nueva.
Se pierden las alineaciones manuales (poemas, menús), que de todos modos no pasan por
esta caja.

La caja se dibuja en `overlay`, **después de los sprites**, para que tape al jugador y a
los NPCs como la ventana del original. Usa la paleta `text` del juego (slot 7).

Hablar: `A` con un NPC en la celda a la que miras. El jugador debe estar entre celdas
(a mitad de paso no se habla, igual que en el GB); el NPC gira hacia ti y el mundo se
congela (sin caminar y sin giros de NPC) mientras la caja está abierta.

## UI: listas y menú de pausa

`ui/list` es la ventana que compartirán el menú de pausa, el equipo, la mochila y las
opciones: una caja con una fila por entrada y un ▶ en la seleccionada. Guarda `top` y
`cursor` y hace el trabajo aburrido:

- el cursor **da la vuelta**, como el flag `%10101000` del menú original (wrap around,
  START cierra);
- la ventana **sigue al cursor**, así una mochila de 30 objetos no necesita 30 filas, y
  el cursor nunca se sale de la caja;
- con más entradas que filas, ↑/↓ en la columna derecha avisan de que hay más.

`ui/menu` sólo añade las reglas del START: abrir conservando la entrada anterior (el
juego la recuerda en `wBattleMenuCursorBuffer`), cerrar con B o MENU, y `A`, que cierra
y devuelve la entrada elegida.

Dibujo: la misma caja que el textbox (borde de 1 px, 4 px de aire) y la misma paleta (7),
puesta en `overlay` después de los sprites. Son 10 tiles de ancho (**80 px**) pegados al
borde derecho, como `menu_coords 10, 0, 19, 17`, pero de 4 filas y no 18: la pantalla
portada es de 16 tiles de alto, no de 18.

Las etiquetas son las del propio juego, extraídas a bytes del charmap (`#dex`, `#mon`,
`Bag`, `<PLAYER>`), así que `<PLAYER>` imprime el nombre real y `#dex` se expande a
"POKédex" por la misma ruta que el diálogo. Sólo Options tiene pantalla: las otras cuatro
son Pokédex, equipo, mochila y tarjeta de entrenador, y no tienen datos que mostrar hasta
que existan los sistemas de GAM-02/GAM-04/GAM-06 (GAM-07 las cubre). Hoy `A` sobre ellas
cierra el menú y deja la entrada elegida en el log, que es el punto donde se engancharán.

## UI: la pantalla de Option

Las reglas son las del original y caben en una función: **UP/DOWN** elige fila,
**LEFT/RIGHT** cambia el valor, **A** sólo hace algo en `Done`, **B/MENU** salen. No hay
que confirmar nada porque los ajustes se aplican al cambiarlos — por eso el estado cabe en
un `uint8_t` por ajuste y sobrevive a la pantalla, como los tres bytes de opciones del jue­go.

Tres decisiones de forma:

- **Caja de ancho completo, 7 filas de 14.** El original muestra 5 de 14; aquí caben 7.
- **Dos líneas por entrada** (nombre y valor debajo): la fila del original son 20 tiles y
  aquí hay 16, y "Text Autoscroll" solo ya ocupa 15.
- **La fila elegida es una barra en negativo, no un ▶.** Con 120 px de texto útil no queda
  columna para el cursor; el nombre usa el ancho completo (15 tiles) y se dibuja en color
  papel sobre una barra de tinta. En las listas de una línea, donde sí cabe, sigue el ▶.

De los trece ajustes, hoy **Text Speed**, **Running Shoes** y **Turning Speed** tienen a
quién configurar:

- **Text Speed**: `game.c` empuja el valor al textbox al abrirlo, y la tabla es la del
  original (`PrintLetterDelay`: Slow/Medium/Fast esperan 5/3/1 frames por letra e Instant
  imprime la página).
- **Running Shoes**: el ajuste no funciona como un interruptor normal. El original
  (`.RunCheck` en `engine/overworld/player_movement.asm`) corre cuando el bit del ajuste y
  el botón B **no coinciden**, así que con el ajuste en Off se corre *manteniendo* B y con
  el ajuste en On se corre siempre y B sirve para andar. La decisión es una línea
  (`options_run` en `ui/options.c`) y el movimiento también: la fila de zapatillas de
  `StepVectors` es la de andar con el doble de píxeles por frame (2 px × 8 frames, en vez de
  1 px × 16), o sea la misma celda de 16 px recorrida en la mitad de tiempo. **No hace falta
  animación aparte**: la hoja de sprites avanza cada 8 px andados, y los píxeles llegan al
  doble de velocidad solos, que es exactamente el `>>2` en lugar de `>>3` de
  `OBJECT_ACTION_RUN` frente a `OBJECT_ACTION_STEP`.

- **Turning Speed**: cuanto tarda el jugador en cambiar de dirección. El original no
  cambia la cara de golpe: `StepFunction_Turn` (en `map_objects.asm`) pasa por dos fases de
  `GetTurningSpeed()` frames, la primera con la cara vieja y la segunda con la nueva, y sólo
  cuando termina puede empezar un paso. `GetTurningSpeed` devuelve 4 con Slow y 2 con Fast,
  así que girar cuesta 8 o 4 frames. `options_turn_frames` da ese número y `ow_player_update`
  lo consume: mientras el giro dura no se anda, y un toque corto gira sin dar el paso (que es
  lo que el jugador nota al tocar una dirección para mirar algo). Correr no acorta el giro:
  el original ahí cuenta frames, no píxeles.

Los demás se guardan y se muestran, pero esperan a sus sistemas: sonido, batalla, las 8
fuentes del juego y el marco (`gfx/frames`).

## Celdas ocupadas por NPCs

Un NPC no está en la colisión del tileset: el original le reserva la celda por su
cuenta. Por eso `OwMap` tiene una capa opcional `cells` (una por celda de 16 px) que
`ow_cell_blocked` consulta después de los tiles. `mark_actors()` la rellena al cargar el
mapa, antes de colocar al jugador, así que nadie aparece dentro de un NPC ni lo
atraviesa. Cuando los NPCs se muevan (OW-06) ésta es la tabla que hay que actualizar.

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
| Flash (8 mapas + 3 tilesets + textos + fuente + menús) | ver `.mote`: **49 556 B** de código+datos |
| RAM de módulo (`.bss`) | **3 044 B** (capa de celdas de NPC + estado de texto/menús + el warp) |
| Arena | ~34 KB (11 hojas de sprite + jugador expandidas a RGB565) |
| Raster host (`perf()`), 128×128 | **~20 µs/frame** (`scripts/perf.sh 200`) |
| Fundido de puerta, 16 384 px | **61 µs** en host (host 3 GHz; en device sería ~0,7 ms por frame durante 20 frames, estimación por reloj, sin medir) |

El `.mote` de 49 556 B incluye los ocho mapas (New Bark Town, Ruta 29, Ciudad Cerezo y
los cinco interiores), sus tilesets —`johto_traditional` más `lab` y `johto_house`, que
son casi todo el crecimiento desde los 28 512 B de la 0.7.0 (los comandos de texto y
las puertas añaden menos de 1 KB más)—, los diálogos, la fuente y
el menú. El presupuesto de flash no es el problema en esta fase: lo será la extracción
en lote (458 mapas, 53 tilesets), que es CON-01/CON-02.

La pasada del fundido es lo más caro que se añade al frame: recorre los 16 384 píxeles
una vez por frame durante los 20 de la transición (`blend()` por canal, sin SIMD). Sólo
ocurre dentro de una puerta, así que no compite con el juego normal.

Los tiles se leen de flash en el bucle de raster (XIP): no se copian a RAM. Sólo los
sprites se expanden a RGB565 (16×16 con transparencia), porque se dibujan con
prioridad y flips.

Medición del raster: `scripts/perf.sh` imprime la línea que el juego loguea una vez por
segundo con `mote->perf()`. En el host da ~19 µs; **no es un número de device** (240 MHz,
bandas en dos núcleos, XIP), así que hay que repetirlo en hardware antes de dar 60 fps
por seguros.

## Modelo de movimiento

- Paso de 16 px, 16 frames a 60 fps (la velocidad del original), con `dt` para no
  depender de los fps.
- Pedir dirección gira en el sitio y arranca el paso si la celda destino es libre —
  igual que en el GB. Pulsar **la dirección a la que ya miras también anda** (al
  aparecer mirando abajo, pulsar abajo tiene que mover, no congelar).
- El paso se completa aunque se suelte el botón a mitad: el jugador nunca queda entre
  celdas.
- Si la celda destino no es libre (pared, NPC, borde del mapa) el jugador **anda en el
  sitio**. El original cae en `.NotMoving` → `._WalkInPlace`, cuyo `movement_step_bump`
  pone `OBJECT_ACTION_BUMP`; y en `ObjectActionPairPointers` esa acción es el mismo código
  que la del paso (`SetFacingBumpAction` cae por fallthrough en `SetFacingStepAction`), así
  que el contador de frame sigue corriendo y las piernas se mueven mientras la cara se
  queda en el obstáculo y la celda no cambia. No es un paso: no deja nada pendiente y se
  apaga el frame en que se suelta el botón. Correr no acelera el empuje (allí el GB cuenta
  frames, no píxeles), y el `SFX_BUMP` del original espera a que haya audio.
- La colisión consulta el bitmask del metatile para el cuadrante de 16 px que ocupa el
  jugador.
- Animación: 2 frames (quieto / paso) alternando cada 8 px recorridos. El empuje reusa los
  mismos dos frames y el mismo contador, sólo que sin píxeles que sumar.

## Convenciones de código

Reglas completas en [07-conventions.md](07-conventions.md). En resumen: el código se
ordena por complejidad (fichero si es simple, carpeta propia si es o puede ser complejo),
cada módulo tiene test unitario, y el build se hace con `scripts/build.sh` (la
herramienta de Mote no compila subcarpetas).

- `game/src/gen/*.h` es **generado**: no se edita (`tools/extract.py` lo reescribe).
- Nada de `malloc`: todo buffer sale de `mote->alloc` en `init`.
- Los datos grandes son `static const` (flash/XIP); sólo lo que se transforma se copia.
- El raster no asigna ni llama al motor: sólo escribe en `fb` (seguro en ambos núcleos).
- Nombres de datos espejo del original (`mt_tiles`, `mt_pal`, `mt_coll`, `map_blocks`)
  para poder contrastar con la fuente asm en cualquier momento.

## Riesgos abiertos

| Riesgo | Detalle | Mitigación |
|---|---|---|
| Coste de raster en device | 128×128 a 60 fps con decodificación por píxel; medido sólo en host (~19 µs) | Medir con `perf()` en hardware; si sobra, cachear tiles expandidos por paleta (8 paletas) |
| Prioridad BG-sobre-OAM | el atributo de tile del GB tiene un bit de prioridad (tile sobre sprite) que aún no se aplica; hoy los sprites siempre van encima | Añadir cuando un mapa lo necesite (copas de árbol, puertas); es un segundo pase de bandas sólo con esos tiles |
| 16 tiles de ancho de UI | los textboxes del original son de 20 tiles | rediseñar a 16, o marco con scroll de texto |
| Memoria en batalla | el motor de batalla es el trozo más grande (31 K líneas asm) | por fases: primero una batalla salvaje con 1 Pokémon, sin animaciones |
| Audio | buzzer 4 kHz | 2 canales + lowpass; aceptar pérdida de fidelidad |
| Licencia | IP de Nintendo/Game Freak | proyecto personal, sin builds públicas |
