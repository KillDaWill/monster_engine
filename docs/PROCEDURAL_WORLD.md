# Sistema de Mundos Procedurales — Monster Engine

## 1. Visión general y alcance

El **Sistema de Mundos Procedurales** sustituye las arenas planas experimentales por territorios tridimensionales ecológicamente funcionales mediante una arquitectura híbrida **heightfield + geología procedural SDF + hidrología estática + biomas y fertilidad**. El mismo territorio sirve como fuente única de verdad para locomoción, navegación A*, percepción/línea de visión, contacto anatómico, colocación de recursos/entidades, simulación headless, entrenamiento en **Brain Evolution Lab** y renderizado acelerado por GPU.

Principios de diseño vigentes:
- **Compatibilidad plana de primera clase:** el modo plano (`worlds/flat.world.json` o `world == NULL`) se conserva íntegro para regresión, trazabilidad científica y compatibilidad con archivos `.brain.json` v1 y v2 (1.416 parámetros en `recurrent_candidate_policy` sin sensores ni recompensas acopladas al terreno).
- **Núcleo agnóstico del renderizador:** los 11 módulos del núcleo de mundo pertenecen exclusivamente a `CORE_SRCS` sin dependencias de SDL ni OpenGL; `make test` y ambos ejecutables headless compilan y ejecutan sin pila gráfica.
- **Propiedad inmutable y segura entre hilos:** `WorldStaticData` es inmutable tras su generación, utiliza conteo de referencias atómico (`WorldStaticData_Retain` / `Release`) y se comparte mediante caché LRU segura entre hilos (`WorldStaticCache`) entre snapshots triple-buffered de `EvolutionSimulationRunner` y trabajadores paralelos de `BrainEvolutionLab`.

## 2. Arquitectura y fuente única de verdad

```text
*.world.json + generator_version + terrain_seed
                   │
                   ▼
        WorldGenerator_Generate()
   ┌───────────────┼───────────────┐
   ▼               ▼               ▼
TerrainGenerator  WorldHydrology  WorldBiome
(Simplex/fBm/     (Priority-Flood (EDT agua, humedad,
 erosión térmica)  lagos, ríos,    fertilidad, RGBA,
                   orillas)        biomas)
                   │               │
                   └───────┬───────┘
                           ▼
                     WorldGeology
           (4 familias SDF, proxies 3-lóbulos,
            revalidación de orillas, grid CSR)
                           │
                           ▼
               WorldStaticData (inmutable, refcounted,
                 + WorldNavBaseGrid precalculado)
         ┌─────────────────┼─────────────────┐
         ▼                 ▼                 ▼
  WorldCollision    EcosystemWorld    EnvironmentRenderer
  WorldPlacement    CreatureNav/AI    Terrain/Water/Rock GPU
  TerrainAnimAdapt  Lab Evaluator     Minimap & Ray Picking
```

## 3. Mapa de módulos

### Núcleo independiente del renderizador (`CORE_SRCS`)

| Módulo | Cabecera / Fuente | Responsabilidad |
| --- | --- | --- |
| `TerrainHeightfield` | `include/TerrainHeightfield.h`, `src/TerrainHeightfield.c` | Malla regular acotada, división diagonal autoritativa CCW `+Y`, altura/normal baricéntrica exacta, jerarquía min/max `8×8`, raycast 2D DDA + Möller–Trumbore, huella FNV-1a. |
| `WorldIO` | `include/WorldIO.h`, `src/WorldIO.c` | Carga/serialización estricta `*.world.json` (`version: 1`, generadores físicos `1` y `2`), derivación SplitMix64 (`WorldSeed_Derive`), presets incorporados y límites de presupuesto de memoria. |
| `TerrainGenerator` | `include/TerrainGenerator.h`, `src/TerrainGenerator.c` | Ruido Simplex 2D determinista, fBm, multifractal en cresta (*ridged*), deformación de dominio, moldeado de valles/cuencas y erosión térmica con doble búfer. |
| `WorldHydrology` | `include/WorldHydrology.h`, `src/WorldHydrology.c` | Priority-Flood con min-heap + cola FIFO acíclica, acumulación de flujo, cuencas de lagos con cota de desbordamiento exacta, ríos monotónicos tallados y extracción de `WorldShoreSite`. |
| `WorldBiome` | `include/WorldBiome.h`, `src/WorldBiome.c` | Distancia al agua por EDT Chamfer de 2 pasadas, rugosidad, humedad, fertilidad, 4 canales materiales (`grass`, `drySoil`, `rock`, `wetMud`) y clasificación de bioma. |
| `WorldGeology` | `include/WorldGeology.h`, `src/WorldGeology.c` | 4 familias de rocas (`BOULDER`, `FRACTURED`, `BEDROCK`, `CLUSTER`), variantes SDF acotadas, colocación estratificada protegiendo orillas, proxies conservadores `RockCollisionLobe[3]` y malla espacial CSR. |
| `WorldStaticData` | `include/WorldStaticData.h`, `src/WorldStaticData.c` | Contenedor inmutable con conteo de referencias atómico, `WorldNavBaseGrid` precalculado (hasta `512×512`) y caché LRU `WorldStaticCache` con *single-flight*. |
| `WorldGenerator` | `include/WorldGenerator.h`, `src/WorldGenerator.c` | Pipeline completo `WorldGenerator_Generate` y utilidades de ensamblado sintético para pruebas (`WorldGenerator_FinalizeFromHeightfield`). |
| `WorldCollision` | `include/WorldCollision.h`, `src/WorldCollision.c` | `TraversalProfile`, muestreo de superficie/agua, validación de paso/pendiente/vadeo/rocas, línea de visión 3D y marcos orientados (`World_BuildBodyFrame`, `Local↔World`). |
| `WorldPlacement` | `include/WorldPlacement.h`, `src/WorldPlacement.c` | Filtros ecológicos `WorldHabitatFilter`, colocación determinista de criaturas/plantas/recursos y asentamiento seguro de cadáveres (`WorldPlacement_ResolveCarcass`). |
| `TerrainAnimationAdapter` | `include/TerrainAnimationAdapter.h`, `src/TerrainAnimationAdapter.c` | Adaptador local ↔ mundo del contrato `World` (`Monster.world`) para IK de patas sobre pendientes tridimensionales sin romper la caché de animación local. |

### Capa compartida de renderizado GPU (`RENDER_SRCS`)

| Módulo | Cabecera / Fuente | Responsabilidad |
| --- | --- | --- |
| `TerrainRenderer` | `include/TerrainRenderer.h`, `src/TerrainRenderer.c` | Chunks `32×32`, 4 niveles LOD por error proyectado, 16 máscaras de borde, protección de orillas, culling por MVP y 8 vistas de diagnóstico. |
| `WaterRenderer` | `include/WaterRenderer.h`, `src/WaterRenderer.c` | Malla estática recortada contra el contorno v2, con sombreado GLSL 330 de flujo, absorción y Fresnel. |
| `RockVisualCache` | `include/RockVisualCache.h`, `src/RockVisualCache.c` | Mallas cerradas proyectadas desde SDF por variante geológica, renderizado instanciado GPU (escala 3D + cuaternión) y depuración wireframe de lóbulos de colisión. |
| `EnvironmentRenderer` | `include/EnvironmentRenderer.h`, `src/EnvironmentRenderer.c` | Presentación unificada usada por `demo_evolution_arena` y `demo_brain_evolution_lab`, minimapa topográfico `256×256`, picking de rayo sobre terreno/agua y overlays conformes al relieve. |

## 4. Integración en aplicaciones

### Evolution Arena (`demos/demo_evolution_arena` y `demos/demo_evolution_arena_headless`)

Opciones CLI nuevas (soportadas por ambos ejecutables):
- `--world=ARCHIVO.world.json`: carga una receta de mundo procedural (`worlds/river_valley.world.json`, `worlds/mountain_lakes.world.json`, `worlds/rocky_highlands.world.json`, `worlds/wetlands.world.json`, `worlds/flat.world.json`, etc.).
- `--world-seed=N`: sobrescribe la semilla determinista del terreno independientemente de la semilla poblacional `--seed=N`.
- `--terrain-debug=MODO` (solo visual): inicia en modo de visualización `natural`, `elevation`, `slope`, `hydrology`, `water_depth`, `biome`, `navigation` o `collision`.
- `--show-shores` muestra las estaciones accesibles y el contorno continuo en v2. `--show-rock-collision` activa los wireframes de los lóbulos de colisión de rocas.
- `--terrain-lod0`, `--wireframe`, `--camera-position=X,Y,Z`, `--camera-target=X,Y,Z`, `--perspective=GRADOS`, `--water-time=SEGUNDOS` y `--msaa=N` permiten repetir capturas y comprobar costuras.

Controles interactivos adicionales en la versión visual:
- `A`: cicla entre los presets de mundo (`flat`, `river_valley_v2`, `mountain_lakes_v2`, `rocky_highlands_v2`, `wetlands_v2`) realizando un reinicio atómico del hilo de simulación.
- `Shift+D`: cicla los 8 modos de depuración del terreno (el modo `collision` activa también los wireframes de lóbulos de roca).
- `Shift+Q`: alterna el modo wireframe de la malla de terreno.
- `R` / `Shift+R`: reinicia el experimento / reinicia generando una nueva semilla de mundo.
- Clic izquierdo: selección de criaturas o picking 3D exacto del terreno y cuerpos de agua mediante `EnvironmentRenderer_PickGroundRay`.

### Brain Evolution Lab (`demos/demo_brain_evolution_lab` y `demos/demo_brain_evolution_lab_headless`)

- Los archivos `environments/*.environment.json` pueden declarar `"world"`, `"world_seed_policy"` (`fixed`, `episode_seed`, `seed_bank`) y `"resource_placement"` (hábitat por humedad/pendiente/altura).
- `EvolutionEvaluator` reutiliza `WorldStaticCache` entre todos los hilos trabajadores sin reconstruir el terreno cuando la semilla de mundo se repite.
- `EvolutionCheckpointIO` valida la huella del mundo (`world_fingerprint`) y la versión del generador (`generator_version`) al reanudar checkpoints.
- El viewport 3D de replay en `demo_brain_evolution_lab` usa el mismo `EnvironmentRenderer` y paso fijo real (`replay.world.clock.fixedStep`).

## Generadores físicos v1 y v2

Las recetas originales y los entornos de Brain Evolution Lab conservan `generator_version: 1`. Los archivos con sufijo `_v2` seleccionan el cauce continuo, el dominio por cuerpo y la profundidad firmada sobre el mismo triángulo que usa el terreno. El contorno, el picking y los contactos de orilla comparten ese campo. El entorno `brumacerno_river_valley_v2.environment.json` permite entrenar y reproducir en v2. Los checkpoints exigen el entorno, la versión y la huella correspondientes. Los activos de renderizado quedan fuera de la huella física.

La comparación de captura y rendimiento se recoge en [Calidad visual de terreno y agua](TERRAIN_WATER_VISUAL_QUALITY.md).

## 5. Limitaciones conocidas y elementos fuera de alcance

Quedan explícitamente fuera de este hito (con puntos de extensión previstos en las estructuras de datos):
- Mundos infinitos o streaming dinámico de regiones.
- Deformación de terreno en vivo, erosión dinámica, simulación de fluidos Navier–Stokes o inundaciones.
- Cuevas, voladizos navegables o escalada sobre rocas multi-nivel (el heightfield define una superficie de suelo por coordenada XZ y las rocas grandes actúan como obstáculos conservadores).
- IA completa de natación submarina o vuelo (las criaturas terrestres disponen únicamente de vadeo somero según su `TraversalProfile`).
- Clima dinámico, evolución de flora o nuevas arquitecturas neuronales (NEAT).

## 6. Documentación relacionada

- [Formato de recetas de mundo (`*.world.json`)](WORLD_RECIPE_FORMAT.md)
- [Hidrología, lagos, ríos y acceso en orilla](WORLD_HYDROLOGY.md)
- [Locomoción, navegación y animación en terreno](TERRAIN_LOCOMOTION_AND_NAVIGATION.md)
- [Renderizado de mundo y rendimiento medido](WORLD_RENDERING_AND_PERFORMANCE.md)
- [Evolution Arena](EVOLUTION_ARENA.md) y [Brain Evolution Lab](BRAIN_EVOLUTION_LAB.md)
