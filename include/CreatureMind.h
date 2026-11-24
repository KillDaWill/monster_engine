/** @file CreatureMind.h
 * @brief Inteligencia modular independiente del render y de la anatomía. */
#ifndef CREATUREMIND_H
#define CREATUREMIND_H
#include "CreatureMotor.h"
#include "CreatureNavigation.h"
#include "CreatureNeeds.h"
#include "CreaturePerception.h"
#include "NeuralBrain.h"
#include "NeuralController.h"
typedef struct CreatureMind {
    const BrainProfile *profile; /* Referencia inmutable prestada; debe sobrevivir a la mente. */
    CreatureControllerType controllerType;
    unsigned desiredFoodCategory; /**< 0 = cualquiera (legacy), ECO_PLANT o ECO_MEAT. */
    NeuralBrain neuralBrain;
    NeuralControllerState neuralState;
    CreatureNeeds needs;
    CreaturePerception perception;
    CreatureNavigation navigation;
    CreatureGoal goal;
    CreatureAction action;
    AIObjectId self, target, failedTarget;
    Vector3 destination, approachResourcePosition;
    uint32_t random;
    double time, nextDecision, started, cooldownUntil, recoveryUntil, interactionStarted;
    float score;
    bool destinationValid, retreating;
    Vector3 retreatGoal, retreatStart;
    InteractionPhase interactionPhase;
    InteractionTarget interaction;
    float contactTime;
    unsigned completedMeals, repositionAttempts;
    unsigned drinks, completedDrinks, actionFailures;
    bool externalPhysiology, transactionalIntake, molecularMode;
    float intakeRequested;
    float intakeMassRequested; /**< Solicitud física de masa (u_m) en modo molecular. */
    CreatureNeed intakeNeed;
    CreatureIntent intent;
} CreatureMind;
/** @brief Inicializa estado privado, semilla y planificación escalonada. */
void CreatureMind_Init(CreatureMind *, const BrainProfile *, AIObjectId, uint32_t seed);
/** @brief Sustituye perfil: conserva urgencias comunes; borra memoria, tareas y
 * targets. */
void CreatureMind_AssignProfile(CreatureMind *, const BrainProfile *);
/** @brief Libera lógicamente estado sin liberar el perfil prestado. */
void CreatureMind_Reset(CreatureMind *);
/** @brief Necesidades -> percepción -> decisión -> acción -> navegación. dt=0
 * pausa. */
void CreatureMind_Update(CreatureMind *, const AIWorld *, const CreatureBodyState *, float dt);
/** @brief Urgencia normalizada; devuelve cero para necesidades desactivadas. */
float CreatureMind_GetNeed(const CreatureMind *, CreatureNeed);
/** @brief Acción vigente con estado persistente. */
CreatureAction CreatureMind_GetAction(const CreatureMind *);
/** @brief Objetivo abstracto, independiente de la fase de ejecución. */
CreatureGoal CreatureMind_GetGoal(const CreatureMind *);
/** @brief PRNG privado determinista. */
float CreatureMind_Random(CreatureMind *);

/** @brief Confirma sólo beneficio realmente transferido, después del arbitraje mundial. */
void CreatureMind_ConfirmIntake(CreatureMind *,float confirmedBenefit);

/** @brief Asigna un genoma neuronal validado por copia independiente y reinicia estado recurrente.
 *
 * Preserva identidad, PRNG, tiempo y necesidades; cancela tareas/rutas previas y nunca
 * transfiere memoria perceptiva de otro individuo. Ante genoma inválido no modifica nada.
 */
bool CreatureMind_AssignNeuralGenome(CreatureMind *, const NeuralGenome *);

/** @brief Obtiene el genoma neuronal de sólo lectura, o NULL si no usa controlador neuronal. */
const NeuralGenome *CreatureMind_GetNeuralGenome(const CreatureMind *);

/** @brief Configura parámetros de cadencia, compromiso y ruido del controlador neuronal. */
void CreatureMind_SetNeuralConfig(CreatureMind *, NeuralControllerConfig);

/** @brief Registra retroalimentación real de fallo físico entre ticks de decisión. */
void CreatureMind_RecordFailure(CreatureMind *, NeuralInteractionOutcome);

/** @brief Registra retroalimentación real de daño físico recibido entre ticks de decisión. */
void CreatureMind_RecordDamage(CreatureMind *, float damage);

/** @brief Registra el resultado de la última interacción física para inspección. */
void CreatureMind_RecordOutcome(CreatureMind *, NeuralInteractionOutcome);

/** @brief Registra masa física ingerida confirmada (separada del beneficio nutricional). */
void CreatureMind_RecordIngestion(CreatureMind *, float confirmedMass);

/** @brief Enclava consecuencias metabólicas reales entre pasos de inferencia neural. */
void CreatureMind_RecordMetabolicEffects(CreatureMind *, float energyAbsorbed, float waterAbsorbed,
                                         float nutrientsAbsorbed, float toxicityDelta,
                                         float metabolicDamage);
#endif
