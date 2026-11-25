# Renderizado de Mundos Procedurales y Rendimiento — Monster Engine

## 1. Arquitectura de presentación compartida (`RENDER_SRCS`)

Para evitar divergencias visuales entre aplicaciones, `demo_evolution_arena` y `demo_brain_evolution_lab` comparten exactamente la misma pila gráfica en `RENDER_SRCS`:

- **`TerrainRenderer` (`include/TerrainRenderer.h`, `src/TerrainRenderer.c`):**
  - Divide el heightfield en **chunks de `32×32` celdas** (p. ej. `256` chunks para `513×513`), generando 4 niveles de detalle (`LOD 0..3`) con **cosido de bordes mediante 16 máscaras** (*stitching*) que elimina grietas entre chunks vecinos.
  - Aplica relajación iterativa de LOD para garantizar una diferencia máxima de 1 nivel entre chunks adyacentes y protege los chunks de orilla (`shoreProtect`) contra simplificaciones agresivas.
  - Realiza **frustum culling exacto de 6 planos** extraídos directamente de `GL_PROJECTION_MATRIX * GL_MODELVIEW_MATRIX` (válido tanto para cámara ortográfica como en perspectiva) sobre la AABB 3D de cada chunk.
  - Shader GLSL 330 con mezcla de 4 materiales (`grass`, `drySoil`, `rock`, `wetMud`), proyección triplanar en acantilados, variación micro-superficial, iluminación solar/hemisférica, niebla atmosférica y **8 modos de depuración** (`NATURAL`, `ELEVATION`, `SLOPE`, `HYDROLOGY`, `WATER_DEPTH`, `BIOME`, `NAVIGATION`, `COLLISION`).
- **`WaterRenderer` (`include/WaterRenderer.h`, `src/WaterRenderer.c`):**
  - Construye una única malla indexada estática solo sobre las celdas con agua real (`waterDepth > 0`), manteniendo el coste de GPU en 1 *draw call*.
  - Shader GLSL 330 con absorción espectral por profundidad, reflexión Fresnel, normales de ondas animadas alineadas con el vector de flujo fluvial y espuma costera en bajas profundidades.
- **`RockVisualCache` (`include/RockVisualCache.h`, `src/RockVisualCache.c`):**
  - Genera mallas cerradas proyectando icósferas subdivididas sobre el SDF de cada variante geológica (`WorldGeology_EvaluateVariantSDF`) al cargar la época del mundo.
  - Dibuja cientos de formaciones rocosas en pocos lotes instanciados por GPU con escala 3D no uniforme, orientación completa por cuaternión, estratificación mineral y musgo en caras superiores.
  - Incluye `RockVisualCache_RenderCollisionDebug` para superponer los wireframes conservadores de `RockCollisionLobe[3]`.
- **`EnvironmentRenderer` (`include/EnvironmentRenderer.h`, `src/EnvironmentRenderer.c`):**
  - Sincroniza automáticamente los recursos GPU cuando cambia `epochId` o `dataFingerprint` de `WorldStaticData`, despachando de forma transparente entre modo plano compatible y modo procedural 3D.
  - Precalcula una textura de **minimapa topográfico `256×256`** con relieve sombreado, hidrología y rocas.
  - Proporciona `EnvironmentRenderer_PickGroundRay` (DDA 2D + intersección exacta de triángulos y láminas de agua) y overlays conformes al terreno (anillos de selección, objetivos, rutas A* y marcadores de sitios de orilla).

## 2. Medidas de generación y memoria (`make benchmark-world`)

Equipo de medición: AMD Ryzen 5 5600H (6 núcleos / 12 hilos), 14 GiB RAM, NVIDIA GeForce RTX 3050 Laptop GPU (4 GiB VRAM, OpenGL 4.6.0), GCC `-Wall -Wextra -Wpedantic -O2`.

| Mundo | Resolución | Tamaño | Tiempo total | Memoria estática | Lagos | Ríos | Orillas | Rocas | Transitable | Mojado |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `flat` | `17×17` | `64 u.m.` | `0,90 ms` | `0,09 MB` | 0 | 0 | 0 | 0 | `100,0%` | `0,0%` |
| `lab_valley_small` | `129×129` | `64 u.m.` | `18,44 ms` | `1,41 MB` | 5 | 1 | 59 | 2 | `63,4%` | `20,8%` |
| `wetlands` | `385×385` | `192 u.m.` | `135,32 ms` | `9,96 MB` | 10 | 1 | 179 | 8 | `81,4%` | `20,8%` |
| `river_valley` | `513×513` | `256 u.m.` | `255,06 ms` | `17,43 MB` | 17 | 1 | 210 | 65 | `75,0%` | `20,3%` |
| `mountain_lakes` | `513×513` | `256 u.m.` | `268,52 ms` | `17,43 MB` | 16 | 1 | 187 | 137 | `29,2%` | `30,5%` |
| `rocky_highlands` | `513×513` | `256 u.m.` | `276,60 ms` | `17,43 MB` | 6 | 1 | 150 | 212 | `25,2%` | `27,9%` |
| `large_territory_512` | `1025×1025` | `512 u.m.` | `1277,19 ms` | `68,61 MB` | 23 | 1 | 506 | 298 | `73,1%` | `22,5%` |

Desglose típico para `river_valley` (`513×513`): terreno `103,2 ms`, hidrología `39,8 ms`, biomas `41,8 ms`, geología `10,0 ms`, `WorldNavBaseGrid` `49,9 ms`.

## 3. Escalabilidad de simulación headless (6 hilos, Molecular v2)

| Escenario | Población | Media / tick | P95 / tick | P99 / tick | Velocidad efectiva | RSS pico |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `flat_compat_22` | 22 | `0,074 ms` | `0,125 ms` | `0,728 ms` | `223,88×` | — |
| `flat_compat_100` | 100 | `0,163 ms` | `0,254 ms` | `0,957 ms` | `102,00×` | — |
| `flat_compat_500` | 500 | `0,760 ms` | `1,372 ms` | `1,872 ms` | `21,93×` | `74,7 MiB` |
| `flat_compat_1000` | 1.000 | `1,496 ms` | `2,771 ms` | `3,340 ms` | `11,14×` | — |
| `proc_small64_22` | 22 | `0,126 ms` | `0,190 ms` | `0,761 ms` | `132,43×` | — |
| `proc_valley256_22` | 22 | `0,112 ms` | `0,214 ms` | `0,741 ms` | `148,94×` | — |
| `proc_valley256_100` | 100 | `0,280 ms` | `0,548 ms` | `1,059 ms` | `59,43×` | — |
| **`proc_valley256_500`** | **500** | **`1,386 ms`** | **`2,174 ms`** | **`2,745 ms`** | **`12,02×`** | **`74,7 MiB`** |
| `proc_valley256_1000` | 1.000 | `3,138 ms` | `4,537 ms` | `5,493 ms` | `5,31×` | `76,1 MiB` |
| `proc_large512_500` | 500 | `1,157 ms` | `1,735 ms` | `2,271 ms` | `14,41×` | `129,6 MiB` |

## 4. Rendimiento visual GPU en 1080p con 500 criaturas vivas

Perfilado en `demos/demo_evolution_arena` con `worlds/river_valley.world.json` (`513×513`, `256 u.m.`, 400 Brumacernos + 100 Velkares, 700 plantas, calidad `MED`, 6 trabajadores, velocidad solicitada **`4×`**, resolución **`1920×1080`**, 120 frames en tiempo real):

| Métrica | Valor medido |
| --- | ---: |
| Tiempo de frame (media / P50 / P95 / P99) | `5,25 ms` / `4,35 ms` / `6,77 ms` / `8,16 ms` (~`190 FPS`) |
| Tiempo GPU (media) | `3,41 ms` |
| Tiempo de tick de simulación (media / P95) | `2,39 ms` / `3,15 ms` |
| Velocidad solicitada vs efectiva | `4,000×` vs **`4,014×`** (`240,86 ticks/s`) |
| Criaturas dibujadas (media) / instanciadas | `471,8` / `1.176,7` instancias |
| Llamadas de dibujado de criaturas (media) | `6,99` (`3,99` instanciadas) |
| Chunks de terreno activos / triángulos entorno (1280×800) | `250 / 256` chunks, `196.942` tris, `20,8 MB` VRAM |
| Memoria RSS pico del proceso completo | `242.100 KiB` (`236,4 MiB`) |

## 5. Escalado paralelo en Brain Evolution Lab con terreno (`brumacerno_river_valley`, 16 pop × 2 semillas, 60 s)

| Configuración | Tiempo | Aceleración | Episodios/s | Ticks/s | Diferencia determinismo |
| --- | ---: | ---: | ---: | ---: | ---: |
| `1 trabajador (caché fría)` | `697,68 ms` | `1,00×` | `49,3` | `177.364` | referencia |
| `1 trabajador (caché caliente)` | `664,52 ms` | `1,05×` | `51,9` | `186.672` | `0.0e+00` |
| `4 trabajadores (caché caliente)` | `219,44 ms` | `3,03×` | `165,7` | `596.371` | `0.0e+00` |
| `6 trabajadores (caché caliente)` | `168,90 ms` | `3,93×` | `226,5` | `815.337` | `0.0e+00` (`138 hits / 4 misses`) |

## 6. Evidencia visual automatizada (`tools/validate_procedural_world.py`)

El validador `make validate-procedural-world` verifica determinismo headless bit a bit en todas las recetas `worlds/*.world.json` y captura 14 vistas OpenGL verificadas en `docs/validation/procedural-world/`:
- Territorios y geología: `01_river_valley_overview.png`, `02_mountain_lakes.png`, `03_rocky_highlands_collision.png`, `04_wetlands_shores.png`.
- Capas de depuración del terreno: `05_debug_elevation.png`, `06_debug_slope.png`, `07_debug_hydrology.png`, `08_debug_navigation.png`.
- Contacto anatómico, vadeo y orillas: `09_near_brumacerno_slope.png`, `10_near_velkar_slope.png`, `11_shore_closeup.png`.
- Escala, compatibilidad plana y Lab: `12_500_creatures_valley.png`, `13_flat_compat_comparison.png`, `14_lab_replay_terrain.png`.
