# Perfil de construcción de criaturas — Agent J

## Estado de evidencia

**Rendimiento: medido en CPU, sin renderer.** Se compiló y enlazó el harness completo y se midieron cinco fuentes válidas en serie (caballo, Brumacerno, asno, preset perro, preset lagarto), con las cuatro etapas, `--runs 6`, `--voxel 0.18`, `maxCells=600000`, `maxResolution=320`. Cada caso salió con código 0, CSV válido y cero fases fallidas. El slot exclusivo se liberó al terminar todas las corridas; no se ejecutaron suites, capturas ni validadores.

La medición corresponde al código del commit `bd22d641b0b8a867f4e7e813716a0312c8135877` (rama `agent/perf-20261106-j`), con solo los tres archivos propios del agente. El commit de referencia revisado es `bc83fd586d7b1b8edae881c65ee25e96536ac08f`; el punto de partida de esta rama contiene únicamente el ledger añadido respecto a esa referencia. El informe externo `INFORME_OPTIMIZACION_MONSTER_ENGINE_SDF_PELO_2026-11-05.md` no está disponible en este checkout. `docs/validation` está excluido; no se leyó ni alteró referencia visual alguna.

El índice de código devolvió “No embedding-capable provider found”; reintentar indexación falló igual. La arquitectura siguiente se basa en los fuentes actuales de este worktree.

## Recetas/fixtures encontrados

| Especie o fixture | Fuente comprobada | Parse JSON | Nota |
|---|---|---:|---|
| Caballo | `recipes/caballo.creature.json` (también catálogo) | Sí | Fixture externo apropiado para `--recipe`; la receta JSON persiste como `CreatureRecipeDocument` durante el uso. |
| Brumacerno/bovino | `recipes/brumacerno.creature.json` (ID 3; también preset incorporado) | Sí | Fuente canónica externa para Brumacerno; `src/CreatureRecipeBrumacerno.generated.inc` es generado. |
| Asno | `recipes/asno.creature.json` (también catálogo) | Sí | Receta externa presente. |
| Perro | `CreatureRecipes_Dog()` | No | Preset compilado ID 2; no hay JSON canónico perro en `recipes/`. |
| Lagarto | `CreatureRecipes_Lizard()` | No | Preset compilado ID 1; no hay JSON canónico lagarto en `recipes/`. |
| `velkar` | `CreatureRecipes_Velkar()` / ID 4 | No | Preset compilado; no se interpreta aquí como caballo sin verificar su semántica de especie. |

La lista se obtuvo inspeccionando `recipes/catalog.txt`, `recipes/README.md`, `src/CreatureRecipes.c` y las rutas actuales. Dog/lizard usan `--species`; las recetas externas usan `--recipe`. No se inventó una receta JSON para los presets.

## Harness y reproducción

Archivos añadidos:

- `benchmarks/benchmark_creature_construction.c`: ejecutable C headless. Emite CSV por etapa/repetición y fase, contadores de malla/conectores y estimaciones de bytes CPU.
- `tools/benchmark_creatures.py`: compila el harness contra los objetos `CORE_SRCS` existentes mediante sus reglas make, sin modificar el `Makefile`; cap de dos jobs, crea un ejecutable en `build/` y guarda metadatos en `build/creature-construction-metadata.json`.

Reproducción desde la raíz del checkout (los cinco casos se corrieron uno por vez bajo `run-bench.sh`; cada especie tiene su CSV aparte):

```sh
/mnt/storage/monster-engine-agents/run-bench.sh python3 tools/benchmark_creatures.py -- --recipe recipes/caballo.creature.json --runs 6 --voxel 0.18 --output /mnt/storage/monster-engine-agents/scratch/perf-20261106-j/horse.csv
/mnt/storage/monster-engine-agents/run-bench.sh python3 tools/benchmark_creatures.py -- --recipe recipes/brumacerno.creature.json --runs 6 --voxel 0.18 --output /mnt/storage/monster-engine-agents/scratch/perf-20261106-j/brumacerno.csv
/mnt/storage/monster-engine-agents/run-bench.sh python3 tools/benchmark_creatures.py -- --recipe recipes/asno.creature.json --runs 6 --voxel 0.18 --output /mnt/storage/monster-engine-agents/scratch/perf-20261106-j/asno.csv
/mnt/storage/monster-engine-agents/run-bench.sh python3 tools/benchmark_creatures.py -- --species dog --runs 6 --voxel 0.18 --output /mnt/storage/monster-engine-agents/scratch/perf-20261106-j/dog.csv
/mnt/storage/monster-engine-agents/run-bench.sh python3 tools/benchmark_creatures.py -- --species lizard --runs 6 --voxel 0.18 --output /mnt/storage/monster-engine-agents/scratch/perf-20261106-j/lizard.csv
```

La compilación enlazada usó las reglas Core existentes (`make -j2` desde el wrapper) y no editó `Makefile`. `--runs 6` emite exactamente seis muestras: `rep=0` es el primer caso de la ejecución (frío respecto al harness, **no** garantiza cache de disco/OS frío); luego se ejecuta una pasada completa descartada a `/dev/null`; `rep=1..5` son post-warmup. Cada repetición medida vuelve a crear la criatura. `sdf_build_steady` y `mesh_generation_steady` incluyen un calentamiento adicional no reportado sobre el mismo objeto y luego miden reutilización de buffers (`sample_class=steady_object_reuse`, n=6 incluyendo rep 0). Las estadísticas p50/p95/max se calcularon por interpolación lineal de percentiles; los grupos post-warmup juntan cinco repeticiones por etapa, y `phase-summary.csv` conserva el desglose por etapa y sample class.

Máquina: AMD Ryzen 5 5600H con Radeon Graphics, 12 CPUs lógicas, Linux `7.0.0-38-generic`, GCC `13.3.0-6ubuntu2~24.04.1`; flags `-Wall -Wextra -Wpedantic -O2 -Iinclude -Itests`, link `-lm -pthread`. Se registraron SHA de fuente, wrapper, ejecutable, CSV, logs de slot y muestras de `loadavg` en `manifest.json`. Load average de 1 minuto al adquirir el lock / muestreo durante la ejecución: caballo `1.02 / 1.02` (3 muestras), Brumacerno `1.02 / 1.02` (3), asno `1.11 / 1.11` (2), perro `1.01 / 1.01` (2), lagarto `1.37 / 1.34–1.37` (2). No se lanzó otro benchmark/captura concurrente según coordinación. El muestreo fue breve (corridas de 1–3 s); loadavg describe todo el sistema, no solo esta tarea.

El CSV tiene exactamente 17 columnas. `status` distingue fases medidas, fallidas, ausentes/no aplicables y no medidas. Los contadores son nullable y solo se emiten donde se instrumentan: `distance_calls` en `sdf_region_*` y fases mesh; `connector_*` solo en fases mesh; `vertices/triangles` solo en mesh/map/fur; `root_count` solo en raíces de fur exitosas. En otras fases quedan vacíos (no significan cero). `cpu_bytes` es una estimación de capacidades de estructuras contadas (no RSS ni allocator overhead); `gpu_bytes` queda vacío porque el harness no usa renderer/GPU. `end_to_end_cpu_creation` está explícitamente como `not_measured_noncontiguous`: no se midió de forma contigua. `sum_of_profiled_cpu_phases` es solo la suma aritmética de `Creature_BuildMonster`, SDF frío, mesh frío, mapeo y raíces; el mapeo se midió sobre el mesh después de la repetición steady, y la suma no es end-to-end ni incluye parseo, consultas diagnósticas o pose. La fila de suma solo se escribe tras éxito de fur y pose. Fallo de fur, pose o de una fase requerida registra el estado fallido, aborta con código distinto de cero y no permite una suma exitosa.

La fase `color_only_data_update` mide solo `Monster_SetSurface`, no empaquetado/upload del shader. `growth_update_build` reconstruye el intermedio entre dos etapas contiguas, no mide el flujo async del editor. El harness no perfila `LongHairSet_Build` internamente por subfase; lo marca como no perfilado para no etiquetar falsamente el caballo como general. La cobertura de pelo corto se representa mediante construcción de raíces sobre la malla CPU cuando el fenotipo tiene densidad positiva; no equivale al coste de sombreado por shells en GPU.

### Matriz medida por especie/fase

Los valores son **p50 / p95 / máximo en ms**, post-warmup, agrupando las cuatro etapas (n=20 salvo growth n=15; etapa terminal no tiene siguiente etapa). La tabla es legible-resumen; `phase-summary.csv` contiene p50/p95/máximo por etapa y clase de muestra. `creature_creation` ya resuelve anatomía y rig; las fases anatomy/creation no son aditivas. `sum` es deliberadamente una suma de fases instrumentadas, no end-to-end.

| Especie | Fenotipo | Anatomía | Creación completa | SDF build | Mesh CPU | Mapeo | Fur roots | Pose-only | Color-only | Growth build | Suma de fases |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Caballo | .002/.004/.005 | .039/.051/.051 | .126/.138/.166 | .055/.071/.071 | 18.749/33.623/34.031 | 1.790/3.541/3.602 | .070/.141/.142 | .302/.317/.323 | .003/.005/.006 | .187/.263/.298 | 20.823/37.492/37.905 |
| Brumacerno | .002/.007/.007 | .057/.076/.076 | .164/.188/.192 | .067/.082/.083 | 20.419/65.815/67.344 | 1.433/5.071/5.281 | .073/.308/.317 | .319/.377/.379 | .003/.006/.007 | .286/.411/.481 | 22.099/71.382/72.963 |
| Asno | .001/.004/.004 | .037/.055/.059 | .134/.154/.165 | .055/.069/.092 | 15.883/26.129/27.411 | 1.272/2.660/2.680 | .061/.114/.114 | .299/.311/.319 | .003/.005/.006 | .188/.277/.289 | 17.420/29.095/30.319 |
| Perro preset | .002/.003/.004 | .038/.062/.070 | .080/.099/.113 | .076/.087/.121 | 14.971/29.666/32.951 | .693/1.963/2.258 | .030/.091/.093 | no presente (gait NONE) | .002/.003/.003 | .131/.148/.163 | 15.896/32.087/35.126 |
| Lagarto preset | .002/.005/.006 | .066/.099/.108 | .240/.325/.437 | .065/.114/.124 | 8.414/36.156/36.198 | .525/3.904/3.982 | .025/.138/.142 | .205/.269/.280 | .002/.003/.004 | .356/.473/.528 | 9.379/40.362/40.611 |

Parse JSON (solo recetas externas; p50/p95/max, n=5 post-warmup): caballo `.567/.659/.681`, Brumacerno `.589/.708/.725`, asno `.576/.630/.639`. Los presets compilados de perro/lagarto: parse **no aplica**. Pose del perro: no aplica porque el preset tiene `CREATURE_GAIT_NONE`; esto no es medición de pose cero.

La muestra fría `rep=0` del adulto (etapa 3), en ms `creature_creation / SDF build / mesh / surface map / fur roots / pose / color / sum`, fue: caballo `.121/.054/34.174/3.528/.140/.298/.003/38.018`; Brumacerno `.167/.073/64.285/4.968/.248/.323/.004/69.741`; asno `.128/.065/26.344/2.492/.108/.287/.004/29.137`; perro `.091/.079/29.343/1.916/.087/no aplica/.002/31.516`; lagarto `.220/.101/37.028/3.848/.135/.259/.002/41.332`. El resumen por etapa conserva también las otras muestras frías.

Reuso de buffers medido aparte (adulto etapa 3; `steady_object_reuse`, n=6 incluyendo rep 0; SDF steady p50/p95/max ms, luego mesh steady p50/p95/max): caballo `.042/.043/.043 ; 33.460/35.626/35.814`; Brumacerno `.052/.060/.063 ; 63.263/65.862/66.091`; asno `.043/.053/.056 ; 24.640/25.725/25.945`; perro `.064/.065/.065 ; 28.164/28.559/28.629`; lagarto `.082/.106/.114 ; 35.139/35.380/35.409`.

Diagnóstico SDF: la fase exacta regional evaluó **32 puntos × 27 celdas = 864 llamadas por etapa/corrida**, cada una sobre el SDF completo y distribuida en una rejilla espacial 3×3×3. El CSV conserva ms/calls por celda; esa cuadrícula es una microcarga sintética, no rayos adaptativos ni tiempo de frame. En el mesh adulto post-warmup (mediana de 5 muestras), `distanceEvaluationCount / connectorCandidateCount / connectorExactEvaluationCount / connectorPrunedCount` fue caballo `43,910 / 2,371,140 / 285,640 / 2,085,500`; Brumacerno `75,657 / 4,842,048 / 489,872 / 4,352,176`; asno `34,888 / 1,883,952 / 210,646 / 1,673,306`; perro `35,270 / 3,244,840 / 382,292 / 2,862,548`; lagarto `42,116 / 5,138,152 / 369,000 / 4,769,152`. Son contadores agregados del mesher, no cantidades de conectores únicos. Etapas 0–3 están detalladas en el CSV resumen; el preset lagarto registra cero candidatos en etapas 0–1 (dato medido de esos contadores).

Memoria: la estimación `cpu_bytes` de capacidades de estructuras en la suma adulta fue caballo `2,186,808 B`, Brumacerno `4,032,552 B`, asno `1,838,836 B`, perro `1,710,572 B`, lagarto `2,627,388 B`. Una repetición separada del mismo caso bajo `/usr/bin/time -v` dio max RSS real de proceso, respectivamente `7,184 / 9,856 / 6,604 / 6,036 / 7,816 KiB`. No son métricas intercambiables: la primera omite allocator/stack y solo suma capacidades conocidas; RSS incluye el proceso completo. No se capturó memoria GPU.

Malla: todas las cifras dependen de la configuración fija `voxelSize=.18`, `maxCells=600000`, `maxResolution=320`; el renderer y el tiempo de frame no están incluidos ni disponibles en este harness headless. No se redujo resolución para completar ninguna especie.

Integridad/archivos: cada CSV primario tiene 1,014 filas, 17 columnas, `rep=0..5`, las cuatro etapas y cero `failed`; los cinco tuvieron exit status 0. El validator confirmó nulabilidad y counters scopeados en todas las filas. Artefactos (fuera del checkout): `/mnt/storage/monster-engine-agents/scratch/perf-20261106-j/`. `phase-summary.csv` SHA-256 `3d084988ccca54c9a868238040bcb70f36da837eb8437261b8f4c72bdb448bd0`; `manifest.json` SHA-256 `e01b34e1a82926c803465f830b46024951bd6fc495bd5d31e443268d536abc12`. SHA CSVs: caballo `f5f6cb04690b656fd96ed5ff0b14f94dc3740ac754b42c962d309393d5ac1a1b`; Brumacerno `333e51371c66a4a9118c3a843942debb4be6affc970b2bf8a6de0247d30d5c75`; asno `90e012fa16ca2ed1f6d17d987997dcc27da5fd1ce7e05d807c6d0f949c6ada7d`; perro `f104ab90d23ab1335ebdc1a0b403f583255e56e4bbef8034548dc8221024dcb8`; lagarto `e72e0f9e3ec4b96eaf74fbc395879ffcda79cc97110d8bc68004bf4bc100d6a3`.

## Flujo de datos / matriz de invalidación

Evidencia directa en código:

1. `CreatureRecipeIO_LoadFile` produce un documento que posee `recipe.name`; `recipes/README.md` exige mantener el documento vivo. `CreatureRecipe_EvaluateDevelopment` interpola las cuatro etapas y `CreaturePhenotype_Normalize` sanea/normaliza campos.
2. `Creature_BuildMonster` llama `Creature_ResolveAppearance`, que resuelve `AnatomyGraph`, `SurfaceMapping`, pigmento y datos de cabeza y luego instala rig/animación según gait. Es una operación de creación amplia, no una simple actualización de pose.
3. `MonsterSDF_Build` compila un snapshot del monstruo, conectores, nodos BVH, bocas/mandíbula, bounds y receta de superficie. Su header declara no seguridad concurrente para `Build` sobre una instancia mientras otra hebra la lee.
4. `SDFMesher` expone distancia, candidatos/exactos/podados, muestras, triángulos, threads y estadísticas de rango. `SurfaceMapper_MapMeshWithStats` opera sobre vértices producidos. `FurRootSet_Build` genera raíces estables del mesh; el renderer/shell pass queda fuera.
5. `MonsterAnimation_Update` está separado y rechaza anatomía que cambió sin reconfigurar rig. `Monster_SetSurface` declara actualizar apariencia sin tocar geometría/rig/SDF.
6. `MonsterSDF_CompileConnectorCandidates` compila candidatos para un AABB espacial; la jerarquía usa bounds y cotas dependientes del smoothness. No asumir que candidatos de otra caja o generación valen para un nuevo snapshot.
7. `MorphSpatialCache_UpdateDirty` actualiza solo bricks sucios, pero recompila candidatos y evalúa distancia al centro para cada brick dirty. `MorphChunkManager` marca AABBs dirty y remalla chunks; estas estructuras son de morph/remallado y no constituyen una caché global de distancias exactas.

Recomendación de claves; es inferencia de la topología observada, no una implementación aprobada:

| Cambio | Reutilizar | Invalidar/reconstruir | Prueba necesaria |
|---|---|---|---|
| Color/pigmento, sin cambios en tamaños/cutters | SDF geométrico, anatomía y conectividad de malla; atributos de material según API | `SurfaceRecipe`/atributos GPU y mapeo de pigmento si cambian dominios | Confirmar que campo geométrico no depende de la receta de superficie; comprobar bytes/tiempo de upload con renderer. |
| Parámetros de integumento/fur | SDF y mesh si no afecta envelopes | roots/tufts/paquete de fur; máscara si cambió cobertura | Diferenciar parámetros ópticos de los que alteran densidad/raíz/longitud. |
| Pose | SDF de reposo y malla canónica solo si la deformación es explícita downstream | pose palette/deformer; mouth/jaw field si el campo consultado incluye articulación | Rig fingerprint, jaw generation y consumidores de SDF posed deben ser claves separadas. |
| Proporciones fenotípicas/edad | Receta/topología estática si sigue compatible | grafo anatómico, SDF, bounds, mesh y surface correspondence afectada | comparar un mismo ID anatómico no basta: radios, centro, smoothness/cutters cambian también. |
| Topología/receta/módulos/anchors | Ninguna geometría derivada por defecto | anatomía y todo downstream; invalidar ids ausentes | digest de receta+orden+IDs y generación de documento. |
| Escala/voxel/config SDF | receta y anatomía semántica quizá | SDF bounds/candidatos; mesh por configuración/voxel | no reutilizar un mesh generado con otro voxel/config, aunque la anatomía coincida. |

Clave mínima propuesta para un futuro cache local de consultas: `(creatureInstance, recipeFingerprint, anatomyGeneration, sdfConfigFingerprint, geometryGeneration, spatialCell/AABB, queryClass)`. Un resultado exacto puntual solo es reusado para la misma posición, campo completo y generación; para regiones, la caja y el certificado conservador forman parte de la clave. Nunca trasladar valores entre puntos suponiendo Lipschitz universal ni podar cutters/smooth unions con AABB de componentes sin una cota conservadora del campo completo.

## Oportunidades priorizadas — inferencias, aún no aprobadas

| Orden | Oportunidad | Esfuerzo | Incertidumbre | Carga de prueba | Propietario sugerido |
|---|---|---:|---:|---:|---|
| 1 | Medición comparativa de cinco especies, fases, mesh/query counts y pose/material | completada; CPU headless | baja | baja | Agent J; resultados en esta nota y scratch |
| 2 | Separar el coste interno de `SDFMesher` (muestreo, clasificación, polygonization): el mesh CPU fue la fase dominante medida en las cinco fuentes, mientras `MonsterSDF_Build` fue ≤0.124 ms de máximo en la matriz agregada | medio | media | alta: topología, bounds y equivalencia de mesh | propietario de `SDFMesher`, solo tras profiling interno |
| 3 | Evaluar si el reuso de scratch del `SDFMesher` merece optimización: la ruta `_steady` sigue costando 24.6–63.3 ms p50 en mesh adulto, sin evidencia aquí de beneficio atribuible al reuso | medio | media | alta: salida/topología idéntica y comparación controlada | propietario de `SDFMesher` |
| 4 | Aprovechar dirty chunks/bricks ya existentes en morph para age updates compatibles | alto | alta | alta: geometría swept conservadora, costuras, fallback total | propietario MorphChunkManager tras benchmark |
| 5 | Cachear candidates por caja/generación dentro de trabajo local repetido | bajo/medio | media | media: semántica de smooth-min/cutters y generación | propietario SDF/MorphSpatialCache |
| 6 | Reutilizar resultados puntuales SDF entre puntos cercanos | no recomendar sin nueva prueba | alta | muy alta | no asignar todavía |

En particular, el nodo BVH de conectores aplica `lowerBoundScale` solo en el camino axial con pruning habilitado, y mezcla upper ranges con smoothness; eso no prueba que una AABB ordinaria pueda excluir de forma segura una contribución de campo para cualquier unión/cutter. Una propuesta debe derivar cotas para la composición completa y caer a evaluación exacta ante UNKNOWN.

## Diferenciales/limitaciones y handoff

- Sí hay baseline CPU frío/post-warmup y reuso steady para cinco fuentes bajo una configuración y máquina concretas; no hay comparación de render FPS, GPU ni una base histórica comparable de la misma configuración. No inferir tendencia temporal ni generalizar a otras resoluciones.
- El benchmark genérico registra fases separadas y la reconstrucción entre etapas, pero `creature_creation` ya incluye anatomía/rig; no sumar esa fase con anatomía para inventar un total aditivo. `growth_update_build` es construcción intermedia y no equivale a actualización async real.
- `long_hair_rebuild` permanece explícitamente no perfilado dentro del harness genérico: medirlo requiere ruta adulta exacta del caballo + mandíbula y no confundirla con `rootFullness=0`.
- El CSV no exporta `SDFMesherStats.cellRangeQueryCount` ni histograma de memoria allocator en esta primera versión; se conservan counts de distancia/candidatos. Ampliar el schema en una integración posterior si Agent A o la integración requieren campos de intervalos. No se cambia `Makefile` para registrar targets compartidos.
- Los límites pendientes son end-to-end CPU contiguo, subfases de `LongHairSet_Build`, actualización morph/async real y mediciones GPU/render; `sum_of_profiled_cpu_phases` no los sustituye. No se ejecutaron suite completa, validación visual ni benchmark GPU en esta tarea. Un siguiente paso de optimización debe perfilar internamente la fase de meshing dominante, mantener comparación de malla equivalente y recoger evidencia visual antes de proponer cambios runtime.
