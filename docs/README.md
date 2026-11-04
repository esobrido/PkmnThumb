# Documentación — Polished Crystal → Thumby Color (Mote)

Port nativo del *hack* Polished Crystal al handheld Thumby Color, implementado como
módulo de juego de **Mote**. No es un emulador: se reescribe el motor en C y se
reutiliza el **contenido** del juego original (mapas, tilesets, colisiones, arte,
paletas, texto, datos de Pokémon).

## Índice

| Doc | Qué contiene |
|---|---|
| [01-target-hardware.md](01-target-hardware.md) | Thumby Color y Mote: cifras reales, ABI, modelo de memoria, ciclo de vida de un módulo |
| [02-pc-anatomy.md](02-pc-anatomy.md) | Anatomía del repo de Polished Crystal medido: tamaño por subsistema, formato de ROM, toolchain |
| [03-content-formats.md](03-content-formats.md) | **Los formatos de datos**, con las trampas que costaron horas: `.ablk`, metatiles, atributos, colisión, tilesets, sprites, paletas, LZ, texto, scripts |
| [04-port-architecture.md](04-port-architecture.md) | Decisiones de arquitectura del port, geometría, presupuesto de RAM/flash, qué se reutiliza y qué se reescribe |
| [05-dev-workflow.md](05-dev-workflow.md) | Cómo se compila y se verifica aquí (sin root), extractor, capturas headless, entradas guionizadas |
| [06-findings-log.md](06-findings-log.md) | Bitácora de hallazgos y **callejones sin salida**, para no repetirlos |

Plan de trabajo y estado por componente: [`../TASKS.md`](../TASKS.md).

## Estado en una frase

El *slice* de overworld funciona de punta a punta (New Bark Town renderizado desde los
datos originales, movimiento en rejilla de 16 px, colisión del propio juego, cámara,
5 NPCs colocados por `object_event`), y el pipeline de extracción está validado. Falta
texto, interacción/scripts, warps, batalla, audio y el resto de sistemas.

## Principio rector

> Casi todo lo que costó años de autoría en Polished Crystal es **dato**, no código.

Los mapas y los scripts de mapa se ensamblan a **bytecode** (`macros/scripts/maps.asm`)
que interpreta una VM (`engine/overworld/scripting.asm`); los tilesets, el texto, los
datos de Pokémon y la música son tablas. Por eso el port puede conservar el contenido
y limitarse a reescribir el motor: es la única forma realista de portar un juego de
~300 000 líneas de asm a un handheld con 520 KB de RAM.
