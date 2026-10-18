/** @file CreatureMind.h
 * @brief Inteligencia modular independiente del render y de la anatomía. */
#ifndef CREATUREMIND_H
#define CREATUREMIND_H
#include "CreatureMotor.h"
#include "CreatureNavigation.h"
#include "CreatureNeeds.h"
#include "CreaturePerception.h"
typedef struct CreatureMind {
    const BrainProfile *profile; /* Referencia inmutable prestada; debe sobrevivir a la mente. */
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

#endif
