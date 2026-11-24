/**
 * @file NeuralController.h
 * @brief Esquemas versionados de sensores (32) y acciones (10), y controlador neuronal a 5 Hz.
 *
 * Actúa únicamente sobre la percepción y memoria individual de la criatura.
 * Nunca invoca el controlador legacy de utilidad como respaldo oculto.
 */
#ifndef NEURAL_CONTROLLER_H
#define NEURAL_CONTROLLER_H

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

/** @brief Selector explícito entre el controlador legacy de utilidad y el controlador neuronal. */
typedef enum CreatureControllerType {
    CREATURE_CONTROLLER_UTILITY = 0,
    CREATURE_CONTROLLER_NEURAL = 1
} CreatureControllerType;

/** @brief Diez canales versionados de intención conductual (monster_action_v1). */
typedef enum NeuralActionIntent {
    ACTION_INTENT_IDLE = 0,
    ACTION_INTENT_EXPLORE = 1,
    ACTION_INTENT_REST = 2,
    ACTION_INTENT_HYDRATE = 3,
    ACTION_INTENT_FEED_PLANT = 4,
    ACTION_INTENT_FEED_MEAT = 5,
    ACTION_INTENT_HUNT = 6,
    ACTION_INTENT_FLEE = 7,
    ACTION_INTENT_MATE = 8,
    ACTION_INTENT_FOLLOW = 9,
    ACTION_INTENT_COUNT = 10
} NeuralActionIntent;

/** @brief Resultado real de la última interacción física, expuesto para inspección y depuración. */
typedef enum NeuralInteractionOutcome {
    NEURAL_OUTCOME_NONE = 0,
    NEURAL_OUTCOME_CONSUMED_PLANT,
    NEURAL_OUTCOME_CONSUMED_MEAT,
    NEURAL_OUTCOME_DRANK_WATER,
    NEURAL_OUTCOME_ATTACK_HIT,
    NEURAL_OUTCOME_MATED,
    NEURAL_OUTCOME_FAILED_ACTION,
    NEURAL_OUTCOME_FAILED_DIET,
    NEURAL_OUTCOME_FAILED_ATTACK,
    NEURAL_OUTCOME_FAILED_MATE,
    NEURAL_OUTCOME_TOOK_DAMAGE
} NeuralInteractionOutcome;

/** @brief Configuración genérica del ciclo de decisión neuronal (sin prioridades de especie). */
typedef struct NeuralControllerConfig {
    float decisionInterval;    /**< Periodo de inferencia en segundos simulados (0.2s = 5 Hz). */
    float commitmentDuration;  /**< Compromiso genérico mínimo para evitar oscilación (0.6s). */
    float switchMargin;        /**< Margen mínimo para interrumpir durante el compromiso (0.10). */
    float explorationNoise;    /**< Ruido Gaussiano opcional con PRNG privado (0.0 = determinista). */
} NeuralControllerConfig;

/** @brief Candidatos percibidos seleccionados de forma determinista desde la memoria individual. */
typedef struct NeuralSensorCandidates {
    AIObjectId plantId;
    AIObjectId meatId;
    AIObjectId waterId;
    AIObjectId conspecificId;
    AIObjectId mateCandidateId; /**< Conspecífico compatible percibido para reproducción. */
    AIObjectId otherSpeciesId;
    bool otherSpeciesVisible;
} NeuralSensorCandidates;

/** @brief Estado transitorio del controlador neuronal (no heredable). */
typedef struct NeuralControllerState {
    NeuralControllerConfig config;
    NeuralActionIntent selectedIntent;
    float selectedScore;
    double intentStarted;
    uint64_t decisionCount;
    uint64_t intentCounts[ACTION_INTENT_COUNT];
    NeuralSensorCandidates candidates;
    /* Retroalimentación acumulada/enclavada entre ticks de decisión reales. */
    float pendingActionFailure;
    float pendingNutrientIntake;
    float pendingDamageReceived;
    float lastFeedback[3]; /**< [failure, intake, damage] leídos en el último paso. */
    NeuralInteractionOutcome lastOutcome;
    double lastOutcomeTime;
} NeuralControllerState;

/** @brief Configuración por defecto del controlador neuronal (5 Hz, determinista). */
NeuralControllerConfig NeuralControllerConfig_Default(void);

/** @brief Valida intervalos y ruido del controlador. */
bool NeuralControllerConfig_Validate(const NeuralControllerConfig *config);

/** @brief Inicializa o reinicia el estado transitorio del controlador neuronal. */
void NeuralControllerState_Reset(NeuralControllerState *state, NeuralControllerConfig config);

/** @brief Nombre diagnóstico de cada canal de acción (monster_action_v1). */
const char *NeuralActionIntent_Name(NeuralActionIntent intent);

/** @brief Etiqueta compacta para cada una de las 32 entradas sensoriales (monster_sensor_v1). */
const char *NeuralSensor_Name(unsigned index);

/** @brief Nombre legible del último resultado de interacción física. */
const char *NeuralInteractionOutcome_Name(NeuralInteractionOutcome outcome);

/**
 * @brief Codifica el vector de 32 entradas sensoriales exclusivamente desde estado corporal
 *        y memoria perceptiva individual.
 */
void NeuralController_EncodeSensors(const CreaturePerception *perception,
                                    const PerceptionProfile *perceptionProfile,
                                    const CreatureBodyState *body,
                                    double now,
                                    const float pendingFeedback[3],
                                    float outInputs[NEURAL_RNN_INPUTS],
                                    NeuralSensorCandidates *outCandidates);

/**
 * @brief Ejecuta inferencia a 5 Hz y traduce la intención ganadora a objetivos/acciones
 *        de @ref CreatureMind sin respaldo oculto del controlador de utilidad.
 */
void NeuralController_Update(struct CreatureMind *mind, const AIWorld *world,
                             const CreatureBodyState *body);

#ifdef __cplusplus
}
#endif

#endif /* NEURAL_CONTROLLER_H */
