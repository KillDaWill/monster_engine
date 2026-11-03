# Segunda reconstrucción: mandíbula y distribución muscular

Base: `8920a58`, rama `feature/universal-anatomy-foundation`.

**Estado: implementación y validación parciales; no se cumplen todos los criterios
de aceptación de la misión.** Las capturas son del motor C y OpenGL reales. No
se han generado imágenes sintéticas ni sustituido las mallas por modelos externos.

## Causa de la mandíbula ausente

El snapshot adulto anterior, conservado en [before/jaw.txt](before/jaw.txt), resolvía:

| Dato local | Valor |
|---|---|
| Bisagra | `(0, -0.171074, -0.208811)` |
| Centro de entrada | `(0, -0.843006, 0.917059)` |
| Semiextensiones de entrada | `(0.256122, 0.003987, 0.145809)` |
| Centro mandibular | `(0, -0.931841, 0.342628)` |
| Radios mandibulares | `(0.445516, 0.084848, 0.551438)` |
| Sínfisis | `(0, -0.913947, 0.894066)` |
| `faceCapWeight` | `1` |
| Plano posterior anterior | `0.771250` |

La longitud bisagra–sínfisis era `1.102877`; el plano eliminaba `0.980061`,
aproximadamente **88.9 %** del trayecto. La imagen aislada muestra el pequeño
fragmento conservado; el barrido sin planos muestra la extensión perdida.
Los seis centros y radios exactos están en el mismo archivo.

La causa era la reutilización de `faceCapWeight` para interpolar el límite
posterior mandibular. No era un problema de tamaño del vóxel ni de falta de
vértices. Además, el perfil previo copiaba las estaciones craneales y tenía un
espesor casi uniforme, incluso después de recuperar la porción perdida.

### Solución

- `HeadSurfaceRecipe` dispone de un perfil mandibular opcional con seis
  secciones propias: labio/sínfisis, cuerpo rostral, cuerpo medio, ángulo, rama
  y cóndilo. Centros, anchura y profundidad no se copian del cráneo superior.
- `MonsterSDF_Build()` compila tangentes limitadas una vez y una escotadura
  ventral intermandibular. El perfil se estrecha hacia la sínfisis, conserva
  profundidad caudal y no rellena toda la garganta con un bloque.
- `jawPosteriorZ` es ahora la autoridad caudal del evaluador.
  `jawRearClipWeight` conserva explícitamente el comportamiento bóvido anterior
  sin depender del casquete facial. Los équidos usan la bisagra con un pequeño
  margen de tejido posterior. No se desactiva globalmente `faceCapWeight`.
  El snapshot adulto nuevo conserva `faceCapWeight=1` y mide
  `posteriorZ=-0.246992`, frente al límite incorrecto `0.771250`;
  [coordenadas actuales](after/caballo-clay/jaw.txt).
- La boca equina resuelta empieza cerrada. `Mouth_Create()` conserva su valor
  heredado `0.5` para las demás rutas; `Monster_ResolveHead()` sigue conservando
  la apertura que haya solicitado el llamador.
- Un nuevo test de contacto detectó otra causa de separación labial: la cavidad
  oral elipsoidal avanzaba más allá de la comisura y excavaba el labio superior
  cerrado. Su centro y longitud ahora se resuelven detrás de la comisura; la
  entrada incisiva conserva únicamente la hendidura fina.
- Se conserva la extracción mandibular independiente y la rotación existente
  alrededor de la bisagra. No se ha fingido un cierre soldando ambas mallas.

## Iteración visual y decisiones

Se renderizó tras reconstruir la mandíbula, después de ajustar el contacto
labial, tras cambiar la cabeza, tras corregir la cresta cervical y tras cada
revisión de miembros delanteros y traseros. Los intermediarios están bajo
`build/equine-*`; la evidencia persistente está en `before/` y `after/`.

- **Cabeza:** seis secciones regionales opcionales para labios, cartílago,
  huesos nasales, frente, bóveda y poll. Masetero más largo y menos ancho,
  reborde orbital menos expuesto, narinas laterales curvadas y menos profundas.
  Pabellones ligeramente divergentes en el plano sagital, conservando las
  orejas mayores del asno. **No se considera terminada:** el casquete distal
  sigue siendo excesivamente plano y las transiciones orbitales necesitan
  más trabajo.
- **Cuello:** contornos dorsal y ventral independientes. Se sustituye la
  potencia dorsal que retenía la masa junto al poll y se reduce la elevación
  senoidal de `.22` a `.045`. La garganta y la anchura se expanden más tarde
  hacia la base. La comparación lateral muestra la desaparición de la gran
  joroba, pero no certifica medidas veterinarias.
- **Hombro:** ancla escapular alta, caudal y medial; menor anchura transversal
  y distribución longitudinal del brazo/antebrazo. No se aumentan radios de
  unión suave para tapar las bolas. Los cuatro pivotes IK se conservan.
- **Miembro posterior:** perfil propio para muslo y gaskin, con menor expansión
  de babilla y corvejón. Las masas se distribuyen desde pelvis hacia las
  articulaciones, sin reutilizar la sección del miembro anterior.
- **Tronco y grupa:** extremo sacrocaudal más bajo y estrecho. Se corrige un
  error de signo: mover el centro `d/2` y restar `d/2` a la altura dejaba el
  techo intacto, pese al comentario que anunciaba un descenso dorsal. Ahora se
  suma `d/2` a ambos, conservando el vientre y desplazando el techo.
- **Recetas:** autor actualizado primero; abdomen y sección torácica posterior
  recalibrados, documentos regenerados por validación C. Se conservan JSON v1
  e IDs 1005/1006. No se han ajustado parámetros para ocultar el recorte caudal.

## Referencias realmente consultadas

Se revisó [REFERENCIAS.md](../REFERENCIAS.md) y se consultaron de nuevo:

- [ADW, Equus caballus](https://animaldiversity.org/accounts/Equus_caballus/):
  tronco profundo y musculoso, cuello y cabeza alargados, casco monodáctilo
  oval y diferencia entre crin erecta silvestre y crin doméstica caída.
- [Warmblood polaco, Tanya Dewey, 2004](https://animaldiversity.org/collections/contributors/tanya_dewey/horse4/):
  fotografía tres cuartos para continuidad de escápula/pecho, volumen de cuello,
  transición cara–morro y ubicación lateral de los ojos. CC BY-NC-SA 3.0;
  se inspeccionó fuera del repositorio y no se incorpora bajo licencia del código.
- Missouri volvió a responder **403**. No se atribuyen a esta nueva consulta
  mediciones ni ratios que no se pudieron obtener. Los capítulos Elsevier y
  las restantes fuentes de la misión no se han verificado de nuevo en este bloque.

La fotografía no tiene la misma proyección que las cámaras ortográficas del
motor. No existe ajuste píxel a píxel ni una calibración biológica precisa.

## Evidencia y reproducción

```bash
make build/creature_editor_bridge build/capture_bovine_anatomy
python3 tools/generate_equine_recipes.py
python3 tools/validate_equine_anatomy.py --opengl \
  --output docs/validation/equine-reconstruction/refinement/after \
  --compare-with docs/validation/equine-reconstruction/refinement
make test
make editor-test
make build/validate_recipe_gpu
build/validate_recipe_gpu recipes/caballo.creature.json 3
build/validate_recipe_gpu recipes/asno.creature.json 3
```

Cada carpeta `after/{caballo,asno}-{clay,surface}/` conserva vistas completas
lateral/frontal/tres cuartos/posterior/superior, primeros planos cefálicos,
mandíbula aislada, envolvente sin planos, mandíbula sin recorte posterior,
aperturas 0/0.35/1, sección sagital con estaciones, cuello, hombro, cuarto trasero
y cascos. `jaw.txt` contiene las coordenadas resueltas. Las esferas naranjas de
la sección son marcadores diagnósticos C, no parte de la anatomía.

Las cámaras anatómicas mantienen escala y encuadre. La apertura completa usa
un encuadre más amplio para no cortar el mentón; no se usa para medir una
reducción de tamaño. Las vistas `surface` emplean los materiales actuales,
**no demuestran una crin ni una cola pilosa terminadas**.

- [Antes/después: cabeza, mandíbula y cuerpo del caballo](after/caballo-comparison.png).
- [Antes/después: cabeza y cuerpo del asno](after/asno-comparison.png).
- [Hoja de arcilla del caballo](after/caballo-clay/sheet.png).
- [Hoja de arcilla del asno](after/asno-clay/sheet.png).
- [Materiales del caballo](after/caballo-surface/sheet.png).
- [Materiales del asno](after/asno-surface/sheet.png).

La mandíbula aislada de la captura base conserva su color original, mientras
la nueva es gris; la comparación de forma no debe interpretarse como una
comparación de materiales. Las capturas base del asno usan el capturador del
commit original: no incluyen mandíbula aislada ni secuencia articulada.

## Pruebas específicas

`tests/test_equine_anatomy.c` comprueba para ambas especies y cuatro etapas:

- reposo cerrado y límite posterior junto a bisagra;
- muestras interiores en seis secciones mandibulares, incluidas rama y cuerpo;
- independencia respecto a `faceCapWeight`;
- contacto entre labios en tres secciones incisivas, comprobando ambos campos;
- extracción real, extensión cóndilo–sínfisis y distribución por cinco bandas;
- exactamente un componente conectado de la malla indexada;
- articulación de la malla extraída en 0, 0.35 y 1 sin pérdida de vértices;
- conservación de IDs y topología juvenil–adulto, además de casco y apoyo.

No se confunde el conteo de vértices con una demostración de anatomía correcta.

## Resultados de ingeniería

| Comprobación | Resultado |
|---|---|
| `make test` final | PASS, todas las suites; [registro](tests.log) |
| `make editor-test` | PASS, 9 pruebas, 35.026 s; [registro](editor.log) |
| Criaturas anteriores frente a `8920a58` | PASS, 16 casos: lagarto, perro, Brumacerno y Velkar, cuatro etapas; índices, vértices y materiales sin cambios; [datos](legacy-regression.json) |
| Metamorfosis visual perro–lagarto | PASS, referencia 42878, extra 2376 (0.055), faltante 2186 (0.051); [registro](morph-ownership.log) |
| CPU/intervalo/compute/fragmento | PASS, seis recetas adultas, sin aumentar tolerancias; [registro](gpu.log) |
| Caballo, 2590 muestras GPU | compute = fragmento = `8.94069672e-07 m`; intervalo `9.31322575e-08 m` |
| Asno, 2590 muestras GPU | compute = fragmento = `5.36441803e-07 m`; intervalo `2.99885869e-07 m` |

Una ejecución inicial falló en `AppendageMeshContinuity: cola mallada en cada
LOD`; las ejecuciones completas posteriores pasaron. No se ha determinado la
causa de ese fallo aislado ni se ha modificado esa prueba para ocultarlo.
El PASS GPU demuestra concordancia en muestras del campo, no equivalencia
exhaustiva de todos los píxeles ni aceptación anatómica.

### Rendimiento

Mediana de siete pares alternados de 1000 resoluciones, `clock()`, misma máquina,
sin mallado. Se compilaron la base archivada y el árbol actual; no se reutilizan
los tiempos históricos de otra ejecución. [Datos completos](performance.json).

| Receta | Base | Actual | Nodos/conexiones |
|---|---:|---:|---:|
| Caballo | 20.297 µs | 20.426 µs | 77/92 |
| Asno | 19.992 µs | 20.217 µs | 77/92 |

`sizeof(Monster)` pasa de 181000 a 181296 bytes (+296 por los perfiles resueltos);
`CreatureRecipe`, `CreaturePhenotype` y `AnatomyGraph` permanecen en 37704, 8520 y
121880 bytes. No se infiere FPS a partir de estas mediciones.

Preview fino, una ejecución por caso: incluye proceso, resolución, extracción y
serialización; no es un benchmark de FPS. Los tiempos pueden variar con la carga.
Datos base en [baseline-preview.json](baseline-preview.json), actuales en
[after/results.json](after/results.json).

| Receta | Etapa | Vértices base | Vértices actuales | Tiempo actual |
|---|---:|---:|---:|---:|
| Caballo | 0 | 74671 | 72170 | 3.216 s |
| Caballo | 1 | 82374 | 80683 | 3.490 s |
| Caballo | 2 | 90936 | 89023 | 3.671 s |
| Caballo | 3 | 61291 | 64848 | 3.033 s |
| Asno | 0 | 79629 | 81469 | 4.827 s |
| Asno | 1 | 83707 | 84580 | 4.737 s |
| Asno | 2 | 90053 | 90918 | 4.741 s |
| Asno | 3 | 96954 | 94252 | 4.774 s |

### Por qué disminuyen los vértices del caballo adulto

Se instrumentó el puente con las estadísticas del mesher, también en la copia
base, sin alterar sus algoritmos. La caída ya está en la extracción cruda:
juvenil **88784** vértices de cuerpo frente a **58911** adultos. El filtro de
componentes retira sólo **68** y **30** respectivamente: no explica una diferencia
de casi 30000. Ambos casos de caballo indican `budgetAdjusted=false` y
`detailAdjusted=false`, por lo que no es agotamiento del presupuesto.

La ruta `MonsterVisual_RebuildNow()` activa el octree adaptativo incluso si el
puente inicializa `adaptiveDetail=false`. El tamaño base pedido es `.065` en
unidades mundiales para todas las edades; los objetivos regionales sí varían
con el tamaño de los rasgos. Las divisiones son discretas, a mitades. Se miden
espaciados máximos efectivos `.0463276` (juvenil) y `.0543241` (adulto), mínimos
`.00579095` y `.00679051`, y 482553 frente a 370658 celdas visitadas en la base.
La causa principal de esa caída es la selección de detalle adaptativo dependiente
de escala/regiones, no el descarte de una parte adulta por propiedad o componentes.
No se interpreta una malla más pequeña como una optimización anatómica. La
mandíbula incompleta era un defecto independiente, presente también en otras
etapas, y ahora tiene tests espaciales propios.

## Archivos y funciones modificados

- `include/Head.h`, `src/Head.c`: perfiles resueltos opcionales,
  `HeadAnatomy_Resolve()`, reposo equino, boca y rasgos externos.
- `include/MonsterSDF.h`, `src/MonsterSDF.c`: `MonsterSDF_Build()`,
  `MonsterSDF_EvalJawBase()`, límite posterior y cavidad oral compilada.
- `src/AxialBodyUprightTetrapod.c`:
  `AxialBodyUprightTetrapod_BuildSweepStations()`, cresta y contornos axiales.
- `src/LimbMammal.c`: `Cursorial()`, distribución escapular, brazo, muslo y gaskin.
- `tools/generate_equine_recipes.py`, `recipes/{caballo,asno}.creature.json`:
  autor y documentos regenerados mediante validación C.
- `tools/capture_bovine_anatomy.c`, `tools/validate_equine_anatomy.py`:
  diagnósticos, estaciones, articulación, arcilla/materiales y comparaciones.
- `tools/creature_editor_bridge.c`: estadísticas adicionales del mesher en
  metadatos; no cambia el formato binario de vértices ni el algoritmo.
- `tests/test_equine_anatomy.c`: regresiones espaciales y de malla mandibular.
- `src/MonsterSDF{Shader,Compute}.generated.h`: regenerados exclusivamente con
  `tools/generate_sdf_shader.py`.

## Límites y trabajo pendiente

1. El hocico tiene una cara distal demasiado plana y la cabeza aún no alcanza
   el objetivo de warmblood convincente. El masetero, la unión caudal de la
   mandíbula y las órbitas requieren otra revisión de superficie.
2. La reducción de bolas musculares y de joroba es visible; la grupa sigue
   mostrando una transición glútea demasiado redonda en tres cuartos.
3. La captura de marcha intenta `AnimatedVisual_Bind()` y comunica
   `WALK_UNAVAILABLE` si falla. Se ha observado ese fallo con la malla cursorial;
   **no se ha validado una pose de marcha**. Las pruebas de actualización del rig
   no sustituyen esa comprobación visual. Debe resolverse la correspondencia
   entre conexiones de superficie y árbol esquelético antes de aceptar la marcha.
4. No se ha implementado una extensión de pelo largo: crin, flequillo y cola
   siguen pendientes. No se han añadido púas ni ornamentos sólidos para imitarlos.
5. Las etapas de crecimiento son controles del motor, no una ontogenia
   veterinaria calibrada. Falta validación visual exhaustiva de articulación
   durante metamorfosis, además de los tests de extremos y topología.

No se declara terminado el hito anatómico ni la misión completa.
