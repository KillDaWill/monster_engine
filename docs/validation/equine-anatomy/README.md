# Anatomía équida: casco único y composición craneal

Continuación en `feature/universal-anatomy-foundation`, sobre el bloque de
[dependencias anatómicas](../anatomy-foundation/README.md). Se conserva el motor,
sus resolutores anteriores y el núcleo sin SDL/GL.

## Alcance y arquitectura

- `HEAD_ARCHETYPE_EQUINE=4` se añade **después** de los cuatro valores existentes.
  Su composición usa un puente nasal largo, perfil facial inclinado, labios
  distales más anchos, órbitas laterales, maseteros y pabellones auriculares.
  No escala la receta cefálica canina ni genera una trufa de carnívoro.
- El ungulado existente admite `digitCount=1` (casco central) y `2` (pezuña
  hendida). La cápsula de queratina, el material `HOOF`, cotas, shader y rig
  son los existentes. No se duplica el SDF por especie ni se añaden primitivas.
  Un casco conserva los IDs del primer dedo y no emite un segundo dedo oculto.
- La suela de cada casco coincide con el apoyo publicado al rig. Sus radios,
  longitud, inclinación de pared, redondeo y talón siguen siendo editables.
  La validación rechaza otros conteos ungulados y espolones en el casco único.
- Los documentos de caballo y asno comparten contratos y geometría; cambian
  proporciones, orejas, pigmento y tamaño. IDs externos `1005` y `1006`, compatibles
  con la reserva de IDs del editor. No hay generadores geométricos por especie.
- JSON **v1** permanece válido: no se añaden campos obligatorios ni se requiere
  migración. El esquema se regenera desde las cabeceras. Los controles del editor
  ofrecen cabeza équida y selección de casco único/pezuña hendida, conservando
  edición avanzada, API transaccional y deshacer/rehacer.
- No aumentan capacidades ni tamaños persistentes: `CreatureRecipe=36264`,
  `CreaturePhenotype=8160`, `AnatomyGraph=121880`, `Monster=178904` bytes en esta
  plataforma de 64 bits. Cada receta équida resuelve 53 nodos y 52 conexiones.

## Recetas y reproducción

```bash
make build/creature_editor_bridge
python3 tools/generate_equine_recipes.py
make test
make editor-test
make visual-equine
make build/capture_bovine_anatomy
mkdir -p build/equine-opengl
build/capture_bovine_anatomy recipes/caballo.creature.json build/equine-opengl
make benchmarks/benchmark_anatomy
benchmarks/benchmark_anatomy recipes/caballo.creature.json recipes/asno.creature.json
```

`generate_equine_recipes.py` es un **autor de ejemplos deterministas**, no el
generador universal de plantillas/semillas pendiente en el hito 6. Valida los
documentos por el puente C antes de escribirlos. Las cuatro etapas mantienen
miembros completos; «larva» es el nombre heredado del contrato, no una afirmación
de que los mamíferos tengan una larva biológica. Las etapas no son una ontogenia
veterinaria calibrada.

## Validación anatómica y referencias

Referencia descriptiva: Christopher Clement, [Equus caballus, Animal Diversity
Web](https://animaldiversity.org/accounts/Equus_caballus/). Describe casco oval
de un solo dedo, cuello grueso y largo, torso profundo, extremidades delgadas,
cabeza alargada y orejas erectas. No aporta cocientes exactos de cráneo/tronco:
**los parámetros de las recetas son ajustes artísticos, no medidas científicas**.

Se consultó la fotografía de Tanya Dewey, [Polish warmblood, Mikolajki,
2004](https://animaldiversity.org/collections/contributors/tanya_dewey/horse4/)
para revisar cabeza descendente, masa cervical, pecho y cascos. Su licencia
CC BY-NC-SA 3.0 se conserva en la página de origen: no se redistribuye aquí.
Frente a esa referencia, el primer render tenía dorso/barril demasiado rectos;
se ajustaron las estaciones torácicas y de grupa, y se corrigió el grosor de
la mandíbula inferior para acompañar el plano oral inclinado.

Para el asno se consultó Rachael Pagan, [Equus asinus, Animal Diversity
Web](https://animaldiversity.org/accounts/Equus_asinus/). La receta no reproduce
todavía todas las marcas regionales descritas allí (morro claro, raya dorsal,
marcas de extremidades); no se presenta como una réplica zoológica calibrada.

Los tests comprueban medidas **internas del motor**, no tolerancias biológicas
inventadas: suela a y=0, un casco por miembro, alineación con metapodio, continuidad
de tejido entre casco y menudillo, material, intervalos conservadores, simetría
cefálica, 180 variantes por semilla, cuatro etapas y IDs estables durante la
metamorfosis juvenil-adulto. El script visual comprueba finitud y repetición
exacta de mallas y guarda estaciones resueltas en `results.json`.

Imágenes del campo C: [caballo](caballo.png) · [asno](asno.png).
Cada hoja muestra lateral, frontal, tres cuartos y superior en cuatro etapas.
Son mallas reales, no sustitutos geométricos de JavaScript; la rasterización
diagnóstica no representa el shader de pelo.

Captura adicional: [OpenGL neutro y detalle del casco](caballo-opengl.png),
con el capturador anatómico existente aplicado al documento équido, y
[captura desde la API del editor](agent-caballo.png). El OpenGL neutro dibuja
las mallas extraídas por CPU; **no demuestra concordancia del raymarch fragmentario**.

## Resultados de ejecución

| Comprobación | Resultado |
|---|---|
| `make test` | PASS, todas las suites, incluidas anatomía équida y metamorfosis previas |
| `make editor-test` | PASS, 9 pruebas, 34.427 s |
| `make visual-morph-ownership` | PASS, silueta sin cuerpo obsoleto visible |
| Mallas équidas finitas y deterministas | PASS, 8 casos, 4 vistas por caso |
| Compatibilidad con la base | PASS, 16 casos antiguos; delta máximo de vértices 0, índices y materiales idénticos |
| Regenerar recetas y esquema | PASS, SHA-256 sin cambios |
| `git diff --check` | PASS |
| Comparación GPU de caballo adulto | FAIL fragmentario; compute e intervalos dentro de tolerancia |

La comparación con la base se ejecutó usando su binario previo y un directorio
sin el nuevo catálogo: el ejecutable antiguo no puede interpretar arquetipos
que aún no conocía. `tools/validate_anatomy_dependencies.py` admite
`--baseline-cwd` para aislar esa lectura, sin cambiar los documentos comparados.

Una ejecución del suite bajo carga concurrente de remallado agotó la espera fija
de 10 s de `AppendageMeshContinuity` en el ecosistema. La ejecución completa
aislada pasó sin modificar esa prueba ni ampliar sus límites. La primera
ejecución también necesitó retirar el límite externo de 120 s del proceso.

Resolución anatómica, mediana de 7 ejecuciones alternadas de 1000 iteraciones
(`clock()`, microsegundos por resolución, sin mallado):

| Receta | Base original | Actual |
|---|---:|---:|
| Lagarto | 44.807 | 43.084 |
| Perro | 30.025 | 29.070 |
| Brumacerno | 30.103 | 29.842 |
| Velkar | 38.460 | 37.342 |
| Caballo | No disponible | 12.537 |
| Asno | No disponible | 12.073 |

Las diferencias pequeñas entre las recetas antiguas no se presentan como una
optimización demostrada: dependen de frecuencia/caché y ruido de ejecución.
Los équidos tienen menos estaciones digitales que los cuadrúpedos anteriores;
no es una comparación de calidad equivalente. Datos completos: [performance.json](performance.json).

Preview fino, con presupuestos actuales del editor: caballo 3.95–4.12 s,
asno 4.38–4.51 s por reconstrucción. Adultos: caballo 152455 vértices / 304862
triángulos; asno 146588 / 293136. No es reconstrucción interactiva a 60 FPS.
`results.json` contiene tiempos, materiales, cajas y estaciones de cada caso.

## Limitaciones abiertas

- Este bloque implementa cráneo y casco a través del pipeline; **no completa
  el hito 2 ni toda la fase B**. Faltan crin/cola pilosa regionales, refinamiento
de masas escapulares y grupa, validación de marcha equina específica y
  ontogenia biológica. Se reutiliza la marcha ungulada existente, sin afirmar
  que sea una simulación de marcha equina contrastada.
- Continúa el fallo preexistente de concordancia del SDF fragmentario en GPU.
  El caso caballo adulto da compute `7.15255737e-7 m` (dentro de `2e-4 m`),
  intervalos `1.11758709e-7 m`, pero fragmentario `1.49185181 m`: **FAIL** global.
  La base ya fallaba con Brumacerno (`2.19592166 m`, ver informe anterior).
  No se modificaron primitivas, shaders ni sus cabeceras generadas.
- Siguen pendientes los contratos de aletas/caparazones/apéndices blandos,
  marcos completamente arbitrarios en todos los resolutores, axiales flexibles,
  astas ramificadas, trompas, aves/peces/invertebrados y generador universal.
  El catálogo previo conserva sus defectos visuales; no se presentan como arreglados.

## Archivos de esta continuación

- `include/Head.h`, `src/Head.c`: composición y preset équidos.
- `include/CreaturePhenotype.h`, `src/LimbMammal.c`, `src/CreatureRecipe.c`:
  contrato mono/bidáctilo, geometría y validación.
- `src/CreatureRecipeSchema.inc`: salida regenerada por
  `tools/generate_recipe_schema.py`, nunca editada manualmente.
- `recipes/caballo.creature.json`, `recipes/asno.creature.json`,
  `recipes/catalog.txt`: documentos v1 y catálogo.
- `tools/generate_equine_recipes.py`, `tools/validate_equine_anatomy.py`:
  autor de ejemplos y capturas comprobables.
- `tools/validate_anatomy_dependencies.py`: aislamiento del catálogo de referencia.
- `tests/test_equine_anatomy.c`, `tests/main_test.c`, `Makefile`: suite y target visual.
- `tools/creature_editor/app.js`, `tools/creature_editor/test_editor.py`:
  controles y regresión HTTP/JSON/mallas/historial.
- `docs/CREATURE_EDITOR.md`: uso de cabeza équida y autopodios mono/bidáctilos.
- `benchmarks/benchmark_anatomy.c`: añade medición de documentos externos.
- Este directorio y nota en `docs/validation/anatomy-foundation/README.md`:
  evidencias y separación del informe anterior.
