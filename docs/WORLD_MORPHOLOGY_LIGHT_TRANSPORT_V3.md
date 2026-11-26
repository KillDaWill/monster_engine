# Transporte de luz y morfología hidrológica v3

Este informe documenta una revisión anterior. Los comandos y las recetas con sufijo corresponden a esa revisión. Las recetas actuales se describen en la [guía de mundos procedurales](PROCEDURAL_WORLD.md).

La revisión partió de `b71d955d068005b2c450109dfd8916b0d714ca11`, verificado contra `origin/main` y `refs/heads/main` del remoto. La presentación actual se puede usar con física plana, v1, v2 o v3. Las recetas v3 introducen cambios físicos y producen una huella distinta. La evidencia de esta ejecución está en `build/v3-report/`. Las cifras de este informe corresponden a esa revisión. La corrección posterior de las uniones río–lago se documenta en [Hidrología](WORLD_HYDROLOGY.md#reparación-de-taludes-excavados-en-las-uniones-v3).

## Auditoría y método

El archivo local `terrain-biomes-proof.zip` contiene la serie de huellas y el JSON independiente de River Valley v2. El comando poblacional de `validation_summary.txt` se refiere al archivo v1. En el JSON v2 hay 18 lagos, 11 redes, 223 sitios, 67 rocas, fracción mojada de 0,2089 y 46 supervivientes después de 3.600 ticks. Sus 104 fallos de navegación y 127 de acción son contadores agregados. Los dos PNG etiquetados como primeros planos de Velkar y Brumacerno tienen SHA-256 `369b7b63db50f3ebaba398f170898d901028215eda428a18af86b216664f9a01`. La imagen ancha de Velkar sí muestra el animal, con sombras solares y poca separación tonal en las zonas oscuras. La imagen duplicada contiene terreno y hierba.

Antes de editar se guardaron los ejecutables Arena GL y headless. El bloque de iluminación y la repetición de profundidad de `b71d955` ya compartían dirección solar. Se revisaron las rutas de color por material, la proyección de sombras y la captura de sujeto. Se mantuvo la composición sRGB. El cambio concentra el trabajo de entorno al sincronizar el mundo y la oclusión corporal al generar la malla compartida. La profundidad simplificada sólo afecta a las instancias. Los cambios de material y luz actúan con las mismas recetas y estados de simulación.

La prueba GL conserva el gris lineal de 0,18 en 118 sRGB, el valor de mezcla al 50 % en 202 y la visibilidad solar 0/255. La muestra sRGB decodificada volvió a 118. La muestra de irradiancia de cielo dio 191 con AO 1 y 162 con AO 0,7. La contribución directa del fixture se conservó con ambas oclusiones. Las pruebas se ejecutaron con MSAA 0 y 4, dos vistas ortográficas o perspectivas según el caso y ambos tipos de destino. La comprobación de textura normal rotada y proyección negativa dio 255/255.

## Diagnóstico de las bandas en el terreno

En una toma v3 pareada de orilla, el patrón se reprodujo en la visualización de visibilidad y desapareció con sombras desactivadas. También estaba ausente en los diagnósticos de albedo y normal con LOD0. Era auto-sombreado de la ladera por comparar varios texels contra una sola profundidad. El nuevo receptor de terreno evalúa la profundidad de su plano en cada centro de texel y pondera cuatro comparaciones. El cambio conserva el mapa solar y las pruebas comunes para objetos y agua.

La región seca de la toma controlada (columnas 30–289 y filas 60–419) pasó de media 225,46 y desviación 23,06 en el canal de visibilidad a 255,00 y desviación 0,00. La huella y el hash del tick 1 fueron iguales en las tres variantes, con sombra anterior, corregida y apagada. El PNG `shadow-comparison/contact-sheet.png` muestra también el compuesto con sombras de vegetación.

## Morfología medida

Se generaron cinco pares de recetas con 16 semillas por par, incluidas 42, 108, 317 y 707. Cada casilla es la mediana entre semillas. La curvatura es el laplaciano absoluto del terreno seco en unidades de altura por unidad de distancia al cuadrado. Las bandas usan el campo de distancia al agua del bioma. La pendiente de rasante raster es el P95 absoluto entre muestras mojadas del mismo río, aproximadamente alineadas con el vector de flujo. La métrica incluye efectos de mezcla en el triángulo final. Los perfiles del grafo miden aristas de la línea central y se exportan por separado.

| Receta | Generación v2 → v3 (ms) | Mojado v2 → v3 | Sitios v2 → v3 | Curvatura 1–3 m v2 → v3 | Curvatura 3–8 m v2 → v3 | P95 rasante raster v2 → v3 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| River Valley | 295 → 358 | 22,60 % → 18,22 % | 200 → 250 | 0,161 → 0,117 | 0,157 → 0,108 | 0,196 → 0,078 |
| Mountain Lakes | 320 → 391 | 29,07 % → 22,44 % | 205 → 313 | 0,420 → 0,246 | 0,425 → 0,324 | 0,433 → 0,118 |
| Rocky Highlands | 329 → 413 | 27,32 % → 27,30 % | 162 → 347 | 0,587 → 0,460 | 0,560 → 0,607 | 0,226 → 0,127 |
| Wetlands | 164 → 213 | 19,02 % → 14,90 % | 168 → 160 | 0,067 → 0,107 | 0,057 → 0,046 | 0,115 → 0,045 |
| Lab Valley | 23,3 → 29,3 | 21,46 % → 11,61 % | 56 → 49 | 0,391 → 0,140 | 0,397 → 0,175 | 0,286 → 0,078 |

En River Valley la mejora de curvatura es del 27 % en 1–3 m y del 31 % en 3–8 m. Lab Valley cumple el objetivo exploratorio de 30 % en ambas franjas y pierde una parte importante de área húmeda. Wetlands seed 707 conserva dos lagos mayores con 22,79 % de superficie mojada, frente a nueve lagos y 20,82 % en v2. La mediana de Wetlands todavía pierde área mojada y empeora la rugosidad de la franja interior, a pesar del talweg más regular. Highlands conserva crestas fuertes y crece la rugosidad a 3–8 m de la orilla. El ensayo 1025² de River Valley tardó 985 ms en v2 y 1.397 ms en v3.

River Valley seed 42 conserva 11 redes fluviales. Los lagos pasan de 18 a 13, los sitios de 223 a 287 y la fracción transitable genérica de 0,7583 a 0,8261. El P95 de rasante de las aristas centrales trazadas cae de 0,208 a 0,088. En Mountain Lakes seed 108 el P95 cae de 0,568 a 0,118 y permanece una arista de hasta 0,362. La proyección descendente se verificó en los grafos exportados. Algunas diferencias de cota raster en la dirección aproximada del flujo siguen siendo ascendentes. El JSON incluye esos conteos y los perfiles originales para distinguirlos del grafo.

`morphology-final/metrics.json` recoge también volúmenes de corte y relleno, conteos de cuerpos, áreas mojadas, área de bancos empinados y percentiles P50/P95/P99/máximo. Los PNG y CSV incluyen DEM antes y después de tallar, cambios de relajación, rasantes, perfil transversal, profundidad firmada, acumulación y segmento de referencia. El diagnóstico del perfil omite enlaces a lago y bordes sin un nodo fluvial aguas abajo. Faltan el segundo candidato de segmento, la jerarquía de depresiones y el balance de recursos por componente navegable. La segunda ejecución del barrido (`morphology-release/metrics.json`) incluye conectividad del grid genérico con pendiente ≤ 0,65 rad, vadeo ≤ 0,35 y holgura ≥ 0,85. Las medianas de componentes fueron 21 → 6,5 en River Valley, 12,5 → 1 en Lab y 733,5 → 434,5 en Mountain Lakes. Highlands pasó de 905,5 a 1.100 componentes. La proporción de celdas transitables en componentes con alguna estación pasó del 64,6 % al 87,8 % en Mountain Lakes y del 54,1 % al 60,4 % en Highlands. Es una prueba de conectividad geométrica, con las rocas incluidas.

## Compatibilidad y ecología

| River Valley seed 42, 120 ticks | Huella física | Hash de eventos, 1 y 6 workers |
| --- | --- | --- |
| v1 | `78d9cc6b5e01bdde` | `77fefaaf3cc537b4` |
| v2 | `cd3c91a569dcc3ba` | `3dc3d76e40242214` |
| v3 | `2fbdc4ae9744f188` | `308b926974a8db82` |

Se repitieron las huellas históricas de las recetas planas, v1 y v2. Los hashes anteriores de v1 y v2 coinciden con el informe histórico. En los ensayos de 50 fundadores y 3.600 ticks sobrevivieron 46/45 individuos en v2/v3 legacy y 49/50 en v2/v3 molecular. La comparación combina relieve y recursos recolocados. Los resultados de acción y navegación no identifican por sí solos si falló el acceso, el controlador o el contacto. Los fallos de navegación fueron 104 → 65 en legacy y 56 → 182 en molecular. Los de acción fueron 127 → 91 y 95 → 205 respectivamente.

El replay GL de Lab registró tick 42, semilla 42 y huella v3 `345e2012f55bc54e`. El entrenamiento headless generó un checkpoint con la misma huella. Se reanudó con un worker y se rechazó cargarlo con el entorno v2. Las pruebas de libro molecular incluyen recursos finitos e infinitos con las tres versiones. En el replay de bebida en Lab Valley v3 se registraron dos transferencias al tick de captura. El Velkar adulto aceptó 0,008333 al tick 5536 y el Brumacerno adulto aceptó 0,007500 al tick 6704. Las dos proyecciones orales alcanzaron el mismo cuerpo que el contacto objetivo. La mayor penetración de vértice en los apoyos de estas capturas fue 0,048 y 0,045 unidades, respectivamente. Las pruebas estrictas de captura juvenil con semillas poblacionales 42 y 108 terminaron sin una toma apta. Esas pruebas no prueban transferencia juvenil.

## Rendimiento medido

Ryzen 5 5600H, RTX 3050 Laptop de 4 GiB, controlador NVIDIA 580.178.04, GL 4.6, 1920 × 1080, MSAA 0, VSync desactivado, seis workers. Cámara ortográfica fija, zoom 160, calidad y sombras medias, seed 42, 400 Brumacernos y 100 Velkares. Se excluyeron 36 frames de calentamiento de los 456. Cada fila resume la mediana de tres repeticiones. La serie real conserva las muertes naturales. La serie exacta usa cuatro ticks por frame y permite comparar la carga y el hash a igual tick.

| Escenario | Población inicial → final | FPS | P50/P95/P99 de frame (ms) | P50/P95 GPU (ms) | Sombra GPU (ms) | Triángulos de sombra | Velocidad real |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| v2, b71d955, tiempo real | 500 → 491 | 71,9 | 13,91 / 16,26 / 16,68 | 11,43 / 11,63 | 3,99 | 5.108.012 | 4,011× |
| v2, actual, tiempo real | 500 → 491 | 76,8 | 13,01 / 15,41 / 15,87 | 10,53 / 10,78 | 2,68 | 973.476 | 4,002× |
| v3, actual, tiempo real | 500 → 492 | 77,9 | 12,82 / 15,44 / 15,99 | 10,35 / 10,54 | 2,67 | 973.716 | 3,987× |
| v2, b71d955, ticks exactos | 500 → 490 | 72,2 | 13,85 / 16,12 / 16,67 | 11,39 / 11,58 | 3,99 | 5.058.812 | — |
| v2, actual, ticks exactos | 500 → 490 | 77,2 | 12,92 / 15,34 / 16,17 | 10,47 / 10,71 | 2,68 | 966.640 | — |

La pareja de ticks exactos termina en 1.824 y coincide en el hash `cd80bec6c41a6f38` en seis ejecuciones. En tiempo real los ticks finales difieren con la cadencia. El P95 v3 varió de 15,09 a 15,56 ms. Hubo siete llamadas de sombra y un fallo de caché estático en el perfil final. La pasada de objetos v2 aumentó de 3,02 a 3,43 ms GPU. La del terreno aumentó de 2,04 a 2,08 ms. El RSS máximo mediano fue de 264.660 → 267.108 KiB con la misma física, y la VRAM declarada del entorno de 92.757.060 → 92.844.440 bytes. El mapa solar se mantuvo en 33.554.432 bytes. El perfil único con `--global-fill` obtuvo 3,44 ms de objetos, frente a 3,43 ms en el perfil local mediano. Esa diferencia está dentro de la variación observada y no aísla un incremento GPU del atlas. El objetivo de 2,5 ms de sombra sigue pendiente.

La siguiente exploración de escala y calidad se hizo antes del ajuste final del relieve y del receptor de sombra. Registra una repetición por caso. Los perfiles `v3-*.json` conservan la huella de esa iteración.

| Vista / configuración exploratoria | Fundadores → final | P95/P99 (ms) | Sombra (ms) | Velocidad real |
| --- | ---: | ---: | ---: | ---: |
| General media, 4× | 22 → 22 | 11,61 / 12,35 | 2,28 | 4,00× |
| General media, 4× | 100 → 100 | 12,89 / 13,34 | 2,35 | 3,99× |
| General media, 4× | 1.000 → 980 | 16,89 / 17,14 | 3,07 | 3,46× |
| General baja, 4× | 500 → 495 | 13,11 / 13,97 | 0,00 | 4,01× |
| General alta, 4× | 500 → 493 | 15,44 / 16,38 | 3,00 | 4,01× |
| Cercana media, 1× | 500 → 500 | 12,29 / 12,88 | 1,35 | 1,00× |
| Cercana media, 16× | 500 → 491 | 12,41 / 13,04 | 1,35 | 7,47× |
| Cercana media, máximo | 500 → 491 | 13,03 / 13,64 | 1,35 | 6,37× |

El Lab headless ejecutó 225 episodios por ensayo, con cuatro workers, 24 genomas y tres generaciones. Se hicieron tres repeticiones por combinación. En terreno fijo, las medianas fueron 175,2 episodios/s para v2 y 172,6 para v3. Con terreno derivado de la semilla de episodio, fueron 151,9 y 147,3 episodios/s. Dentro de cada combinación, los episodios de test exportados coincidieron entre repeticiones. Las tres huellas físicas v3 del test por episodio fueron distintas e iguales entre repeticiones. El terreno fijo quedó identificado en `report.json`, `experiment.json` y el checkpoint. Esta prueba mide evaluación y reutilización del caché, con las políticas y semillas indicadas.

## Evidencia y verificación

- `final-captures/creatures-release.png` reúne adultos pareados, órbita y otra cota solar. La serie `transport/captures` añade juveniles, diagnósticos directos e indirectos, AO, visibilidad, LOD explícitos y deformación CPU. Los cambios de lectura tonal en los animales oscuros son moderados. Las imágenes permiten inspeccionar ojos y queratina en los sujetos presentes. El conjunto no incluye referencias fotométricas equivalentes para todas las familias de escamas, pelo largo o tejido semitransparente.
- `shadow-comparison/contact-sheet.png` aísla las bandas de auto-sombra. `morphology-release/matched-river-bank.png` muestra el corte de terreno autoritativo en las mismas coordenadas.
- `final-captures/terrain-release.png` muestra entrada a lago, confluencia y escarpe, con la pendiente física al lado. `final-captures/overview-{baseline,v2,v3}.png` documenta panorámicas al tick 244 con 500 individuos vivos. Las panorámicas pareadas v2 comparten el hash `5cceacbe8ea36904`.
- `final-captures/interactions-release.png` reúne marcha, ingestión al tick de captura y replay de Lab. El visor registró posición de boca, cuerpo de agua, transferencias y muestras de apoyo. Las capturas planas de estudio y con luz natural coincidieron en el hash `19e4409e151322f6` al tick 62.

Pasaron `make test`, `make test-scene-lighting`, `make validate-procedural-world`, `make validate-ecosystem`, `make editor-test`, `make world-sanitize` y `make world-tsan`. Las pruebas de v3 cubren cuenco/vertedero, corredor descendente, microhoyo rechazado, identidad y profundidad firmada, triangulación del agua, libro molecular y caché. Los ensayos de skinning con sujetos presentes ejecutaron una malla por captura y quedaron dentro de las tolerancias CPU/GPU. Los perfiles y capturas enumerados registraron cero errores GL.

## Reproducción

```sh
make demos/demo_evolution_arena demos/demo_evolution_arena_headless \
  demos/demo_brain_evolution_lab demos/demo_brain_evolution_lab_headless build/audit_world_surface

./demos/demo_evolution_arena --world=worlds/river_valley_v3.world.json \
  --world-seed=42 --seed=42 --quality=medium --shadows=medium \
  --brumacernos=400 --velkares=100 --plants=700 --capacity=640 \
  --workers=6 --width=1920 --height=1080 --speed=4

./demos/demo_evolution_arena_headless --world=worlds/river_valley_v3.world.json \
  --world-seed=42 --seed=42 --validate-world-only

./demos/demo_brain_evolution_lab --species=species/brumacerno.species.json \
  --environment=environments/brumacerno_river_valley_v3.environment.json \
  --population=12 --generations=2 --train-seeds=2 --val-seeds=2 --workers=4 \
  --episode-seconds=60 --pretrain-gens=2 --replay-seed=42 --replay-ticks=30 \
  --frames=12 --no-hud --check-gl --capture=build/lab-v3.ppm

python3 tools/validate_world_morphology.py --large --out build/v3-report/morphology-final
python3 tools/validate_scene_lighting.py --mode transport \
  --baseline build/v3-report --out build/v3-report/transport
python3 tools/validate_scene_lighting.py --mode transport-profile \
  --baseline build/v3-report --out build/v3-report/profiles-release
```

Para v1 y v2 utiliza el correspondiente archivo sin sufijo o con sufijo `_v2`. Para reproducir las comparaciones de iluminación, el directorio `--baseline` debe contener el ejecutable `demo_evolution_arena` compilado desde el commit inicial. Los dos ejecutables comparten los materiales del checkout.

## Límites de la revisión

La aproximación indirecta conserva un mapa de horizonte de terreno de baja frecuencia. Las rocas y criaturas dinámicas no proyectan oclusión indirecta en ese mapa. La prueba de captura cuantifica cobertura opaca del cuerpo y comprueba su altura frente al agua. Una máscara por píxel después de translucencia permitiría medir mejor los casos de oclusión parcial. La malla de sombra FAR conserva la postura rígida que sigue la ruta instanciada. El sombreado de estudio permanece disponible, incluido el modo plano predeterminado.

La morfología sigue usando encaminamiento D8 y contornos discretos de altura. No se validaron con fixtures independientes todos los cruces, vertederos y confluencias posibles. Los límites pendientes incluyen la distribución de zonas húmedas en Wetlands y Lab, aristas raster ascendentes cerca de encuentros, navegación desglosada por causa, conectividad de recursos por región, emisores fuera de cámara, caster animado MID simplificado, sombreado de dos animales junto a la misma roca y acceso oral de juveniles en orilla. Se conservó la cobertura de materiales, el agua autoritativa y la calidad media de sombras.
