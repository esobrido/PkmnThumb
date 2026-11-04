# TASKS — Port completo de Polished Crystal a Thumby Color (Mote)

Inventario de **todo** lo que hay que migrar y plan de trabajo por fases.
Estado a día de hoy: **slice 1 (overworld) funcionando**; ver `docs/`.

Progreso: **20 hechos · 5 parciales · 66 pendientes** (el conteo incluye las tablas
de inventario y las tareas del plan).

Leyenda de estado: `✅ hecho` · `🟡 parcial` · `⬜ pendiente` · `⛔ bloqueado`
Tamaño: `S` (1–2 días) `M` (~1 semana) `L` (2–4 semanas) `XL` (>1 mes), a tiempo parcial.

---

## 1. Inventario de sistemas

### 1.1 Motor del Game Boy (`home/`, 12 963 líneas asm)

| ID | Sistema | Original | Equivalente en el port | Estado |
|---|---|---|---|---|
| SYS-01 | Raster de tiles (LCD) | `home/lcd.asm` | `g_band()` en `game.c` | ✅ |
| SYS-02 | Paletas BG/OAM | `home/palettes.asm` | `bg_pal[]`, expansión a RGB565 | 🟡 sólo BG, sólo grupo `day` |
| SYS-03 | OAM / sprites | `home/sprites.asm` | blit en `g_overlay()` | 🟡 frame 0, sin prioridad fina |
| SYS-04 | Joypad | `home/joypad.asm` | `mote->input()` → `MoteInput` (`held`/`just_pressed`), `mote_pressed()` | ✅ |
| SYS-05 | Reloj y RTC | `home/time.asm`, `engine/rtc/` | **no hay reloj en la API** (sólo `dt`): acumular tiempo de juego y, si se quiere hora real, leer el RTC del RP2350 desde la capa de OS | ⬜ |
| SYS-06 | VBlank / IRQ | `home/vblank.asm` | `render_band` + `overlay` | ✅ |
| SYS-07 | Copia de datos (bankswitch) | `home/copy.asm` | XIP directo (no hace falta) | ✅ N/A |
| SYS-08 | SRAM / guardado | `home/sram.asm` | `mote->save/load/kv_*` | ⬜ |
| SYS-09 | Fuente de texto | `home/text.asm` | `mote->text_*` + fuente propia | ⬜ |
| SYS-10 | Audio (4 canales) | `home/audio.asm` | `audio_note` / mezclador | ⬜ |

### 1.2 Overworld (`engine/overworld/`, 18 334 líneas)

| ID | Sistema | Original | Estado |
|---|---|---|---|
| OW-01 | Carga de mapa y tileset | `engine/tilesets/map_gfx.asm` | ✅ (1 mapa, extracción previa) |
| OW-02 | Movimiento en rejilla | `engine/overworld/player_movement.asm` | ✅ |
| OW-03 | Colisiones | `engine/overworld/collision.asm` | ✅ (por cuadrante de 16 px) |
| OW-04 | Cámara / scroll | `engine/overworld/player_step.asm` | ✅ |
| OW-05 | Objetos/NPCs (posiciones) | `engine/overworld/objects.asm` | 🟡 colocados, estáticos |
| OW-06 | Movimiento de NPCs, patrones | `engine/overworld/map_objects.asm` | ⬜ |
| OW-07 | VM de scripts | `engine/overworld/scripting.asm` | ⬜ **clave** |
| OW-08 | Warps y conexiones entre mapas | `engine/overworld/warp.asm` | ⬜ |
| OW-09 | Escenas y coord/bg events | `engine/events/std_scripts.asm` | ⬜ |
| OW-10 | Cambio de tileset y animación de tiles | `engine/tilesets/` | ⬜ (agua animada) |
| OW-11 | Día/noche, tiempo | `engine/rtc/` | ⬜ (depende de SYS-05) |
| OW-12 | Bicicleta, surf, headbutt, etc. | `engine/overworld/*` | ⬜ |
| OW-13 | Followers (PC los tiene) | `engine/overworld/followers.asm` | ⬜ |

### 1.3 Eventos y guion (`engine/events/`, 21 940 líneas)

| ID | Sistema | Estado |
|---|---|---|
| EV-01 | Intérprete de comandos de script | ⬜ |
| EV-02 | Textboxes y comandos de texto | ⬜ |
| EV-03 | `giveitem`, `givemon`, `givemoney`… | ⬜ (depende de 3.x) |
| EV-04 | Batallas disparadas por script | ⬜ |
| EV-05 | Objetos del suelo (item balls) | ⬜ |
| EV-06 | Cutscene/movimiento de cámara | ⬜ |
| EV-07 | Entrenadores en el mundo | ⬜ |

### 1.4 Batalla (`engine/battle/` + `battle_anims/`, 40 582 líneas)

| ID | Sistema | Estado |
|---|---|---|
| BT-01 | Estructuras de batalla y turnos | ⬜ |
| BT-02 | Cálculo de daño/estadísticas | ⬜ |
| BT-03 | Efectos de movimientos (`data/moves/effects.asm`) | ⬜ |
| BT-04 | Estados alterados | ⬜ |
| BT-05 | IA de entrenador | ⬜ |
| BT-06 | Captura | ⬜ |
| BT-07 | Cambios, huida, fin de combate | ⬜ |
| BT-08 | UI de batalla (16 tiles de ancho) | ⬜ |
| BT-09 | Animaciones de movimientos | ⬜ (última prioridad) |
| BT-10 | Batallas dobles | ⬜ |

### 1.5 Pokémon, objetos, progresión (`engine/pokemon|items|pc|pokedex`, 28 000 líneas)

| ID | Sistema | Estado |
|---|---|---|
| PK-01 | `base_stats`, tipos, ratios | ⬜ |
| PK-02 | Curvas de experiencia y niveles | ⬜ |
| PK-03 | Movimientos aprendidos, MT/MO | ⬜ |
| PK-04 | Evolución (nivel/objeto/piedra/hora) | ⬜ |
| PK-05 | Huevos y crianza | ⬜ |
| PK-06 | Equipo y cajas (PC) | ⬜ |
| PK-07 | Mochila e items | ⬜ |
| PK-08 | Pokédex (vistas/capturas) | ⬜ |
| PK-09 | Guardado completo (partida) | ⬜ |
| PK-10 | Intercambio/Cable Club | ⛔ fuera de alcance (sin link) |

### 1.6 UI y menús (`engine/menus|movie`, 12 887 líneas)

| ID | Sistema | Estado |
|---|---|---|
| UI-01 | Fuente 8×8 y render de texto | ⬜ |
| UI-02 | Textbox con paginación y flecha | ⬜ |
| UI-03 | Menú de pausa | ⬜ |
| UI-04 | Listas con cursor y scroll | ⬜ |
| UI-05 | Estadísticas de Pokémon | ⬜ |
| UI-06 | Tienda | ⬜ |
| UI-07 | Pokégear / teléfono | ⬜ (baja prioridad) |
| UI-08 | Intro, títulos, créditos | ⬜ (baja prioridad) |
| UI-09 | Minijuegos (slots, etc.) | ⬜ (opcional) |

### 1.7 Audio (`audio/`, 141 460 líneas, 100 canciones)

| ID | Sistema | Estado |
|---|---|---|
| AU-01 | Parser de las macros de canción → datos | ⬜ |
| AU-02 | Mezclador en el buzzer (2 canales + lowpass) | ⬜ |
| AU-03 | SFX (recetas SFXR de Mote) | ⬜ |
| AU-04 | Cries de Pokémon | ⬜ |

### 1.8 Contenido y pipeline (`data/`, `maps/`, `gfx/`)

| ID | Sistema | Estado |
|---|---|---|
| CP-01 | Extractor de mapas + metatiles + colisión | ✅ |
| CP-02 | Extractor de tilesets y paletas BG | ✅ |
| CP-03 | Extractor de sprites de overworld | ✅ |
| CP-04 | Extractor de texto (desde fuentes `.asm`) | ⬜ |
| CP-05 | Extractor de scripts (bytecode) | ⬜ |
| CP-06 | Extractor de `base_stats`, movimientos, items | ⬜ |
| CP-07 | Extracción en lote de 458 mapas y 53 tilesets | ⬜ |
| CP-08 | Empaquetado y presupuesto de flash | ⬜ |
| CP-09 | Validación automática (tamaños, ids, colisiones) | 🟡 (parcial) |

### 1.9 Plataforma e ingeniería

| ID | Sistema | Estado |
|---|---|---|
| EN-01 | Toolchain host sin root | ✅ `setup.sh` |
| EN-02 | Verificación headless (capturas + input guionizado) | ✅ `shot.sh` |
| EN-03 | Build de device (`arm-none-eabi` + Pico SDK) | ⬜ no probado aquí |
| EN-04 | Medición de rendimiento (`perf()`) | ⬜ |
| EN-05 | Test de regresión visual automatizado | ⬜ |
| EN-06 | Empaquetado `.mote` y despliegue al handheld | ⬜ |

---

## 2. Plan de trabajo

Orden por **dependencias reales**, no por vistosidad. Cada fase tiene una puerta de
salida verificable; no se pasa a la siguiente sin cumplirla.

### Fase 0 — Fundaciones (✅ casi cerrada)

Objetivo: un overworld que se pueda ver y tocar, y un pipeline fiable.

| Tarea | Detalle | Tam | Estado |
|---|---|---|---|
| FND-01 | Toolchain host sin root (cmake + SDL2 dummy + mote_host) | M | ✅ |
| FND-02 | Extractor mapa/metatiles/colisión/tiles/paletas/sprites | L | ✅ |
| FND-03 | Raster por bandas con paleta por tile | M | ✅ |
| FND-04 | Movimiento, colisión, cámara | M | ✅ |
| FND-05 | Colocación de NPCs desde `object_event` | S | ✅ |
| FND-06 | Cargar N mapas en tiempo de ejecución (no un header fijo) | M | ⬜ |
| FND-07 | Animación de NPCs y frames de andar del jugador | S | ⬜ |
| FND-08 | Prioridad y orden de dibujado (sprites sobre bloques, Y-sort) | S | ⬜ |
| FND-09 | `perf()` y optimización del raster si hace falta | M | ⬜ |
| FND-10 | Build de device y ejecución en el handheld | L | ⬜ |

**Puerta F0**: andar por New Bark Town con NPCs animados y ≥55 fps en device, con el
raster medido.

### Fase 1 — Texto y UI (la siguiente)

Objetivo: que el juego *hable*. Es lo que convierte una demo en un juego.

| Tarea | Detalle | Tam |
|---|---|---|
| TXT-01 | Extraer la fuente 8×8 (`gfx/font/normal.png`) a bitmap C | S |
| TXT-02 | Parser de las cadenas fuente `.asm` con el charmap → tabla de textos | M |
| TXT-03 | Render de textbox a 16 tiles de ancho, con paginación y velocidad | M |
| TXT-04 | Comandos `<PLAYER> <RAM> <NUM> <PAUSE> <SOUND> <WAIT>` | M |
| TXT-05 | Marco de listas con cursor, scroll y repetición de input | M |
| TXT-06 | Menú de pausa (Pokémon/Objetos/Jugador/Opciones) | M |

**Puerta F1**: hablar con un NPC y leer su diálogo real, con nombre del jugador
interpolado, y navegar el menú de pausa.

> Nota de diseño: el original usa textboxes de 20 tiles; aquí caben 16. Hay que elegir
> entre reflow (recomendado) o marco con scroll horizontal.

### Fase 2 — Scripts y mundo

Objetivo: el mundo deja de ser un decorado.

| Tarea | Detalle | Tam |
|---|---|---|
| SCR-01 | Extractor del bytecode de scripts de mapa | L |
| SCR-02 | VM de scripts en C (tabla de comandos, pila, flags) | XL |
| SCR-03 | Interacción: hablar, leer carteles, objetos del suelo | M |
| SCR-04 | Warps, conexiones entre mapas, carga en caliente | L |
| SCR-05 | Escenas y `coord_event`/`bg_event` | M |
| SCR-06 | Movimiento de NPCs (`SPRITEMOVEDATA_*`) | M |
| SCR-07 | Cutscenes (mover cámara/jugador/NPC desde script) | M |
| SCR-08 | Día/noche con RTC y cambio de paletas | M |

**Puerta F2**: recorrer New Bark Town → Ruta 29 → Cherrygrove con warps reales, hablar
con Elm y que su script se ejecute del bytecode original.

### Fase 3 — Sistemas de juego (datos + progresión)

| Tarea | Detalle | Tam |
|---|---|---|
| GAM-01 | Extractor de `base_stats`, tipos, movimientos, items | L |
| GAM-02 | Estructuras de Pokémon y equipo | M |
| GAM-03 | Experiencia, niveles, subida de stats | M |
| GAM-04 | Mochila e items | M |
| GAM-05 | Guardado/guardado completo en `save()` | M |
| GAM-06 | Pokédex | M |

### Fase 4 — Batalla

| Tarea | Detalle | Tam |
|---|---|---|
| BAT-01 | Bucle de batalla salvaje (1 vs 1, sin animaciones) | XL |
| BAT-02 | Daño, tipos, críticos, precisión | L |
| BAT-03 | Efectos de movimiento (tabla de `effects.asm`) | XL |
| BAT-04 | Estados, cambios, objetos en batalla | L |
| BAT-05 | Captura y huida | M |
| BAT-06 | Batallas de entrenador + IA | L |
| BAT-07 | UI de batalla adaptada a 128×128 | L |
| BAT-08 | Animaciones de movimientos | L (opcional) |
| BAT-09 | Batallas dobles | L (opcional) |

**Puerta F4**: capturar un Pokémon salvaje en Ruta 29 con el motor nuevo, con la misma
fórmula de daño que el original (verificable contra el asm).

### Fase 5 — Audio

| Tarea | Detalle | Tam |
|---|---|---|
| AUD-01 | Parser de canciones → eventos de nota | L |
| AUD-02 | Mezclador 2 canales para buzzer con envolventes | M |
| AUD-03 | SFX de menú/batalla | S |
| AUD-04 | Cries (o sustituto) | S |

### Fase 6 — Contenido completo y cierre

| Tarea | Detalle | Tam |
|---|---|---|
| CON-01 | Extracción en lote (458 mapas, 53 tilesets, 217 sprites) | L |
| CON-02 | Presupuesto de flash y formato de empaquetado (módulo vs archivo) | L |
| CON-03 | Test de regresión visual (hash de frames por escena) | M |
| CON-04 | Repaso de rendimiento y batería en device | M |
| CON-05 | QA: recorrido completo del juego | XL |

**Puerta final**: partida jugable de principio a fin (o hasta el punto que se decida),
con audio, guardado y sin caídas de fps.

---

## 3. Decisiones abiertas

| # | Decisión | Opciones | Cuándo hay que tomarla |
|---|---|---|---|
| D1 | UI a 128 px de ancho | reflow a 16 tiles / marco con scroll / reducir fuente a 6×8 | Fase 1 |
| D2 | Empaquetado en device | todo en el `.mote` / datos en archivo aparte leído de flash | Fase 6 |
| D3 | Alcance de batalla | salvajes+captura / completo con dobles | Fase 4 |
| D4 | Audio | buzzer con 2 canales / silencio + SFX | Fase 5 |
| D5 | Ramas del contenido | Kanto completo (PC incluye Kanto) / sólo Johto | Fase 6 |
| D6 | Guardado | 1 slot / varios con `kv_*` | Fase 3 |

## 4. Riesgos

| Riesgo | Impacto | Mitigación |
|---|---|---|
| Rendimiento del raster en RP2350 | alto (fps) | medir con `perf()` en F0; cachear tiles expandidos por paleta |
| La VM de scripts es grande | alto (bloquea mundo) | hacerla incremental: empezar por los ~30 comandos más usados y contar los que aparecen al recorrer New Bark Town |
| 16 MB de flash para 458 mapas | medio | extraer en lote y medir antes de decidir D2 |
| Buzzer de 4 kHz | medio (experiencia) | 2 canales, aceptar pérdida |
| Sin build de device en este entorno | alto (no hay validación real) | provisionar `arm-none-eabi-gcc` + Pico SDK en `setup.sh` |
| Alcance total es de años a tiempo parcial | alto | fases con puertas; cada fase deja algo jugable |
