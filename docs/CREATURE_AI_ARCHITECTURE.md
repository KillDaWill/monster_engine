# Inteligencia modular de criaturas

La inteligencia pertenece a la entidad, no a su receta anatómica. `BrainProfile` describe una personalidad reutilizable; `CreatureMind` contiene el estado privado del individuo. El mismo perfil controla cualquier cuerpo que publique `CreatureBodyState` y ejecute `CreatureIntent`. El núcleo de IA no incluye SDL/OpenGL, no consulta shaders, no reconstruye rigs y no evalúa SDF.

La bebida usa el landmark oral real transformado por la pose. El rig incorpora un pivote cervical auxiliar en la inserción torácica, ligeramente ventral según el radio anatómico del anfitrión. El controlador corporal orienta esa rama con CCD acotado y transiciones de hasta 1,2 rad/s; no mueve el tronco ni cambia los objetivos plantares. El cerebro sólo pide mirar e interactuar con la orilla. La interacción exige una distancia tridimensional máxima de 0,3 unidades al agua; representa el pequeño alcance oral, sin proyectar la boca al suelo ni permitir beber al otro lado del estanque.

## Separación y módulos

| Módulo | Responsabilidad |
|---|---|
| `BrainProfile` | Definición declarativa, validación y cuatro presets inmutables. |
| `CreatureNeeds` | Urgencias normalizadas, integración, satisfacción y curvas de prioridad. |
| `AIWorld` | Registro acotado, IDs, candidatos, visibilidad geométrica, obstáculos e interacción. |
| `CreaturePerception` | Campo visual y memoria individual con expiración y suspensión de objetivos fallidos. |
| `CreatureDecision` | Selección de objetivos por utilidad, compromiso mínimo y margen de cambio. |
| `CreatureActions` | Estados persistentes, búsqueda, aproximación, bebida, descanso, compañía y recuperación. |
| `CreatureNavigation` | A* en XZ, caminos reutilizados, llegada y detección de estancamiento. |
| `CreatureMotor` | Traducción de intenciones a rumbo y velocidad admitida, incluida marcha atrás. |
| `CreatureMind` | Ciclo de vida y coordinación de los módulos anteriores. |

`CreatureGoal` expresa el objetivo; `CreatureAction` expresa su fase de ejecución. Por ejemplo, hidratarse puede ejecutar `SEARCH_WATER`, `APPROACH_WATER` o `DRINK`. Una acción tiene condiciones de entrada, estado conservado entre pasos, terminación, fallo y limpieza. Al terminar de beber, una retirada en marcha atrás despeja la orilla antes de permitir otro giro.

La escena es el adaptador físico: extrae capacidades y landmarks de la anatomía existente, conserva la limitación de giro/zancada y alimenta `MonsterAnimation`. Ninguna decisión compara nombres de especies. Los parámetros anatómicos sólo aparecen en esa capa corporal.

## Propiedad y ciclo de vida

Un perfil se presta mediante `const BrainProfile*`; debe vivir más que todas las mentes que lo usan. Los presets tienen almacenamiento estático. Un perfil personalizado debe permanecer en el sistema propietario de entidades, sin editarse mientras esté compartido.

Cada mente contiene sus propias necesidades, memoria, tarea, camino, generador xorshift32 y tiempos. No necesita `malloc` ni destructor de heap. `CreatureMind_Init` valida el perfil e inicializa el estado. `CreatureMind_Reset` borra la mente y no libera el perfil. Retirar la entidad del registro antes de destruir su cuerpo impide referencias futuras.

`CreatureMind_AssignProfile` conserva el ID, semilla vigente, tiempo y urgencias de necesidades que siguen habilitadas. Elimina conocimiento, caminos, tareas, objetivos y temporizadores de recuperación; las necesidades deshabilitadas vuelven a cero. Un perfil inválido produce una mente inerte. El llamador puede comprobar `BrainProfile_Validate` antes de asignarlo.

Los IDs son únicos durante la vida de `AIWorld`, incluso si una ranura se reutiliza. No guardar punteros devueltos por `AIWorld_Get`: son préstamos para una consulta inmediata. Un reinicio completo de `AIWorld` requiere reiniciar también sus mentes; R sólo recoloca las entidades existentes.

### Compatibilidad con Monster y Ager

`MonsterBehavior` sigue siendo una interfaz heredada. `Monster_Update` puede ejecutar su callback y `Monster_CopyInto` copia su puntero. Esta implementación no instala un segundo callback: la escena llama una sola vez a `CreatureMind_Update` y ejecuta por separado `MonsterAnimation_Update`.

`StageActor` posee la mente fuera de `Monster`. Los clones de origen, destino y edades del Ager no contienen ni poseen ese estado. La reconfiguración anatómica conserva el perfil, sed, memoria, grupo social y objetivo; después se vuelven a consultar capacidades y se invalida el camino si cambia materialmente el espacio necesario. Las asociaciones nodo–joint y extremos de conexiones se cachean por generación del rig.

## Crear y asignar un perfil

```c
/* El almacenamiento de cauteloso debe sobrevivir a los individuos. */
BrainProfile cauteloso = BrainProfile_Default();
cauteloso.name = "cauteloso";
cauteloso.needs[NEED_HYDRATION].rate = .012f;
cauteloso.needs[NEED_HYDRATION].satisfactionRate = .18f;
cauteloso.perception.range = 20;
cauteloso.social = SOCIAL_SOLITARY;
if (BrainProfile_Validate(&cauteloso)) {
    CreatureMind_Init(&entidad.mind, &cauteloso, entidad.id, 42);
}
```

Los presets `balanced`, `solitary`, `waterless` y `social` corresponden a animal equilibrado, solitario, independiente del agua y afiliativo. No pertenecen a ninguna receta. La escena asigna `balanced` al perro y `solitary` al lagarto; las opciones `--dog-brain` y `--lizard-brain` permiten intercambiarlos.

Las necesidades usan urgencia: cero significa satisfecha y uno máxima urgencia. `NeedDefinition` configura habilitación, tasa, exponente de la curva, umbral crítico, tasa de satisfacción y nivel deseado. La prioridad usa `pow(urgencia, exponente)`.

`waterless` desactiva hidratación y los pesos de búsqueda/aproximación/bebida. Su urgencia de hidratación se mantiene en cero, aunque pueda observar y recordar agua. Deshabilitar sólo el peso de beber tampoco habilita una vía oculta de consumo. Hambre tiene datos configurables, pero ninguna acción de alimentación; no genera objetivos imposibles.

## Percepción y memoria

El sensor recibe una posición de ojos y rumbo cefálico del cuerpo. La escena transporta landmarks desde su anfitrión anatómico al rig poseído y al mundo; la mutación actualiza esos puntos. La aproximación sin cabeza usa la raíz más una altura de referencia de una unidad.

Y es vertical. En XZ, el vector frontal es `(sin(yaw), 0, cos(yaw))`. La percepción filtra candidatos por distancia, producto con el frontal, FOV horizontal, FOV vertical opcional y línea de visión. La oclusión usa cilindros con altura; no hay muestreo de superficie ni consulta GPU.

Cada entrada guarda ID, categoría, grupo, posición/radio observados, tiempo, visibilidad, propiedad de bebida y suspensión por fallo. La posición conocida sólo cambia al observar. Salir del FOV marca la entrada como recordada en el siguiente refresco; sigue disponible hasta su expiración. Los objetivos retirados se invalidan en cada paso, incluso entre refrescos de visión. La memoria tiene 32 entradas; si se llena, reemplaza la observación más antigua.

Las decisiones recorren esta memoria. Sólo la percepción enumera recursos candidatos; el decisor no pregunta dónde están todas las fuentes. La interacción sí contrasta la identidad y geometría actual del objeto conocido. La navegación puede consultar la transitabilidad física del mundo sin convertir esos obstáculos en recursos conocidos por el cerebro.

## Decisiones y acciones

Los pesos habilitan comportamientos; las curvas de necesidades, distancia, visibilidad/recuerdo, capacidad corporal y suspensión de objetivos influyen en la utilidad. El desempate conserva el primer candidato en un recorrido determinista. La exploración utiliza el generador privado del individuo, nunca `rand()`.

Se reevaluará aproximadamente cada 0,3 s; el compromiso mínimo es de 1 s y el margen de cambio 0,12 en los presets. Una urgencia crítica puede interrumpir el comportamiento ordinario. La bebida conserva una prioridad mínima hasta alcanzar su nivel deseado, sin bloquear una necesidad nueva de mayor prioridad.

Una mente sedienta sin conocimiento de agua explora mediante `SEARCH_WATER`. Una observación habilita la aproximación; llegar al banco no satisface la necesidad. Primero solicita detenerse. `DRINK` sólo reduce urgencia cuando la velocidad real está por debajo del límite de interacción, el punto está cerca de la orilla y no hay oclusión. La reducción es proporcional al tiempo simulado. Al alcanzar el nivel deseado inicia la retirada y después otra tarea.

El fallo limpia el camino, activa recuperación breve y suspende durante ocho segundos el objetivo recordado. Las suspensiones por entrada sobreviven a nuevas observaciones, de modo que dos fuentes inaccesibles no se anulan entre sí. `NavigationProgress_Update` detecta falta de avance y persecuciones circulares. La espera de interacción tiene un plazo de ocho segundos contado desde la llegada, independiente del tiempo dedicado a viajar. La retirada conserva el rumbo y retrocede por la huella anterior hasta liberar distancia suficiente; también detecta estancamiento.

## Movimiento, obstáculos y morfología

A* usa una cuadrícula de 48×48 celdas, vecinos cardinales, heurística Manhattan y validación de aristas. Simplifica el resultado sólo cuando el segmento mantiene su espacio libre. Los buffers de búsqueda son automáticos y el camino persistente tiene 256 puntos. Se reutiliza hasta cambiar destino, revisión de obstáculos, tamaño o estado de progreso. Los cambios de posición de criaturas no invalidan toda la cuadrícula.

El disco de navegación describe un espacio de planificación. El adaptador calcula un espacio direccional de orilla a partir del volumen articulado y del radio observado del estanque; evita que la anchura de un cuerpo se convierta en un margen longitudinal excesivo. El control físico verifica también nodos y conexiones mediante cápsulas, incluyendo apéndices y margen de pelaje, antes de publicar el movimiento. Cabeza/cuello son la región de interacción. La contención exterior conserva `CreatureBoundary_Solve` y su envolvente completa.

La escena limita velocidad y giro con la longitud de zancada, duración, proporción de stance y radio de apoyos, tanto avanzando como retrocediendo. Integra el desplazamiento producido por Locomotion; no teletransporta al actor al waypoint ni fuerza una rotación instantánea. La llegada tiene tolerancia de 0,12 unidades para evitar perseguir hacia atrás un punto cruzado por la oscilación de apoyos. La predicción de paredes utiliza la velocidad solicitada con signo; no sustituye el rumbo durante una parada o retirada. El radio de giro se calcula sobre soportes individuales y evita esquinas inexistentes de una AABB agregada.

La orilla usa la misma envolvente de nodos y cápsulas que el contacto, proyectada hacia el estanque respecto al rumbo actual. Cabeza/cuello y sus descendientes pertenecen a la interacción. Una penetración pequeña puede exigir una corrección radial de hasta 0,08 unidades por paso; una propuesta mayor se rechaza restaurando conjuntamente raíz, rumbo, pose y marco locomotor. Un bloqueo persistente provoca retirada y recuperación. No se reconstruyen geometría o rig para tomar decisiones.

## Políticas sociales

`AIWorldObject.group` es una identidad social estable, independiente de la variación visual. Los presets afiliativos buscan conspecíficos; los solitarios/territoriales evitan la proximidad indeseada. El impulso social compite con hidratación y descanso. Los tests usan varias entidades del mismo grupo; la escena mantiene únicamente el perro y el lagarto.

`FOLLOW_TARGET` reutiliza la ejecución de objetivos móviles. Un perfil afiliativo cuyo peso de seguimiento supere al de buscar compañía puede seleccionarlo. No hay reproducción, caza, coordinación, colisión entre agentes ni formación de bandadas.

## Orden y tiempo de simulación

1. Avanzar Ager y actualizar la anatomía/rig existentes si hay mutación.
2. Publicar posiciones de ambos objetos y consultar capacidades/pose corporal.
3. Integrar necesidades con el paso simulado.
4. Expirar/invalidad memoria y refrescar percepción si corresponde.
5. Seleccionar objetivo cuando vence su planificación o falla la tarea.
6. Ejecutar acción y generar intención; resolver navegación y motor.
7. Actualizar marcha, mirada y animación procedural.
8. Resolver estanque y límites articulados exteriores.
9. Publicar deformación/transformados y renderizar.

La visión ronda 6,7 Hz y las decisiones 3,3 Hz; ambas arrancan escalonadas por ID. Necesidades y motor se actualizan cada paso. `dt=0` no avanza la mente y pausa no llama su actualización. La escena conserva el límite de 0,25 s del integrador; necesidades avanzan con el mismo tiempo que el cuerpo, no con fotogramas ni con tiempo de render omitido. Validación usa semilla 42 y paso fijo de 1/60 s si no se especifican.

## Extender el sistema

Para una nueva necesidad, añadir su enum y definición, mantener la convención de urgencia y usar `CreatureNeeds_Satisfy` sólo tras una condición de ejecución verificable. Registrar su utilidad y acción únicamente si hay un ejecutor real.

Para un recurso nuevo, registrar su ID, categoría/etiquetas, geometría y propiedades. Conservar la información necesaria en la memoria al observarlo; añadir validación de interacción en el mundo y un ejecutor. El shader o malla visual deben usar los mismos parámetros geométricos que el objeto lógico.

Para una acción nueva, añadir enum/peso, puntuación, precondiciones, estado persistente, éxito, fallo y limpieza. Emitir una intención que el cuerpo pueda ejecutar. No introducir huesos ni llamadas de render en `CreatureDecision` o `CreatureActions`.

Para otra anatomía, publicar ojos, boca/punto de interacción, grupo, velocidades y espacios de seguridad; consumir las órdenes del motor mediante su locomoción. La lógica de necesidades, memoria y decisión permanece igual.

## Límites y rendimiento

El registro admite 128 objetos y usa broadphase por distancia con recorrido acotado en cada refresco. No es un índice espacial jerárquico para miles de entidades. La visibilidad cilíndrica, cuadrícula plana y cápsulas corporales son aproximaciones; no certifican contacto exacto de toda la superficie SDF con obstáculos interiores. Los cuerpos completos y sus paredes exteriores mantienen el solver anterior.

Natación/vuelo se describen como capacidades, pero no hay ejecutores de natación o vuelo. La navegación acuática puede permitir agua a un cuerpo nadador; la demo sólo usa marcha terrestre. Tampoco hay navmesh, terreno irregular, obstáculos móviles de agentes, serialización ni audición/olfato.

La postura cervical es una aproximación de tres articulaciones, con límites genéricos de flexión declarados al construir el rig. No modela todas las vértebras ni la lengua; el alcance oral de 0,3 unidades admite contacto cercano. Una anatomía cuyo cuello no alcance debe fallar la interacción y recuperar, sin satisfacer sed a distancia. El estanque tiene agua y un anillo de orilla con relieve sencillo; todavía no hay ribera erosionada, refracción ni inmersión corporal.

`benchmark-ai` mide cognición/navegación sin GL ni Ager, para dos y cien agentes. `benchmarks/benchmark_ai --posture` compara el coste de animación estacionaria con y sin alcance cervical. La columna `ai_ms` de la escena incluye adaptación corporal, cachés, cognición, planificación, motor y prueba de contacto interior; el controlador cervical se registra como animación. Los costes ya existentes de deformación, SDF y extracción se registran por separado. Capturas y comprobaciones GPU son diagnósticas; no ocurren en consultas normales de IA.

Referencias de diseño: [utilidad de Game AI Pro](https://www.gameaipro.com/GameAIPro/GameAIPro_Chapter09_An_Introduction_to_Utility_Theory.pdf) y [A* de Red Blob Games](https://www.redblobgames.com/pathfinding/a-star/introduction.html). Se consultaron como referencias, sin incorporar dependencias.
