# Hito 3 — Base de pelo largo por guías

> Evidencia de la implementación anterior, conservada como referencia. La mejora
> de volumen, peinado, mandíbula y colisión se documenta en
> [long-hair-volume](../long-hair-volume/README.md); `make visual-long-hair` genera
> ahora las capturas nuevas allí, sin sobrescribir esta referencia.

## Alcance

Implementación optativa y renderizada: receta v1 → superficie → estaciones
anatómicas → guías → pose → hijos procedurales GPU. El manto corto, los tufts
anteriores y la geometría SDF no se sustituyen. Las recetas de producción de
caballo y asno no se modifican en este hito.

Esto **no** completa la crin ni las colas de los hitos 4 y 5. Los ejemplos de
cresta, parche y soporte caudal son configuraciones de prueba de la misma
infraestructura, no fenotipos finales aceptados anatómicamente.

## Arquitectura

- `SurfacePhenotype.longHair[4]` contiene descriptores, no fibras generadas.
  Cada grupo tiene identidad y semilla, dominio, cantidad de guías/hijos,
  longitud, variación, grosor, dispersión, agrupamiento, rigidez de forma,
  influencia gravitatoria de reposo, densidad y colores de raíz/punta.
- `LongHairDomain` admite curva por hasta ocho IDs de estación, parche elíptico
  proyectado alrededor de una estación y selección por región anatómica/módulo.
  La curva sirve también para trayectorias axiales y cadenas de cola; la banda
  angular controla crestas estrechas o distribución circunferencial.
- Cada raíz conserva grupo, módulo, ID estable, dos estaciones propietarias,
  parámetro longitudinal y posición/normal/dirección de reposo. No se guardan
  índices de triángulos. Cambiar la triangulación no redefine la distribución.
- La proyección usa un snapshot SDF **actual**, sólo al cambiar anatomía o
  controles. No genera otra malla. Una estación ausente retira sus raíces.
  La proyección se limita a un semieje local para no saltar desde un soporte
  oculto hasta la superficie de otro volumen cercano.
- `LongHairSet` pertenece al coordinador visual y reserva sólo las guías. Cada
  guía tiene diez puntos. Su forma inicial conserva longitudes de segmento y
  combina dirección, variación y curvatura gravitatoria de reposo.
- La pose transporta puntos y normal por las dos estaciones propietarias. No
  reconstruye hijos ni modifica las coordenadas de reposo. Un anclaje temporalmente
  no disponible se oculta y puede recuperarse.
- El backend recibe un callback opcional `renderLongHair`. Se integra en
  `MonsterVisual_Render`, `AnimatedVisual` y `MonsterVisualAsync_Render`; el visor
  existente usa esta última ruta. El runtime asíncrono de pelo es propiedad del
  hilo de presentación, no del worker que publica mallas antiguas.
- Se suben 16 vec4 por guía. El shader interpola Catmull-Rom y genera hijos por
  instancia, con dispersión determinista, variación de longitud, agrupamiento,
  estrechamiento y gradiente cromático. No hay arrays CPU de hijos.

La receta superficial GPU anterior conserva sus 139 filas: los datos de guías
viajan en un buffer independiente. Todos los includes SDL/GL siguen confinados
a `src/OpenGLRenderer.c`. El esquema JSON y el encabezado GLSL se regeneran por
sus scripts; los campos nuevos son opcionales en v1.

### Mezcla y controles discretos

Grupos homólogos interpolan longitud, dirección, forma, dispersión y colores.
Cambios de identidad, dominio incompatible o cantidades discretas usan retirada
e introducción con densidad cero en el punto de intercambio. El render reduce
densidad mediante un subconjunto determinista de hijos, no haciendo desaparecer
simultáneamente una tarjeta opaca.

## Evidencia visual e iteración

Capturas genuinas de C/OpenGL sobre Radeon integrada Renoir, Mesa 25.2.8,
OpenGL 4.6 compatibility, MSAA 0. Pillow sólo convierte PPM y compone comparativas.

- [Curva cervical: apagado/encendido](caballo-curve-stage-3-comparison.png).
- [Soporte caudal: apagado/encendido](caballo-tail-stage-3-comparison.png).
- [Parche no équido: perro](perro-patch-stage-3-comparison.png).
- [Guías cervicales](caballo-curve-stage-3/side-q4.png).
- [Guías caudales](caballo-tail-stage-3/rear-three-quarter-q4.png).
- [Pose animada](caballo-curve-stage-3/pose-2.png).
- [Calidad baja](caballo-curve-stage-3/side-q1.png),
  [media](caballo-curve-stage-3/side-q2.png),
  [alta](caballo-curve-stage-3/side-q3.png).

Se conserva la primera implementación en `iteration-1/` y la primera prueba
caudal en `iteration-1-tail/`. Se observaron paquetes demasiado separados y
raíces hijas introducidas en la piel: el disco inicial estaba construido en el
plano transversal de la fibra, no en el plano cutáneo. Se corrigió transportando
también la normal del anfitrión y construyendo la dispersión inicial tangencial.
Además, algunas raíces proximales del soporte caudal saltaban a la grupa durante
la proyección global. La proyección ahora está acotada por la envolvente local;
las raíces tapadas se retiran en lugar de decorar el volumen vecino.
La inspección numérica confirmó que las estaciones caudales proximales de estas
recetas están mayormente dentro de la grupa. El ejemplo final usa las últimas
cuatro estaciones expuestas, sin modificar el soporte ni el SDF. No representa
todavía la distribución pilosa definitiva de un caballo.

Persisten agrupaciones demasiado regulares, puntas punteadas a MSAA 0 y grooming
simple. La cresta se parece todavía a una sucesión de mechones cortos; las colas
de prueba tienen huecos y no distinguen aún el patrón final de caballo/asno.
La barba del perro demuestra reutilización, no una especie nueva terminada.

### Referencias y aproximaciones

La [descripción de ADW de Equus caballus](https://animaldiversity.org/accounts/Equus_caballus/)
sitúa la crin en el dorso cervical y diferencia pelo erecto corto de crines
domésticas largas y caídas. Las fotografías reales inspeccionadas durante los
hitos anteriores, enlazadas en los informes de
[anatomía](../equine-muscle-cohesion/README.md) y
[hocico](../equine-muzzle/README.md), sirven como referencia de continuidad,
densidad y caída. Frente a ellas, los paquetes visibles de estos ejemplos siguen
siendo demasiado regulares. Las dimensiones y colores son controles artísticos
en unidades del motor; no se presentan como mediciones veterinarias.

## Reproducción y pruebas

```sh
make test
make editor-test
make visual-equine
make visual-long-hair
make visual-morph-ownership
make visual-morph-gpu-regressions
```

`visual-long-hair` genera ejemplos fuera del catálogo de producción y captura
curva, parche, cola y perro. Dos configuraciones se recorren en las cuatro etapas.
Cada caso incluye tres vistas, apagado, tres calidades, guías y tres poses.
Excepción explícita: el ejemplo del perro es estático. El vinculador corporal
`AnimatedVisual_Bind` rechaza su malla de prueba gruesa; no se inventan fotogramas
de marcha ni se sustituye por una animación externa. Los ejemplos équidos sí
exigen poses renderizadas diferentes y desplazamiento numérico de sus raíces.
La misma vinculación del perro también falla al compilar el snapshot anterior
al hito 3 (`baseline-dog-binding.log`); es una limitación previa, no una regresión
introducida por las guías.
Los comandos están en `captures.json`; identidades y parámetros, en `roots.json`.
`root-stability.json` compara esos datos entre etapas, no sólo las imágenes.

Los tests nuevos cubren determinismo, propiedad anatómica, reordenamiento de
estaciones, región semántica, parche, finitud, longitudes iniciales, transporte
de raíz, empaquetado, retirada de anfitriones, JSON v1, mezcla discreta,
animación, tres ciclos de crecimiento/morph y propiedad de apariencia async.
La validación GPU usa transform feedback del **mismo shader de dibujo** para
comparar Catmull-Rom con la referencia CPU y rechaza resultados no finitos.

`final-regressions.log` y el log final de pruebas registran los resultados.
Resultados finales:

| Verificación | Resultado |
|---|---|
| `make test` | PASS; `final-core.log` |
| `make editor-test` | PASS, 10 tests; `final-editor.log` |
| `make visual-equine` | PASS, ocho casos; `final-regressions.log` |
| `make visual-long-hair` | PASS, 12 casos; `final-captures.log` |
| Raíces entre etapas | 96 identidades estables en cuatro etapas por configuración; `root-stability.json` |
| Guías CPU/GPU | PASS, error máximo `1.068e-6`; transform feedback del shader real |
| Regresión de especies anteriores | 16 mallas idénticas en índices, posiciones, normales y colores; reordenamiento con delta 0 |
| Paridad SDF compute/fragmento/intervalos | PASS, caballo, asno, lagarto, perro, Brumacerno y Velkar; `final-recipe-gpu.log` |
| Morph ownership / GPU | PASS, incluido worker retrasado 3000 ms y comparación RGB con error 0; `final-regressions.log` |

El primer fallo conservado en `tests-initial-fixture.log` era del esqueleto
sintético del test: su `id` no coincidía con el ID anatómico requerido por
`Skeleton_FindJoint`. Se corrigió la fixture; no se relajaron tolerancias.

## Rendimiento

Datos por caso en `performance.json` y `*-stage-*/capture.log`:

- `roots`: guías candidatas, visibles, generación/proyección en ms, error de
  raíz visible, bytes del runtime y bytes subidos a GPU.
- `off` y `on`: ms/fotograma y FPS sincronizados a 900×900; `on` añade el tiempo
  GPU de ribbons entre timestamps. Este tiempo GPU excluye subir el buffer.
- `pose`: transporte/actualización de guías y animación del cuerpo, medidos por
  separado. **No** es coste de simulación física de pelo: todavía no existe.
- `render`: guías candidatas, hijos visibles candidatos y segmentos por ribbon.
- `parity`: validez y máximo error CPU/GPU en unidades del motor.
- `morph`: interpolación CPU del monstruo y actualización/regeneración de pelo
  por separado, sobre 21 pasos juveniles/adultos y sin remallar el cuerpo.

Se descartan 15 fotogramas y se miden 30 por configuración, con `Finish` antes
de presentar. Son encuadres locales con malla de 0.045 × escala, no FPS de una
escena completa ni una comparación directa con los encuadres del hito 2.
Las calidades reducen hijos y subdivisiones mediante prefijos estables; no
regeneran raíces. Estos presets son diagnósticos, no un LOD automático terminado.

Mediciones finales adultas, calidad alta (24 segmentos por ribbon):

| Ejemplo | Guías / hijos | Generación (ms) | Pose CPU (ms) | Ribbons GPU (ms) | Sin / con pelo (ms) | FPS sin / con |
|---|---:|---:|---:|---:|---:|---:|
| Curva cervical caballo | 96 / 4608 | 6.207 | 0.161 | 0.480 | 5.174 / 5.970 | 193.28 / 167.52 |
| Soporte caudal caballo | 96 / 4608 | 1.430 | 0.167 | 0.548 | 4.493 / 5.570 | 222.56 / 179.55 |
| Curva cervical asno | 96 / 4608 | 4.661 | 0.162 | 0.487 | 5.783 / 6.325 | 172.92 / 158.11 |
| Soporte caudal asno | 96 / 4608 | 1.936 | 0.171 | 0.528 | 3.195 / 3.953 | 313.00 / 252.99 |
| Parche perro, estático | 64 / 3072 | 3.595 | — | 0.328 | 8.659 / 9.515 | 115.49 / 105.10 |

Durante los 21 pasos de crecimiento/morph del caballo, la interpolación CPU
cuesta 0.080 ms y actualizar/regenerar pelo cervical, 6.084 ms por paso; para su
cola, 0.047 y 1.536 ms respectivamente. Esta regeneración por cambio anatómico es
mucho más cara que transportar una pose y sigue pendiente optimizarla. No se
atribuye al coste GPU ni se oculta como si fuera simulación por fotograma. Las
actualizaciones de pose verifican que no cambian generaciones de malla ni guías.

Memoria lógica: `LongHairGroup` ocupa 176 bytes y `LongHairGuide`, 328. Un runtime
de 96 guías ocupa 32240 bytes; sus datos GPU ocupan 24576 bytes, más otros 24576
bytes del staging CPU reutilizable del renderer. No incluye memoria interna del
driver ni equivale a medir RSS. `SurfacePhenotype` pasa de 2368 a 3080 bytes,
`CreaturePhenotype` de 9096 a 9808 y `CreatureRecipe` de 40008 a 42856. Las recetas
sin grupos no reservan guías ni inicializan recursos GPU de pelo largo.

## Archivos y límites

La lista exacta del incremento respecto al árbol local inicial está en
`changed-files.json`; `session.patch` aísla el diff sin descartar cambios previos.
`artifacts.json` enumera las evidencias. El snapshot inicial se conserva fuera
del repositorio en `/tmp/opencode/long-hair-start`.

Pendiente para los hitos 4–6:

- Crin y colas de producción, distribución específica, grooming y colisiones.
- Verlet/PBD, viento, inercia, amortiguamiento y conservación física de longitud
  al doblar. La gravedad actual sólo construye una forma de reposo; el transporte
  cinemático no sustituye una simulación.
- Sombras y respuesta óptica avanzada, AA mejor, cobertura lejana y transiciones
  automáticas de LOD sin pérdida de masa aparente.
- Controles dedicados del editor y aceptación visual exhaustiva de cruces
  interespecíficos/topologías nuevas. El lector/esquema v1 ya transporta los datos.
- Selección semántica limitada a ocho estaciones axiales por grupo; una selección
  mayor se retira y debe especificarse con una ruta de hasta ocho estaciones.

No se ha añadido pelo largo al SDF ni inflado el dock para simular una cola pilosa.
