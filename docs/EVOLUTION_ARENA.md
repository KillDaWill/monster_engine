# Evolution Arena — Laboratorio visual y simulación headless

**Evolution Arena** sustituye a las demos obsoletas `demo_ecosystem` y `demo_ecosystem_headless` (eliminadas del repositorio), proporcionando una plataforma experimental unificada para observar, medir e inspeccionar la evolución de cerebros neuronales recurrentes en poblaciones de **Brumacerno** y **Velkar**.

Ambos ejecutables comparten exactamente el mismo núcleo de simulación ([EvolutionArena.h](../include/EvolutionArena.h), [EvolutionArena.c](../src/EvolutionArena.c) y [EcosystemWorld.c](../src/EcosystemWorld.c)), compilando el ejecutable headless sin ninguna dependencia de SDL ni OpenGL (`ldd demos/demo_evolution_arena_headless` enlaza únicamente `libm` y `libc`).

## 1. Configuración inicial de la arena

Las identidades anatómicas se resuelven desde el registro de recetas (`CreatureRecipes_Find` / `CreatureRecipeRegistry`):
- **Brumacerno** (`recipe_id = 3`, `recipes/brumacerno.creature.json`): herbívoro (`dietMask = ECO_PLANT`, `canAttackPhysically = false`).
- **Velkar** (`recipe_id = 4`, receta incorporada): carnívoro depredador (`dietMask = ECO_MEAT`, `preyMask = 1`, `canAttackPhysically = true`).

Parámetros configurables por defecto:
- **Dimensiones físicas:** arena cuadrada de `40 × 40` unidades (`halfSize = 20.0`), compartida por el dominio de navegación A*, límites de `AIWorld_Walkable`, posiciones iniciales, obstáculos y perímetro renderizado.
- **Población inicial:** 16 Brumacernos y 6 Velkares (capacidad máxima 128).
- **Entorno:** 36 plantas regenerables, 2 estanques de agua accesibles y 5 obstáculos cilíndricos estáticos que bloquean tanto navegación como línea de visión.
- **Tiempo autoritativo:** paso fijo `1/60 s`, día ecológico `120 s`, cadencia de inferencia neuronal **5 Hz** (`0.2 s`).

## 2. Compilación y ejecución

```sh
make test
make demos/demo_evolution_arena
make demos/demo_evolution_arena_headless
make benchmark-neural

# Laboratorio visual 3D
./demos/demo_evolution_arena

# Simulador headless (ayuda completa y experimento ejemplo)
./demos/demo_evolution_arena_headless --help
./demos/demo_evolution_arena_headless \
    --seed=42 \
    --days=12 \
    --brumacernos=16 \
    --velkares=6 \
    --capacity=128 \
    --controller=neural \
    --founders=seeded \
    --mutation-rate=0.05 \
    --mutation-sigma=0.15 \
    --summary=build/evolution/summary.json \
    --history=build/evolution/history.csv \
    --export-brains=build/evolution/brains
```

## 3. Interfaz visual de laboratorio y controles

La interfaz visual (`demos/demo_evolution_arena`) reutiliza el renderizador OpenGL y `EcosystemVisual` (caché compartida de mallas, LOD, skinning GPU/CPU, etapas de crecimiento y animación procedural de Brumacerno y Velkar), añadiendo un HUD oscuro con acentos cian/turquesa:

- **Viewport 3D:** cámara orbital, zoom, pan, vista cenital (`O`), selección por rayo sobre la superficie real (clic izquierdo) o cíclica (`TAB`), seguimiento (`F`), perímetro físico de la arena, obstáculos, estanques, vegetación, cadáveres, anillos de especie/selección y overlays de depuración (`D`: objetivos, rutas A* y cadena oral).
- **Panel izquierdo (estadísticas poblacionales):** semilla, día/hora, velocidad solicitada frente a velocidad efectiva y aviso `BACKLOG`, población total y por especie, nacimientos, muertes por causa, generación máxima, salud/energía media, biomasa, diversidad neuronal, linajes activos, contador de intervenciones y gráfica histórica separada para Brumacerno y Velkar.
- **Panel derecho (inspector del individuo seleccionado y gestión de cerebros):** ID, especie, edad/etapa, generación, progenitores, descendencia, linaje, barras legibles de Salud/Energía/Hidratación/Fatiga, intención neuronal activa, acción, objetivo, estado reproductivo, resultado físico reciente y retroalimentación (`F/I/D`), arquitectura y huella FNV-1a, más seis botones interactivos (`Export`, `Import`, `Copy`, `Paste`, `Swap`, `Random`) con modal seguro de ruta y mensajes de error de validación.
- **Inspector de red neuronal en vivo (`N`):** dibuja las 32 entradas etiquetadas, las 12 neuronas ocultas recurrentes (`H0..H11`), las 10 salidas de intención con barras de preferencia y los pesos reales de `W_xh`, `W_hh` (en ámbar, diferenciando las conexiones recurrentes y autorrecurrentes) y `W_hy` (cian positivo, coral negativo; grosor y opacidad proporcionales a `|w|`). Incluye filtro de umbral de peso (`W-`/`W+` o `[`/`]`) y clic sobre cualquier neurona para aislar sus conexiones y valores exactos. Lee únicamente el estado real de `NeuralBrainRuntime` y jamás dispara inferencia desde el renderizado.

### Controles de teclado y ratón

| Control | Acción |
|---|---|
| `ESPACIO` | Pausar / reanudar simulación (cámara e inspector siguen operativos en pausa) |
| `1`, `2`, `3`, `4`, `5` | Velocidad solicitada `1x`, `4x`, `16x`, `64x` o **`MAX`** (siempre mediante ticks fijos de `1/60 s` en hilo dedicado) |
| `P` / `Q` / `M` | Alternar panel de telemetría de rendimiento / ciclar calidad LOD (`HIGH`/`MED`/`LOW`) / alternar minimapa cenital |
| `TAB` / Clic izquierdo | Seleccionar individuo |
| `F` / `O` | Seguir seleccionado / alternar vista cenital |
| Flechas / Arrastre derecho / Rueda | Órbita y zoom de cámara |
| `W`/`S`/`J`/`L` o Botón central | Desplazamiento (pan) de cámara |
| `N` / `D` | Alternar inspector neuronal / overlays de depuración |
| `E` / `I` | Exportar / importar `*.brain.json` (con protección de sobrescritura) |
| `C` / `V` | Copiar / pegar cerebro al portapapeles |
| `X` | Intercambio atómico de cerebros entre dos criaturas (marca A, luego selecciona B y pulsa `X`) |
| `B` / `R` / `ESC` | Reiniciar cerebro a aleatorio / reiniciar experimento / cancelar modal o salir |

## 4. Escalabilidad 500+ criaturas y rendimiento

La arquitectura actual desacopla el núcleo autoritativo en un hilo dedicado (`EvolutionSimulationRunner`) con *triple buffering*, índice espacial 2D (`AISpatialIndex`), alcance cervical compacto sobre plantillas anatómicas compartidas (`EcosystemAnatomyCache`), paralelismo determinista por fases (`EvolutionJobSystem`) e instancing GPU (`OpenGLRenderer_RenderMeshInstanced`).

- Presets escalables disponibles tanto en visual como en headless: `--preset=small` (22), `--preset=medium` (100), **`--preset=large` (500 criaturas, arena 200×200, capacidad 1.024)** y `--preset=stress` (1.000 criaturas, capacidad 2.048).
- Con **500 criaturas vivas** a **1920×1080**: **317,6 FPS** (`3,15 ms` media, P95 `5,36 ms`, P99 `5,90 ms`, 8–9 *draw calls*) a `1×`, y **295–327 FPS** sin bloqueos de UI a `64×`/`MAX`. En modo headless sostenido: **0,75–1,19 ms/tick** (`14×–22×` tiempo real, 46–73× más rápido que la línea base de `54,75 ms/tick`).
- Véanse los informes detallados en [EVOLUTION_ARENA_PERFORMANCE.md](EVOLUTION_ARENA_PERFORMANCE.md) y [EVOLUTION_ARENA_SCALABILITY.md](EVOLUTION_ARENA_SCALABILITY.md).

### Referencia histórica (línea base inicial `make benchmark-neural`)

Equipo de medición: NVIDIA GeForce RTX 3050 Laptop GPU, GCC `-Wall -Wextra -Wpedantic -O2`.

### Inferencia RNN aislada (CPU puro, un hilo, cero heap)

| Carga | Evaluaciones | µs / evaluación | ms / lote | Evaluaciones / s |
|---|---:|---:|---:|---:|
| 100 redes × 2.000 pasos | 200.000 | 0,979 µs | 0,098 ms | 1.021.303 / s |
| 1.000 redes × 500 pasos | 500.000 | 1,063 µs | 1,063 ms | 940.725 / s |

Como cada criatura evalúa su red a **5 Hz** (1 de cada 12 ticks), el coste amortizado de inferencia neuronal para 100 criaturas es inferior a **0,009 ms/tick**.

### Simulación completa de Evolution Arena (con alcance cervical anatómico autoritativo)

| Población inicial | Ticks | Media / tick | p50 | p95 | p99 | Velocidad máx. aprox. |
|---:|---:|---:|---:|---:|---:|---:|
| 22 (16+6) | 3.600 | 3,278 ms | 1,336 ms | 10,324 ms | 12,784 ms | 5,1× |
| 50 (38+12) | 2.400 | 7,360 ms | 3,903 ms | 20,825 ms | 26,902 ms | 2,3× |
| 100 (76+24) | 1.200 | 23,045 ms | 7,590 ms | 53,408 ms | 57,559 ms | 0,7× |

- **Desglose de cuello de botella medido:** perfilando 600 ticks de 22 criaturas con y sin el resolutor cervical completo (`EcosystemAnatomy_Update` / `AnatomicalReach_Update`), la simulación neural + percepción + navegación A* + fisiología + movimiento consume **0,099 ms/tick** (~168× tiempo real); el resto corresponde al resolutor CCD cervical de 60+ articulaciones para garantizar contacto oral fiel a las recetas.
- En la interfaz visual (1280×800), la escena completa con Brumacerno y Velkar procedurales e inspector neuronal activo alcanza ~48–62 FPS a `1x`.

## 5. Experimentos evolutivos multisemilla

Validación automatizada con `python3 tools/validate_evolution_arena.py --experiments --days=12 --seeds 42 101 202 --jobs=6` (7 configuraciones × 3 semillas = 21 ejecuciones independientes, sin asistencia ni reposición oculta):

| Experimento (media de 3 semillas, 12 días) | Pob. final | Nacimientos | Muertes | Capturas | Gen. máx. obs. | Diversidad | Dist. fundador | Interacc. fallidas | Planta | Carne | Agua |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `utility_baseline` | 1,0 | 6,3 | 27,3 | 8,7 | 1 | 0,0000 | 0,0000 | 0,0 | 34,3 | 43,7 | 18,8 |
| `random_founders` | 0,0 | 1,0 | 23,0 | 0,7 | 1 | 0,0000 | 0,0000 | 33,3 | 12,2 | 3,3 | 2,2 |
| `neural_no_mutation` (control genético) | 0,7 | 9,0 | 30,3 | 8,7 | 3 | 0,0000 | 0,0111 | 135,3 | 10,8 | 52,9 | 13,7 |
| `neural_with_mutation` (mutación física off) | 1,0 | 8,0 | 29,0 | 9,0 | 2 | 0,0152 | 0,0135 | 131,0 | 15,8 | 55,1 | 17,7 |
| `herbivore_only` (16 Brumacernos, 0 Velkares) | 4,0 | 7,0 | 19,0 | 0,0 | 1 | 0,0688 | 0,0258 | 10,3 | 62,4 | 0,0 | 21,3 |
| `two_species_arena` (mutación física+neural) | 0,3 | 7,0 | 28,7 | 8,0 | 1 | 0,0000 | 0,0203 | 110,7 | 16,2 | 44,3 | 13,4 |
| `cross_species_transfer` (cerebros cruzados) | 0,0 | 3,7 | 21,7 | 6,0 | 1 | 0,0000 | 0,0000 | 170,7 | 4,7 | 33,6 | 10,2 |

### Interpretación científica honesta y hallazgos diagnósticos

1. **Fundadores aleatorios (`random_founders`):** se extinguen en las 3 semillas (`pop=0`, `water=2,2`, `dehyd=19,0`), confirmando que no existe supervivencia asistida ni respaldo oculto al controlador de utilidad. Ocasionalmente se producen 1–2 nacimientos fortuitos antes de la extinción.
2. **Herencia neuronal multigeneracional reproducible:** en `neural_no_mutation` (semilla 42) se alcanza **generación 3** (10 nacimientos) y en `neural_with_mutation` (semilla 202, con mutación física desactivada) se alcanza **generación 2** con deriva medible respecto al fundador (`fdist = 0,0404`). En el experimento adicional de 20 días (`multigen_low_crowd`, semilla 101) se confirma igualmente reproducción hasta **generación 2** (`births_per_gen = [0, 5, 1]`). No afirmamos mejora adaptativa estadísticamente significativa con 12 días y 22 individuos: se requieren poblaciones mayores y horizontes más largos para medir selección direccional sostenida.
3. **Transferencia entre especies (`cross_species_transfer`):** al asignar `brains/hunter_carnivore.brain.json` a Brumacernos y `brains/forager_herbivore.brain.json` a Velkares, las interacciones físicamente rechazadas suben al máximo de toda la matriz (**170,7 fallos medios**), el consumo vegetal cae de 34,3/62,4 a **4,7** y la población colapsa en las 3 semillas. Esto verifica empíricamente que el cerebro controla la intención pero el cuerpo impone las restricciones físicas reales.
4. **Causa principal de mortalidad en arenas densas:** tanto en `utility_baseline` (`dehyd=15,3`) como en los modos neuronales (`dehyd=16,0..18,7`), la causa dominante de muerte es la deshidratación por congestión alrededor de las orillas de los estanques (trazado individual confirmado con `--trace`: un Brumacerno solitario opera con `navigation_failures = 0` y ciclos estables de comida/bebida).

## 6. Eliminación de demos obsoletas y migración de validadores

Se eliminaron `demos/demo_ecosystem.c` y `demos/demo_ecosystem_headless.c` junto con sus objetivos en `Makefile` y entradas en `.gitignore`. Se conservaron intactos todos los módulos reutilizables (`EcosystemWorld`, `EcosystemAnatomy`, `EcosystemVisual`, `EcosystemResources`, `CreaturePhysiology`, `CreatureReproduction`, `SimulationClock`) y se migraron los validadores activos:
- `tools/validate_ecosystem.py` → apunta a `demos/demo_evolution_arena_headless`
- `tools/capture_ecosystem_creatures.py`, `tools/capture_editor_bull.py`, `tools/validate_bovine_body.py` → apuntan a `demos/demo_evolution_arena`
- Nuevo validador `tools/validate_evolution_arena.py` para experimentos multisemilla (`--experiments`) y las 13 capturas visuales de laboratorio (`--visual`).
