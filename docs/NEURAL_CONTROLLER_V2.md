# Controlador Neuronal V2 (`recurrent_candidate_policy`) — Monster Engine

## 1. Motivación y diseño sin sesgos dietéticos

El controlador legado v1 (`fixed_rnn`, 32-12-10) codificaba canales separados para planta, carne y agua tanto en sensores como en salidas (`FEED_PLANT`, `FEED_MEAT`, `HYDRATE`). El controlador **v2 (`recurrent_candidate_policy`)** sustituye esa estructura por una arquitectura genérica equivariante a permutaciones:

- **Ninguna neurona de salida está ligada a una sustancia concreta.** Añadir nuevas moléculas o sustancias en `metabolism/` **no** añade neuronas de salida ni altera la arquitectura (`1,416` parámetros fijos).
- **Cero conocimiento privilegiado:** las criaturas reciben únicamente firmas sensoriales observables aproximadas (`6` canales en `[0, 1]`), fase física (`solid`, `liquid`, `semi_solid`), dureza, disponibilidad, distancia, ángulo y visibilidad. Nunca reciben compatibilidad metabólica, rendimiento energético futuro ni etiquetas de toxicidad.
- **Retroalimentación desagregada:** `NeuralMetabolicFeedback` separa explícitamente `ingestedMass` (masa ingerida) de las consecuencias reales retardadas (`energyAbsorbed`, `waterAbsorbed`, `nutrientsAbsorbed`, `toxicityDelta`, `damageReceived`, `interactionFailure`).

## 2. Arquitectura (`1,416` parámetros, `0` asignaciones en inferencia)

| Subred | Entrada → Salida | Parámetros | Función |
| --- | --- | --- | --- |
| **Red de contexto recurrente** | `24 + 16 -> 16` (`tanh`, actualización simultánea) | `24×16 + 16×16 + 16 = 656` | Mantiene memoria temporal de interacciones recientes, estado fisiológico, estado digestivo y retroalimentación metabólica. |
| **Cabeza de acciones globales** | `16 -> 3` | `16×3 + 3 = 51` | Puntúa `IDLE`, `EXPLORE`, `REST`. |
| **Evaluador compartido de recursos** | `(14 + 16) -> 12 -> 1` (`tanh`) | `30×12 + 12 + 12×1 + 1 = 385` | Evalúa hasta `8` recursos percibidos con los **mismos pesos** -> puntuación `CONSUME_TARGET(r)`. |
| **Evaluador compartido de criaturas** | `(11 + 16) -> 10 -> 4` (`tanh`) | `27×10 + 10 + 10×4 + 4 = 324` | Evalúa hasta `6` criaturas percibidas con los **mismos pesos** -> `ATTACK`, `FLEE`, `FOLLOW`, `MATE`. |
| **Total** | — | **`1,416`** | Equivariante a permutaciones; las ranuras ausentes no se evalúan. |

### Optimización de evaluación en caliente
`NeuralBrain_EvaluateV2()` precalcula una sola vez los términos dependientes del contexto `W_ctx * h_t + b` para ambos evaluadores compartidos, reduciendo en ~53 % las multiplicaciones-acumulaciones por candidato sin alterar el resultado numérico.

## 3. Esquemas versionados

- **Sensores (`monster_sensor_v2`):**
  - Contexto (24): energía, hidratación, salud, fatiga, edad normalizada, llenado digestivo, reserva de nutrientes, toxicidad, malestar digestivo, velocidad, `mateReady`, `canAttack`, `waterInteraction`, 7 canales de retroalimentación desagregada y 4 resúmenes de percepción.
  - Por recurso candidato (14): distancia normalizada, `sin/cos` ángulo relativo, visibilidad, fracción disponible, 6 canales de `SensorySignature`, fase líquida, fase semisólida y dureza.
  - Por criatura candidata (11): distancia, `sin/cos` ángulo, visibilidad, velocidad relativa, tamaño relativo, misma especie, compatibilidad reproductiva, salud aparente, `sin/cos` rumbo relativo.
- **Acciones genéricas (`monster_action_v2`):** `IDLE`, `EXPLORE`, `REST`, `CONSUME_TARGET`, `ATTACK_TARGET`, `FLEE_TARGET`, `FOLLOW_TARGET`, `MATE_TARGET`.

## 4. Compatibilidad y seguridad

- Los cerebros v2 se guardan y cargan en formato JSON versionado (`version: 2`, `architecture: "recurrent_candidate_policy"`) mediante `NeuralBrain_SaveFile()` / `NeuralBrain_LoadFile()`.
- El cruce entre arquitecturas distintas (`fixed_rnn` vs `recurrent_candidate_policy`) se rechaza explícitamente.
- En modo molecular de `EcosystemWorld`, intentar asignar un cerebro v1 se rechaza con error descriptivo.
- Transferir un cerebro v2 entre especies distintas (`Brumacerno <-> Velkar`) conserva intacto el perfil metabólico del cuerpo receptor (verificado en el Experimento 8).
