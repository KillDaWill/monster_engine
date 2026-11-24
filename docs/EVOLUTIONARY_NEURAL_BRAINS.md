# Cerebros neuronales evolutivos en Monster Engine

Este documento describe la arquitectura de inteligencia neuronal evolutiva integrada en el núcleo puro C de Monster Engine, completamente independiente de SDL, OpenGL, evaluación SDF, generación de mallas y renderizado.

## 1. Separación entre anatomía, fisiología e inteligencia

Cada individuo de la simulación mantiene capas de estado explícitamente desacopladas:

```
Creature (EcosystemEntity)
├── Receta anatómica (EcosystemSpecies.recipeId -> CreatureRecipes_Find)
├── Genoma físico (CreatureGenome: tamaño, velocidad, eficiencia, percepción, pigmento, morfología, maduración)
├── Perfil fisiológico (PhysiologyProfile: metabolismo, dietMask, preyMask, canAttackPhysically, waterInteraction)
├── Estado fisiológico (CreaturePhysiology: energía, hidratación, fatiga, salud, edad, etapa)
├── Genoma neuronal (NeuralGenome: arquitectura, topología y 670 parámetros heredables)
├── Estado neuronal temporal (NeuralBrainRuntime: entradas, h_{t-1}, h_t, salidas, contador de pasos)
├── Percepción y memoria individual (CreaturePerception: 32 entradas acotadas con expiración)
└── Estado reproductivo (CreatureReproduction: rol, cooldown, gestación y copias de concepción)
```

### Garantías de separación

- Las redes neuronales nunca se almacenan dentro de `CreatureRecipe`, `Mesh`, `CreatureRig` ni `MonsterAger`.
- Dos individuos con cerebros distintos pero anatomía equivalente comparten las mismas cachés geométricas en `EcosystemVisual`; asignar, clonar, intercambiar o mutar un cerebro nunca regenera mallas.
- Asignar cualquier `NeuralGenome` compatible mediante `CreatureMind_AssignNeuralGenome` o `EcosystemWorld_AssignBrain` **no modifica**:
  - especie ni receta anatómica,
  - genoma físico,
  - compatibilidad digestiva (`PhysiologyProfile.dietMask`),
  - capacidades físicas de movimiento o ataque (`canAttackPhysically`, `preyMask`),
  - salud, energía, hidratación, fatiga ni edad,
  - rol reproductivo ni una gestación en curso.

## 2. Módulos del sistema neuronal

| Módulo | Responsabilidad |
|---|---|
| `include/NeuralGenome.h`, `src/NeuralGenome.c` | Genotipo heredable (670 parámetros), inicialización aleatoria/base, huella FNV-1a, distancia RMS L2, recombinación y mutación acotada determinista. |
| `include/NeuralBrain.h`, `src/NeuralBrain.c` | Motor de inferencia RNN síncrona y `NeuralBrainRuntime` separado del genoma; cero asignaciones dinámicas durante inferencia. |
| `include/NeuralBrainIO.h`, `src/NeuralBrainIO.c` | Serialización/deserialización estricta de `*.brain.json` (véase [NEURAL_BRAIN_FORMAT.md](NEURAL_BRAIN_FORMAT.md)). |
| `include/NeuralController.h`, `src/NeuralController.c` | Codificación de sensores (`monster_sensor_v1`), inferencia a 5 Hz, compromiso genérico y traducción a objetivos/acciones (`monster_action_v1`). |
| `include/EvolutionArena.h`, `src/EvolutionArena.c` | Escenario autoritativo compartido por los ejecutables visual y headless. |

## 3. Red neuronal recurrente (hito inicial)

Topología fija compacta: **32 sensores → 12 neuronas ocultas recurrentes → 10 intenciones de acción** (670 parámetros: $384 + 144 + 120 + 12 + 10$).

$$
h_t = \tanh(W_{xh} x_t + W_{hh} h_{t-1} + b_h)
$$

$$
y_t = W_{hy} h_t + b_y
$$

- **Actualización recurrente simultánea:** todas las neuronas ocultas leen la instantánea previa completa $h_{t-1}$ antes de publicar $h_t$.
- **Cadencia independiente de FPS:** la inferencia se ejecuta a **5 Hz simulados** (`decisionInterval = 0.2 s`, cada 12 ticks de `1/60 s`).
- **Seguridad numérica:** las entradas y salidas no finitas (`NaN`/`Inf`) se rechazan antes de modificar el estado temporal; los genomas se validan en construcción/carga/herencia y la inferencia aplica comprobaciones estructurales $O(1)$ (`~0,59 µs/eval`, >1,7 M eval/s en 1 hilo).
- **Sin heap en inferencia:** `NeuralBrain_Evaluate` opera exclusivamente sobre almacenamiento automático y el estado preasignado de la criatura.
- **Escalabilidad a 500–2.048+ individuos:** las tablas de fundadores, linajes e historial parental crecen dinámicamente sin topes fijos; véanse [EVOLUTION_ARENA_SCALABILITY.md](EVOLUTION_ARENA_SCALABILITY.md) y [EVOLUTION_ARENA_PERFORMANCE.md](EVOLUTION_ARENA_PERFORMANCE.md).

## 4. Sistema sensorial privado (`monster_sensor_v1`)

`NeuralController_EncodeSensors` construye el vector de 32 entradas **exclusivamente** a partir de `CreatureBodyState`, la retroalimentación enclavada real y la memoria perceptiva individual (`CreaturePerception`). Nunca consulta el `AIWorld` global para revelar recursos no descubiertos o criaturas ocultas.

- **12 señales internas (`00..11`):** energía, hidratación, fatiga, salud, edad normalizada, disposición reproductiva (`mateReady`), velocidad normalizada, capacidad física de ataque (`canAttack`), capacidad física de interacción hídrica (`waterInteraction`), fallo reciente de acción, ingesta nutritiva confirmada reciente y daño recibido reciente. Las tres últimas se enclavan desde eventos físicos reales entre ticks de decisión y se limpian al ser leídas; no se inyectan recompensas artificiales de fitness.
- **20 señales externas relativas (`12..31`):** cinco categorías (vegetación, carne/carroña, agua, conspecífico y otra especie), cada una con `[confianza, distancia_normalizada, sin(rumbo_relativo), cos(rumbo_relativo)]`.
  - Ausente: confianza `0.0`, distancia `1.0`.
  - Recordado en memoria: confianza decreciente en `[0.1, 0.5]` según la edad del recuerdo respecto a `memoryDuration`.
  - Visible actualmente: confianza `1.0`.
  - Desempate determinista: sin cooldown de fallo > visible > menor distancia > menor `AIObjectId`.
- **Candidato reproductivo:** cuando la criatura cumple las condiciones físicas de madurez/reservas (`body.mateReady`), el canal conspecífico (`24..27`) prioriza el conspecífico compatible percibido (`reproductiveReady && group != reproductiveRole`) y publica su ID en `NeuralSensorCandidates.mateCandidateId`, dando al cerebro información directa para decidir si intentar aparearse.

## 5. Interfaz de acciones y consecuencias físicas (`monster_action_v1`)

Los 10 canales de salida expresan **preferencias conductuales**, no comandos físicos directos:

| Índice | Canal | Traducción en `CreatureMind` |
|---:|---|---|
| 0 | `ACTION_INTENT_IDLE` | `ACTION_IDLE` |
| 1 | `ACTION_INTENT_EXPLORE` | `ACTION_WANDER` |
| 2 | `ACTION_INTENT_REST` | `ACTION_REST` |
| 3 | `ACTION_INTENT_HYDRATE` | `ACTION_APPROACH_WATER` / `ACTION_DRINK` si percibe agua, si no `ACTION_SEARCH_WATER` |
| 4 | `ACTION_INTENT_FEED_PLANT` | `desiredFoodCategory = ECO_PLANT`; `ACTION_APPROACH_FOOD`/`ACTION_EAT` sobre planta percibida o `ACTION_SEARCH_FOOD` |
| 5 | `ACTION_INTENT_FEED_MEAT` | `desiredFoodCategory = ECO_MEAT`; `ACTION_APPROACH_FOOD`/`ACTION_EAT` sobre carne percibida o `ACTION_SEARCH_FOOD` |
| 6 | `ACTION_INTENT_HUNT` | `ACTION_CHASE` / `ACTION_ATTACK` sobre otra especie visible o patrulla (`GOAL_HUNT`) |
| 7 | `ACTION_INTENT_FLEE` | `ACTION_ESCAPE` respecto a la criatura percibida más relevante |
| 8 | `ACTION_INTENT_MATE` | `ACTION_APPROACH_MATE` hacia `mateCandidateId` si `mateReady`, si no búsqueda sin beneficio artificial |
| 9 | `ACTION_INTENT_FOLLOW` | `ACTION_FOLLOW_TARGET` hacia conspecífico u otra especie percibida |

### Integración con el controlador legacy y ausencia de respaldo oculto

- `CreatureControllerType` distingue explícitamente `CREATURE_CONTROLLER_UTILITY` y `CREATURE_CONTROLLER_NEURAL`.
- En modo neuronal, `CreatureMind_Update` **nunca invoca `CreatureDecision_Update`**: no hay huida automática ante depredadores, ni búsqueda obligatoria de comida/agua, ni apareamiento forzado, ni prioridades por especie.
- `CreatureActions_Update` separa las precondiciones físicas compartidas (capacidad hídrica, alcance cervical, contacto sostenido de 0,25 s, oclusión) de los antiguos pesos de utilidad (`BrainProfile.weights`), de modo que un cerebro neuronal puede intentar cualquier acción implementada incluso si todos los pesos legacy valen cero.
- **Regla fundamental:** el cerebro decide qué intentar; el cuerpo y el mundo deciden qué ocurre. Si un Brumacerno (herbívoro sin capacidad de ataque) recibe el cerebro de un Velkar e intenta cazar o comer carne, el motor rechaza el ataque (`NEURAL_OUTCOME_FAILED_ATTACK`) y rechaza la ingesta (`NEURAL_OUTCOME_FAILED_DIET`), devolviendo cero nutrición y alimentando la señal real de fallo (`FAIL_FB`).

## 6. Inicialización de fundadores, herencia y selección natural

### Modos de inicialización

1. **`random`:** pesos aleatorios acotados deterministas (escala Glorot/Xavier) independientes por individuo.
2. **`seeded`:** carga el genoma base genérico (`NeuralGenome_InitSeededBaseline`, expresado 100 % en pesos de la RNN sin reglas ocultas) y opcionalmente muta cada copia fundadora para crear diversidad inicial.
3. **`imported`:** carga uno o varios archivos `*.brain.json` (común o específicos por especie).

### Herencia y gestación segura

- `NeuralGenome_Inherit` soporta tres estrategias configurables:
  - `neuron_wise` (por defecto): recombina filas completas de neuronas ocultas (`w_xh[h,*]`, `w_hh[h,*]`, `b_h[h]`) y salidas (`w_hy[o,*]`, `b_y[o]`), preservando circuitos recurrentes funcionales.
  - `uniform`: mezcla independiente por parámetro.
  - `mutation_only`: clona `parentA` y aplica únicamente mutación (útil para estudios comparativos).
- Parámetros por defecto: `mutationRate = 0.05`, `mutationSigma = 0.15`, `replaceRate = 0.005`, `weightLimit = 5.0`. Con tasa y reemplazo a cero, la herencia es exactamente la recombinación sin ruido. Nunca se modifican los genomas parentales.
- **Instantánea de concepción:** al producirse un apareamiento válido en `EcosystemWorld`, la gestante almacena copias independientes (`gestationNeuralGenomes[2]`) de los genomas neuronales de **ambos progenitores en ese instante**. Si el padre muere durante la gestación, si se reutiliza su ranura o si el usuario reemplaza el cerebro de la madre durante el embarazo, la cría hereda exactamente los genomas registrados al concebir.
- Cada recién nacido inicializa su propia identidad, genoma físico heredado, genoma neuronal heredado, estado recurrente a cero, PRNG propio y memoria perceptiva vacía.
- **Selección natural genuina:** no existe reproducción artificial del individuo con mayor puntuación, ni reposición oculta ante extinción, ni eliminación manual de individuos con bajo desempeño.

## 7. Intercambiabilidad y archivos observacionales

Operaciones públicas en `EcosystemWorld`:
- `EcosystemWorld_AssignBrain`, `EcosystemWorld_CloneBrain`, `EcosystemWorld_SwapBrains` (atómico), `EcosystemWorld_ResetBrainRandom`, `EcosystemWorld_ExportBrain`, `EcosystemWorld_ImportBrain`.
- Cada intervención manual se registra en una bitácora acotada (`EcosystemWorld.interventions`, anillo de 64 entradas + contador total).
- `EcosystemWorld_ExportArchiveBrains` exporta de forma puramente observacional los mejores representantes (`best_offspring`, `longest_survival`, `highest_generation`, `oldest_surviving_lineage`) junto con `archive_manifest.json` indicando explícitamente si cada individuo sigue vivo (`still_living`), sin influir jamás en la reproducción.

## 8. Preparación para neuroevolución estructural (NEAT recurrente)

El diseño actual deja preparadas las fronteras para el siguiente hito (NEAT recurrente):
- `NeuralArchitectureType` y el campo JSON `"architecture"` desacoplan la topología interna del formato y del controlador.
- Los esquemas `monster_sensor_v1` y `monster_action_v1` permanecen inalterados cuando se sustituya la RNN fija por redes con topología variable.
- El ciclo de vida (`Init`/`Clone`/`Destroy`) ya existe en todas las rutas de creación, gestación, muerte y limpieza del mundo para soportar arreglos dinámicos de genes de nodos y conexiones con números de innovación.
- Los linajes neuronales (`lineageId`) y las huellas (`fingerprint`) ya están separados de la especie biológica (`EcosystemSpecies.id`), garantizando que futuras especies de compatibilidad NEAT nunca alteren la especie biológica de una criatura.
- **Trabajo pendiente para NEAT real:** representación de genes de nodos y conexiones con tabla global de innovaciones, mutaciones estructurales (añadir conexión, deshabilitar conexión, dividir conexión añadiendo nodo), cruce alineado por innovación, distancia de compatibilidad estructural y protección de innovaciones (especiación neuronal). En este hito no se afirma soporte NEAT estructural: las arquitecturas implementadas son explícitamente `fixed_rnn` (v1) y `recurrent_candidate_policy` (v2).

## 9. Evolución con Metabolismo Molecular y Arquitectura V2

Con la incorporación del metabolismo molecular basado en datos externos (`metabolism/`) y la arquitectura genérica `recurrent_candidate_policy` (v2, 1.416 parámetros), el sistema evoluciona estrategias de consumo y caza sin salidas cableadas por tipo de alimento (`FEED_PLANT`/`FEED_MEAT`/`HYDRATE`) ni conocimiento metabólico privilegiado:
- Ver [Metabolismo Molecular](MOLECULAR_METABOLISM.md) para el modelo digestivo retardado, toxicidad y descomposición de cadáveres.
- Ver [Controlador Neuronal V2](NEURAL_CONTROLLER_V2.md) para la evaluación equivariante de candidatos.
- Ver [Brain Evolution Lab](BRAIN_EVOLUTION_LAB.md) para el entrenamiento evolutivo rápido separado de la selección natural de `EvolutionArena`.
