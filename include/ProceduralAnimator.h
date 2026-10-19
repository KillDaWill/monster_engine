/** @file ProceduralAnimator.h
 * @brief Coordinador genérico de FK secundario, contactos e IK.
 */
#ifndef MONSTER_PROCEDURAL_ANIMATOR_H
#define MONSTER_PROCEDURAL_ANIMATOR_H
#include "Locomotion.h"
#include "AnatomicalReach.h"
typedef struct ProceduralAnimator {
    Locomotion locomotion;
    GaitProfile gait;
    Vector3 desiredVelocity,lookDirection;
    float angularVelocity,currentAngularVelocity;
    float mouthOpen;
    double crouchRemainder;
    float reachCrouch, reachCrouchVelocity, oralReachTime;
    bool oralReachActive; /**< Solicitud corporal de alcanzar un punto con la boca. */
    Vector3 oralReachTarget; /**< Objetivo en el mismo marco que la pose locomotora. */
    Quaternion cervicalRotation[3]; /**< Continuidad del gesto cervical, sin mover el tronco. */
    bool (*validateInteractionPose)(const Rig *,const SkeletonPose *,void *);
    void *interactionContext;
    void (*applyAxialPose)(const Rig*,SkeletonPose*,void*); /**< Autoridad corporal opcional, antes de resolver apoyos. */
    void *axialContext;
    AnatomicalReachProfile reachProfile;
    AnatomicalReachState reachState;
    InteractionTarget interactionTarget;
    ReachResult reachResult;
    IKResult limbResults[RIG_MAX_LIMBS];
} ProceduralAnimator;
/** @brief Inicializa controlador con perfil y posición mundial. */
bool ProceduralAnimator_Init(ProceduralAnimator* animator,const Rig* rig,GaitProfile gait,World* world,Vector3 position);
/** @brief FK axial antes de IK para no desplazar después los pies plantados. */
bool ProceduralAnimator_Update(ProceduralAnimator* animator,const Rig* rig,SkeletonPose* pose,World* world,float dt);
#endif
