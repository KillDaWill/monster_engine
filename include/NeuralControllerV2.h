/**
 * @file NeuralControllerV2.h
 * @brief Controlador neuronal v2 (`recurrent_candidate_policy`) con evaluación genérica de candidatos.
 *
 * Evalúa recursos y criaturas percibidos mediante evaluadores neuronales compartidos
 * equivariantes a permutaciones. No recibe información privilegiada de compatibilidad
 * metabólica, toxicidad futura ni recompensas de dieta; añadir sustancias nuevas no
 * requiere neuronas de salida adicionales.
 */
#ifndef NEURAL_CONTROLLER_V2_H
#define NEURAL_CONTROLLER_V2_H

#include "AIWorld.h"
#include "BrainProfile.h"
#include "CreatureMotor.h"
#include "CreaturePerception.h"
#include "NeuralBrain.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct CreatureMind;

/** @brief Intenciones conductuales genéricas del esquema `monster_action_v2`. */
typedef enum NeuralV2ActionType {
    NEURAL_V2_ACTION_IDLE = 0,
    NEURAL_V2_ACTION_EXPLORE = 1,
    NEURAL_V2_ACTION_REST = 2,
    NEURAL_V2_ACTION_CONSUME_TARGET = 3,
    NEURAL_V2_ACTION_ATTACK_TARGET = 4,
    NEURAL_V2_ACTION_FLEE_TARGET = 5,
    NEURAL_V2_ACTION_FOLLOW_TARGET = 6,
    NEURAL_V2_ACTION_MATE_TARGET = 7,
    NEURAL_V2_ACTION_COUNT = 8
} NeuralV2ActionType;

/** @brief Retroalimentación metabólica y física desagregada observada entre pasos de decisión. */
typedef struct NeuralMetabolicFeedback {
    float ingestedMass;        /**< Masa física ingerida confirmada (separada del beneficio). */
    float energyAbsorbed;      /**< Energía fisiológica realmente absorbida por digestión. */
    float waterAbsorbed;       /**< Hidratación realmente absorbida por digestión. */
    float nutrientsAbsorbed;   /**< Nutrientes realmente absorbidos por digestión. */
    float toxicityDelta;       /**< Cambio neto reciente de toxicidad circulante [-1, 1]. */
    float damageReceived;      /**< Daño fisiológico reciente (metabólico o por ataque). */
    float interactionFailure;  /**< Fallo físico real de alcance/contacto/acción. */
} NeuralMetabolicFeedback;

/** @brief Candidatos percibidos y codificados en un paso de inferencia v2. */
typedef struct NeuralV2EncodedStep {
    float contextInputs[NEURAL_V2_CONTEXT_INPUTS];
    unsigned resourceCount;
    AIObjectId resourceIds[NEURAL_V2_MAX_RESOURCE_CANDIDATES];
    uint8_t resourcePhases[NEURAL_V2_MAX_RESOURCE_CANDIDATES];
    float resourceFeatures[NEURAL_V2_MAX_RESOURCE_CANDIDATES][NEURAL_V2_RES_FEAT];
    unsigned creatureCount;
    AIObjectId creatureIds[NEURAL_V2_MAX_CREATURE_CANDIDATES];
    bool creatureVisible[NEURAL_V2_MAX_CREATURE_CANDIDATES];
    bool creatureMateCompatible[NEURAL_V2_MAX_CREATURE_CANDIDATES];
    float creatureFeatures[NEURAL_V2_MAX_CREATURE_CANDIDATES][NEURAL_V2_CRE_FEAT];
} NeuralV2EncodedStep;

/** @brief Estado diagnóstico v2 para inspección visual y estadísticas. */
typedef struct NeuralV2InspectorState {
    bool active;
    NeuralV2ActionType selectedAction;
    AIObjectId selectedTargetId;
    float selectedScore;
    float globalScores[NEURAL_V2_GLOBAL_ACTIONS];
    unsigned resourceCount;
    AIObjectId resourceIds[NEURAL_V2_MAX_RESOURCE_CANDIDATES];
    float resourceScores[NEURAL_V2_MAX_RESOURCE_CANDIDATES];
    unsigned creatureCount;
    AIObjectId creatureIds[NEURAL_V2_MAX_CREATURE_CANDIDATES];
    float creatureScores[NEURAL_V2_MAX_CREATURE_CANDIDATES][NEURAL_V2_CRE_ACTIONS];
    uint64_t actionCounts[NEURAL_V2_ACTION_COUNT];
    NeuralMetabolicFeedback pendingFeedback;
    NeuralMetabolicFeedback lastFeedback;
} NeuralV2InspectorState;

/** @brief Nombre textual estable de cada intención genérica v2. */
const char *NeuralV2Action_Name(NeuralV2ActionType action);

/** @brief Etiqueta de cada una de las 24 entradas de contexto de `monster_sensor_v2`. */
const char *NeuralV2ContextSensor_Name(unsigned index);

/**
 * @brief Codifica el vector de contexto interno y las tablas de candidatos observables
 *        exclusivamente a partir de @ref CreatureBodyState y @ref CreaturePerception.
 *
 * Política determinista de selección cuando hay más de 8 recursos o 6 criaturas:
 * ordena por `(visible desc, distancia asc, id asc)` sin consultar compatibilidad metabólica.
 */
void NeuralControllerV2_Encode(const CreaturePerception *perception,
                               const PerceptionProfile *perceptionProfile,
                               const CreatureBodyState *body,
                               double now,
                               const NeuralMetabolicFeedback *feedback,
                               NeuralV2EncodedStep *outEncoded);

/**
 * @brief Ejecuta inferencia v2 a cadencia configurada y traduce la pareja `(acción, objetivo)`
 *        ganadora a `CreatureMind` sin respaldo oculto del controlador de utilidad.
 */
void NeuralControllerV2_Update(struct CreatureMind *mind, const AIWorld *world,
                               const CreatureBodyState *body);

#ifdef __cplusplus
}
#endif

#endif /* NEURAL_CONTROLLER_V2_H */
