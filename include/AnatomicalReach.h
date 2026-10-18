/** @file AnatomicalReach.h
 * @brief Alcance acotado sin traslaciones, escalado ni asignaciones por fotograma. */
#ifndef MONSTER_ANATOMICAL_REACH_H
#define MONSTER_ANATOMICAL_REACH_H
#include "RigBuilder.h"
#include "InteractionTarget.h"
#define REACH_MAX_JOINTS 8
typedef struct ReachPreset {
    float comfortFraction, velocity, acceleration, strainWeight, continuityWeight, crouchFraction;
    unsigned iterations, torsoJoints;
} ReachPreset;
typedef struct ReachJoint {
    int index;
    uint32_t id;
    float length, comfort;
    SkeletonJoint bounded;
    Quaternion neutral;
    Vector3 preferredBend;
} ReachJoint;
typedef struct AnatomicalReachProfile {
    ReachJoint joints[REACH_MAX_JOINTS];
    unsigned count, torsoCount;
    int head;
    Vector3 oralPoint, oralDirection;
    float maximumReach, maximumCrouch;
    ReachPreset preset;
} AnatomicalReachProfile;
typedef struct AnatomicalReachState {
    Quaternion rotation[REACH_MAX_JOINTS], desired[REACH_MAX_JOINTS];
    unsigned tick;
    Vector3 velocity[REACH_MAX_JOINTS];
    double remainder;
    float stableTime, stalledTime, compensationTime;
    uint32_t objectId;
    Vector3 previousTarget;
    bool initialized;
} AnatomicalReachState;
/** @brief Preset conservador editable, independiente de nombres de especie. */
ReachPreset AnatomicalReach_DefaultPreset(void);
/** @brief Compila ancestros cervicales y segmentos reales del rig actual. */
bool AnatomicalReach_Compile(AnatomicalReachProfile *, const Rig *, ReachPreset);
/** @brief Invalida contacto; transporta sólo rotaciones de articulaciones compatibles. */
void AnatomicalReach_Reset(AnatomicalReachState *, const AnatomicalReachProfile *, const SkeletonPose *);
/** @brief Resuelve posición/orientación y filtra movimiento con pasos de 1/120 s.
 * @param target NULL solicita retirada al reposo; coordenadas del marco de pose.
 * @return Contacto real, nunca convergencia sobre un objetivo proyectado. */
ReachResult AnatomicalReach_Update(const AnatomicalReachProfile *, AnatomicalReachState *,
    const Skeleton *, SkeletonPose *, const InteractionTarget *, float dt);
#endif
