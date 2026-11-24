/** @file AnatomicalReach.h
 * @brief Alcance acotado sin traslaciones, escalado ni asignaciones por fotograma. */
#ifndef MONSTER_ANATOMICAL_REACH_H
#define MONSTER_ANATOMICAL_REACH_H
#include "RigBuilder.h"
#include "InteractionTarget.h"
#define REACH_MAX_JOINTS 12
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
    Quaternion invNeutral;
    Vector3 preferredBend;
    Vector3 restLocalPosition;
    Vector3 swingRightAxis, swingUpAxis, swingFwdAxis;
} ReachJoint;
typedef struct AnatomicalReachProfile {
    ReachJoint joints[REACH_MAX_JOINTS];
    unsigned count, torsoCount;
    int head;
    int baseParentIndex;
    Vector3 oralPoint, oralDirection;
    Vector3 baseParentRestPos;
    Quaternion baseParentRestRot;
    float maximumReach, maximumCrouch, cervicalScale;
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
    /* Caché de resolución compacta (warm-start y planificación adaptativa). */
    bool hasSolvedDesired;
    bool lastSolvedCompensation;
    bool lastInContact;
    uint32_t lastSolvedObjectId;
    Vector3 lastSolvedTarget;
    Vector3 lastSolvedBasePos;
    float lastSolvedError;
    float lastSolvedOrientError;
} AnatomicalReachState;
/** @brief Preset conservador editable, independiente de nombres de especie. */
ReachPreset AnatomicalReach_DefaultPreset(void);
/** @brief Compila ancestros cervicales y segmentos reales del rig actual. */
bool AnatomicalReach_Compile(AnatomicalReachProfile *, const Rig *, ReachPreset);
/** @brief Invalida contacto; transporta sólo rotaciones de articulaciones compatibles. */
void AnatomicalReach_Reset(AnatomicalReachState *, const AnatomicalReachProfile *, const SkeletonPose *);
/** @brief Invalida únicamente el objetivo resuelto en caché (p. ej. al cambiar cerebro o morfología). */
void AnatomicalReach_InvalidateCache(AnatomicalReachState *);
/** @brief Resuelve posición/orientación y filtra movimiento con pasos de 1/120 s (oráculo completo).
 * @param target NULL solicita retirada al reposo; coordenadas del marco de pose.
 * @return Contacto real, nunca convergencia sobre un objetivo proyectado. */
ReachResult AnatomicalReach_Update(const AnatomicalReachProfile *, AnatomicalReachState *,
    const Skeleton *, SkeletonPose *, const InteractionTarget *, float dt);
/** @brief Variante compacta que opera exclusivamente sobre la cadena cervical compilada.
 *
 * Preserva el mismo objetivo, restricciones articulares e integración de 1/120 s sin
 * recorrer ni copiar el esqueleto completo. Devuelve las posiciones y rotaciones mundiales
 * de los eslabones cervicales en @p outWorldPos y @p outWorldRot (ambos opcionales).
 */
ReachResult AnatomicalReach_UpdateCompact(const AnatomicalReachProfile *, AnatomicalReachState *,
    Vector3 baseParentWorldPos, Quaternion baseParentWorldRot, const InteractionTarget *, float dt,
    Vector3 *outWorldPos, Quaternion *outWorldRot, unsigned *outSolveCalls, unsigned *outFkUpdates);
#endif
