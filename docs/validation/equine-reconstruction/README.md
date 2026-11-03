# Reconstrucción équida: sistemas anatómicos reutilizables

> Continuación posterior: [segunda reconstrucción, diagnóstico mandibular y
> revisión muscular](refinement/README.md). Los resultados de este documento
> corresponden al bloque anterior; no sustituyen la validación nueva.

Continuación en `feature/universal-anatomy-foundation`, sobre el bloque de
[dependencias anatómicas](../anatomy-foundation/README.md) y la primera
[anatomía équida](../equine-anatomy/README.md). Se conserva el motor, sus
resolvers anteriores y el núcleo sin SDL/GL.

## Alcance

El objetivo es reconstruir el caballo genérico como un équido reconocible sin
convertirlo en una geometría específica de especie. La solución añade perfiles
anatómicos opcionales y reutilizables: las recetas anteriores conservan el
camino `LEGACY` y sus mallas; caballo y asno activan `CURSORIAL`.

No se dispone de escaneos ni de medidas veterinarias individuales. Las
proporciones son ajustes artísticos dentro de ventanas descriptivas, no
mediciones biológicas certificadas. La auditoría de fuentes y licencias está en
[REFERENCIAS.md](REFERENCIAS.md).

## Arquitectura

- `LimbAnatomyProfile` (`LEGACY`/`CURSORIAL`) separa estaciones de piel de las
  cuatro articulaciones IK. El perfil cursorial construye diez estaciones
  (raíz, brazo/muslo, antebrazo/pierna, carpo/tarso, caña, menudillo, cuartilla
  y anclas musculares) sin cambiar el contrato de apoyo ni el rig.
- `AxialContourProfile` (`LEGACY`/`CURSORIAL`) añade trece estaciones axiales
  con cresta cervical, cruz, tórax, abdomen, lomo, grupa y base de cola. El
  perfil antiguo permanece intacto.
- `BODY_CONNECTION_MUSCLE_UNION` y `BODY_CONNECTION_HOOF_CAPSULE` se añaden
  **después** de los valores existentes. La unión muscular entierra el origen y
  suaviza la transición; la cápsula de casco mantiene la suela, el material
  `HOOF`, las cotas conservadoras y el shader.
- `SDF_HoofCapsule` se implementa en las primitivas SDF, en el intervalo y en
  los shaders generados. El casco único conserva los IDs del primer dedo y no
  emite un segundo dedo oculto.
- La cabeza équida usa un puente nasal largo, frente profunda, cintura nasal,
  labios distales más anchos, órbitas laterales, maseteros y pabellones. La
  mandíbula baja y se estrecha **solo** para équidos; la fórmula bóvida se
  conserva.
- La cobertura de pelo usa `FurTuftPhenotype` (máximo cuatro grupos) y las
  mismas filas de `SurfaceRecipe` (103 filas) en CPU y shader. Es cobertura
  regional, no una crin o cola pilosa completa.
- Las recetas `1005` (Caballo) y `1006` (Asno) se generan con
  `tools/generate_equine_recipes.py`. JSON **v1** sigue siendo válido; el
  esquema se regenera desde las cabeceras y no se edita a mano.
- Solo `src/OpenGLRenderer.c` incluye SDL/GL. El resto de `CORE_SRCS` sigue
  siendo agnóstico al render y `make test` enlaza solo con `-lm`.

## Recetas y reproducción

```bash
make build/creature_editor_bridge
python3 tools/generate_equine_recipes.py
make test
make editor-test
make visual-morph-ownership
python3 tools/validate_equine_anatomy.py --output docs/validation/equine-reconstruction
make build/capture_bovine_anatomy
build/capture_bovine_anatomy recipes/caballo.creature.json build/equine-opengl 3
make benchmarks/benchmark_anatomy
benchmarks/benchmark_anatomy recipes/caballo.creature.json recipes/asno.creature.json
build/validate_recipe_gpu recipes/caballo.creature.json 3
build/validate_recipe_gpu recipes/asno.creature.json 3
```

`generate_equine_recipes.py` es un autor de ejemplos determinista, no el
generador universal de plantillas pendiente del hito 6. Las cuatro etapas
mantienen miembros completos; «larva» es el nombre heredado del contrato, no una
afirmación de que los mamíferos tengan una larva biológica. Las etapas no son
una ontogenia veterinaria calibrada.

## Resultados

| Comprobación | Resultado |
|---|---|
| `make test` | PASS, todas las suites, incluidas anatomía équida, bóvida, dependencias, lagarto, perro, ecosistema y metamorfosis |
| `make editor-test` | PASS, 9 pruebas, 33.615 s |
| `make visual-morph-ownership` | PASS; referencia 42878, extra 2376 (0.055), missing 2186 (0.051), silueta sin cuerpo obsoleto |
| `git diff --check` | PASS |
| Mallas équidas finitas y deterministas | PASS, 8 casos, cuatro vistas por caso |
| Comparación GPU caballo adulto | PASS; intervalo 9.68575478e-08 m, compute = fragmento = 6.85453415e-07 m |
| Comparación GPU asno adulto | PASS; intervalo 3.42726707e-07 m, compute = fragmento = 7.15255737e-07 m |
| Regenerar recetas y esquema | PASS; los documentos se validan por el puente C antes de escribirse |

La comparación GPU usa 2590 muestras del campo, no una comparación de todos los
píxeles de la pantalla. Es una comprobación del campo SDF y de la extracción, no
una demostración de que el shader fragmentario completo sea idéntico en toda la
imagen.

### Mallas y tiempos de preview

Datos completos, incluidas cajas, materiales y estaciones resueltas, en
[results.json](results.json). Tiempos de una ejecución; no son un benchmark
interactivo.

| Receta | Etapa | Vértices | Triángulos | Nodos | Preview |
|---|---:|---:|---:|---:|---:|
| Caballo | 0 | 74456 | 148872 | 77 | 3.041 s |
| Caballo | 1 | 81906 | 163764 | 77 | 3.290 s |
| Caballo | 2 | 90500 | 180944 | 77 | 3.502 s |
| Caballo | 3 | 60784 | 121528 | 77 | 2.707 s |
| Asno | 0 | 79827 | 159642 | 77 | 4.742 s |
| Asno | 1 | 82972 | 165911 | 77 | 4.723 s |
| Asno | 2 | 89764 | 179492 | 77 | 4.726 s |
| Asno | 3 | 96078 | 192088 | 77 | 4.843 s |

### Resolución anatómica

Mediana de siete ejecuciones de 1000 iteraciones, `clock()`, microsegundos por
resolución y sin mallado. `base` es el commit anterior `1f2877c`; `actual` es
este árbol de trabajo. Las diferencias pequeñas de las recetas antiguas no se
presentan como una optimización demostrada.

| Receta | Base | Actual |
|---|---:|---:|
| Lagarto | 44.807 | 43.230 |
| Perro | 30.025 | 28.813 |
| Brumacerno | 30.103 | 29.481 |
| Velkar | 38.460 | 37.412 |
| Caballo | No disponible | 20.911 |
| Asno | No disponible | 20.628 |

Los équidos resuelven ahora 77 nodos y 92 conexiones, frente a 53/52 de la
primera rebanada équida. El coste por resolución es mayor porque hay más
estaciones; no se compara 20.9 µs con 12.5 µs como si midieran la misma
estructura. Datos completos: [performance.json](performance.json).

## Evidencia visual

- [caballo.png](caballo.png): 2000x1440, cuatro etapas por cuatro vistas
  (lateral, frontal, tres cuartos y superior), mallas C reales.
- [asno.png](asno.png): la misma hoja para el asno.
- [caballo-opengl.png](caballo-opengl.png): 1800x960, captura OpenGL neutra con
  lateral, frontal, tres cuartos y detalle de casco (lateral, frontal y suela).
- [agent-caballo.png](agent-caballo.png): 1100x800, vista tres cuartos generada
  desde la API de preview del editor.

Las hojas C y la captura del agente son rasterizaciones diagnósticas; no
representan el shader de pelo. La captura OpenGL dibuja las mallas extraídas por
CPU; no demuestra por sí sola la concordancia del raymarch fragmentario.

## Estado visual y limitaciones

La reconstrucción corrige la órbita frontal, el disco plano del hocico y el
casco cuadrado de la base. Conserva, sin embargo, defectos visibles que este
bloque no resuelve:

- el tronco sigue siendo demasiado tubular y la grupa forma una masa redonda;
- el cuello es estrecho para la masa corporal;
- la mandíbula inferior muestra un escalón o hueco visible en algunas vistas;
- las extremidades son finas y la transición de la grupa al corvejón no está
  validada como marcha equina;
- la cola es un apéndice liso: los grupos de pelo no forman todavía una cola
  larga y caída, y la crin no está resuelta como mechones;
- las proporciones son ajustes artísticos; no hay ontogenia veterinaria,
  fotogrametría ni comparación con un mismo animal en varias vistas.

El caballo es reconocible y el pipeline es reutilizable, pero **no se presenta
como una réplica zoológica calibrada ni como el hito 2 completo**. Siguen
pendientes la crin/cola pilosa regional, el refinamiento de escápula y grupa, la
marcha equina específica y la validación con un juego ortográfico calibrado.

## Archivos de esta continuación

- `include/Limb.h`, `src/LimbMammal.c`: perfil `CURSORIAL`, estaciones de piel,
  unión muscular y conservación de las cuatro articulaciones IK.
- `include/Anatomy.h`, `src/AxialBodyUprightTetrapod.c`: perfil axial y
  estaciones de cresta, lomo y grupa.
- `include/SDFPrimitives.h`, `src/SDFPrimitives.c`, `include/SDFInterval.h`,
  `src/SDFInterval.c`: cápsula de casco y cotas.
- `src/Head.c`, `src/MonsterSDF.c`: cabeza équida, órbitas laterales, labios y
  mandíbula equina; las rutas bóvida y compartida se conservan.
- `include/Fur.h`, `src/Fur.c`, `include/Surface.h`, `src/Surface.c`,
  `shaders/fur_guard.*`, `shaders/monster_surface.frag`: grupos de pelo y
  superficie.
- `recipes/caballo.creature.json`, `recipes/asno.creature.json`,
  `tools/generate_equine_recipes.py`: documentos v1 y autor determinista.
- `tools/validate_equine_anatomy.py`, `tools/capture_bovine_anatomy.c`,
  `tools/creature_editor/render_preview.py`: capturas y resultados.
- `tests/test_equine_anatomy.c`, `tests/main_test.c`, `Makefile`: suite équida y
  targets visuales.
- `benchmarks/benchmark_anatomy.c`: medición de documentos externos.
- `src/CreatureRecipeSchema.inc`, `src/MonsterSDFShader.generated.h`,
  `src/MonsterSDFCompute.generated.h`, `src/MonsterSurfaceShader.generated.h`:
  salidas regeneradas por los scripts, nunca editadas a mano.
