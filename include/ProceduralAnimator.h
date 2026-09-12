/** @file ProceduralAnimator.h
 * @brief Coordinador genérico de FK secundario, contactos e IK.
 */
#ifndef MONSTER_PROCEDURAL_ANIMATOR_H
#define MONSTER_PROCEDURAL_ANIMATOR_H
#include "Locomotion.h"
typedef struct ProceduralAnimator {
    Locomotion locomotion;
    GaitProfile gait;
    Vector3 desiredVelocity,lookDirection;
    float mouthOpen;
    IKResult limbResults[RIG_MAX_LIMBS];
} ProceduralAnimator;
/** @brief Inicializa controlador con perfil y posición mundial. */
bool ProceduralAnimator_Init(ProceduralAnimator* animator,const Rig* rig,GaitProfile gait,World* world,Vector3 position);
/** @brief FK axial antes de IK para no desplazar después los pies plantados. */
bool ProceduralAnimator_Update(ProceduralAnimator* animator,const Rig* rig,SkeletonPose* pose,World* world,float dt);
#endif
