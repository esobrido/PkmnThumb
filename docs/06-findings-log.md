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

## Trampas nuevas (fase 1: el giro)

- **Girar no es asignar la cara.** Lo natural es `p->dir = input` y andar; el original
  intercala un movimiento entero (`StepFunction_Turn`) con dos mitades de `GetTurningSpeed()`
  frames y, mientras dura, *no* deja andar. Sin ese estado, tocar una dirección para mirar
  algo te lleva media celda, y el jugador no puede asomarse a un cartel sin moverse.
- **Un paso empezado no se puede redirigir.** La versión anterior aceptaba la nueva
  dirección a mitad de celda y seguía avanzando por el eje nuevo: el jugador se salía de la
  rejilla de 16 px y el siguiente `try_step` se calculaba desde una posición a medias. Ahora,
  como en el original, la dirección sólo cuenta con el jugador parado en una celda.
- **MOTE_KEYS cuenta lecturas de botones, no frames presentados.** El lanzador de Mote lee
  botones antes de que el módulo empiece a dibujar, así que un `left:20-25` se ejecuta unas
  cuatro o cinco "frames" antes de lo que sugiere el número: intentando capturar el giro con
  `MOTE_SHOT_FRAME=22` ya había terminado. Para ver algo de pocos frames, graba la secuencia
  (`MOTE_REC`/`MOTE_REC_N`) y elige los fotogramas después, en vez de adivinar el número.
- **El giro no se ve en una captura suelta.** Con el mapa quieto y la cara cambiada, la
  prueba es la secuencia: en `artifacts/turn.png` los fotogramas 19 y 22 tienen el mismo mapa
  y distinta cara, y el 40 ya está andando.

## Trampas nuevas (fase 1: el empuje)

- **La animación de empuje no es un sprite nuevo: es el mismo código que el paso.**
  `BumpStep` pone `OBJECT_ACTION_BUMP`, y en `ObjectActionPairPointers` esa entrada cae por
  fallthrough en `SetFacingStepAction`: las dos llaman a `_GetNextStepFrame`, que incrementa
  `OBJECT_STEP_FRAME` igual. O sea que empujar una pared mueve las piernas con los frames de
  andar; no hay "frame de empuje" que buscar.
- **El empuje no es un paso de 0 píxeles.** Programado así queda un `step_left` pendiente que
  al soltar el botón deja al jugador con la pata levantada, y peor: retiene la entrada como
  un paso de verdad (el paso es irrevocable). Tiene que ser un estado sin compromiso:
  se enciende cada frame con la dirección bloqueada y se apaga al soltar.
- **Correr no acelera el empuje.** El paso avanza píxeles; el contador de frame del GB
  avanza frames. En el empuje no hay píxeles que duplicar, así que se suma `adv_px` sin el
  ×2 de las zapatillas: 8 frames por pose aunque llegues corriendo.
- **Suponer qué dirección está bloqueada sale caro.** Di por hecho que al este del spawn
  había muro y la grabación mostraba al jugador andando. Hubo que meter un log temporal de
  `px/py/dir/step/bump` y probar las cuatro direcciones (`up` choca de inmediato, `down`
  anda una celda y luego choca, `left`/`right` andan cuatro). El log se quitó después.
- **El diff de dos frames dice si hubo movimiento.** Que cambien 74 píxeles en la caja del
  sprite no prueba nada (son las piernas); lo que prueba que el jugador no se movió es que
  *fuera* de esa caja no cambie nada y que 16 frames después la imagen sea idéntica.

## Trampas nuevas (fase 1: las zapatillas)

- **El ajuste de zapatillas no es un interruptor, es un XOR.** `Running Shoes: On` no
  significa "B corre": significa *se corre siempre y B anda*. La rutina original
  (`.RunCheck`) devuelve "corre" cuando el bit del ajuste y el botón B **no coinciden**, así
  que el camino fácil —`if (opción) correr`— invierte el botón B en silencio cuando el
  jugador lo activa. Es una línea en `options_run` y una tabla de verdad en su test.
- **Correr no necesita animación nueva.** La tentación es añadir un estado "corriendo" con
  su propio ciclo de piernas. No hace falta: `StepVectors` no cambia la animación, sólo los
  píxeles por frame (1×16 → 2×8), y la hoja de sprites avanza cada 8 px *andados*. Al
  duplicar los píxeles, el ciclo se duplica solo, que es exactamente lo que hace
  `OBJECT_ACTION_RUN` con dos `rrca` en vez de tres.
- **Las capturas no sirven para medir distancias.** Con `MOTE_DT_MS=16` el glue acumula
  0.96 px por frame, así que 40 frames dan 38 px sobre el papel pero 42 px en el build, y
  la última celda se redondea al llegar a ella. Para números, el escenario (`run right 32`
  → 4 celdas) o un log temporal; para la vista, la captura.
- **La cámara clampea en el borde del mapa**, así que una captura del jugador andando al
  oeste deja de mostrar scroll a partir de cierto punto: la diferencia entre andar y correr
  se mide mejor en el log que en dos PNG alineados.

## Trampas nuevas (fase 1: la pantalla de Option)

- **Los dos glyphs de flecha estaban corridos un byte.** `constants/charmap.asm` dice
  `↑ = $f2`, `↓ = $f3`; `ui/list` dibujaba `$f3`/`$f4`, que son `↓` y `′`. Nadie lo vio
  porque el menú de cuatro entradas nunca scrollea y el test sólo pedía "hay tinta ahí".
  La lección no es el byte: es que **asserte "aparece tinta" no distingue un glyph de otro**;
  ahora el test compara la celda 8×8 píxel a píxel con `font_glyph()`.
- **Con 16 tiles no caben un ▶ y "Text Autoscroll" a la vez.** El nombre más largo mide
  120 px y el ancho interior de la caja es 126: sin cursor entran los 15 glyphs, con cursor
  sólo 14. Por eso la pantalla de Option marca la fila elegida con una barra en negativo en
  vez de con el ▶ que usan las listas de una línea.
- **Los valores del original vienen rellenados a una columna fija** (`"Slow   @"`,
  `"On @"`). Copiarlos tal cual deja tres espacios colgando en nuestra segunda línea; el
  extractor les quita el relleno y lo dice en el comentario del header generado.
- **En `option_names.asm` la cadena está en la línea siguiente a la etiqueta**
  (`.TextSpeed:` / `db "Text Speed@"`), mientras que en `start_menu.asm` van en la misma.
  Un extractor que sólo mira la línea de la etiqueta encuentra cero nombres.
- **El orden del menú es el de la tabla `dw`, no el de las cadenas en el fichero.**
  `OptionNames` lista los `dw .Label` en orden de menú y las cadenas vienen detrás; leer el
  fichero de arriba abajo da un orden que parece correcto hasta que se cruza un label.
- **Frame no es una cadena.** `Options_Frame` guarda `wTextboxFrame` y lo imprime como
  `valor + 1` con `PrintNum`: no hay tabla de "01".."20" que extraer, y buscarla no
  encuentra nada.

## Trampas nuevas (fase 1: menús)

- **El terminador de cadena va detrás de la comilla**: `db "#dex@"`, no `"#dex"@`. Un
  `endswith('"@')` en el extractor no encuentra ni una etiqueta y el menú se queda sin
  textos; el mensaje de error lo dice, pero se pierde si se mira sólo `tail -2` de un
  pipeline (el traceback de Python no está en el mismo orden que stdout).
- **Las etiquetas del menú no están en `data/text`.** Son `db "..."@` inline en
  `engine/menus/start_menu.asm`, porque la caja las imprime directamente. Buscarlas en
  `data/text/*.asm` da cero resultados y hace pensar que el menú usa otra fuente.
- **Una lista dibuja lo que le digan y nada más:** `ui_list_draw` indexa
  `items[item]` hasta `count`, así que pasar 4 etiquetas para una lista de 10 es un
  segfault, no un hueco. El test lo provocó y el contrato quedó escrito en el header
  (`items` debe tener al menos `count` entradas).
- **Reabrir el menú olvida el cursor si se guarda «sólo mientras está abierto».** El
  juego recuerda la selección entre aperturas; hay que leer el cursor del propio struct,
  no del flag `open` (que ya está a 0 al confirmar).
- **Un menú de 10 tiles de ancho en una pantalla de 16 no cabe a la derecha si además
  lleva padding**: 80 px con 4 px de aire por lado dejan 64 px útiles, que es justo lo
  que necesita "POKédex" (7 glifos = 56 px) más el ▶. Con 72 px se cortaba la última
  letra contra el borde.

## Trampas nuevas (fase 1: texto)

- **El charmap no es ASCII.** 'Y' es `0x98`, no `0x59`; `0x59` es `<PARA>`. Un test que
escriba `'Y'` en un stream de texto abre una página en vez de pintar una letra. Todo
byte de texto sale de `font_code_from_ascii` o del extractor, nunca de un literal de C.
- **El texto del mapa es bytecode desde el principio.** `text` emite `<START>`, `line`
`<LINE>`… así que el extractor no parte de frases sino de bytes ya resueltos por el
ensamblador. Se ahorra reimplementar el compresor Huffman; 13 textos son 6 KB.
- **Los NPCs no bloquean por sí solos.** La colisión vive en los metatiles; un actor no
está ahí. Sin la capa `cells` el jugador **atraviesa** al NPC y acaba en su celda, con
lo que `A` nunca encuentra a nadie delante. Se descubrió con una captura en la que el
jugador aparecía encima del chaval.
- **`object_event` puede llevar el texto inline** después de `jumpthistextfaceplayer`,
sin etiqueta. Sin soportarlo, la profesora de New Bark Town y medio pueblo se quedan
mudos aunque su texto exista en el fichero.
- **El reflow debe ignorar `<LINE>`.** El original corta a 20 columnas; si se respetan sus
saltos con 15 columnas salen líneas a medias. Sólo `<PARA>`/`<NEXT>` son página nueva.
- **Blink del ▼ sin reiniciar:** `ticks` no se ponía a 0 al abrir la caja, así que el
parpadeo heredaba la fase del diálogo anterior (la flecha no salía en la captura).
- **`MOTE_DT_MS=16` para capturas posicionales:** a ~500 fps el emulador avanza ~0.12 px
por fotograma y una secuencia de 80 frames recorre una celda, no cinco.

## Trampas nuevas (fase 0: varios mapas, actores, medición)

- **Animar NPCs con los píxeles andados, no con frames.** `ow_actor_update` recibe un
  número de actualizaciones. Pasarle el avance en píxeles del jugador (`adv`) parece
  razonable porque coinciden cuando andas a 1 px/frame… pero al quedarte quieto vale 0 y
  los NPCs se congelan. Se detectó comparando capturas en frames 45/46/48/51: el sprite
  era idéntico. Los actores animan **1 por frame**; el jugador sigue avanzando en píxeles.
- **El giro de los `SPINRANDOM_*` es rápido**: `OBJECT_STEP_FRAME` cuenta 0,1,2 y al
  pasar de 2 rota una cara, o sea 20 vueltas por segundo. No es "gira despacio": el
  nombre se refiere a que elige cara al azar, no a la velocidad.
- **`WATER` es caminable** en la tabla de colisión. Buscar "la celda libre más cercana"
  para un spawn puede dejar al jugador **en el mar** (Ciudad Cerezo tiene una bahía que
  ocupa media pantalla y es bloque `53`, ranura de paleta `water`). Los spawns se eligen
  sobre camino/bloque de suelo, no sobre "no bloqueado".
- **`object_event` trae la cara en el tipo de movimiento** (`SPRITEMOVEDATA_STANDING_*`).
  Dibujar todos los NPCs mirando al sur (frame 0) desperdicia el dato: el rival y Lyra
  están `STANDING_RIGHT` en su propio mapa.
- **La toolchain ARM se instala sin root** (tarball oficial a `toolsrc/`, 141 MB). El
  `.mote` de tres mapas pesa 19 860 B (24 512 B con texto y fuente) y el `.bss` pasa de
  596 B a 2 840 B; el build de device
  pasa `-Wstack-usage=2048` sin avisos.
- **`perf()` mide en el host también** (`raster_us` ≈ 19 µs), pero es un dato del host:
  no dice nada de 240 MHz con XIP. Sirve para comparar versiones entre sí, no para dar
  por buenos 60 fps en hardware.

## Trampas nuevas (fase 1: las puertas)

- **La colisión de un metatile NO estaba girada 180°.** `tilecoll a,b,c,d` guarda los
  cuatro cuadrantes en orden **TL, TR, BL, BR**: `GetCoordTileCollision` (`home/map.asm`)
  indexa `(x & 1) + 2 * (y & 1)` sobre esos cuatro bytes. El extractor los remapeaba con
  `order = (1, 3, 0, 2)` creyendo que venían como `BL, TL, BR, TR`, así que **cada
  metatile tenía la colisión girada 180°**. Se notó al implementar las puertas: las 8
  celdas de warp de New Bark Town se leían como `WALL` (0 de 8 daban `DOOR`), y por eso
  el jugador no podía pisarlas. Con el orden identidad, las 8 dan `DOOR` o
  `WARP_CARPET_*` exactos. El giro también afectaba a la hierba alta y al agua: lo que
  se veía caminable a veces no lo era, y al revés.
- **No hay animación de tile de puerta en Polished Crystal.** `MAPSETUP_DOOR`
  (`data/maps/setup_scripts.asm`) es `FadeOutPalettes` + `warpsound` + carga + 
  `FadeInPalettes`, y los dos fundidos llevan `c = 10` (`timeofday_pals.asm`). Buscar un
  tile animado es un callejón: la puerta *es* el fundido, más el paso al umbral.
- **Puerta y alfombra disparan en momentos distintos del frame.**
  `CheckTileEvent` corre al **terminar** un paso (`player_step.asm`) y es donde
  `CheckWarpTile` mira el tile bajo los pies; `.CheckWarp` va **después de `TryStep`** y
  sólo cuando el paso pedido no pudo empezar. De ahí que la estera de salida de una casa
  se active empujando hacia abajo y no al llegar. Si se igualan, entrar en una casa te
  saca de ella en el mismo frame.
- **Aterrizar no es andar.** Al entrar aterrizas justo en la estera de salida. El port
  necesita dos frenos: la estera sólo dispara con el pad apretado *y* el paso bloqueado,
  y además el warp queda desarmado hasta que se suelte el pad. Con uno solo, entrar
  manteniendo la dirección sale rebotado.
- **`COLL_DOOR` empuja una celda**: la puerta está en el listado de `.CheckTile` que
  fuerzan un paso hacia abajo (`player_movement.asm`). Por eso salir de un edificio te
  deja **por delante** de la puerta y no sobre ella. En el port se aplica en el frame
  blanco en vez de animar un paso invisible.
- **`spawn_points.asm` es `x, y`, no `y, x`**, aunque el comentario del macro diga
  `; map, y, x`: el motor lee los dos bytes en `wXCoord` y `wYCoord`, y Cherrygrove con
  `29, 4` sólo cabe así en un mapa de 40×22 celdas. El punto de New Bark Town (15,6) es
  la celda pegada a la puerta de casa, no el spawn del port.
- **`PlayersNeighborsHouse` no tiene `.ablk`**: comparte el de `JohtoHouse4` por
  etiqueta en `data/maps/blocks.asm`. Sin resolver el `INCBIN`, la casa se extrae como
  mapa de ceros.
- **`MOTE_KEYS` cuenta lecturas del pad, no frames presentados**
  (`mote_plat_host.c`: *one frame == one buttons read*). Con `MOTE_DT_MS=16` una ventana
  de 9 lecturas movió 2 celdas (≈40 frames de juego), y la misma ventana dio 0 celdas en
  otra ejecución: la proporción depende de la velocidad del host. Las capturas con
  posición exacta se guían por algo que **absorbe** (pared, borde del mapa o la propia
  puerta), no por contar frames.

## Trampas nuevas (fase 1: los comandos de texto)

- **Un comando de texto que no se entiende no se ignora: arrastra sus argumentos.** Los
  bytes `$00-$09` tienen longitud variable (`<NUM>` 3 bytes, `<SOUND>` 1, `<FAR>` 3), así
  que imprimir el comando «como si fuera una letra» mete además sus argumentos en el
  diálogo. La caja tiene que consumir **todos** los comandos aunque todavía no exista el
  sistema del que dependen; los que no hacen nada llevan escrito por qué.
- **La tabla de comandos se lee de `constants/charmap.asm`, no se copia.** Doce `#define`
  escritos a mano en el generador son doce oportunidades de imprimir basura; ahora
  `text_commands()` los parsea y el extractor falla si no encuentra ninguno.
- **`<WAIT>` no es un salto de página.** Para el impresor a mitad de página, parpadea el
  ▼ y A **sigue imprimiendo en la misma página**; sólo después A pasa a la siguiente. Al
  revés, el texto se parte en dos cajas donde el original no lo hacía.
- **`<PAUSE>` tampoco es un botón.** Es `DelayFrames` de 30 frames
  (`TextCommand_PAUSE`), con A/B como atajo: si se trata como `<WAIT>`, cada `<PAUSE>`
  del juego se convierte en una pulsación que el jugador no esperaba.
- **El relleno de un `<NUM>` es un espacio que separa palabras.** Con el reflow por
  palabras, los espacios que añade el formateo cortaban la palabra y el número salía
  pegado al texto anterior. El impresor distingue ahora un espacio *del flujo* (separa
  palabra) de un espacio *de una expansión* (es un glifo más).
- **`<NUM>` no tiene WRAM que leer.** Tres argumentos son dirección baja, alta y formato;
  sin memoria del juego, el port pregunta al llamante por esa dirección (`TextVars.nums`)
  y una dirección no registrada imprime "?", no un número inventado.
- **`<ASM>` es la mitad insegura.** En el original salta a código encajado en el propio
  flujo de texto (`jp hl`); el port lo trata como fin de cadena, que es la otra rama del
  original, antes que imprimir instrucciones de máquina como letras.

## Trampas nuevas (fase 2: ensamblar los scripts)

- **`const_def -1` existe y el signo importa.** El bloque de direcciones
  (`STANDING $-1$, DOWN $0$, UP $1$, LEFT $2$, RIGHT $3$) empieza en −1; un parser que
  solo acepta positivos arranca el contador donde lo dejó el bloque anterior y `LEFT`
  sale **11** en vez de 2. No falla nada: el NPC gira a un valor que no es una dirección.
  Se vio porque un test comprobaba el argumento de `turnobject`.
- **Un mismo nombre de constante puede vivir en dos espacios.** `LEFT` es dirección de
  caminar (2) y `FACE_LEFT` es dirección de cara (11); mezclarlos compila y no gira nada.
  El diccionario de constantes se queda con la primera definición, así que el orden de
  los ficheros de `constants/` es parte del contrato.
- **`dp` no son dos bytes sueltos.** `macros/data.asm` lo define como
  `db LOW(especie), HIGH(especie) << MON_EXTSPECIES_F | forma`: el segundo byte lleva la
  **forma** en los bits bajos, no el byte alto crudo. `setmonval TOGEKISS` (especie
  `$10f`) emite `$0f $21`, y leerlo como "par de bytes" daría `$0f $01`, que es la especie
  271 con la forma equivocada. La constante se lee del propio fichero fuente, no se
  adivina.
- **`dr` es una distancia relativa y big-endian.** Solo aparece en las variantes `*fwd`
  de los saltos, que existen para ahorrar bytes. El port no las necesita: el extractor
  emite el salto absoluto equivalente, y así hay un solo formato de salto que pueda estar
  mal.
- **Dentro de una etiqueta de script hay datos, no solo código.** Un bloque puede seguir
  con `text "..."` o con pasos de movimiento después de su `end`, y más tarde tener otra
  etiqueta `.local` que sí es código (`.FirstTimeBanking:` detrás de un texto en línea).
  Cortar el bloque en el primer `text` pierde esa segunda mitad; hay que saltar los datos
  hasta la siguiente etiqueta.
- **Los `.local` se referencian hacia adelante.** `iftruefwd .Totodile` precede a
  `.Totodile:` en el mismo bloque, así que una sola pasada marca como externo lo que es
  local. Hacen falta dos: una para los tamaños y las posiciones de las etiquetas, otra
  para los valores.
- **Algunos macros inyectan código como argumento** (`giveitem ITEM, iffalse_endtext`):
  `giveitem` llama a `giveitem_unsafe \1` (los tres bytes del objeto) y después escribe
  `\#`, que en RGBDS no es "lo que sobra en la línea" sino **los argumentos que `shift`
  no ha consumido**. Cada uno de ellos se ensambla como un comando más, en la misma
  posición. Es lo que hace que la tienda de objetos y su fallback (`iffalse_endtext`)
  sean dos comandos seguidos, no un argumento del `giveitem`.
- **`if _NARG == n` dentro de un macro no se puede ignorar.** `givepoke`/`loadwildmon`
  emiten distinto número de bytes según cuántos argumentos reciben; saltarse la condición
  emite las dos ramas y una de ellas referencia un `\3` que no existe.
- **`elif` también lleva argumentos.** `pokepic` y `cry` hacen `if \1 == 0` …
  `elif _NARG == 2` … `else`, y la condición se evalúa **después** de sustituir `\1`:
  comparar el texto `\1` con 0 no da nunca. Un `if` mal evaluado no falla: elige la rama
  equivocada y emite la forma equivocada.
- **El `.asm` del mapa también está lleno de condicionales de compilación.**
  `if DEF(DEBUG)` envuelve una escena entera de Elm y trozos de sus textos (unas 30 líneas
  sólo para el build de debug). Ignorarlos no rompe el ensamblado: mete texto de debug en
  `pc_texts` y desplaza los índices de todos los textos siguientes. El port es una
  compilación normal (`DEBUG=0`, `FAITHFUL=0`), así que el extractor resuelve
  `if`/`elif`/`else`/`endc` y tira la otra rama.
- **Hay tablas de saltos dentro de un script**: `scalltable .Tabla` seguido de
  `dw Script1` / `dw Script2`. Son datos con forma de código: si se saltan "hasta la
  siguiente etiqueta" el `scalltable` acaba apuntando a otra cosa, y si se emiten como
  texto, el comando lee basura. Se emiten como palabras LE con el offset de la etiqueta,
  igual que un `sjump`.

## Trampas nuevas (fase 2: ejecutar los scripts de un NPC)

- **El noveno campo de `object_event` no siempre es una etiqueta.** Con
  `OBJECTTYPE_COMMAND` el campo es *un comando con su argumento*
  (`jumptextfaceplayer, Route29YoungsterText`), y el motor lo ejecuta copiando ese campo a
  `wScriptPointer`: es un script de tres bytes que vive en el propio `object_event`. Tratarlo
  como etiqueta deja a media población de Johto muda.
- **`jumpthisopenedtext` suele imprimir la etiqueta *siguiente*.** El comando imprime "el
  texto donde está el script"; cuando es la última línea del bloque (`MysticWaterGuy`), ese
  puntero cae en el label de texto que viene después, no en un texto inline. Un extractor que
  solo mire dentro del bloque no encuentra nada y —peor— si se conforma con "saltar líneas
  hasta el siguiente label" se come el bloque de texto entero y desplaza todo lo que sigue.
- **`jumptext` no vuelve.** Salta a `{opentext; repeattext; waitendtext}`: el script termina
  con ese texto y los bytes posteriores son datos. Una VM que "continue" después del texto
  intenta ejecutar el texto como código (se ve como `unsupported 0xbc` al cerrar la caja).
  Se arregla con un flag de "esto era lo último", no con `pos = len`, porque `pos == len`
  ya significa "se salió del script" y es un error.
- **Los flags y los eventos son dos arrays en el original** (`wEngineFlags` y
  `wEventFlags`). La VM recibe uno solo, así que `FLAG_x` y `EVENT_x` comparten bit cuando
  el número coincide; el tamaño se calcula desde los scripts (`PC_SCRIPT_FLAG_BYTES`), no a
  ojo, para que ningún índice caiga fuera del array.
- **`MOTE_KEYS` no sirve para posicionar a un NPC.** Con ventanas de 100 lecturas el jugador
  acaba al lado pero no enfrente, y un `a` a destiempo no habla (o habla a otro). Para
  verificar diálogos, cerrar la receta contra algo que absorba (una pared, el borde) o
  comprobarlo con la tabla de objetos de `world.h` en la mano.

- **`bg_event` es lo que hay delante cuando no hay nadie.** El motor prueba
  `object_event` y luego `bg_event`; un cartel, una estantería o la nevera no son tiles de
  colisión ni NPCs, así que sin esa segunda búsqueda A no hacía nada aunque el jugador
  estuviera mirando al cartel.
- **`BGEVENT_JUMPTEXT` no apunta a un script**, apunta a un texto: el motor se fabrica el
  script (`jumptext <puntero>`) y lo ejecuta. Tratarlo como etiqueta de script no compila;
  el port emite esos tres bytes.
- **El cambio de mapa de depuración (RB/LB) no recoloca al jugador.** Conserva la posición
  del mapa anterior, así que una receta que empieza con RB no está en el spawn de destino:
  para posicionarse, entrar por la puerta o mirar la tabla de celdas.
- **`MOTE_SHOT=/dev/null` no ejecuta la receta.** Sin un PNG donde escribir, el host no
  llega al frame pedido y no hay ni logs ni captura; para depurar posiciones, apuntar a un
  fichero real.

## Trampas nuevas (fase 2: estanterías y objetos del suelo)

- **Un `bg_event` que nombra un std script no lleva puntero, lleva un índice.**
  `BGEVENT_JUMPSTD, difficultbookshelf` guarda la entrada de `StdScripts::` (el orden de
  `constants/std_constants.asm`), y el motor salta a esa tabla. La tabla y los `const`
  pueden desincronizarse en silencio y cada celda apuntaría al script de al lado, así que
  el extractor compara los dos y falla si no cuadran. En el port el script se **copia** al
  blob: no hace falta tabla en flash ni un opcode `jumpstd`.
- **`farjumptext` es `jumptext` sin bancos.** `DifficultBookshelfScript` es una línea,
  `farjumptext DifficultBookshelfText`; el `far` es un puntero de 3 bytes a otro banco, que
  aquí no existe. Traducirlo a mano en cada std script no escala: el ensamblador mapea el
  macro al opcode normal.
- **Los textos de los std scripts viven en otro fichero** (`data/text/std_text.asm` y
  `common.asm`), y las etiquetas de `data/text` son globales (`Label::`). `label_blocks`
  solo entendía `Label:`, así que `_FoundItemText` "no existía"; aceptar `::` lo arregla y no
  cambia ningún texto de los mapas (se comprobó con el diff de `gen/text.h`).
- **Un `text` de más no emite otro `<START>`.** En `_FoundItemText` hay dos `text` separados
  por un `text_ram`; `macros/scripts/text.asm` solo manda `$00` en el primer `text` del
  bloque (`_dtxt`), y los siguientes son caracteres. Un extractor que emita `$00` por cada
  `text` mete un byte de control en mitad de la frase.
- **`BGEVENT_ITEM` no está en los `const`**: es `DEF ... EQU NUM_BGEVENTS`, así que el id
  del objeto es `tipo - BGEVENT_ITEM` y el cuarto campo es el **flag**, no el objeto. El
  motor comprueba el flag antes de leer; el port hace lo mismo en `script/trigger` y el
  flag lo pone el juego al meter el objeto en la bolsa.
- **Un objeto escondido puede estar en una celda que no se pisa.** `place_player` recoloca
  al jugador si el punto de spawn está bloqueado (busca la celda libre más cercana), así
  que un spawn temporal en la celda de al lado puede acabar dos celdas más allá y la A cae
  en el vacío. Verificar con la tabla de collision de `world.h` en la mano, no a ojo.
- **La primera A de una receta se pierde mientras la caja está revelándose.** Abrir un
  texto y "cerrarlo" con la siguiente ventana puede ser la misma pulsación; para ver si un
  texto se reabre hace falta una tercera ventana separada ~50 lecturas. Los logs sí salen
  desde `g_update` (los de `g_init` también), así que el oráculo barato es un contador
  temporal en `try_talk` sobre la salida del host.

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

- `tools/extract.py` extrae mapa, metatiles, atributos, colisión, tiles, paletas BG
  (incluida la `indoor`), sprites, objetos, textos y **warps**, y **falla con mensaje
  claro** si los tamaños no cuadran.
- `game/src/game.c` renderiza 8 mapas con paleta por tile y por entorno, mueve al jugador
  en pasos de 16 px con colisión real, panea la cámara, dibuja los NPCs y **cruza
  puertas**: fundido de 10+10 frames, carga del destino y vuelta afuera por la estera.
- Verificado: `scripts/test.sh` (15 programas unitarios y 7 escenarios, todos en verde,
  `test_text_box` incluidos los 24 comandos),
  build host y de device, captura headless (`docs/img/door.png` es la secuencia
  exterior → blanco → interior), movimiento con `MOTE_KEYS` y compilación sin warnings
  propios (`-Wall -Wextra`; los avisos que salen son de las cabeceras de Mote).
- No verificado: nada en hardware, ni los fps en device, ni el audio (`SFX_ENTER_DOOR`
  no suena todavía: el audio es TXT-10).
