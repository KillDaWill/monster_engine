/** @file InteractionTarget.h
 * @brief Contrato de contacto tridimensional entre recursos, cuerpo y mente. */
#ifndef MONSTER_INTERACTION_TARGET_H
#define MONSTER_INTERACTION_TARGET_H
#include "Vector.h"
#include <stdint.h>
typedef enum InteractionType { INTERACTION_DRINK, INTERACTION_EAT } InteractionType;
typedef struct InteractionTarget {
    uint32_t objectId, revision;
    InteractionType type;
    Vector3 position, normal, approach, velocity;
    float positionTolerance, orientationTolerance;
    bool valid, moving, blocked;
} InteractionTarget;
typedef enum ReachStatus { REACH_INVALID, REACH_APPROACHING, REACH_REACHED,
    REACH_BLOCKED, REACH_UNREACHABLE } ReachStatus;
typedef struct ReachResult {
    ReachStatus status;
    Vector3 mouth, direction;
    float error, orientationError, saturation, stability, stableTime;
    float footError;
    unsigned iterations, blockedReason;
    uint32_t objectId;
} ReachResult;
typedef enum InteractionPhase { INTERACTION_SEARCH, INTERACTION_APPROACH, INTERACTION_ALIGN,
    INTERACTION_REACH, INTERACTION_STABLE_CONTACT, INTERACTION_CONSUME, INTERACTION_WITHDRAW,
    INTERACTION_REPOSITION, INTERACTION_UNREACHABLE, INTERACTION_CANCELLED } InteractionPhase;
/** @brief Valida vectores y tolerancias antes de resolver o consumir. */
bool InteractionTarget_Validate(const InteractionTarget *target);
#endif
