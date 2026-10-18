# Interacciones orales con límites anatómicos

## Diagnóstico del comportamiento anterior

La longitud de los huesos ya se conservaba: `SkeletonPose_UpdateWorldTransforms`
compone traslaciones locales fijas con rotaciones. Por tanto, una superficie que
parece estirarse no demuestra que el solver haya alargado un hueso.

Se encontraron cuatro problemas independientes:

1. `OralReach` elegía exclusivamente el menor residuo posicional. Su semilla
   analítica ignoraba la componente lateral del objetivo y probaba ambos sentidos
   de flexión, 25 orientaciones cefálicas y hasta cuatro reinicios de 32 pasadas.
   No valoraba orientación oral, comodidad, neutralidad ni continuidad de la
   solución elegida. La inserción cervical permitía 2.55 radianes, aproximadamente
   146 grados. El suavizado limitaba velocidad, pero no aceleración.
2. El enlace de piel reconocía el pivote auxiliar al validar la conexión, pero
   asignaba el extremo proximal al tórax. Durante la flexión, parte del cuello
   permanecía anclada al tórax mientras su extremo distal seguía al cuello.
   La nueva regresión `auxiliarySkinTest` comprueba un vértice proximal cuya
   posición debe seguir exactamente la rotación del pivote, sin cambiar su radio.
3. La IA aceptaba bebida dentro de una tolerancia fija de 0.3 unidades. No exigía
   orientación ni permanencia de contacto. `AIWorld_CanDrink` describía la orilla
   de un estanque; no era un contrato universal para cuencos o comida elevada.
4. La aproximación debía concluir antes de intentar alcanzar. Además, recalcular
   en cada fotograma un destino angular de reposicionamiento desde la posición
   corriente producía una persecución circular del propio destino. Ahora se
   conserva cada destino hasta llegar, fallar o desplazarse el recurso.

La instrumentación previa está en `validation/interactions/before-reach.txt`,
`before-scene.csv` y `before-scene.log`. En los cuatro sondeos, el código anterior
consumía entre 0.30 y 0.80 ms por actualización corporal. En el objetivo a diez
unidades seguía optimizando una postura cuyo residuo permanecía entre 7.45 y
7.98 unidades. La ejecución previa de 600 fotogramas no completó ninguna bebida.

La revisión de los morros reveló un quinto problema: las criaturas con grafo
anatómico incluyen el cráneo en la malla general, donde recibía pesos de varios
segmentos cervicales. Los ojos y la mandíbula explícita seguían rígidamente la
cabeza y dejaban de encajar con aquella superficie deformada. En GPU también
faltaba aplicar la pose mandibular a la mandíbula incluida en el campo disperso.

Ahora `cranialSkinTest` reproduce aquella desviación respecto al cráneo rígido
(0.310119 unidades en el lagarto y 0.180242 en el perro) y verifica que cráneo,
morro, narinas y anclajes faciales conservan sus distancias, con error inferior
a 0.000001 unidades. `AnatomyDeformer_BindCranium` compila los pesos una vez:
delante del plano occipital sigue exclusivamente la cabeza; sólo el collar
posterior mezcla piel. El plano y su anchura proceden de los landmarks y de las
dimensiones craneales resueltas, sin identificar especies.

La validación con mutación detectó que la proximidad a conexiones no basta para
clasificar el cráneo: un tórax más ancho puede convertirse en su vecino más
cercano. El bind CPU consulta también el campo cefálico ya compilado; la GPU
conserva una etiqueta de propiedad craneal procedente de las operaciones SDF.
Esta propiedad prevalece sobre la proximidad. No oculta ni elimina triángulos.

La superficie GPU aplica el mismo plano y conserva la identidad mandibular al
extraer el campo. La mandíbula gira rígidamente sobre su bisagra con la pose
actual, descontando la apertura ya presente en la geometría de referencia.
El puente posterior conserva pesos de anclaje entre cráneo y mandíbula; esos
pesos se calculan al extraer la superficie, no durante cada fotograma. La
cobertura y el peso de bisagra se empaquetan con ocho bits cada uno en el atributo
material existente, junto con las etiquetas de articulación.
No se regenera la geometría para abrirla o bajar la cabeza. El diagnóstico
`--validate-skinning` captura las salidas del shader de vértices mediante
transform feedback y comprueba cabeza y mandíbula tanto cerrada como abierta.
También se corrigen posición y eje de bisagra cuando la boca tiene orientación
propia. La ruta mandibular GPU actual admite una boca articulada por criatura.

No se ha introducido dual quaternion skinning. LBS permanece en el tejido blando
del cuerpo y del collar; no se garantiza volumen exacto para toda rotación extrema.

## Perfil compilado y resolución

`AnatomicalReach_Compile` recoge la cadena real desde la cabeza hasta la inserción
cervical, y opcionalmente dos ancestros torácicos registrados por el rig. Admite
hasta ocho articulaciones, sin identificar especies por su nombre. Guarda
identidades, longitudes, reposo, restricciones duras, rango cómodo, dirección
preferida de flexión, efector oral y presupuesto de movimiento. Las dimensiones
proceden de la anatomía resuelta; el comportamiento se configura con `ReachPreset`.

El preset actual usa velocidad angular máxima de 1.2 rad/s, aceleración de
4 rad/s² y comodidad al 65% del recorrido. La dirección preferida se deriva del
segmento y del eje de bisagra. Los límites de la inserción cervical del rig son
[-0.45, 1.4] rad; los conos de cuello y cabeza son 0.75 y 0.95 rad respectivamente.
Son valores de configuración conservadores, no mediciones veterinarias ni una
promesa de validez biomecánica para todas las anatomías futuras.

La búsqueda combina CCD con ensayo acotado de pasos y descenso sobre ejes
locales. Su objetivo incluye error 3D, orientación, esfuerzo fuera del rango
cómodo, flexión inversa y distancia a la pose anterior. Ejecuta hasta doce pasadas
por resolución, a 30 Hz. La trayectoria se integra a 120 Hz con amortiguación,
límites de velocidad y aceleración. No se cambian traslaciones cervicales ni
longitudes. No hay asignaciones dinámicas ni generación geométrica en el solver.

`ReachResult` comunica posición y dirección oral **reales**, residuos posicional
y angular, saturación de límites, velocidad articular máxima, tiempo de contacto,
trabajo efectuado y error de apoyos. Sus estados son:

| Estado | Significado |
|---|---|
| `REACH_REACHED` | Error y orientación dentro de tolerancias, movimiento lento. |
| `REACH_APPROACHING` | Pose válida; el movimiento aún no ha terminado. |
| `REACH_UNREACHABLE` | El objetivo excede la envolvente geométrica de longitud. |
| `REACH_BLOCKED` | Colisión, apoyos incompatibles o búsqueda estancada. |
| `REACH_INVALID` | Objetivo, tiempo, perfil o esqueleto incompatibles. |

Una búsqueda estancada se comunica como bloqueo: el mínimo local no constituye
una demostración de imposibilidad anatómica. Ningún objetivo se sustituye por su
proyección sobre una esfera para declarar contacto.

## Compensación corporal

El cuello se utiliza primero. Tras dos segundos pueden participar los ancestros
torácicos, siempre dentro de sus restricciones originales. La bajada corporal es
vertical y está limitada al 20% de la pata más corta y al espacio libre bajo las
estaciones torácicas. Conserva los objetivos originales de apoyo. Esta bajada es
una postura de agachamiento, no una traslación de navegación hacia el recurso.
La raíz no recibe ninguna corrección horizontal para simular alcance.

Antes de publicar se comprueba el alcance de las patas, el suelo y la envolvente
de apoyos cuando el reposo axial ya está contenido en ella. La IK de extremidades
se ejecuta sobre la postura candidata. Si sus residuos superan 0.004, se descarta
la compensación y se vuelve a resolver sobre los apoyos anteriores, con un máximo
de dos intentos. El adaptador de escena comprueba estaciones y conexiones como
volúmenes conservadores contra los cilindros 3D registrados en `AIWorld`.

El centro axial es una aproximación de equilibrio, no un centro de masa físico.
El sistema no implementa dinámica rígida, contactos de mallas arbitrarias ni
colisiones continuas de todo el cuerpo entre dos poses. Los objetos decorativos
que no se registran como obstáculos no añaden restricciones físicas.

## Contrato entre cerebro, cuerpo y recursos

`InteractionTarget` contiene ID y revisión del objeto, punto mundial, normal,
dirección de entrada, tipo, tolerancias y datos de movimiento/validez. El solver
usa el marco de `SkeletonPose`; el adaptador transforma posiciones, direcciones
y tolerancias entre ese marco y el mundo. Un punto de contacto nunca se obtiene
proyectando la boca al suelo universalmente.

El proveedor actual de `AIWorld` describe discos orientables: proyecta sobre la
superficie real, limita al borde y aplica la altura de la comida sobre su normal.
Un recurso puede definir su dirección de entrada. Para agua sin dirección
explícita, la acción escoge entrada oblicua desde el borde, combinando tangente y
normal. No exige a todas las anatomías una boca vertical. La tolerancia angular
predeterminada es 1.4 rad; objetivos explícitos pueden exigir tolerancias menores.
La tolerancia de posición corporal deriva del efector oral y se limita también
por el tamaño del recurso.

La escena distingue agua que ocupa suelo de agua elevada cuyo plano permite
caminar debajo: el atributo `walkable` lo declara, sin comprobar nombres de
objetos. Para superficies transitables, la posición de aproximación usa la
proyección oral; para estanques mantiene separación y margen de orilla.

El cerebro publica intención y el cuerpo devuelve certificación:

`SEARCH → APPROACH → ALIGN → REACH → STABLE_CONTACT → CONSUME → WITHDRAW`

También existen `REPOSITION`, `UNREACHABLE` y `CANCELLED`. La acción exige ID
coincidente, contacto geométrico 3D actual, orientación aceptada, boca corporal
certificada, velocidad corporal baja y al menos 0.25 s de contacto continuado.
Sólo entonces satisface hidratación o hambre. El recurso se consulta de nuevo;
no se consume con un contacto viejo tras su desplazamiento o retirada.

Hay como máximo dos reposicionamientos antes de un fallo, seguido de memoria y
cooldown de ocho segundos. El preset `forager` habilita hambre y las acciones de
comida. `balanced`, `solitary`, `social` y `waterless` conservan sus necesidades
configuradas. `CreatureActions_Cancel` cancela explícitamente sin consumir.

`MonsterAnimation_Configure` recompila el perfil y descarta contacto certificado.
Transporta rotaciones por identidad y conserva velocidades compatibles. Un
perfil cuya identidad, longitud o restricciones ya no coinciden con el esqueleto
se rechaza. Los cambios de apariencia no reconstruyen geometría por interacción.

## Archivos modificados para esta tarea

| Archivos | Finalidad |
|---|---|
| `include/InteractionTarget.h` | Objetivo, resultado y fases compartidos. |
| `include/AnatomicalReach.h`, `src/AnatomicalReach.c` | Compilación, búsqueda y trayectoria acotadas. |
| `include/ProceduralAnimator.h`, `src/ProceduralAnimator.c` | Integración, compensación y validación de apoyos. |
| `include/RigBuilder.h`, `src/CreatureRig.c` | Envolventes, límites, plano craneal y bisagra mandibular orientada. |
| `src/MonsterAnimation.c` | Recompilación y transporte compatible al mutar. |
| `include/AnatomyDeformer.h`, `src/AnatomyDeformer.c`, `src/AnimatedVisual.c` | Pivote auxiliar, cráneo rígido y apertura mandibular orientada. |
| `include/OpenGLRenderer.h`, `src/OpenGLRenderer.c` | Pose craneal/mandibular GPU y captura de diagnóstico. |
| `shaders/sdf_surface_mesh.vert`, `shaders/sdf_scene.glsl`, `shaders/sdf_surface_emit.comp`, `shaders/sdf_sparse_material.comp`, `shaders/sdf_surface_material.glsl` | Vinculación craneal y propiedad mandibular persistente; cabeceras regeneradas con el generador. |
| `include/AIWorld.h`, `src/AIWorld.c` | Superficies orientadas y barridos volumétricos 3D. |
| `include/CreatureMotor.h` | Intención y certificación corporal independientes. |
| `include/CreatureMind.h`, `include/CreatureActions.h`, `src/CreatureActions.c` | Contacto estable, consumo, retirada y recuperación. |
| `include/BrainProfile.h`, `src/BrainProfile.c`, `src/CreatureDecision.c` | Necesidad de hambre, acciones y preset `forager`. |
| `src/CreaturePerception.c` | Acotación del coseno para visión de 360 grados. |
| `demos/demo_game_scene.c` | Cuenco/comida, casos reproducibles y depuración. |
| `tests/test_anatomical_reach.c`, `tests/main_test.c` | Nueva suite de regresión integrada. |
| `tests/ai_test_helpers.h`, `tests/test_creature_actions.c` | Certificación corporal explícita en pruebas de IA. |
| `benchmarks/benchmark_interactions.c`, `Makefile` | Instrumentación reproducible y registro de fuentes. |

El checkout ya contenía cambios anteriores. Esta tabla describe únicamente el
trabajo de interacciones, no todo el diff respecto a Git.

## Reproducir

```sh
make -B -j6 test demos benchmarks/benchmark_interactions
benchmarks/benchmark_interactions

demos/demo_game_scene --headless --cycles=0 --validate-ai --frames=1800 --fixed-dt=.016666667 --seed=137 --benchmark-uncapped --profile=/tmp/interactions.csv

for caso in bowl ground raised high moving blocked; do
  demos/demo_game_scene --headless --cycles=0 --interaction-case="$caso" --frames=720 --fixed-dt=.016666667 --seed=137 --capture-prefix="/tmp/$caso" > "/tmp/$caso.log" 2>&1
done
```

`--interaction-case=pond` conserva el estanque como único recurso de agua para
inspeccionar rechazos físicos de la orilla. La escena normal añade un cuenco y
comida a altura intermedia. `--dog-brain=forager --lizard-brain=forager` permite
seleccionar alimento a través de la necesidad de hambre. `--debug` muestra cadena,
objetivo, normal y contacto, y registra límites, residuos y estado.

Las capturas `before-ai-2-drink.png` y las capturas `*-stable.png` de
`validation/interactions/` muestran el estado anterior y contactos certificados.
No son fotogramas equivalentes: la captura antigua corresponde a la entrada en
la acción, y la nueva a contacto estable. Las vistas laterales complementan la
inspección de cuello, tronco y apoyos.

## Resultados y límites de validación

La suite nueva verifica posiciones y orientaciones producidas por poses válidas,
longitudes, límites, velocidad y aceleración, contacto sostenido, objetivos
imposibles, obstrucción vertical, invalidación por movimiento, cancelación,
comida, recompilación juvenil y enlace de piel del pivote. Los objetivos estáticos
producen la misma pose a 30, 60 y 120 Hz con diferencia menor de 1e-5. Esto no
significa que la IA y toda la marcha tengan trazas idénticas con cualquier paso:
percepción, navegación y suelo conservan sus planificaciones existentes.

La evidencia ejecutada y las mediciones finales se recogen en
[el informe de validación](validation/interactions/RESULTADOS.md). La imposibilidad
geométrica se distingue de un bloqueo de búsqueda; los rechazos del estanque en
una postura de lagarto no se ocultan con consumo a distancia. La selección de un
cuenco alternativo forma parte de la integración de IA.
