# 07 — Convenciones: organización, módulos y tests

Estas reglas son obligatorias para cualquier cambio en `game/src`. Existen por dos
motivos concretos: el motor se va a portar durante meses y crecerá a decenas de módulos,
y **cada módulo debe poder probarse sin emulador ni hardware**.

## 1. El código se ordena por complejidad: fichero si es simple, carpeta si no

No hay una regla de "un sistema = un fichero". La regla es la contraria: **el tamaño y la
complejidad deciden la forma**.

| Si el código es… | Va en… | Ejemplo actual |
|---|---|---|
| simple: una sola idea, ~200 líneas o menos, sin sub-piezas | **un fichero** `game/src/<nombre>.c` + `.h` | `gfx_sprite.c` (expansión 2bpp y blit) |
| complejo o con pinta de crecer: varias responsabilidades, o >~200 líneas, o una lista de tareas pendientes en `TASKS.md` | **su propia carpeta** `game/src/<sistema>/`, un fichero por pieza | `ow/` → `map.c`, `camera.c`, `player.c`, `render.c` |
| siempre | `game/src/game.c` (glue de Mote) y `game/src/gen/` (datos generados) | — |

```
game/src/game.c              glue: vtable, arena, input. Único que habla con Mote.
game/src/gfx_sprite.c/.h     simple -> fichero suelto
game/src/ow/                 sistema complejo -> carpeta
    map.c/.h                 geometría y colisión
    camera.c/.h              ventana de scroll
    player.c/.h              máquina de estados del caminar
    actor.c/.h               NPCs: cara, giro, orden de dibujado
    render.c/.h              raster por bandas
    dir.h                    vocabulario compartido (enum de dirección, frames)
game/src/text/               texto y (pronto) UI: sistema complejo -> carpeta
    font.c/.h                glifos 1bpp y expansión de n-gramas/nombres
    box.c/.h                 caja: reflow, paginación, revelado, dibujo
game/src/ui/                 ventanas de menú: otro sistema complejo -> carpeta
    frame.c/.h               la caja con marco que comparten las ventanas
    list.c/.h                ventana de una columna: cursor, scroll, caja
    options.c/.h             la pantalla de Option: 13 ajustes y sus valores
game/src/gen/world.h         datos extraídos (no se editan)
game/src/gen/text.h          textos como bytecode del juego
game/src/gen/font.h          fuente 1bpp (114 + 6 tiles)
game/src/gen/ui.h            entradas del menú de pausa, como bytes
game/src/gen/options.h       nombres y valores de los ajustes, como bytes
```

Detalles de la regla:

- **Un nivel de anidamiento como máximo** (`ow/map.c`, no `ow/render/band.c`). Más
  profundidad no aporta y complica el build y los nombres de los tests.
- **Dentro de una carpeta, los ficheros no repiten el nombre del sistema**: `ow/map.c`,
  no `ow/ow_map.c`.
- **Los `#include` llevan siempre la ruta desde `src/`**: `#include "ow/map.h"`. Así el
  mismo include vale para el juego y para los tests.
- **Promover y despromover es normal**: si una carpeta se queda con un solo fichero de
  menos de ~200 líneas, se puede bajar a fichero suelto; si un fichero suelto empieza a
  tener varias piezas, se sube a carpeta. Es un refactor barato y las pruebas no cambian.
- **Ningún módulo incluye a otro en cascada ni comparte estado global**: si dos necesitan
  lo mismo, eso es un tercer módulo.

### Por qué hace falta un wrapper de build

`mote build` compila **solo los `.c` del primer nivel de `game/src`** (usa `os.listdir`,
no recursión: `vendor/mote/tools/mote`, función `game_sources`). Un módulo en una carpeta
no se compila y el enlace falla con `undefined symbol`. La organización no se subordina a
esa limitación: `scripts/build.sh` construye un espejo plano temporal y llama a la
herramienta oficial sobre él.

```
game/build/flat/src/            copia de game/src (los headers conservan su ruta)
game/build/flat/src/ow_map.c    shim de una línea: #include "ow/map.c"
```

Así el build oficial —incluido el de device, que es el que empaqueta el `.mote`— sigue
siendo el de Mote, sin duplicar flags. **Compilar siempre con `scripts/build.sh`**, nunca
llamando a `mote build game` directamente.

## 2. Todo módulo (todo `.c`) tiene test unitario

`tests/unit/test_<ruta con / → _>.c`:

| Módulo | Test |
|---|---|
| `game/src/gfx_sprite.c` | `tests/unit/test_gfx_sprite.c` |
| `game/src/ow/map.c` | `tests/unit/test_ow_map.c` |
| `game/src/ow/player.c` | `tests/unit/test_ow_player.c` |
| `game/src/ow/connect.c` | `tests/unit/test_ow_connect.c` |
| `game/src/ow/actor.c` | `tests/unit/test_ow_actor.c` |
| `game/src/ow/camera.c` | `tests/unit/test_ow_camera.c` |
| `game/src/ow/fade.c` | `tests/unit/test_ow_fade.c` |
| `game/src/ow/move.c` | `tests/unit/test_ow_move.c` |
| `game/src/ow/render.c` | `tests/unit/test_ow_render.c` |
| `game/src/ow/warp.c` | `tests/unit/test_ow_warp.c` |
| `game/src/text/box.c` | `tests/unit/test_text_box.c` |
| `game/src/text/font.c` | `tests/unit/test_text_font.c` |
| `game/src/ui/list.c` | `tests/unit/test_ui_list.c` |
| `game/src/ui/menu.c` | `tests/unit/test_ui_menu.c` |
| `game/src/ui/frame.c` | `tests/unit/test_ui_frame.c` |
| `game/src/ui/options.c` | `tests/unit/test_ui_options.c` |
| `game/src/bag.c` | `tests/unit/test_bag.c` |
| `game/src/script/trigger.c` | `tests/unit/test_script_trigger.c` |
| `game/src/script/scene.c` | `tests/unit/test_script_scene.c` |
| `game/src/script/vm.c` | `tests/unit/test_script_vm.c` |
| `game/src/mon/level.c` | `tests/unit/test_mon_level.c` |
| `game/src/mon/mon.c` | `tests/unit/test_mon_mon.c` |
| `game/src/mon/party.c` | `tests/unit/test_mon_party.c` |
| `game/src/battle/type.c` | `tests/unit/test_battle_type.c` |
| `game/src/battle/damage.c` | `tests/unit/test_battle_damage.c` |
| `game/src/battle/turn.c` | `tests/unit/test_battle_turn.c` |
| `game/src/ui/battle.c` | `tests/unit/test_ui_battle.c` |

Además, `tests/unit/test_gen_data.c` prueba **los datos generados** en sí: que cada
índice de texto/sprite que guarda un `PcObject` apunta a algo, que cada stream empieza en
`<START>` y acaba en `<DONE>` o `<PROMPT>`, que los objetos caen dentro del mapa, y que
los valores de `gen/move.h` son los del `.asm` (el `step_end` en `$47`, `step_sleep` en
`$46`, `WALK_UP_DOWN` en 4 y `WALK_LEFT_RIGHT` en 5). Un
desplazamiento de un índice (un NPC que dice la frase de otro) falla ahí, no en el
dispositivo.

`scripts/test.sh` compila cada test contra todos los módulos y lo ejecuta; además **falla
si un módulo no tiene su test** (cobertura estructural, no de líneas):

```
FAIL game/src/ow/thing.c has no tests/unit/test_ow_thing.c
```

Qué se prueba en un test unitario: bordes e invariantes del módulo (límites del mapa,
cuadrantes de colisión, alineación a la rejilla, recorte de bandas, transparencia,
espejado), no la integración con Mote.

## 3. Tests de comportamiento: escenarios serializables

Para lo que un humano quiere leer sin abrir el código (movimiento, colisiones, cámara, y
más adelante scripts y batalla) hay **escenarios**: un fichero de texto con un estado
inicial, unos pasos y un estado final.

```
# tests/scenarios/ow_stop_at_wall.txt
kind unit

map 4x2
..#.
....

start 0 0 right
hold right 64            # 64 frames = 64 px, one step every 16
expect 1 0 right
```

### Formato

| Línea | Significado |
|---|---|
| `# ...` | comentario (se ignora) |
| `kind unit` | tipo de escenario. Hoy solo `unit` (lógica pura, sin emulador) |
| `map <ancho>x<alto>` | tamaño del mapa **en celdas de 16×16 px** |
| `<ancho caracteres>` × `<alto>` líneas | el mapa: `.` = transitable, `#` = bloqueado. Un carácter por celda |
| `start <x> <y> [dir]` | posición inicial en celdas y orientación (`down/up/left/right`) |
| `hold <dir> <frames>` | mantiene esa dirección N frames (`none` = sin input). 1 frame = 1 px: el GB anda 16 px en 16 frames a 60 fps. Si la dirección no es la que se mira, los primeros 8 frames son el giro en el sitio (Turning Speed = Slow). Si la celda de delante está bloqueada el jugador se queda en la suya (anda en el sitio), así que manténgase la dirección lo que haga falta |
| `run <dir> <frames>` | lo mismo con las zapatillas puestas: 2 px por frame, así que una celda de 16 px cuesta 8 frames |
| `expect <x> <y> [dir]` | estado final esperado, en celdas |

Si no hay `expect`, el runner imprime el estado alcanzado (`state 4 0 right`) para que se
pueda copiar. Si no coincide, imprime esperado y obtenido y sale con código 1.

Detalle del formato que importa: el mapa en celdas se agrupa internamente en bloques de
2×2 celdas, y el patrón de bloqueo de cada bloque se convierte en el id de metatile
correspondiente. Así el escenario ejercita la misma ruta de colisión que el juego real
(bitmask por cuadrante) sin necesitar arte ni datos extraídos.

### Cuándo escenario y cuándo test unitario

| Quiero comprobar… | Herramienta |
|---|---|
| que un `switch` cubre un cuadrante o un borde | test unitario |
| que "andando contra una pared el jugador se para donde toca" | escenario |
| que el mapa se dibuja con la paleta correcta | test unitario (`ow/render`) |
| que el juego arranca, pinta y responde a botones reales | captura (`scripts/shot.sh`) |

Los escenarios describen **intención**, así que deben poder escribirse sin conocer la
implementación: si un escenario necesita saber que existe `step_left` o que el jugador
avanza 1 px por frame, está mal planteado y se simplifica el formato, no el test.

## 4. Reglas de datos generados

- `game/src/gen/*.h` lo produce `tools/extract.py`; **nunca** se edita a mano.
- Si el extractor cambia, se regenera y se vuelve a pasar `scripts/test.sh` y la captura.
- Cualquier tabla nueva debe venir con su validación en el extractor (tamaños que
  cuadren, ids en rango): que el pipeline falle antes de que el juego pinte basura.

## 5. Verificación antes de dar algo por terminado

```sh
scripts/test.sh                    # unit + escenarios (rápido, sin emulador)
./scripts/shot.sh artifacts/x.png 45 "a:2-5,down:20-70"
./scripts/perf.sh 900              # fps / update_us / raster_us
scripts/build.sh --device          # el .mote tiene que compilar
```

Y si el cambio toca datos o render, comparar la captura con `docs/img/overworld.png`
(referencia conocida) para detectar regresiones visuales.
