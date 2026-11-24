# Formato portable de cerebros neuronales (`*.brain.json`)

Este documento especifica el formato JSON versionado, legible por humanos y completamente independiente de las recetas anatómicas (`*.creature.json`) que utiliza Monster Engine para exportar, importar, archivar e intercambiar cerebros neuronales.

## 1. Principios de diseño

- **Independencia anatómica:** un archivo `*.brain.json` no almacena especie, receta, malla, rig, fisiología ni memoria perceptiva. Cualquier archivo válido puede asignarse a cualquier criatura compatible (por ejemplo, entre Brumacerno y Velkar).
- **Serialización estricta por campos:** implementada en [NeuralBrainIO.h](../include/NeuralBrainIO.h) y [NeuralBrainIO.c](../src/NeuralBrainIO.c). Nunca vuelca structs C en crudo ni direcciones de memoria.
- **Precisión sin pérdida para `float32`:** todos los pesos y sesgos se escriben con formato `%.9g` (9 dígitos significativos), garantizando recuperación exacta en coma flotante IEEE-754 de precisión simple tras un ciclo de exportación e importación.
- **Carga atómica y segura:** el parser valida el documento completo en un genoma temporal antes de tocar el destino; cualquier fallo deja intactos tanto la criatura como su cerebro anterior.
- **Preparado para NEAT recurrente futuro:** el campo `architecture` y los esquemas versionados `sensor_schema`/`action_schema` permiten añadir futuras arquitecturas estructurales (por ejemplo `"recurrent_neat"`) conservando intacto el contrato sensorial y de acciones.

## 2. Esquema raíz (versión 1)

```json
{
  "version": 1,
  "architecture": "fixed_rnn",
  "sensor_schema": "monster_sensor_v1",
  "action_schema": "monster_action_v1",
  "activation": "tanh",
  "recurrent_update": "simultaneous",
  "matrix_layout": "row_major",
  "precision": "float32",
  "topology": {
    "inputs": 32,
    "hidden": 12,
    "outputs": 10
  },
  "weights": {
    "w_xh": [ "... 384 float32 ..." ],
    "w_hh": [ "... 144 float32 ..." ],
    "w_hy": [ "... 120 float32 ..." ]
  },
  "biases": {
    "b_h": [ "... 12 float32 ..." ],
    "b_y": [ "... 10 float32 ..." ]
  },
  "metadata": {
    "label": "seeded_baseline_v1",
    "origin": "seeded_baseline",
    "lineage_id": 1,
    "generation": 0,
    "parent_ids": [0, 0],
    "fingerprint": "e786fce8d23837ce",
    "founder_fingerprint": "e786fce8d23837ce"
  }
}
```

## 3. Disposición matricial y semántica matemática

Para `architecture = "fixed_rnn"` con topología $I=32$, $H=12$, $O=10$ (670 parámetros entrenables/evolucionables):

| Arreglo | Dimensiones | Longitud | Indexación (`row_major`) | Significado |
|---|---:|---:|---|---|
| `weights.w_xh` | $12 \times 32$ | 384 | `w_xh[h * 32 + i]` | Peso desde sensor $i$ hacia neurona oculta $h$ |
| `weights.w_hh` | $12 \times 12$ | 144 | `w_hh[h * 12 + k]` | Peso recurrente desde $h_{t-1}[k]$ hacia $h_t[h]$ |
| `weights.w_hy` | $10 \times 12$ | 120 | `w_hy[o * 12 + h]` | Peso desde neurona oculta $h$ hacia salida $o$ |
| `biases.b_h` | $12$ | 12 | `b_h[h]` | Sesgo de la neurona oculta $h$ |
| `biases.b_y` | $10$ | 10 | `b_y[o]` | Sesgo del canal de salida $o$ |

Ecuaciones de inferencia síncrona (`activation = "tanh"`, `recurrent_update = "simultaneous"`):

$$
h_t = \tanh(W_{xh} x_t + W_{hh} h_{t-1} + b_h)
$$

$$
y_t = W_{hy} h_t + b_y
$$

Todas las neuronas ocultas leen la instantánea completa $h_{t-1}$ antes de publicar $h_t$. Al inicializar un individuo, al nacer o al asignar un cerebro importado/intercambiado, $h_0 = 0$.

## 4. Contratos de sensores y acciones (`v1`)

### 4.1. Esquema de sensores (`monster_sensor_v1`, 32 entradas)

- **Señales internas (`00..11`):**
  `00` Energy, `01` Hydration, `02` Fatigue, `03` Health, `04` Normalized age, `05` Reproductive readiness, `06` Normalized movement speed, `07` Physical attack capability, `08` Physical water interaction capability, `09` Recent action failure, `10` Recent confirmed nutrient intake, `11` Recent damage received.
- **Señales externas relativas (`12..31`, 5 categorías × 4 valores `[confidence, norm_dist, sin_bearing, cos_bearing]`):**
  - `12..15`: Vegetación (`ECO_PLANT`)
  - `16..19`: Carne / carroña (`ECO_MEAT`)
  - `20..23`: Agua (`ECO_WATER`)
  - `24..27`: Conspecífico (prioriza pareja compatible percibida si la criatura está lista para reproducirse)
  - `28..31`: Criatura de otra especie

### 4.2. Esquema de acciones (`monster_action_v1`, 10 salidas)

`0` `ACTION_INTENT_IDLE`, `1` `ACTION_INTENT_EXPLORE`, `2` `ACTION_INTENT_REST`, `3` `ACTION_INTENT_HYDRATE`, `4` `ACTION_INTENT_FEED_PLANT`, `5` `ACTION_INTENT_FEED_MEAT`, `6` `ACTION_INTENT_HUNT`, `7` `ACTION_INTENT_FLEE`, `8` `ACTION_INTENT_MATE`, `9` `ACTION_INTENT_FOLLOW`.

## 5. Huella canónica y validación estricta

La huella `fingerprint` es un hash FNV-1a de 64 bits (16 dígitos hexadecimales minúsculas) calculado sobre versión, arquitectura, nombres de esquemas, dimensiones y los bits IEEE-754 canónicos (normalizando `-0.0f` a `+0.0f`) de los 670 parámetros. Los metadatos (`label`, `origin`, `lineage_id`, `generation`) no alteran la huella funcional del cerebro.

`NeuralBrain_LoadFile` y `NeuralBrain_DeserializeJSON` rechazan de forma explícita:
- Archivos vacíos o mayores de `NEURAL_BRAIN_MAX_FILE_BYTES` (512 KiB).
- JSON malformado, escapes no soportados, claves duplicadas, claves desconocidas o basura tras el cierre `}`.
- Versiones distintas de `1` o arquitecturas distintas de `"fixed_rnn"`.
- Esquemas `sensor_schema` o `action_schema` desconocidos, o valores incompatibles en `activation`, `recurrent_update`, `matrix_layout` o `precision`.
- Dimensiones de `topology` o longitudes de arreglos distintas de `32 / 12 / 10` (`384 / 144 / 120 / 12 / 10`).
- Campos obligatorios ausentes.
- Valores no finitos (`NaN`, `Infinity`) o fuera de la cota dura de validación `[-10.0, 10.0]`.

Al exportar, `NeuralBrain_SaveFile(..., overwrite=false, ...)` rechaza sobrescribir un archivo existente salvo aprobación explícita.

## 6. Archivos de ejemplo incluidos en `brains/`

- [brains/seeded_baseline.brain.json](../brains/seeded_baseline.brain.json): cerebro base genérico (funciona en cualquier especie únicamente a partir de sus sensores internos y externos).
- [brains/forager_herbivore.brain.json](../brains/forager_herbivore.brain.json): variante orientada a forrajeo vegetal y huida (útil también para pruebas de transferencia entre especies).
- [brains/hunter_carnivore.brain.json](../brains/hunter_carnivore.brain.json): variante orientada a caza y carroña (asignable a un Brumacerno para verificar que el cuerpo rechaza ataques y carne sin corromper la simulación).
- [brains/random_founder_seed42.brain.json](../brains/random_founder_seed42.brain.json): fundador aleatorio acotado determinista (semilla 42).
- [brains/brumacerno_forager_v2.brain.json](../brains/brumacerno_forager_v2.brain.json): cerebro v2 evolucionado para Brumacerno en `brumacerno_mixed` (100 % supervivencia ciega).
- [brains/velkar_scavenger.brain.json](../brains/velkar_scavenger.brain.json): cerebro v2 evolucionado para Velkar en `velkar_scavenging` (100 % supervivencia ciega).
- [brains/velkar_hunter_v2.brain.json](../brains/velkar_hunter_v2.brain.json): cerebro v2 evolucionado mediante currículo completo desde fundadores aleatorios en `velkar_hunting` (0 cadáveres iniciales, 100 % supervivencia ciega, caza y consumo independiente de cadáveres).

## 7. Esquema versión 2 (`recurrent_candidate_policy`)

El formato versión `2` extiende `*.brain.json` manteniendo compatibilidad completa con archivos versión `1`:
- `"version": 2`
- `"architecture": "recurrent_candidate_policy"`
- `"sensor_schema": "monster_sensor_v2"`, `"action_schema": "monster_action_v2"`
- `"topology"`: `context_inputs: 24`, `context_hidden: 16`, `global_actions: 3`, `resource_features: 14`, `resource_hidden: 12`, `creature_features: 11`, `creature_hidden: 10`, `creature_actions: 4` (`1,416` parámetros totales).
- `"weights"` y `"biases"` divididos por subred (`ctx_w_xh`, `ctx_w_hh`, `ctx_b_h`, `glob_w`, `glob_b`, `res_w1`, `res_b1`, `res_w2`, `res_b2`, `cre_w1`, `cre_b1`, `cre_w2`, `cre_b2`).
- Metadatos opcionales de procedencia de entrenamiento en `metadata` (`trainer`, `trainer_generation`, `species_id`, `environment_id`, `validation_fitness`, `validation_survival`).

Ver [Controlador Neuronal V2](NEURAL_CONTROLLER_V2.md) para las ecuaciones completas.
