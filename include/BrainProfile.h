/** @file BrainProfile.h
 * @brief Inteligencia modular independiente del render y de la anatomía. */
#ifndef BRAINPROFILE_H
#define BRAINPROFILE_H
#include <stdbool.h>
typedef enum CreatureNeed {
    NEED_HYDRATION,
    NEED_REST,
    NEED_SOCIAL,
    NEED_HUNGER,
    NEED_COUNT
} CreatureNeed;
typedef enum CreatureAction {
    ACTION_WANDER,
    ACTION_SEARCH_WATER,
    ACTION_APPROACH_WATER,
    ACTION_DRINK,
    ACTION_REST,
    ACTION_SEEK_COMPANY,
    ACTION_AVOID_COMPANY,
    ACTION_FOLLOW_TARGET,
    ACTION_IDLE,
    ACTION_SEARCH_FOOD,
    ACTION_APPROACH_FOOD,
    ACTION_EAT,
    ACTION_CHASE,
    ACTION_ATTACK,
    ACTION_ESCAPE,
    ACTION_APPROACH_MATE,
    ACTION_COUNT
} CreatureAction;
typedef enum CreatureGoal {
    GOAL_EXPLORE,
    GOAL_HYDRATE,
    GOAL_REST,
    GOAL_COMPANY,
    GOAL_AVOID,
    GOAL_FOLLOW,
    GOAL_FEED,
    GOAL_HUNT,
    GOAL_FLEE,
    GOAL_MATE,
    GOAL_RECOVER
} CreatureGoal;
typedef enum SocialPolicy {
    SOCIAL_NEUTRAL,
    SOCIAL_AFFILIATIVE,
    SOCIAL_SOLITARY,
    SOCIAL_TERRITORIAL
} SocialPolicy;
typedef struct NeedDefinition {
    bool enabled;
    float rate, exponent, critical, satisfactionRate, desiredLevel;
} NeedDefinition;
typedef struct PerceptionProfile {
    float range, horizontalFOV, verticalFOV, interval, memoryDuration;
} PerceptionProfile;
typedef struct BrainProfile {
    const char *name;
    NeedDefinition needs[NEED_COUNT];
    PerceptionProfile perception;
    float weights[ACTION_COUNT];
    unsigned dietMask, preyMask, threatMask;
    float dangerDistance;
    SocialPolicy social;
    float socialDistance, decisionInterval, switchMargin, commitment, explorationRadius;
} BrainProfile;
/** @brief Obtiene un preset inmutable; NULL si el nombre no existe. */
const BrainProfile *BrainProfile_Find(const char *name);
/** @brief Construye una configuración declarativa editable por el propietario.
 */
BrainProfile BrainProfile_Default(void);
/** @brief Valida configuración declarativa antes de compartirla. */
bool BrainProfile_Validate(const BrainProfile *);
/** @brief Nombre diagnóstico del estado, sin comparaciones de cadenas en
 * decisiones. */
const char *CreatureAction_Name(CreatureAction action);
/** @brief Nombre diagnóstico del objetivo abstracto. */
const char *CreatureGoal_Name(CreatureGoal goal);

#endif
