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
música (re-expresada)                     guardado, reloj (el RTC no existe)
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
pc_maps[PC_MAP_COUNT]       // { ancho, alto, bloques, tileset, objetos, n objetos, conexiones }
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

## Conexiones: caminar de un mapa al de al lado

`ow/connect.c` es lógica pura sobre la tabla `PcConnection` del mapa; no conoce ni el
tileset ni Mote.

```c
const PcConnection *ow_connect_at(conns, n, dir)
int ow_connect_on_edge(cx, cy, w, h, dir)
int ow_connect_land(c, dest_w, dest_h, cx, cy, &px, &py)
```

El original **no funde** al cruzar una conexión: `EnterMapConnection`
(`engine/overworld/warp_connection.asm`) cambia de mapa y rellena una franja del borde con
el vecino, que luego entra arrastrada por el scroll. Aquí no hay búfer de borde
compartido (cada mapa se dibuja de su tamaño), así que el cruce es un **corte**: al
apretar contra la celda del borde, el jugador reaparece en la celda del borde opuesto del
mapa vecino, mirando hacia donde iba, y la dirección mantenida lo hace seguir andando.
Nada de fundido: el GB tampoco lo usa ahí.

Aterrizaje, con `offset` en bloques (el original lo convierte en celdas con
`offset * -2`):

| dirección | celda de destino |
|---|---|
| oeste | `x = ancho_destino*2 - 1`, `y = y - 2*offset` |
| este | `x = 0`, `y = y - 2*offset` |
| norte | `y = alto_destino*2 - 1`, `x = x - 2*offset` |
| sur | `y = 0`, `x = x - 2*offset` |

`ow_connect_on_edge` es lo que impide que una pared del borde se tome por una salida: la
conexión sólo existe en la celda del borde (`x == 0`, `x == ancho-1`, `y == 0`,
`y == alto-1`), y `ow_connect_land` devuelve 0 si la celda calculada se sale del mapa
destino. En `game.c` el disparo va después de las puertas y sólo si el jugador está
quieto (`step_left == 0`), de modo que un paso a medias nunca cruza.

El desplazamiento se comprobó contra los datos: de las 210 conexiones de
`attributes.asm`, **58 sólo encajan con `-2*offset` y ninguna con `+2*offset`**; otras
124 encajan con los dos signos (su offset es 0, o las dos orillas son andables de punta a
punta) y 28 no tienen ninguna celda andable en común con ninguno de los dos. No es una
convención deducida a ojo del macro.

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

### Paletas: la franja que elige la hora

`gfx/tilesets/bg_tiles.pal` no tiene una paleta BG sino **cinco grupos de ocho**: `morn`,
`day`, `nite`, `eve` e `indoor`, más un sexto grupo `water` con una fila por franja. El
extractor emite los cuatro de exterior como `bg_pal[4][8][4]` (orden `OW_TOD_*`: morn,
day, nite, eve) y el de interior como `bg_pal_indoor[8][4]`; el slot 7 (texto) es el
mismo blanco-sobre-negro en los cuatro, así que los menús se quedan en el grupo `day`.

Qué grupo usa un mapa lo deciden dos datos del original, no una heurística
(`engine/gfx/color.asm LoadMapPals` → `data/maps/environment_colors.asm`):

| | Elige |
|---|---|
| `map_environment` = TOWN/ROUTE/ISOLATED/CAVE/DUNGEON | el grupo de exterior de **su fase** |
| `map_environment` = INDOOR/GATE | `indoor` por la mañana y de día; los grupos **nite/eve de exterior** de tarde y de noche |
| `PcMapDef.pal` (campo horario del header) | `PALETTE_AUTO` sigue al reloj; `PALETTE_DAY/NITE/MORN/EVE` fijan la fase; `PALETTE_INDOOR` fuerza el grupo de interior |

De ahí salen dos cosas que parecen raras y son fieles: una casa **sin** `PALETTE_DAY`
(ninguna de este build) se pone azul de noche, y una casa con `PALETTE_DAY` —Elm, la
tuya— sigue clara a las 23:00. Los dos mapas de exterior del build son `PALETTE_AUTO`
(New Bark Town, Ruta 29, Ciudad Cerezo) y los cinco interiores son `PALETTE_DAY`.

Falta el grupo `water`: el original sustituye el slot 3 de exterior por su fila `water`
(de ahí los índices `$28-$2b` de `EnvironmentColorsPointers`), y el port usa el slot 3
del grupo normal. Son colores de agua animada, no un cambio de fondo.

## El reloj: día y noche

El Thumby Color no tiene RTC (ni pila) y la API de Mote sólo da un `dt` por frame, así
que el port acumula **tiempo jugado**: `ow/clock` con `hour/minute/second` y
`ow_clock_advance(ms)`. Es el camino que el propio original trae para cartuchos sin RTC:
un segundo de juego por segundo real y el reloj **6× por delante**
(`NO_RTC_SPEEDUP`), o sea un día cada cuatro horas de partida
(`data/options/initial_options_descriptions.asm` lo dice con esas palabras).

Las fronteras son las del original (`constants/misc_constants.asm`): MORN a las 5, DAY a
las 9, EVE a las 17, NITE a las 21, y todo lo anterior a las 5 cuenta como NITE porque
`GetTimeOfDay` sustituye la hora por `NITE_HOUR` (`ow_clock_tod`). La partida empieza a
las **10:00**, dentro de DAY, porque el port no tiene pantalla de puesta en hora ni
puede preguntarle al hardware.

Un cambio de fase hace dos cosas en `game.c`: `g_band` pasa a elegir otro grupo de
paletas (sin fundido: el original recarga paletas, no hace `FadeOutPalettes`) y
`refresh_actors()` vuelve a decidir qué NPCs están fuera. El reloj corre **también** con
el textbox o el menú abiertos, porque en el original el RTC no se para al abrir un
menú. La hora actual se registra una vez por fase (`tod: eve`) para que una captura
diga a qué hora se tomó.

Los NPCs con horario son el otro lado del día/noche: cada `object_event` lleva una
máscara de cuatro bits (un bit por fase, `-1` = siempre; `home/map_objects.asm
CheckObjectTime`) y `ow_clock_object_out()` decide si sale. Un NPC fuera de hora no se
dibuja, no bloquea su celda y no contesta a A; su índice en la tabla no cambia, así que
los guiones siguen apuntando al objeto correcto. En este build hay seis NPCs con horario
(el colegial de New Bark Town sale `MORN|DAY`, y en Ciudad Cerezo hay otros con 1, 2, 4,
8 y 12) y el resto sale siempre.

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

## Datos de Pokémon: especies y movimientos

`tools/extract.py` emite dos cabeceras más, con **los ids del original**, para que un
número del asm (un `MON_*`, un `move`) siga significando lo mismo aquí:

| Cabecera | Qué contiene |
|---|---|
| `gen/pokemon.h` | `pc_species[292]`: nombre (offset + longitud en un blob de charmap), los dos tipos, las seis stats base en el orden de `wBaseStats` (PS, ataque, defensa, velocidad, at. esp., def. esp.), catch rate, exp base y curva de crecimiento. También los enums de tipos (`PC_TYPE_*`, 19 valores) y de curvas (`PC_GROWTH_*`), y `pc_growth_rates[4]` con los coeficientes `a/b·n³ + c·n² + d·n − e` de `data/growth_rates.asm`. 40 KB |
| `gen/moves.h` | `pc_moves[256]`: nombre, tipo, categoría (física/especial/estado), potencia, precisión (`-1` = no falla), PP, el número de `EFFECT_*` para BAT-03 y la probabilidad del efecto; y `pc_move_priorities` de `data/moves/priorities.asm`. Id 0 = `NO_MOVE`, 1..255 los movimientos. 33 KB |

Tres reglas del original que el extractor respeta y que no se pueden adivinar leyendo el
`.asm` por encima:

- **`data/pokemon/base_stats.asm` es posicional, no una lista de constantes.** Son 337
  `INCLUDE` de ficheros `base_stats/<stem>.asm` (uno por forma) sin id dentro. La especie
  sale del prefijo más largo que case con una constante —`rattata_plain` → `RATTATA`,
  `nidoran_f` → `NIDORAN_F`— y **gana la primera entrada**: la forma por defecto. Las 47
  formas restantes (Alolan, Galarian, Megas, …) se saltan y quedan para cuando haya un
  campo de forma en `Mon`.
- **La tabla de movimientos es una `move` por línea con los argumentos en otro orden que
  los campos**: anim, efecto, potencia, tipo, precisión, PP, probabilidad y categoría. Se
  guardan reordenados; el número de animación no se usa todavía.
- **Los nombres son bytes de charmap**, no ASCII: `B` es `$81` y `a` es `$b4`. Se leen con
  el mismo `charmap` que los textos, así que `mon_name` puede pasarlos a `text/font` sin
  traducción.

`mon/level` evalúa las cuatro curvas con la fórmula de `CalcExpAtLevel` —el término
cúbico se divide **antes** de sumar el cuadrático, y `c` es negativo en Medium Slow— y
`mon_add_exp` reproduce `GiveExperiencePoints` + `CalcLevel`: un solo premio puede subir
varios niveles (devuelve cuántos, para anunciarlos y enseñar sus movimientos), la
experiencia se para en el total del nivel 100 y el PS actual sube con el máximo por la
diferencia de `UpdatePkmnStats`, sin resucitar a un mon desmayado. `mon_init` deja al mon
con la experiencia de su nivel, como uno salvaje.

`mon/mon` y `mon/party` son las estructuras que consumen esos datos (ver
[05](05-dev-workflow.md) para el tamaño de cada una): un `Mon` no guarda stats base ni
tipos, igual que el original —los pide a la tabla por su especie—, y **no guarda el DV de
PS**: lo deriva de los otros cuatro con la regla de Gen 2, así que no puede tener una
combinación que el juego no produciría. Todavía no los usa `game.c`, así que su código
se compila pero el enlazador lo descarta (`--gc-sections`): el `.mote` no crece.

## Batalla: tipos, daño y turnos

Tres módulos puros que no saben nada de Mote ni de la pantalla, para que la batalla se
pueda probar sin emulador y la UI (BAT-07) sea la única que dibuja:

| Módulo | Qué resuelve |
|---|---|
| `battle/type` | Escanea `pc_type_matchups` como `_CheckTypeMatchup`: cada fila que case con el tipo del movimiento y con uno de los dos del defensor dobla o parte el valor Q4 que empieza en 16, y un 0 lo corta. Tierra contra un Volador es 0 aunque no haya fila (la contesta `CheckAirborne`, que aquí es sólo el tipo). |
| `battle/damage` | `DamagePass1..4` en su orden —`(2·nivel/5 + 2)`, por potencia, por ataque, entre defensa, entre 50, más 2— y luego el modificador Q4 (tipo y STAB juntos) y la variación de 85-100 %. Un golpe no inmune nunca baja de 1; una inmunidad es 0. |
| `battle/turn` | Un turno completo: prioridad del movimiento, luego velocidad, luego RNG en el empate; PP descontado, un ataque por bando, el que cae no responde, y al ganar la experiencia del KO (`exp_base · nivel / 7`) entra por `mon_add_exp`, que devuelve los niveles ganados. |

El estado de la batalla es un `Battle` con **punteros** a los dos mons, no copias: los
puntos de golpe, el PP y la experiencia quedan en el equipo del jugador y en el salvaje
que los llamó. El RNG (xorshift) vive en la estructura, así que una semilla fija da el
mismo turno en un test o en una repetición.

Lo que BAT-01 **no** hace todavía, y por eso no se finge: precisión y críticos (BAT-02),
efectos de movimiento (BAT-03, donde vive la potencia calculada de `power = 1`), estados
y cambios de stats (BAT-04), captura (BAT-05) y la pantalla de batalla (BAT-07). Un
movimiento de estado gasta su PP y no hace nada más. Nada de esto lo usa aún `game.c`.

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

## Scripts

`script/vm` interpreta el bytecode que emite `tools/extract.py` (`gen/scripts.h`), que es
el del propio juego ensamblado desde sus macros. No hay traducción de scripts a C: un
`showtext Text_WaitPlayer` del `.asm` es un `$bc` con el índice de ese texto, el motor
hace lo mismo que `Script_showtext`, y un macro que toma código como argumento
(`giveitem ITEM, iffalse_endtext`) produce dos comandos seguidos, no uno. Los ocho mapas
del arranque suman 2 767 B de bytecode (2 508 de código y 259 de bloques de movimiento),
79 etiquetas de script, 32 scripts de objeto, 40 celdas `bg_event` (39 con script),
18 escenas en 5 mapas y 23 `coord_event`.

Tres decisiones de forma:

- **La VM no toca el mundo.** `scr_vm_next` corre hasta que el script necesita algo
  (imprimir, girar, sonar, esperar) y devuelve un `ScrOut` con la clase de trabajo y sus
  argumentos; el llamante lo hace y vuelve a llamar. Así la VM se prueba sin pantalla y
  `game.c` sigue siendo el único que sabe qué es un textbox. Es también lo que hace que un
  `showtext` no pueda bloquear al intérprete: la espera la decide el que dibuja.
- **Nada de punteros del juego.** Los saltos son offsets dentro del blob del mapa, los
  textos son índices de `pc_texts` y los objetos son el número del `object_event`. Una
  referencia que el extractor no pudo resolver llega como `$FFFF`/`$FF` y se ve en la
  salida de `tools/extract.py`, en vez de convertirse en un salto a la nada.
- **La pila tiene 5 huecos**, como `wScriptStackSize`; la sexta llamada da la vuelta al
  índice en vez de corromper memoria (el original se come el byte alto de la entrada más
  antigua — el fallo está documentado en `docs/03` del propio juego).

Lo que hay: control de flujo (`sjump`, `scall`, `ifequal`, `iftrue`, `iffalse`, `end`,
`endcallback`), flags y eventos (`checkflag`, `setflag`, `clearflag`, `checkevent`,
`setevent`, `clearevent`), texto (`showtext`, `showtextfaceplayer`, `opentext`,
`closetext`, `writetext`, `waitbutton`, `promptbutton`, `endtext`, `waitendtext` y las
familias `jumptext*`/`if*_jumptext*`/`jumpopenedtext`/`if*_jumpopenedtext`), mundo
(`turnobject`, `faceplayer`, `disappear`, `appear`, `playsound`, `pause`,
`setlasttalked`, `moveobject`, `reloadmap`, y `reanchormap` como no-op porque el mapa es
una tabla estática), movimiento (`applymovement`, `applymovementlasttalked`,
`applyonemovement`, `follow`, `stopfollow`), escenas (`checkscene`, `setscene`,
`checkmapscene`, `setmapscene`), `sdefer` y `waitsfx` como no-op.

Lo que falta, y se dice en voz alta: **un opcode que la VM no conoce no se interpreta a
medias**. Devuelve `SCR_EV_UNSUPPORTED` con su número y la VM se marca muerta, porque sus
argumentos tienen longitudes distintas por comando y seguir leyendo sería interpretar
datos como código. Batalla (`loadtrainer`, `startbattle`, `winlosstext`), `special`,
`jumpstd`, `readvar`/`loadvar`, `yesorno`, `showemote` y las entrega de Pokémon u
objetos esperan a los sistemas que los usan.

### Hablar, leer un cartel: los dos acaban en la VM

A en el overworld arranca el script de la celda de enfrente, en el orden del original
(`CheckAPressOW`): primero el `object_event` que hay ahí y, si no hay, el `bg_event`
—cartel, estantería, la nevera—. El primero se busca recorriendo los actores; el segundo
lo resuelve `script/trigger` (`pc_bg_event`), que además exige el lado correcto en las
celdas `BGEVENT_UP/DOWN/RIGHT/LEFT`, contesta que no cuando no hay nada que leer y, en una
celda de objeto escondido, comprueba el flag antes: un objeto ya cogido deja de estar ahí,
como en `TryBGEvent`.

Las tres formas de ejecutar un `bg_event` son tres formas de rellenar el blob, no tres
motores: un cartel es `jumptext <texto>`, una estantería es **el std script copiado
entero** (`DifficultBookshelfScript` → `jumptext <texto de std_text>`) y un objeto
escondido es `opentext` + `jumptext <texto de _FoundItemText con el nombre dentro>`. Por
eso `game.c` solo distingue un caso especial: si el `bg_event` esconde un objeto, primero
lo mete en la bolsa (`bag.c`, ids del original) y marca el flag; si la bolsa está llena no
hace nada y la celda sigue ahí — es la rama `pocketisfull` del original, sin su texto.

No hay una capa intermedia que traduzca el script a acciones: el despachador de `game.c`
hace lo que el evento pide y vuelve a llamar,

| Evento | Qué hace el port |
|---|---|
| `SCR_EV_TEXT` | abre el textbox con ese índice y deja **esperando** a la VM |
| `SCR_EV_TEXT_NOWAIT` | abre y escribe el texto y **sigue**: es `writetext`, el guion continúa con la caja puesta |
| `SCR_EV_TEXT_WAIT` | el guion espera a que el jugador termine el texto (`waitbutton`/`promptbutton`) |
| `SCR_EV_TEXT_END` | cierra la caja y el guion acaba ahí (`endtext`) |
| `SCR_EV_PLACE` | coloca un objeto en otra celda sin andar (`moveobject`) |
| `SCR_EV_RELOADMAP` | recarga el mapa bajo el jugador y para el guion (`reloadmap`) |
| `SCR_EV_FACE` | gira el actor (o lo calcula, si es "hacia el jugador") |
| `SCR_EV_TEXT_OPEN`/`_CLOSE` | nada: el port abre una caja por texto |
| `SCR_EV_SOUND`, `_DELAY` | nada todavía (audio es TXT-10) |
| `SCR_EV_SPRITE` | nada: `appear`/`disappear` no se dibujan aún (SCR-06) |
| `SCR_RUN_END` | el script acabó |
| `SCR_EV_UNSUPPORTED`, `SCR_RUN_ERROR` | **vuelta al texto plano del objeto** |

Las dos capas acaban en la misma VM y en el mismo despachador: `try_talk` sólo cambia
qué offset le pasa a `scr_vm_start`.

Esa última fila es la decisión que importa: un comando que el port no ejecuta no deja al
NPC mudo a media frase. El script se detiene, se anota el opcode en el log (`script:
stopped at 0x1e`) y se muestra el texto que `first_text_ref` extrajo para ese objeto, que
es el comportamiento que tenía el port antes de tener VM. Medido, tras portar el texto
continuo y `moveobject` (0.17.0): de los 32 objetos con script, **25 llegan a un texto** y 7
se paran en el primero (`readvar`, `special` o batalla); de los 25, **veinte coinciden** con
el texto plano y cinco no, porque su primer texto depende de un evento (la maestra de New
Bark Town elige entre tres).

Los flags y eventos están en **un solo array** de `PC_SCRIPT_FLAG_BYTES` (222 B en este
build, el índice más alto que tocan los scripts), porque la VM recibe un puntero y un
tamaño. En el original son dos arrays (`wEngineFlags` y `wEventFlags`); compartirlo es
aceptable mientras ningún script mezcle `FLAG_x` con `EVENT_x` del mismo número, y está
anotado en `TASKS.md` como deuda.

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

## Escenas y `coord_event`: lo que dispara al andar

`script/scene.c` es lógica pura: no conoce Mote ni el mapa cargado.

```c
int  pc_scene_active(const ScrScenes *s)          // la escena del mapa, o -1
void pc_scene_set(ScrScenes *s, int scene)        // setscene
int  pc_scene_slot(const ScrScenes *s, g, m)      // map_id -> hueco, o -1
int  pc_scene_at(const ScrScenes *s, int slot)    // la escena de otro mapa
int  pc_coord_event(sm, x, y, scene, &script)     // ¿dispara esta celda?
```

El byte de escena es uno por mapa y lo guarda `game.c` en un array (el original lo tiene
en WRAM, `data/maps/scenes.asm`); a `ScrScenes` se le pasa **prestado**, con el hueco del
mapa cargado y el tamaño de su tabla. Un mapa sin tabla de escenas (`PlayersNeighborsHouse`,
`LyrasHouse1F`, `ElmsHouse`) contesta `-1` y no deja escribir: `setscene` ahí no inventa
estado.

El disparo va **el último del paso**, después de las puertas y de las conexiones, y sólo
si el paso acaba de terminar (`player.just_arrived`), no hay fundido en marcha y no hay
script corriendo. Es el orden del original: `CheckCurrentMapCoordEvents` se llama desde
`DoPlayerEvent`, que es el último frame del paso. Una puerta en esa misma celda gana,
porque la pantalla ya se está fundiendo.

Verificado andando: la maestra de New Bark Town para al jugador en (1,9) con
`Text_WaitPlayer` en pantalla (`artifacts/scene_teacher.png`), y desde 0.16.0 la escena
sigue: corre hacia el jugador, habla, lo arrastra cuatro celdas y él la sigue (ver
movimiento, abajo).

## Movimiento de NPCs: `applymovement`, `follow` y el paseo

`ow/move.c` es puro y **no toca píxeles**: el intérprete recorre los bytes del bloque y
dice qué toca hacer en cada frame (`OwMoveOut`: STEP, TURN, WAIT, SHOW, HIDE, END,
UNSUPPORTED). `game.c` es quien aplica la colisión, arranca el paso del actor y avanza el
frame: así el mismo intérprete sirve para un NPC y para el jugador, y se puede probar sin
mapa, sin Mote y sin framebuffer.

```c
void ow_move_start(OwMove *m, const uint8_t *code, int actor)  // applymovement
int  ow_move_running(const OwMove *m)
OwMoveOut ow_move_next(OwMove *m, OwActor *a)                  // un frame
void ow_move_stop(OwMove *m)
```

- **Un paso no se termina hasta que el actor llega.** El intérprete no devuelve el
  siguiente comando mientras `ow_actor_walking(a)`: es el `step_end` del original.
- **Un solo movimiento scriptado a la vez** por actor, y mientras dura el script el pad no
  anda (el mundo queda congelado como con un textbox). El paso del jugador sale del mismo
  intérprete con `MOVER_PLAYER`, no de una copia aparte.
- Velocidades `OW_STEP_FAST/NORMAL/SLOW` = 8/16/32 frames por celda; el original las elige
  con `step_sleep` y con las variantes `slow_*`/`fast_*` de cada paso.
- **`follow`/`stopfollow`**: el seguidor entra en la celda que el líder acaba de dejar, en
  el frame en que el líder termina su paso (`follow_arrivals`). El seguidor puede ser otro
  NPC o el jugador, que es como la maestra lo devuelve a la carretera.
- **Paseo autónomo**: `ow_wander_next` implementa `RandomWalkContinue` — una dirección
  aleatoria por paso (arriba/abajo o izquierda/derecha según el patrón), una espera de
  16-79 frames armada en cada decisión, y si la celda está ocupada no anda y lo vuelve a
  intentar cuando toque. La capa de celdas ocupadas (`mark_actors`) se rehace **en cada
  llegada**, no sólo al arrancar el paso: si no, el NPC bloquea su celda de origen para
  siempre y se queda clavado tras el primer paso.

Verificado en el emulador con la receta de `docs/05`: la cutscene de la maestra corre
entera — texto, cuatro pasos hacia el jugador, segundo texto, cuatro pasos de vuelta con
el jugador siguiéndola, tercer texto — y el log termina en `script: stopped at 0x0f`, que
es `special RestartMapMusic`, lo último del script (`artifacts/scr06.png`). Los NPCs con
patrón se mueven solos en New Bark Town y Ciudad Cerezo.

## Cutscenes: texto continuo, `moveobject` y `sdefer`

Una cutscene del original es una sola lista de comandos: gira, mueve, imprime, espera,
vuelve a mover. El port ya tenía el movimiento; lo que faltaba era **todo lo demás que
ocurre entre paso y paso**, y son cuatro cosas:

- **`writetext` no espera.** `showtext` abre la caja y deja la VM parada hasta que el
  jugador la cierra; `writetext` (134 usos en los scripts del build) abre, escribe y
  **sigue**, porque el original imprime el texto entero dentro de la misma llamada. El
  guion continúa con la caja en pantalla y quien quiera esperar lo pide con
  `waitbutton`/`promptbutton`. Es la diferencia entre un NPC que dice una frase y se para
  y uno que dice una frase y sigue moviéndose.
- **Esperar es "que se cierre la caja".** La caja del port pagina sola: cada A revela la
  página o pasa a la siguiente, y el último A la cierra. Por eso `waitbutton` se implementa
  como "espera a que la caja se cierre", que es la misma señal que ya usaba `showtext`.
  Diferencias con el original, dichas: `promptbutton` no deja la caja abierta, y un
  `waitbutton` sin caja abierta se da por cumplido en vez de pedir un botón. Todo guion
  real llega al `waitbutton` con algo escrito delante.
- **`moveobject` no anda: aparece en la celda.** `moveobject person, x, y` coloca al
  objeto en una celda (x, y en tiles del mapa, sin el borde de 4 que suma el original); el
  port lo hace con `ow_actor_init` y rehace la capa de celdas. Es cómo el rival aparece al
  lado del jugador antes de que empiece la conversación.
- **`sdefer` encola un guion.** No ejecuta nada: deja el offset en la VM
  (`scr_vm_deferred`) y el despachador lo arranca cuando el actual termina, con un tope de
  cuatro niveles para que un guion que se encola a sí mismo no cuelgue. `reloadmap` es lo
  contrario: recarga el mapa bajo el jugador y **corta** el guion.

`reanchormap` se consume sin efecto: en el GB apunta la ventana de banco a los datos del
mapa, y aquí el mapa es una tabla estática en XIP. Igual que `waitsfx`, es un no-op
honesto, no un comando olvidado.

La prueba que se usa es la de la maestra (movimiento) y Elm: su script empieza con
`checkevent` + `iftrue_jumpopenedtext`, y desde 0.17.0 su primer texto sale del bytecode en
vez del texto plano de reserva — el log ya no escribe `script: stopped at 0xb6`.

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
| Flash (8 mapas + 3 tilesets + textos + fuente + menús + scripts + movimiento) | ver `.mote`: **83 776 B** de código+datos |
| RAM de módulo (`.bss`) | **3 812 B** (capa de celdas de NPC + estado de texto/menús + el warp + 222 B de flags/eventos + la bolsa, 20 huecos + los 8 bytes de escena y su descriptor + el intérprete de movimiento, 2 actores × (`OwMove` + `OwWander` + seguidor) + el `sdefer` pendiente + el reloj y la máscara de horario de los actores) |
| Arena | ~34 KB (11 hojas de sprite + jugador expandidas a RGB565) |
| Raster host (`perf()`), 128×128 | **~20 µs/frame** (`scripts/perf.sh 200`) |
| Fundido de puerta, 16 384 px | **61 µs** en host (host 3 GHz; en device sería ~0,7 ms por frame durante 20 frames, estimación por reloj, sin medir) |

El `.mote` de 83 776 B incluye los ocho mapas (New Bark Town, Ruta 29, Ciudad Cerezo y
los cinco interiores), sus tilesets —`johto_traditional` más `lab` y `johto_house`, que
son casi todo el crecimiento desde los 28 512 B de la 0.7.0 (los comandos de texto y
las puertas añaden menos de 1 KB más)—, los diálogos, la fuente, el menú y desde 0.16.0
los **56 bloques de movimiento**. De los 4 500 B que crece el módulo desde 0.15.0
(77 668 → 82 196), 259 B son bytes de movimiento dentro de los blobs; el resto es el
intérprete (`ow/move.c` mide 512 B de `.text`), lo que se añade a `ow/actor.c`, el glue de
`game.c` y las tablas de etiquetas `Movement_*`. La 0.17.0 añade 556 B más
(82 196 → 82 752, `script/vm.c` mide 2 968 B de `.text`): el texto continuo, `moveobject`,
`reloadmap` y el `sdefer`, que no llevan datos nuevos —todo son opcodes ya presentes en
los blobs— sino código de la VM y del despachador. La 0.18.0 añade 1 024 B más
(82 752 → 83 776): tres tablas de paleta BG completas (192 B de datos nuevos, porque el
grupo `day` ya estaba), los ocho bytes nuevos por objeto (`time_mask`) y `ow/clock.c`,
que mide 268 B de `.text` para llevar la hora, la fase, el grupo de paleta y la máscara
horaria. La 0.19.0 **no añade flash**: las tablas de especies y movimientos (72 KB de
cabecera) y `mon/mon` (`9 402 B` de `.text` en su objeto) existen y compilan, pero no
los llama nadie, así que el enlazador los tira; el `.bss` sube 4 B. La 0.20.0
(experiencia y niveles: `mon/level` más `mon_add_exp`) y la 0.21.0 (la batalla:
`battle/type`, `battle/damage` y `battle/turn`) tampoco mueven el `.mote` ni el `.bss`,
por el mismo motivo. El presupuesto de flash no es
el problema en esta fase: lo será la extracción en lote (458 mapas, 53 tilesets), que es
CON-01/CON-02.

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
