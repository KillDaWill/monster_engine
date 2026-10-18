# Mutación continua y rendimiento de Game Scene

Fecha: 6 de octubre de 2026.

La optimización posterior de IK y contacto, el límite de 60 FPS y la detección de órbitas se documentan en [GAME_SCENE_AGER_PACING.md](GAME_SCENE_AGER_PACING.md), con comparaciones sobre variantes completas.

> Actualización del 7 de octubre: esta medición es histórica. La construcción con la receta base omitía los accesorios añadidos por algunas variantes. La reparación y los perfiles con módulos completos están en [GAME_SCENE_GROWTH_CONTINUITY.md](GAME_SCENE_GROWTH_CONTINUITY.md); no deben usarse las cifras siguientes para certificar 30 FPS con todos los accesorios.

La tecla **T** selecciona otra variante aleatoria y cambia su anatomía y superficie mediante la edad continua de `MonsterAger`. El lagarto se representa durante toda la transición con la superficie dispersa GPU; la geometría y las escamas avanzan juntas, sin sustituir una malla al terminar. La animación conserva fase, navegación y apoyos compatibles al actualizar el rig.

## Costes encontrados y cambios

- Los halos de bricks vecinos repetían evaluaciones del mismo punto SDF. Ahora cada esquina global se reclama una sola vez por generación; todos los bricks comparten su valor. Las páginas de muestras sólo cubren el trabajo solicitado, manteniendo la retícula global fija.
- La limpieza de aristas recorría toda la tabla hash cada fotograma. Ahora se liberan mediante contadores de referencias y una cola de aristas retiradas. La auditoría compara estos contadores con los índices realmente publicados.
- Los bricks totalmente interiores o exteriores ejecutaban muestreo y clasificación. Los intervalos conservadores certifican esos casos; las regiones inciertas conservan la evaluación completa, incluido el halo de normales y la mandíbula.
- Cada fragmento repetía el SDF de color y la búsqueda del dominio anatómico. El color semántico se conserva por vértice y se refresca al cambiar la paleta; la búsqueda de dominio se comparte con la pose. Las escamas siguen siendo procedurales por fragmento.
- Ager construía un segundo snapshot para el mallador CPU, aunque la demo GPU no lo consumía. La nueva opción permite omitir esa copia manteniendo la edad continua. Otros consumidores conservan el comportamiento anterior por defecto.
- El deformador CPU repetía influencias del mismo joint. Se compactan al crear el binding, evitando transformaciones redundantes y reutilizando la normalización.

No se redujeron la resolución SDF, el MSAA, el pelaje, los detalles anatómicos ni la frecuencia de actualización visible. La duración interactiva sigue siendo de tres segundos reales.

## Medición

AMD Radeon integrada Renoir, Mesa 25.2.8; 1200 × 850, MSAA 4, pelaje del perro activo, voxel SDF 0,075. Las pruebas se ejecutaron secuencialmente, sin otra demo ni pruebas CPU en paralelo. Cada variante parte de Emerald Climber y usa semilla 42. Se incluyen los fotogramas 30–210: inicio y final de la mutación, 181 fotogramas por variante. La simulación headless usa pasos de 1/60 para recorrer edades reproducibles; se mide el tiempo real completo del fotograma.

La referencia anterior registró **56,57 ms** medios por fotograma y **68,96 ms** en p95 en la mutación a gecko (fotogramas 35–208). El coste GPU era 52,93 ms, con 26,24 ms de muestreo y 16,08 ms de renderizado. La comparación usa el mismo hardware y calidad; los CSV conservan cada muestra.

| Destino | Media ms | FPS medios | p95 ms | p99 ms | Máximo ms | Fotogramas > 33,33 ms |
|---|---:|---:|---:|---:|---:|---:|
| Emerald Climber | 19.32 | 51.8 | 22.06 | 22.34 | 22.57 | 0 / 181 |
| Desert Horned | 25.32 | 39.5 | 31.10 | 32.54 | 33.62 | 2 / 181 |
| Blue Runner | 22.99 | 43.5 | 28.68 | 29.43 | 30.18 | 0 / 181 |
| Fat-Tailed Gecko | 25.68 | 38.9 | 32.44 | 33.51 | 34.52 | 3 / 181 |
| Volcanic Spiny | 23.43 | 42.7 | 29.88 | 30.85 | 31.66 | 0 / 181 |
| Sand Burrower | 18.85 | 53.1 | 23.35 | 24.16 | 25.06 | 0 / 181 |
| Stone Armored | 24.59 | 40.7 | 30.10 | 31.92 | 33.25 | 0 / 181 |
| Sailback | 20.73 | 48.2 | 24.46 | 24.93 | 25.50 | 0 / 181 |
| Jewel Chameleon | 23.78 | 42.1 | 29.30 | 29.57 | 30.13 | 0 / 181 |
| Aquatic Fin-Tail | 20.77 | 48.2 | 24.41 | 27.39 | 28.21 | 0 / 181 |

Las diez variantes cumplen p95 < 33,33 ms. Hubo cinco picos por encima de ese presupuesto entre 1.810 fotogramas; el máximo fue 34,52 ms. Por tanto, el objetivo de 30 FPS se sostiene en la distribución medida, con pequeños picos aislados; no se garantiza un máximo estricto en cada fotograma ni en otras GPU o encuadres.

La memoria de buffers GPU registrada es 261,0–261,3 MiB. Antes de compactar las páginas, la versión con muestreo compartido reservaba 325,4 MiB. Estas cifras representan buffers de superficie y caché, no el consumo total del proceso/driver. CPU de anatomía/animación y certificación sigue siendo el mayor coste pendiente; la extracción GPU ya queda aproximadamente entre 11 y 15 ms medios según variante.

## Evidencia de corrección

- `make -B -j6 test demos`: recompilación completa; todas las suites pasan, sin avisos del compilador.
- Diez variantes, 101 edades cada una: grafo, rig y SDF válidos (`variants.log`).
- Paridad GPU/CPU en edades inicial, intermedias y final, más muestras de los halos: error máximo inferior a 0,000001 (`paged.log`).
- A/B con y sin certificación de bricks vacíos: firmas de triángulos, posiciones y normales **exactamente iguales** (`paged-signature.json` y `paged-reference-signature.json`).
- Auditorías GPU: cero overflow, cero errores del asignador y cero errores de referencias de aristas. Los valores compartidos son coherentes.
- Comparación de material con la ruta exacta por píxel: error RGB medio de 0,0675/255 en el recorte del lagarto; las diferencias son locales a la interpolación de atributos de vértice. Se conserva el modo de referencia `MONSTER_GPU_EXACT_MATERIAL=1`. Ver `material-comparison.png`.
- Capturas de seis edades en `paged-*.ppm`, con cuerpo, dedos y escamas visibles. Las lecturas de diagnóstico se ejecutan aparte del benchmark porque añaden esperas GPU.

## Regresión del renderer compartido

La prueba regional perro → lagarto se ejecutó en edades 0, 0,5 y 1, vista 3 y voxel 0,075, con ida y vuelta de historia. La igualdad de historia, retícula, aristas, profundidad y normales pasa en las tres edades. La comparación estricta contra raymarch exacto **no pasa** sus umbrales regionales geométricos/de apariencia a esta resolución; el muestreo grueso de detalles pequeños limita esa prueba. No se relajaron los umbrales para declarar éxito.

Para comprobar si la optimización introducía pérdida, se comparó además con la extracción GPU completa sobre la misma retícula, en las tres edades y con capturas planas, de normales y de material: **cero diferencias** de silueta, profundidad, normales o RGB en las nueve comparaciones. Esto certifica la equivalencia entre ambas extracciones en esos casos, no una igualdad universal entre marching cubes y raymarch. Evidencia: `renderer-regression/metrics.json`, `dense-sparse-comparison.json` y sus capturas.

## Mutaciones consecutivas

También se ejecutaron 930 fotogramas y cuatro cambios aleatorios: Emerald Climber → Stone Armored → Jewel Chameleon → Volcanic Spiny → Blue Runner. Los tres últimos ciclos, sin lecturas de diagnóstico intermedias, registraron 23.65 ms medios, p95 28.84 ms y un máximo de 33.70 ms en 537 fotogramas activos. El consumo permaneció en 261,2 MiB y la auditoría final no encontró overflow, errores de asignación, referencias de aristas ni valores compartidos inconsistentes. Ver `performance-cycles-summary.json` y `final-cycles.log`.

## Reproducción

```sh
make -B -j6 test demos
./demos/demo_game_scene
./demos/demo_game_scene --headless --frames=240 --seed=42 --target=3 --profile=/tmp/game-gecko.csv
./demos/demo_game_scene --headless --frames=240 --seed=42 --target=2 --validate-gpu --capture-prefix=/tmp/game-qa
MONSTER_SPARSE_NO_RANGE_CERTIFICATION=1 ./demos/demo_game_scene --headless --frames=240 --seed=42 --target=2 --validate-gpu --capture-prefix=/tmp/game-reference
```

Los perfiles finales y los resúmenes están en `docs/validation/game-scene/perf-final-0.csv` … `perf-final-9.csv` y `performance-final.json`.
