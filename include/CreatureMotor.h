/** @file CreatureMotor.h
 * @brief Inteligencia modular independiente del render y de la anatomía. */
#ifndef CREATUREMOTOR_H
#define CREATUREMOTOR_H
#include "Vector.h"
#include "InteractionTarget.h"
typedef struct CreatureCapabilities {
    bool ground, waterInteraction, swimming, flying, hasInteractionPoint;
    float contactTolerance, maximumOralReach;
    float clearance, bankClearance, drinkReach, mouthForward, maximumSpeed, maximumTurn;
} CreatureCapabilities;
typedef struct CreatureBodyState {
    Vector3 position, eye, mouth, interactionPoint;
    ReachResult reach;
    Vector3 mouthDirection;
    float yaw, visionYaw, speed;
    unsigned group, species, reproductiveRole;
    bool mateReady, canAttack;
    float energy, fatigue, attackRange;
    CreatureCapabilities capabilities;
} CreatureBodyState;
typedef struct CreatureIntent {
    bool moving, lookAt, reverse, interact, pursuing;
    InteractionTarget interaction;
    Vector3 destination, lookTarget;
    float speed;
} CreatureIntent;
typedef struct CreatureMotorCommand {
    float heading, speed;
} CreatureMotorCommand;
/** @brief Intención abstracta convertida a velocidad acotada y rumbo; rig
 * ajeno. */
CreatureMotorCommand CreatureMotor_Resolve(const CreatureIntent *, const CreatureBodyState *);

#endif
