# Calidad visual de terreno y agua

La línea base es `b464d2bd93f6e7318a1473ed2afae9d5c980dc6d`. Se verificó que coincidía con la punta de `main` en GitHub. Los cambios se encuentran en el árbol de trabajo sobre esa revisión.

## Causas reproducidas

- El terreno original usaba normales suaves del campo de alturas. Su shader construía el color con ruido y modulaba el brillo sin incorporar un mapa de normales. Las capturas cercanas de River Valley mostraron superficies lisas incluso donde la malla tenía suficiente resolución.
- El agua original remapeaba cada vértice únicamente por índice de rejilla y rellenaba los vértices secos con la primera media del cuádruple. La consulta física v1 normalizaba los pesos de los vértices húmedos y usaba otra condición de profundidad. En River Valley 42, la auditoría CPU del rasterizado original sobre LOD0 encontró 5.376 falsos húmedos y 32 falsos secos en 284.515 sondas de celdas con al menos un vértice húmedo. Encontró 619 celdas con más de un cuerpo y un error máximo de cota de 0,209436 unidades entre sondas húmedas comunes. Se usan la diagonal original, los vértices originales y el criterio de descarte y profundidad de la pasada antigua.
- El perfil circular v1 terminaba el interior por debajo de la superficie y empezaba la ribera por encima de ella. Resolvía el corte, la cota y el flujo con criterios distintos. Las capturas a ras de suelo también reprodujeron bandas repetidas de reflexión. La pasada v2 usa el mismo plano físico para ambos lados del contorno y una normal geométrica suavizada por cuerpo para la iluminación.

## Materiales y agua

`TerrainMaterials` carga los tres arrays locales de `assets/terrain/` una vez por instancia del renderizador compartido. Las cuatro capas de 512² son hierba, suelo seco con partículas minerales, roca fracturada y limo húmedo. El albedo sRGB tiene cuatro canales y los arrays lineales RG8 contienen normales XY y rugosidad/AO. Una quinta capa de normales sirve para el agua. Con toda la cadena de mipmaps son **11.883.850 bytes (11,33 MiB)**. Las texturas repiten en unidades del mundo, usan filtrado trilineal y hasta 4× anisotropía. La carga fallida activa colores simplificados y deja un mensaje en stderr.

El manifiesto registra el tamaño de baldosa, el formato, SHA-256 y el origen reproducible. Los mapas son generados con semilla mediante `tools/generate_terrain_materials.py` y se distribuyen bajo [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/). El script de regeneración usa NumPy y SciPy. El ejecutable carga directamente los binarios incluidos. Las referencias técnicas aportaron principios de diseño y los activos son originales.

El terreno mezcla los pesos de `WorldBiome` y aplica detalle a las normales mediante gradientes de superficie. Las rocas reutilizan el material triplanar y las normales de las variantes SDF con corrección de escala. La variación de color a escala de decenas de unidades y la reducción del detalle por huella de píxel evitan que la baldosa domine el panorama. Baja calidad usa albedo y luz simplificada. Media conserva mapas de normales y máscaras, con transición suave hacia las capas de más peso. Alta admite más capas y un umbral LOD más estricto. Los recursos vegetales se representan con 36 triángulos de hojas usando los identificadores, escalas y biomasa existentes.

Las dos capas de onda se advectan con fases cruzadas y velocidades de río o viento de lago. El período común es de 80 segundos de presentación. La pausa congela las ondas. Las capturas pueden fijar el tiempo independientemente de la velocidad de simulación. La reflectancia de Fresnel parte de 0,02 y la luz solar tiene intensidad acotada. El agua usa absorción espectral aproximada por recorrido, un cielo hemisférico analítico común y una sola composición alfa. Un fondo atmosférico aparece gradualmente en cámaras en perspectiva cercanas al horizonte. La animación modifica las normales de iluminación sobre una malla estática.

## Migración física y contorno v2

El esquema JSON sigue en `version: 1`. El campo `generator_version` selecciona la física. Las recetas v1 y el entorno original de Lab son reproducibles y conservan su consulta histórica. La representación renderizada de esos mundos también usa el constructor de compatibilidad, con la óptica nueva. El contorno de concordancia exacta requiere v2.

Los presets `river_valley_v2`, `mountain_lakes_v2`, `rocky_highlands_v2`, `wetlands_v2` y `lab_valley_small_v2` son una selección explícita. Para migrar una receta propia, copiarla, asignarle un nuevo `id` y cambiar la versión a 2. Comparar huellas, estaciones accesibles y recursos antes de usar el mundo para evaluar un entrenamiento previo. El caché incluye el generador en la identidad. Los checkpoints de Lab validan la versión del mundo y su huella. La migración física requiere un nuevo entrenamiento o una nueva evaluación con un checkpoint correspondiente al entorno v2.

Priority-Flood conserva el orden de drenaje. Los nuevos enlaces usan desplazamientos locales de hasta un cuarto de celda en nodos interiores y anclan las fuentes, juntas y salidas. Las cotas se suavizan en dos pasadas acotadas y se imponen en orden monótono desde las salidas. El caudal define el ancho, la profundidad y la dirección. El rasterizado visita el AABB de cada segmento con alcance en unidades físicas. La misma elección de segmento aporta cota, lecho y flujo. El perfil sumergido y la transición de ribera son continuos, con relleno local que se reduce hasta cero en el borde y ribera de corte. Los lagos mantienen la altura de vertido y un filtro acotado ajusta el lecho somero en pendientes moderadas.

El recorte se hace con dos restricciones lineales en los triángulos originales. El dominio pertenece a un cuerpo y la profundidad firmada debe ser mayor que 0,01 unidades. El lago siempre usa el mismo plano. En los límites entre cuerpos se recorta cada dominio con su propia cota. El búfer de agua se construye por huella y época, con vértices compartidos por posición y cuerpo. La consulta física, el recorte, el picking y el overlay de orilla usan este campo. El minimapa consulta el mismo predicado. La humectación visual del suelo se interpola a partir de las muestras de terreno y los pesos de bioma. La extracción de estaciones parte de aristas continuas y valida pendiente, calado, línea de visión y espacio contra rocas.

Se conservan los chunks de 32 celdas y los búferes compartidos de índices. El error se mide contra todos los vértices del terreno en cada nivel de la diagonal autoritativa y se proyecta con la matriz y el viewport efectivos. La histéresis, el límite de diferencia vecinal y las máscaras de costura controlan el cambio de nivel. En v2 se protegen los chunks que cruzan una orilla, los bordes de poca profundidad y la zona de contacto cercana a la cámara. La malla física, las normales del suelo y la consulta de alturas siguen en el núcleo C.

## Reproducción

```sh
# En el checkout v1
make -j8 demos/demo_evolution_arena
mkdir -p build/terrain-water/before
cp demos/demo_evolution_arena build/terrain-water/before/
# En el checkout de la revisión visual, con el ejecutable v1 a mano
make -j8 demos/demo_evolution_arena demos/demo_brain_evolution_lab build/audit_world_surface
build/audit_world_surface worlds/river_valley.world.json
build/audit_world_surface worlds/river_valley_v2.world.json
python3 tools/validate_procedural_world.py --visual-only --surface-suite \
  --baseline-arena=build/terrain-water/before/demo_evolution_arena \
  --out=build/terrain-water/final-captures

demos/demo_evolution_arena --world=worlds/river_valley_v2.world.json --world-seed=42 --seed=42 \
  --brumacernos=400 --velkares=100 --plants=700 --capacity=640 --workers=6 --speed=4 \
  --quality=medium --frames=420 --realtime --view=three-quarter --zoom=160 \
  --show-perf --minimap --width=1920 --height=1080 --profile=build/terrain-water/frame-profile.json
```

En la comparación pareada se captura el primer tick con la cámara ortográfica fija y el mismo mundo y nivel de calidad. La serie física v2 se guarda por separado. La suite incluye tomas en perspectiva, MSAA 4×, capturas de fase a ambos lados de 4 s y 80 s, alambre en LOD automático y LOD0, una confluencia de Wetlands 707, Arena y la repetición del campeón de Lab.

## Mediciones

Equipo de referencia con Ryzen 5 5600H, **RTX 3050 Laptop de 4 GiB**, NVIDIA 580.178.04, OpenGL 4.6 de compatibilidad y shaders GLSL 330. Para la tabla de Arena se usaron 1920×1080, VSync desactivado, MSAA 0, cámara ortográfica, calidad media, River Valley semilla 42, seis workers, 420 frames y una solicitud de 4×. El FPS se calcula con el tiempo de pared medio por frame y excluye el precalentamiento de anatomía.

| Arena, vista general | Población inicial → final | P50 / P95 / P99 de frame, ms | FPS medio | Velocidad real | GPU total media, ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| Línea base v1 | 500 → 488 | 8,48 / 9,69 / 14,19 | 116,1 | 4,014× | 6,012 |
| Render actualizado, mismo v1 | 500 → 488 | 8,88 / 9,35 / 9,58 | 110,3 | 3,996× | 6,460 |
| Render actualizado, física v2 | 500 → 490 | 9,60 / 10,04 / 10,37 | 102,7 | 3,989× | 7,144 |

En las repeticiones de referencia con 500 criaturas, el P95 de v1 base osciló entre 8,86 y 11,59 ms. Una repetición pareada anterior al último ajuste longitudinal pasó de 11,59 a 12,27 ms en el render v1, un 5,9 % de aumento. Los P50 de la tabla final aumentaron un 4,8 % para v1 y un 13,3 % en la comparación física v2. El primer lote de perfiles, anterior al ajuste de LOD y materiales, alcanzó 12,76 ms de P95 para v1 actualizado. Se conservan los resultados sin promediar ejecuciones con condiciones distintas.

Las pasadas v2 de terreno, agua y roca usaron **2,037 / 0,517 / 0,053 ms GPU** en el panorama final. Usaron **0,202 / 0,012 / 0,042 ms CPU** de media para la entrega de comandos. Se recogieron 416 muestras de GPU mediante pares de timestamps con comprobación de disponibilidad y tres frames de retardo. El render CPU completo tuvo P50/P95/P99 de 1,75/2,16/2,62 ms. Los percentiles GPU totales fueron 7,13/7,41/7,62 ms. La línea base proporcionó la media GPU total.

El entorno v2 dibujó 491.080 triángulos y 266 llamadas en el último frame. El render v1 actualizado dibujó 292.166 y 266. El entorno v2 reservó 35.972.854 bytes GPU, incluidos 11.883.850 de materiales. La sincronización inicial del entorno v2 llevó 130,6 ms y el precalentamiento de la geometría de criaturas llevó 2,90 s. El pico de RSS del proceso fue 260.720 KiB. El presupuesto incluye los recursos del entorno y se midió aparte de la memoria GPU de las criaturas.

| Render a 4×, pruebas de escala | P95 v1 base, ms | P95 v1 actualizado, ms | P95 v2, ms |
| --- | ---: | ---: | ---: |
| 22 iniciales | 8,56 | 8,76 | 9,32 |
| 100 iniciales | 7,98 | 7,19 | 7,89 |
| 1.000 iniciales | 13,97 | 14,55 | 14,85 |

La vista de orilla con zoom 14 y giro de cámara de 0,25 rad/s sostuvo 500 criaturas hasta fin de la muestra a 1×. Su P95 fue 7,85 ms. A 16× y máximo solicitados, registró 9,18× y 9,40× reales con P95 de 8,30 y 8,77 ms, y acabó con 494 criaturas. Son pruebas de carga de la versión previa al filtro longitudinal final. Los datos de perfil guardan el número de criaturas, la velocidad realmente alcanzada y los ticks por segundo.

| Headless, River Valley, Molecular v2 | V1 media / P95, ms por tick | V2 media / P95, ms por tick | Población sostenida |
| --- | ---: | ---: | ---: |
| 22 | 0,118 / 0,283 | 0,081 / 0,082 | 22 |
| 100 | 0,317 / 0,602 | 0,407 / 0,691 | 100 |
| 500 | 1,432 / 2,258 | 1,516 / 2,266 | 500 |
| 1.000 | 3,262 / 4,792 | 3,178 / 4,733 | 1.000 |

A 500 criaturas la media headless v2 fue un 5,9 % mayor, con P95 prácticamente igual. El caso de 100 criaturas tuvo un aumento del 28 % en la media y distinto trabajo de navegación por la física v2. River Valley 513² tardó 256,62 ms y usó 17,43 MiB estáticos en v1, frente a 281,98 ms y 17,93 MiB en v2. La prueba 1025² de la misma extensión ocupó 66,84 MiB estáticos y tardó 935 ms en v2. La resolución de los presets estándar se mantiene en 513² para los mundos de 256 unidades. El caché caliente del Lab con seis workers registró 141 aciertos, cinco fallos y 216,5 episodios/s en la prueba de referencia.

## Archivos y alcance

- Materiales y filtros en `assets/terrain/`, `src/TerrainMaterials.c`, `include/TerrainMaterials.h`, `tools/generate_terrain_materials.py`, `tools/embed_environment_shaders.py` y `shaders/environment/`.
- Relieve y dominio físico en `src/WorldHydrology.c`, `include/WorldHydrology.h`, `src/WorldWaterSurface.c`, `src/WorldIO.c` e `include/WorldIO.h`. Los nuevos archivos `worlds/*_v2.world.json` y `environments/brumacerno_river_valley_v2.environment.json` exponen la migración.
- Render compartido y diagnóstico en `src/EnvironmentRenderer.c`, `src/TerrainRenderer.c`, `src/TerrainLodIndices.c`, `src/WaterRenderer.c`, `src/RockVisualCache.c`, sus cabeceras y ambos demos. `PrimitiveMesh_GeneratePlantTuft` da geometría visual a recursos existentes.
- Auditoría y regresiones en `tools/audit_world_surface.c`, `tools/validate_procedural_world.py`, `tests/test_terrain_heightfield.c`, `tests/test_world_hydrology.c`, `tests/test_world_snapshot.c`, `tests/test_world_water_access.c`, `benchmarks/benchmark_world.c` y `Makefile`.

La pasada de agua utiliza una malla estática en una sola llamada de superficie. Quedan fuera la refracción con copia de escena y la vegetación decorativa independiente. En ángulos rasantes todavía puede leerse una cresta transversal en cambios rápidos de pendiente del río. La geometría compatible v1 conserva el límite histórico aproximado. El P95 de escala depende de la combinación de cámara, población y velocidad de simulación. Los resultados cubren la GPU indicada y las resoluciones medidas.

## Pruebas y capturas

Pasaron `make test`, `make validate-procedural-world`, `make validate-ecosystem`, `make world-sanitize`, `make world-tsan`, `make benchmark-world`, `make benchmark-molecular-evolution` y `make benchmark-evolution-scalability`. Las pruebas añadidas cubren 50.000 sondas de ocupación y altura más un caso mixto de lagos, el plano lacustre, la arista compartida exacta, el picking v2, 96 combinaciones de costura y el libro finito de agua para ambos generadores. Lab prueba determinismo entre trabajadores y restauración de checkpoints con huellas compatibles.

A 120 ticks y semilla 42, la línea base v1, headless actualizado y Arena en calidad baja y alta dieron el mismo hash de eventos `77fefaaf3cc537b4` y huella de mundo `78d9cc6b5e01bdde`. Headless y Arena baja/alta también coincidieron en v2 con hash de eventos `3dc3d76e40242214` y huella `cd3c91a569dcc3ba`. El área mojada v2 es 20,886 %, con 18 lagos, 11 redes fluviales, 382 segmentos, 3.099,694 unidades de contorno y 223 estaciones accesibles. El caso de confluencia corresponde a Wetlands 707.

Evolution Arena capturó agua en ambos tipos de proyección, con MSAA 0 y 4× y auditoría de errores GL habilitada en las tomas de referencia. La prueba sin archivos de material imprimió el diagnóstico y produjo una captura válida. En la serie fija de flujo, el cambio medio entre 3,99 y 4,01 s fue 0,052 niveles de 8 bits por canal y píxel de la imagen completa. Entre 79,99 y 80,01 s fue 0,071. Los tres intervalos ordinarios de aproximadamente 0,33 s dieron entre 0,572 y 0,716 niveles. El resultado cubre esa cámara y esas fases.

Las capturas de bebida usan `ACTION_DRINK` y `REACH_REACHED` en el mundo pequeño v2. Se midió la cota en la proyección horizontal del centro de la boca física y en el punto de orilla objetivo.

| Captura | Agua bajo la boca | Boca sobre la cota local, unidades | Cuerpo objetivo |
| --- | --- | ---: | ---: |
| Brumacerno adulto | Sí | 0,0071 | 7 |
| Velkar adulto | Sí | 0,0054 | 6 |
| Brumacerno juvenil | Suelo seco en la muestra | — | 1 |
| Velkar juvenil | Sí | 0,0270 | 6 |

El Brumacerno juvenil alcanzó el objetivo húmedo dentro de la tolerancia del sistema de alcance. La imagen conserva esa discrepancia. La transferencia molecular real y el agotamiento del agua se comprueban con una prueba de ticks sobre el lago finito. Los máximos de error de apoyo registrados en las secuencias de captura fueron 0,014–0,329 unidades. Esos máximos abarcan las poses registradas durante cada secuencia y dejan pendiente una validación exhaustiva de los pies en ambas riberas y todas las etapas.

- [Comparación v1 pareada y miniaturas de v2](validation/terrain-water/comparacion-pareada.png)
- [Meandro, confluencia, lago, criaturas, LOD y Lab](validation/terrain-water/evidencia-v2.png)

Los PNG originales y perfiles se guardan en `build/terrain-water/`. El archivo de evidencia reúne las comparaciones, las fases de agua, las capturas de acción y las mediciones.
