# Animación esquelética procedural

La morfología crea la criatura; la pose mueve su malla existente. El primer
adaptador es el lagarto, pero Skeleton, IK, Locomotion y AnatomyDeformer no
consultan especies ni identidades reservadas del lagarto.

## Responsabilidades y propiedad

| Capa | Responsabilidad |
| --- | --- |
| `AnatomyGraph` | Anatomía de reposo resuelta por el fenotipo; conserva la identidad geométrica. |
| `Skeleton` | Árbol de articulaciones, identidades anatómicas, transformados locales de reposo y límites. |
| `SkeletonPose` | Transformados locales mutables; los mundiales siempre se reconstruyen con FK. |
| `Rig` / `RigBuilder` | Mapeo anatómico, cadenas funcionales y roles de miembros. |
| `IKChain` | Secuencia contigua y objetivo espacial, independiente del gait. |
| `GaitProfile` / `Locomotion` | Ritmo, apoyos mundiales persistentes y trayectorias de balanceo. |
| `ProceduralAnimator` | Coordina columna, cola, cuello/cabeza, mandíbula y objetivos de extremidades. |
| `AnatomyDeformer` | Vincula una malla de reposo y aplica morfología o pose en O(V). |
| `MonsterAnimation` | Estado poseído por Monster: rig, pose y controlador. |
| `AnimatedVisual` | Bases de ojos/cabeza y bindings de una generación de MonsterVisual. Propiedad del consumidor visual. |

`Monster_Free` destruye la animación. `Monster_CopyInto` y `Monster_Clone`
producen snapshots de **morfología**: no copian ni comparten animación mutable.
El destino pierde su animación anterior. Para iniciar animación en un snapshot,
instalar explícitamente un rig con `MonsterAnimation_Configure`.

`Lizard_BuildMonster` instala automáticamente el rig correspondiente a la anatomía
que acaba de resolver. Reconstruir la morfología conserva velocidad, fase,
apertura y dirección de mirada; ajusta la altura de apoyo a las nuevas proporciones
y reinicializa contactos. No se conserva el apoyo antiguo a costa de estirar huesos.

## Convenciones matemáticas

Y es vertical; el lagarto mira hacia +Z. Ángulos esqueléticos en **radianes**;
la boca heredada usa grados y el adaptador convierte una sola vez.
`Quat_Multiply(a,b)` aplica b y después a. Quaternion mantiene producto de Hamilton,
normalización defensiva, inversión, arco corto en slerp y manejo determinista de
vectores opuestos en FromTo. Una entrada degenerada de rotación produce identidad.

El padre precede al hijo en los arrays. `restPosition` es local al padre; la raíz
usa coordenadas del modelo. Los límites se expresan como desviaciones de
`restRotation`. La traslación mundial del actor se aplica a la raíz de la pose,
nunca a los centros del grafo de reposo.

## Construcción del rig

`RigBuilder_FromAnatomy` valida un árbol conectado y orienta las conexiones desde
una raíz elegida por el perfil, sin cambiar las aristas del grafo original. Usa IDs
estables para resolver referencias, no posiciones en el array. Rechaza ciclos,
grafos desconectados y cadenas funcionales que no siguen padres contiguos.

`LizardRig` elige la pelvis como raíz, invierte la dirección esquelética de la rama
axial anterior y registra hombro–codo–muñeca–mano y cadera–rodilla–tobillo–pie.
Los dedos existentes siguen a sus autopodios por FK. Identifica también columna,
cuello, cabeza y cuatro estaciones de cola. La mandíbula añade un joint estable
asociado anatómicamente a la cabeza y situado en el pivote oral resuelto.

Los ejes de codo/rodilla derivan del producto vectorial entre los segmentos en
reposo. Sus límites se calculan respecto a la flexión anatómica existente: no
pueden atravesar la extensión recta e invertir su flexión. Hombros/caderas,
muñecas/tobillos y cabeza usan conos y límites de twist definidos en LizardRig.
La suela considera tanto el autopodio como los extremos anatómicos de los dedos.

## FABRIK y restricciones

El solver realiza una pasada hacia el objetivo y otra desde la raíz fija. En la
segunda convierte las direcciones a rotaciones locales y las proyecta sobre los
límites. Propaga inmediatamente la posición **real** resultante; la siguiente
articulación nunca usa una posición que contradiga el límite anterior.

La proyección de bisagras puede estancar un FABRIK puramente posicional. Una
relajación angular desde el efector hacia la raíz corrige el residuo usando el
objetivo final y aplica de nuevo los mismos límites. Esta corrección no modifica
longitudes. Se conserva la mejor pose encontrada, con presupuesto explícito
(máximo defensivo 256 iteraciones) y residuo real en `IKResult`.

FIXED conserva rotación de reposo; HINGE proyecta al plano de la bisagra y acota el
ángulo con signo; BALL descompone swing/twist, acota el cono y la torsión, y recompone.
Objetivos inalcanzables devuelven una pose finita y `converged=false`. Segmentos
nulos no se dividen por su longitud. El algoritmo no contiene IDs ni ángulos del lagarto.

## Deformación sin remallado

La implementación original de LizardMorph se ha trasladado a AnatomyDeformer.
LizardMorph conserva funciones y tipos compatibles como puente para el ager.
La ruta morfológica sigue vinculando dos conexiones dominantes por vértice y
adaptando sus radios. Ahora resuelve extremos por identidad también si se reordenan
las conexiones, y maneja rotaciones antiparalelas mediante Quaternion.

`AnatomyDeformer_BindSkeleton` reutiliza esos pesos y deriva hasta cuatro influencias
articulares. La transición distal es suave; las articulaciones fijas no añaden una
influencia redundante. Cada frame precomputa matrices rest→pose y transforma bases
inmutables de posición y normal. Incluye torsión: cambiar solo los centros de una
copia del grafo no bastaría para representar twist.

`SkeletonPose_ToAnatomy` permite obtener una copia temporal de centros posados para
consultas. No es necesario construirla para deformar: el deformer consume los
transformados completos de la pose. Ninguna de estas rutas escribe en el grafo de reposo.

AnimatedVisual transforma también las bases de los ojos y la cabeza separada, si
existe. Para la boca obtiene el ángulo del joint mandibular, usa una copia temporal
del parámetro de apertura y reutiliza el articulador existente de mandíbula y tejido
gular. Después aplica el transformado de cabeza, sin duplicar la apertura.

## Fotograma normal

```text
velocidad deseada
  → locomoción: traslación corporal + objetivos mundiales de pies
  → pose de reposo + FK de columna, cola, cuello/cabeza y mandíbula
  → FABRIK por miembro + orientación del autopodio sobre la normal
  → FK final
  → deformación desde bases inmutables
  → renderizado
```

El FK axial se aplica **antes** de IK, para que el movimiento de columna no desplace
pies ya resueltos. Las funciones de actualización/deformación no asignan memoria,
no llaman a MonsterSDF_Build ni a los meshers y conservan índices y buffers.

```c
/* Tras construir morfología y una generación visual nueva. */
AnimatedVisual binding = {0};
AnimatedVisual_Bind(&binding, &visual, &monster);

/* Bucle normal; velocity está en coordenadas del mundo. */
monster.animation->animator.desiredVelocity = velocity;
MonsterAnimation_Update(&monster, dt);
AnimatedVisual_Deform(&binding, &visual, &monster);
MonsterVisual_Render(&visual, &renderer);

/* Liberar binding antes de destruir visual/monster. */
AnimatedVisual_Free(&binding);
```

Comprobar los retornos booleanos en código de aplicación. Una generación visual o
del rig diferente invalida bindings. `AnimatedVisual_Bind` comprueba además que la
morfología corresponde a la geometría publicada. Repetir Bind sobre la misma
generación es idempotente: no toma una malla ya posada como base nueva.

## Gait y terreno

LizardGaits_Walk define fases diagonales 0/.5/.5/0, apoyo del 68%, ciclo de 1,4 s,
zancada y elevación escaladas con el fenotipo. El controlador recorre miembros
registrados, no nombres ni especies. Suaviza la velocidad y acota su magnitud al
perfil. Reduce el avance de fase a velocidades bajas.

En stance el objetivo permanece fijo en coordenadas mundiales. Al entrar en la
ventana de swing se elige el siguiente apoyo a partir de la velocidad, la posición
neutral del efector y una consulta al suelo. La trayectoria interpola suavemente
y añade elevación sinusoidal; al aterrizar guarda un nuevo contacto. Al parar,
los balanceos ya iniciados se completan y el cuerpo desacelera hasta reposo.

World_SampleGround usa el callback nuevo de posición/normal o adapta el callback
heredado de altura con diferencias finitas. Sin World ofrece un plano Y=0. Una
consulta fallida no inventa un nuevo apoyo. Inicializar World con ceros antes de
asignar callbacks. El autopodio intenta alinear su eje superior con la normal,
sujeto a límites articulares. No hay motor de colisiones ni búsqueda de obstáculos.

Columna y cola usan ondas FK leves ligadas a fase/velocidad. El cuello y la cabeza
reparten una orientación de mirada acotada. La mandíbula sigue mouthOpen mediante
un joint bisagra y el puente de tejidos ya existente.

## Morfología, publicación asíncrona y ager

Solo un cambio verdadero de fenotipo requiere construir otra anatomía, rig, SDF,
malla y bindings. El benchmark `--morphology` verifica juvenil caminando → adulto
caminando, conserva la fase y rechaza bindings antiguos antes del remallado legítimo.

El ager existente conserva su ruta GPU SDF y su ruta `--mesh` asíncrona. No se aplica
skinning a buffers del worker ni se modifica la política de publicación. Esta primera
integración visual de animación usa MonsterVisual síncrono: para integrar otro consumidor
asíncrono, vincular **la anatomía del snapshot publicado** cuando cambia displayGeneration,
nunca la solicitud más reciente mientras la malla anterior sigue en pantalla. Animación
y worker deben poseer buffers separados. El ager GPU no representa poses esqueléticas.

## Registrar otra criatura

1. Resolver su AnatomyGraph y elegir una raíz funcional.
2. Usar RigBuilder_FromAnatomy y ajustar restricciones por articulación.
3. Registrar las cadenas con RigBuilder_AddLimb e indicar qué miembros caminan.
4. Asignar listas de columna/cola y joints opcionales de cabeza/mandíbula.
5. Proporcionar GaitProfile y llamar MonsterAnimation_Configure.
6. Generar malla de reposo y vincular AnatomyDeformer; adaptar accesorios visuales si existen.

Capacidades actuales: 200 joints, 32 joints por cadena, 16 miembros y 32 estaciones
por controlador axial. Son límites explícitos, no suposiciones de cuadrupedia. Los
grafos con ciclos requieren que un perfil futuro seleccione un árbol articulado.

## Demos y validación

```sh
make all test
./demos/demo_lizard_ik --limb=0 --debug
./demos/demo_lizard_jaw
./demos/demo_lizard_walk
./demos/demo_lizard_walk --headless --frames=600
./demos/demo_lizard_walk --frames=180 --capture-prefix=/tmp/walk
make benchmark-animation
./benchmarks/benchmark_animation --morphology
```

D alterna el rig; 1..4 cambian vista; espacio pausa. `--view=0..3`, `--capture=...`,
`--frames=N`, `--mode=ik|jaw|walk` y `--limb=0..3` permiten ejecución reproducible.
Las capturas con prefijo incluyen cuatro vistas cada 30 fotogramas. El suelo
cuadriculado proporciona referencias mundiales aunque la cámara siga al cuerpo.

La suite nueva cubre cuaterniones, jerarquía, restricciones, objetivos alcanzables
e inalcanzables, 480 objetivos 3D derivados de poses válidas, segmentos nulos,
presupuesto de iteraciones, malla SDF deformada, reposo, topología, contactos,
transiciones stance/swing, parada, terreno inclinado, snapshots y cambio juvenil/adulto.
El benchmark intercepta malloc/calloc/realloc, MonsterSDF_Build y ambos puntos de
entrada de SDFMesher para comprobar su ausencia durante animación normal.

Resultados y evidencias de la ejecución: [validación](validation/procedural-animation/README.md).

## Límites de este hito

- Skinning lineal CPU: puede perder volumen en flexiones extremas; no hay dual quaternions ni GPU skinning.
- La marcha inicial avanza con orientación corporal fija; no hay controlador completo de giros, carrera o saltos.
- Los dedos siguen al pie por FK; no tienen contactos independientes ni colisiones.
- El suelo proporciona altura/normal; no se resuelven escalones grandes, obstáculos ni equilibrio dinámico.
- Una sustitución morfológica reinicia apoyos y puede producir una transición visible; no hay mezcla entre dos rigs/mallas publicados.
- El puente oral usa la primera articulación mandibular compartida; criaturas con múltiples bocas requieren un perfil de accesorios ampliado.
- Las restricciones no garantizan evitar todas las autointersecciones de la superficie.
- La densidad de una malla SETTLED alta puede superar el presupuesto de 16,7 ms en CPU. No se reduce calidad del SDF para ocultar ese coste.

Próximos pasos: skinning GPU/dual quaternions, consumidor asíncrono de poses con
snapshot publicado explícito, giro y adaptación corporal al terreno, y contactos digitales.

## Comprobación instrumentada

```sh
make BUILD_DIR=/tmp/monster-animation-asan-build \
  TEST_BIN=/tmp/monster-animation-asan-tests \
  CFLAGS='-Wall -Wextra -Wpedantic -O1 -g -Iinclude -Itests -DMONSTER_TEST_INSTRUMENTED -fsanitize=address,undefined -fno-omit-frame-pointer' \
  /tmp/monster-animation-asan-tests
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1 /tmp/monster-animation-asan-tests
```

MONSTER_TEST_INSTRUMENTED omite únicamente el límite temporal absoluto de 15 ms
introducido por la prueba morfológica existente. Todas sus comprobaciones geométricas,
de visibilidad y de memoria siguen activas. El presupuesto temporal se comprueba
con la suite release normal, sin esa macro.

La validación descubrió dos detalles previos: tipos oculares distintos con igual
layout en la ruta asíncrona (advertencias del compilador, no fallo de enlace), y
memcpy de cero índices con punteros nulos en mallas orales vacías (UBSan). La ruta
asíncrona ahora comparte el tipo ocular y se protege la copia cuando no hay índices.
Las referencias del deformador asíncrono usan el typedef compatible extraído.
