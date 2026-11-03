# Cohesión muscular équida — continuación del hito 1

## Resultado y alcance

Implementación funcional de redistribución muscular, corrección del cierre de
grupa y vinculación de piel a pose. **La misión completa no está terminada.**
La aceptación anatómica sigue siendo parcial: desaparecen varios nódulos y el
anillo sacrocaudal se reduce, pero las formas todavía no alcanzan el realismo
del animal de referencia. No se ha implementado el sistema universal de pelo
largo ni el nuevo pigmento del hocico; no se presentan como resueltos.

Base real: rama `feature/universal-anatomy-foundation`, `8920a58` **más los
cambios locales que ya existían**, no el commit limpio de GitHub. Se conservan
[estado inicial](initial-status.txt), [diferencias iniciales](initial.patch) y
[cambios exclusivos de esta sesión](session.patch). No se ha hecho reset,
checkout destructivo ni commit. La documentación anterior en
`equine-reconstruction/refinement/` es trabajo preexistente.

## Diagnóstico y arquitectura empleada

1. **Hombro:** el perfil cursorial ya tenía barridos elípticos, pero una estación
   intermedia más voluminosa que sus vecinas dibujaba un nódulo. Se adelanta la
   distribución longitudinal del brazo, se aplana la envolvente escapular y se
   introduce su origen medialmente. La primera prueba de crecimiento detectó
   que el origen salía del anfitrión en la etapa 0; se bajó el ancla, manteniendo
   las cuatro articulaciones IK originales.
2. **Muslo:** la transición inicial tenía una cintura antes del gaskin. La
   estación de piel del muslo baja y se desplaza caudalmente, conservando
   babilla y corvejón. El ancla glútea pasa a la región craneal de la pelvis,
   en vez de sobresalir sobre la terminación sacrocaudal.
3. **Plenitud duplicada:** se eliminan los pequeños bulges adicionales de la
   conexión muscular: la masa ya está en sus secciones. No se aumentan los
   radios de suavizado, no se añade suavizado global y no se cambia la
   ecuación SDF compartida. Se reutilizan anchura, altura, orientación y
   estaciones independientes del grafo; no hay módulos nuevos por músculo.
4. **Grupa:** estrechar la última estación aproximadamente a media sección y
   terminarla con una tapa redondeada producía un anillo y una segunda masa
   aparente. El perfil `CURSORIAL` conserva una sección terminal más profunda
   y ancha. La ruta axial `LEGACY` no se cambia en esta sesión.
5. **Pedículo caudal:** una cola colgante excluida del loft seguía copiando el
   diámetro pélvico en el conector inicial. Cuando el fenotipo solicita
   `rootMatchStrength=0`, ahora usa su propia sección también en ese extremo.
   Las recetas que solicitan acoplamiento mantienen la ruta anterior; esto
   conserva exactamente Brumacerno. No hay comprobaciones de nombre de especie.
6. **Pose:** el árbol IK y el grafo de piel no son el mismo grafo. La vinculación
   rechazaba cualquier arista de superficie entre nodos no adyacentes en el
   árbol esquelético. Ahora estas aristas interpolan los marcos de sus extremos;
   los huesos adyacentes conservan su regla anterior. Se omiten influencias
   de peso cero antes de consultar las articulaciones.

Se conservan IDs, número de nodos/conexiones, contrato de apoyo, recetas v1,
crecimiento y núcleo sin SDL/GL. El movimiento de pose no remalla el SDF. Las
dimensiones compiladas y sus cotas se comparten con GPU; no se introdujo una
forma alternativa en GLSL. Los dos encabezados SDF generados son idénticos a
los que había al comenzar, aunque ya estaban modificados frente a HEAD.

## Referencias y comparación anatómica

Consultas realizadas para este bloque:

- [UMN, Thoracic Limb](https://open.lib.umn.edu/largeanimalanatomy/chapter/thoracic-limb-forelimb/):
  suspensión muscular del tórax, trapezio/romboides, supraespinoso e
  infraespinoso sobre la escápula, pectorales y tríceps. La masa proximal es
  distribuida y el segmento distal está dominado por tendones.
- [UMN, Pelvic Limb](https://open.lib.umn.edu/largeanimalanatomy/chapter/pelvic-limb/):
  glúteo medio y fascia superficial, bíceps femoral y músculos caudales del
  muslo; diferencia entre una grupa equina redondeada y una bola articular
  independiente. Semitendinoso lateral y semimembranoso medial en vista caudal.
- [ADW, Equus caballus](https://animaldiversity.org/accounts/Equus_caballus/):
  tórax profundo, extremidades largas, cuello y cabeza alargados; crin dorsal
  y distinción entre pelo erecto silvestre y caído doméstico.
- [Warmblood polaco adulto, Tanya Dewey, agosto de 2004](https://animaldiversity.org/collections/contributors/tanya_dewey/horse4/):
  se descargó y se inspeccionó la fotografía real fuera del repositorio. CC
  BY-NC-SA 3.0; no se incorpora esa fotografía a los artefactos del motor.
- Missouri `g2837` respondió 403. No se atribuyen observaciones a esa página
  inaccesible ni a fuentes de la misión que no se consultaron en este bloque.

El warmblood muestra una transición escapular más extensa y un muslo caudal
mejor integrado que el motor. Las capturas finales reducen el nódulo proximal
y el anillo pélvico, pero conservan una grupa demasiado geométrica y un tórax
tubular. No hay medidas veterinarias inventadas, escaneo, calibración de
cámara ni comparación píxel a píxel con la fotografía. Los coeficientes son
aproximaciones artísticas del perfil cursorial, no medidas de una raza.

## Iteración visual y capturas auténticas

- `before/`: núcleo local inicial, con el capturador nuevo para conservar los
  mismos encuadres regionales. No es una base limpia más antigua.
- `iteration-1/`: primera redistribución, adultos. Se detectó una cintura
  excesiva en el muslo y persistencia del anillo de grupa.
- `after/`: corrección caudal del muslo, perfil terminal de grupa y vinculación
  a pose; ambas especies en las cuatro etapas. `anatomy.txt` conserva las
  estaciones, conectores, radios y suavizados reales de cada caso.

Todas son capturas C/OpenGL de las mallas del motor, en AMD Radeon integrada
Renoir, Mesa 25.2.8, OpenGL 4.6, MSAA=0. No se emplearon imágenes generadas ni
mallas externas. Los PPM se convierten a PNG sin retocar la anatomía.

- [Caballo: antes/después](caballo-comparison.png).
- [Asno: antes/después](asno-comparison.png).
- [Caballo lateral completo](after/caballo/full-side.png),
  [frontal](after/caballo/full-front.png).
- Hombro: [lateral](after/caballo/shoulder-side.png),
  [frontal](after/caballo/shoulder-front.png),
  [tres cuartos](after/caballo/shoulder-three-quarter.png).
- Cuarto trasero: [lateral](after/caballo/hip-side.png),
  [posterior](after/caballo/hip-rear.png),
  [tres cuartos](after/caballo/hip-three-quarter.png).
- [Asno tres cuartos](after/asno/full-three-quarter.png).
- Pose de marcha: [caballo](after/caballo/walk-three-quarter.png),
  [asno](after/asno/walk-three-quarter.png).

Las vistas completas nuevas ajustan la caja corporal y no sirven para medir
una reducción de tamaño entre columnas. Los primeros planos tienen idéntica
cámara y escala por especie. Las vistas históricas `side.png` mantienen su
encuadre anterior, que podía cortar el morro; se añadieron `full-*` para
mostrar el animal entero. `walk-*` activa superficies existentes al deformar:
no es una prueba del nuevo pelo largo ni una comparación de iluminación con
las vistas de arcilla.

[Resultados de capturas](captures.json): ocho casos finales con vinculación y
deformación de marcha correctas, frente a ocho `WALK_UNAVAILABLE` en la base.
Es una pose tras 30 actualizaciones, no una validación biomecánica de todo el
ciclo ni una prueba exhaustiva de colisiones.

## Verificación

| Comprobación | Resultado |
|---|---|
| `make test`, ejecución final secuencial | PASS, todas las suites; [registro](tests.log) |
| `make editor-test` | PASS, 9 pruebas, 32.110 s; [registro](editor.log) |
| `make visual-equine` | PASS, 8 casos; [registro](visual-equine.log) |
| Capturas OpenGL en cuatro etapas | PASS; [registro](captures.log) |
| Lagarto, perro, Brumacerno y Velkar | 16 casos con mismos índices, vértices y materiales frente a la base local; permutar módulos tampoco los cambia; [datos](legacy-regression/results.json) |
| CPU/compute/fragmento, seis recetas adultas | PASS sin aumentar tolerancias; [registro](gpu.log) |
| Caballo, 3090 muestras incluidas 500 musculares | error compute/fragmento `9.53674316e-07 m` |
| Asno, 3090 muestras incluidas 500 musculares | error compute/fragmento `7.59959221e-07 m` |
| `make visual-morph-ownership`, retraso 3000 ms | PASS; referencia 42878, extra 3211 (0.075), faltante 2735 (0.064); [registro](morph-ownership.log) |
| `make visual-morph-gpu-regressions` | PASS, material y nueve posiciones de metamorfosis con ida/vuelta; [registro](morph-gpu.log) |
| `git diff --check` | PASS |

Los tests nuevos comprueban anclas interiores al anfitrión, continuidad de
centros de barridos, simetría de campos de miembros aislados, sección del
pedículo, vinculación cíclica al esqueleto, identidad de reposo y 30 poses
finitas con desplazamiento real por caso. No se confunde esto con una prueba
de realismo. El smooth-min global es dependiente del orden; la comprobación
de simetría usa los mismos módulos espejados, no promete igualdad bit a bit
de distancias interiores del cuerpo completo.

Una ejecución paralela falló en `AppendageMeshContinuity: cola mallada en cada
LOD`; [registro conservado](tests-parallel-attempt.log). Esa prueba espera a
un mallador asíncrono con un límite de intentos. La ejecución completa en
solitario pasó. El mismo fallo ya estaba registrado en la documentación
preexistente; no se ha demostrado su causa exacta ni se ha relajado la prueba.

## Rendimiento

Mediana de siete pares alternados de 1000 resoluciones, `clock()`, misma
máquina y mismo benchmark compilado para ambas bases; sin mallado/render.
[Datos crudos](performance.json).

| Receta | Base local | Actual |
|---|---:|---:|
| Caballo | 20.320 µs | 20.364 µs |
| Asno | 20.143 µs | 20.247 µs |

Se mantienen 77 nodos/92 conexiones. Sin incremento de estructuras persistentes:
`CreatureRecipe=37704`, `CreaturePhenotype=8520`, `AnatomyGraph=121880`,
`Monster=181296` bytes. Las diferencias pequeñas no demuestran optimización.

Los tiempos de `captures.json` incluyen proceso, extracción, vinculación,
render de varias vistas y conversión PNG bajo carga de validación. **No son
FPS ni coste GPU por frame.** No hay benchmarks de raíces, guías o fibras del
nuevo sistema porque ese sistema aún no existe; no se inventan esos números.

## Reproducción

```bash
make test
make editor-test
make visual-equine
make visual-equine-muscles
make visual-morph-ownership
make visual-morph-gpu-regressions
make build/validate_recipe_gpu
build/validate_recipe_gpu recipes/caballo.creature.json 3
build/validate_recipe_gpu recipes/asno.creature.json 3
```

`visual-equine-muscles` escribe en `build/equine-muscles`. Para comparar contra
la base local, compilar el núcleo de `8920a58` con `initial.patch` aplicado en
un directorio separado, enlazando **el capturador actual** para usar las mismas
cámaras, y pasar su ejecutable con `--baseline-capture`:

```bash
python3 tools/validate_equine_muscles.py --all-stages \
  --baseline-capture /ruta/base-local/capture_bovine_anatomy \
  --output build/equine-muscle-comparison
```

## Archivos de esta sesión

Modificados, respecto al estado local inicial:

- `src/LimbMammal.c`: estaciones cursoriales y retirada de plenitud duplicada.
- `src/AxialBodyUprightTetrapod.c`: cierre de grupa cursorial.
- `src/MonsterSDF.c`: sección de raíz caudal sin acoplamiento solicitado.
- `src/AnatomyDeformer.c`: aristas de piel cíclicas y pesos cero.
- `tests/test_equine_anatomy.c`: regresiones de cohesión y pose.
- `tools/capture_bovine_anatomy.c`: modo muscular, diagnósticos y encuadres.
- `tools/validate_recipe_gpu.c`: sondeos de envolventes musculares.
- `Makefile`: target `visual-equine-muscles`.

Creados: `tools/validate_equine_muscles.py` y este directorio de evidencia
(informe, capturas, comparaciones, registros, JSON y parches de trazabilidad).
Las modificaciones previas de cabeza, recetas, generador, puente del editor y
encabezados generados no son implementaciones nuevas de esta sesión.

## Pendiente exacto

1. Aceptación visual final del hito 1: suavizar lectura del contorno de grupa y
   unión muslo/gaskin, mejorar faceteado y validar todo el ciclo de articulación,
   no sólo sondas y una pose. No se declara un warmblood anatómicamente terminado.
2. Hito 2: dominios pigmentarios de reposo reutilizables, piel del hocico,
   cobertura de pelo/roughness y vistas de depuración, caballo y asno.
3. Hito 3: dominios anatómicos de raíces, IDs estables, guías 6–12 puntos,
   fibras hijas procedurales, dinámica ligera y render separado del remallado.
4. Hito 4: crin cervical, caída lateral configurable, deformación y colisión.
5. Hito 5: pelo caudal equino frente a distribución distal del asno, dinámica
   y ciclos repetidos de crecimiento/metamorfosis sin desprendimiento.
6. Hito 6: ejemplos no équidos, JSON/editor, LOD, capturas de controles y
   benchmarks separados de CPU/GPU, memoria y FPS con/sin pelo.

No hay nueva arquitectura de pelo que pueda describirse como implementada;
los tufts anteriores siguen siendo la cobertura regional existente.
