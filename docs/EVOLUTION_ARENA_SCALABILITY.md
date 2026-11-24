# Arquitectura Escalable de Evolution Arena (500–2.048+ Criaturas)

Este documento describe la arquitectura implementada para escalar **Evolution Arena** desde ~22 criaturas hasta **500–1.000+ criaturas simultáneas** (con capacidad dinámica de **1.024–2.048+ ranuras**) manteniendo el núcleo de simulación independiente de SDL/OpenGL, el determinismo multi-hilo bit-a-bit y todas las garantías biológicas y neuronales.

## 1. Módulos añadidos y refactorizados

| Módulo | Archivos | Responsabilidad |
|---|---|---|
| **Perfilado ligero** | `include/EvolutionProfiler.h`, `src/EvolutionProfiler.c` | Medición opcional de 28 secciones (media, mediana, P95, P99, máx) y 22 contadores operativos; coste cero cuando está desactivado (`NULL`). |
| **Caché de plantillas anatómicas** | `include/EcosystemAnatomyCache.h`, `src/EcosystemAnatomyCache.c` | Plantillas inmutables compartidas con conteo de referencias por `(recipeId, recipeHash, morphologyBucket, growthStep [0..16])`; reduce la memoria anatómica por entidad de ~320 KB a ~1,2 KB (~200× menos). |
| **Alcance cervical compacto** | `include/AnatomicalReach.h`, `src/AnatomicalReach.c`, `src/EcosystemAnatomy.c` | Cinemática directa compacta sobre la cadena cervical de 1–5 articulaciones, caché incremental de objetivo, ejes swing-twist precomputados, warm-start, salida temprana y planificación adaptativa (~32× más rápido que el oráculo completo, error de boca < 0,02). |
| **Índice espacial 2D y tablas O(1)** | `include/AISpatialIndex.h`, `src/AISpatialIndex.c`, `src/AIWorld.c` | Cuadrícula XZ en 4 capas (`STATIC`, `CREATURES`, `RESOURCES`, `CARCASSES`) con listas intrusivas O(1), hash abierto con borrado *backward-shift* (sin lápidas) y consultas `const` conservadoras sin asignaciones dinámicas ni duplicados. |
| **Navegación A\* escalable** | `include/CreatureNavigation.h`, `src/CreatureNavigation.c` | Mapa de despeje estático cacheado en `AIWorld`, resolución proporcional al mundo (~1,0 u.m., hasta 200×200), montículo binario y memoria *scratch* local al hilo sellada por generación (cero `malloc`/`memset` por búsqueda). |
| **Paralelismo determinista** | `include/EvolutionJobSystem.h`, `src/EvolutionJobSystem.c`, `src/EcosystemWorld.c` | Pool persistente (reutilizando `SDFSamplingPool`) que paraleliza fisiología, percepción+mente+A\*, propuestas de movimiento RVO y alcance cervical, con resolución de conflictos, nacimientos, muertes y estadísticas en orden canónico de ID ascendente. |
| **Hilo de simulación y snapshots** | `include/EvolutionSimulationRunner.h`, `src/EvolutionSimulationRunner.c` | Propietario exclusivo del `EcosystemWorld` autoritativo en la app visual. Cola de comandos con acuses (`PENDING`/`OK`/`FAILED`), *triple buffering* inmutable con propiedad explícita lector/escritor (`AcquireSnapshot`/`ReleaseSnapshot`), snapshot detallado del individuo seleccionado y control de deuda de reloj acotada (`1×`..`64×` y `MAX`). |
| **Visual escalable e instancing GPU** | `include/EcosystemVisual.h`, `src/EcosystemVisual.c`, `src/OpenGLRenderer.c` | Capacidad dinámica (1.024–2.048+), hash O(1) de ranuras, construcción perezosa de `Monster` sólo para `NEAR`/`MID`, animación acotada independiente de la velocidad de simulación, malla `FAR` procedural unificada y `OpenGLRenderer_RenderMeshInstanced` (`glDrawElementsInstanced`) + `OpenGLRenderer_BatchDebugLines`. |

## 2. Desacoplamiento simulación / renderizado

```
Hilo de simulación (EvolutionSimulationRunner)            Hilo de presentación (demo_evolution_arena)
┌─────────────────────────────────────────────┐           ┌────────────────────────────────────────────┐
│ Único escritor de EcosystemWorld            │           │ Nunca lee ni muta EcosystemWorld           │
│ - Cola de comandos segura entre ticks       │──acks────▶│ - Banner de estado y controles de cerebro  │
│ - Fases paralelas deterministas (1..N)      │           │ - EcosystemVisual_SyncSnapshot / Update    │
│ - Publicación en buffer libre (triple buf.) │──snap────▶│ - AcquireSnapshot / ReleaseSnapshot        │
│ - Deuda acotada (sin catch-up ilimitado)    │           │ - Instancing GPU (FAR + entorno) + NEAR/MID│
└─────────────────────────────────────────────┘           └────────────────────────────────────────────┘
```

- **Semántica de sobrecarga:** el paso biológico permanece estrictamente fijo en `1/60 s`. Si el hardware no sostiene `64×`, el corredor entra en modo limitado por capacidad (*throughput-limited*), acota `backlogSeconds` (evitando horas de recuperación pendiente al bajar a `1×`) y expone por separado `requestedSpeed` y `achievedSpeed`.
- **Pausa:** detiene el avance biológico exactamente en la frontera del tick actual; la cámara, la selección, el inspector neuronal y los controles de cerebros siguen completamente interactivos.

## 3. Contabilidad evolutiva y correcciones a gran escala

- **Capacidad dinámica sin topes ocultos:** `obstacles`, `lineages`, `founders` y `parentHistory` crecen dinámicamente (sin límites de 32/64/256), registrando los 500+ fundadores completos (`missing_founder_count == 0`).
- **Métricas biológicas exactas:**
  - Supervivencia observada (`seconds - bornAt`) separada de la edad biológica inicial (`physiology.age`).
  - Actualización inmediata de `stillLiving = false` en los archivos observacionales cuando fallece una criatura registrada.
  - Contabilidad póstuma de descendencia (`offspringCount`, `matureOffspringCount`, `reproductiveOffspringCount`) mediante `parentHistory`.
  - Remapeo de colisiones de `lineageId` al importar cerebros externos y máscara `speciesMask` para linajes transferidos entre especies.
  - Dispersión neuronal exacta $O(N \cdot P)$ (`neuralDispersionRms`) y diversidad poblacional determinista (`neuralDiversity`).
- **Seguridad ante fallos:** si falla la asignación de un genoma neuronal en un recién nacido, el nacimiento se cancela limpiamente (`failedBirths`) en lugar de producir un individuo corrupto; cualquier cambio de cerebro invalida inmediatamente el caché de alcance cervical.

## 4. Presets de arena y comandos de ejecución

| Preset (`--preset=`) | Criaturas (Brum + Velk) | Capacidad | Arena | Plantas | Estanques | Obstáculos |
|---|---:|---:|---:|---:|---:|---:|
| `small` *(defecto)* | 22 (16 + 6) | 128 | 40 × 40 | 36 | 2 | 5 |
| `medium` | 100 (76 + 24) | 256 | 90 × 90 | 160 | 8 | 12 |
| `large` | **500 (400 + 100)** | **1.024** | **200 × 200** | 800 | 40 | 24 |
| `stress` | **1.000 (800 + 200)** | **2.048** | **280 × 280** | 1.600 | 80 | 36 |

### Comandos recomendados

```sh
# 1. Compilar núcleo, pruebas, benchmarks y demos
make -j8 run_tests demos/demo_evolution_arena demos/demo_evolution_arena_headless benchmarks/benchmark_evolution_scalability

# 2. Lanzar Evolution Arena visual con 500 criaturas a 1080p, panel de rendimiento y minimapa
./demos/demo_evolution_arena --preset=large --seed=42 --width=1920 --height=1080 --show-perf --minimap

# 3. Lanzar Evolution Arena visual con 500 criaturas a velocidad 4x o MAX
./demos/demo_evolution_arena --preset=large --seed=42 --speed=4 --show-perf --minimap
./demos/demo_evolution_arena --preset=large --seed=42 --speed=max --show-perf --minimap

# 4. Simulación headless de 500 criaturas durante 5 días (con paralelismo automático)
./demos/demo_evolution_arena_headless --preset=large --seed=42 --days=5 --summary=build/large_500.json

# 5. Ejecutar benchmarks de escalabilidad y validación completa
./benchmarks/benchmark_evolution_scalability --workers=0
python3 tools/validate_evolution_performance.py --all --days=5 --frames=180

# 6. Pruebas de regresión, memoria y concurrencia
make test
make evolution-sanitize
make evolution-tsan
```

### Nuevos controles en la interfaz visual

- `1`..`4`: velocidades `1×`, `4×`, `16×`, `64×`; **`5`**: modo **`MAX`** (máxima velocidad sostenible).
- `P`: panel de telemetría y diagnóstico de rendimiento (~5 Hz: FPS, P95/P99, tick sim, TPS, velocidad solicitada vs alcanzada, backlog, edad de snapshot, LOD NEAR/MID/FAR/culled, draw calls, instancias, caché MB y RSS).
- `Q`: ciclo de preset gráfico/LOD (`HIGH` / `MED` / `LOW`).
- `M`: minimapa cenital de densidad poblacional (estanques, obstáculos, Brumacernos, Velkares, criatura seleccionada y recuadro de cámara).
