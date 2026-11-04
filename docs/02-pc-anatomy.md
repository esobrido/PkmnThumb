# 02 — Anatomía de Polished Crystal (medido)

Todo lo de aquí está medido sobre `vendor/polishedcrystal` (clon de
`Rangi42/polishedcrystal`, versión 3.2.3 según su Makefile).

## Tamaño por subsistema

| Directorio | Ficheros | Líneas asm |
|---|---:|---:|
| `engine/` | 358 | 158 497 |
| `audio/` | 249 | 141 460 |
| `data/` | 828 | 107 557 |
| `maps/` | 1 113 | 102 660 |
| `home/` | 55 | 12 963 |
| `constants/` | 57 | 15 766 |
| `macros/` | 20 | 4 605 |
| `gfx/` | 5 026 (PNG/bin) | — |

Desglose de `engine/` (líneas de asm):

| Subsistema | Líneas | Nota para el port |
|---|---:|---|
| `battle/` | 31 289 | el trozo más grande con diferencia |
| `events/` | 21 940 | eventos de campo, warps, scripts de objetos |
| `overworld/` | 18 334 | movimiento, VM de scripts, mapas |
| `pokemon/` | 12 575 | stats, evolución, crianza, experiencia |
| `gfx/` | 9 876 | paletas, animaciones de tiles, efectos |
| `battle_anims/` | 9 293 | animaciones de movimientos |
| `items/` | 8 853 | efectos de objetos, captura, mochila |
| `menus/` | 7 884 | mochila, equipo, tienda, PC |
| `phone/` | 6 567 | Pokégear/llamadas |
| `pokedex/` | 6 082 | Pokédex |
| `pc/` | 5 307 | cajas, correo, gestión de equipo |
| `movie/` | 5 003 | intro, créditos, títulos |
| `games/` | 4 192 | minijuegos (slots, etc.) |
| `pokegear/` | 4 007 | interfaz del Pokégear |
| `link/` | 2 839 | Cable Club (multijugador) |
| `tilesets/` | 1 737 | carga de tilesets y animaciones |
| `rtc/` | 1 051 | reloj, día/noche |
| `printer/` | 875 | Game Boy Printer |
| `math/` | 500 | aritmética |

## Contenido

| Qué | Cuánto |
|---|---|
| Mapas (layouts `.ablk`) | **458** |
| Tilesets con metatiles | **53** |
| Sprites de overworld (`gfx/sprites/*.png`) | **217** |
| Canciones (`audio/music/*.asm`) | **100** |
| Ficheros de `base_stats` | **334** (+ formas) |
| Movimientos | `data/moves/moves.asm` + 20 tablas auxiliares |
| Charmap | `constants/charmap.asm` (257 líneas, con n-gramas) |

## Build y ROM

- Toolchain: **RGBDS 1.0.4** (`.rgbds-version`), `rgbasm/rgblink/rgbfix/rgbgfx`.
- Makefile: `NAME := polishedcrystal`, `VERSION := 3.2.3`, `EXTENSION := gbc`.
- Salida: `polishedcrystal-3.2.3.gbc` = **2 097 152 B** (2 MiB = 128 bancos de 16 KB).
- Cartucho: `rgbdfix` con **MBC3 + TIMER + RAM + BATTERY**, `-l 0x33` (tamaño de ROM),
  `-r 3` (3 bancos de SRAM = 32 KB).
- Enlace: `layout.link` fija bancos de código y el mapa de RAM (WRAM0 en `$c000`,
  WRAMX 1–7 en `$d000`, SRAM 0–3, HRAM, VRAM).
- Imágenes: PNG → `rgbgfx -c dmg` → `.2bpp`/`.1bpp`; compresión propia **"Polished LZ"**
  (`.lzp`), no compatible con `lz3` estándar. Documentada en `docs/lz_format.md`
  del propio repo; resumida en [03-content-formats.md](03-content-formats.md).

### Variantes

`make faithful|monochrome|noir|hgss|debug|vc|pocket|bsp|freespace|tools`. La variante
`pocket` define `-DANALOGUE_POCKET -DNO_RTC` y usa extensión `.pocket`: es la
referencia de cómo se compila sin RTC, útil si algún día se quisiera comparar
comportamiento sin reloj.

## Por qué el asm no se traduce

158 K líneas de `engine/` + 13 K de `home/` en GBZ80 no se transpilan a algo
mantenible. La estrategia es: **contenido → datos** (extraído) y **motor → C nuevo**,
usando el asm sólo como especificación de comportamiento. `TASKS.md` ordena ese
trabajo por dependencias.
