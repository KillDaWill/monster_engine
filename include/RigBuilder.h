/** @file RigBuilder.h
 * @brief Compila árboles anatómicos y perfiles de miembros independientes de especie.
 */
#ifndef MONSTER_RIG_BUILDER_H
#define MONSTER_RIG_BUILDER_H
#include "IK.h"
#define RIG_MAX_LIMBS 16
#define RIG_MAX_AXIAL_JOINTS 32
typedef enum LimbRole { LIMB_FORE,LIMB_HIND,LIMB_ARM,LIMB_LEG,LIMB_WING,LIMB_OTHER } LimbRole;
typedef enum LimbSide { LIMB_LEFT,LIMB_RIGHT,LIMB_CENTER } LimbSide;
typedef struct LimbRig {
    IKChain chain;
    int rootJoint,endEffectorJoint;
    LimbRole role;
    LimbSide side;
    bool walking;
    bool digitigrade; /**< Metapodio orientado antes de resolver la cadena proximal. */
    bool unguligrade; /**< Apoyo en dos suelas de queratina, por debajo del menudillo. */
    float soleHeight;
} LimbRig;
#define RIG_MAX_TAILS 8
#define RIG_MAX_TAIL_JOINTS 32
typedef struct TailRig {
    uint32_t moduleInstanceId;
    int joints[RIG_MAX_TAIL_JOINTS];
    size_t jointCount;
} TailRig;
typedef struct Rig {
    Skeleton skeleton;
    LimbRig limbs[RIG_MAX_LIMBS]; size_t limbCount;
    int headJoint,neckJoint,jawJoint,pelvisJoint;
    int cervicalBaseJoint; /**< Pivote cervical que no arrastra la cintura ni los apoyos. */
    Vector3 oralDirection; /**< Dirección oral resuelta en el marco cefálico de reposo. */
    Vector3 oralPoint; /**< Landmark oral en el marco local de la cabeza de reposo. */
    Vector3 cranialAttachment,cranialAxis; /**< Plano occipital de reposo: el cráneo es rígido. */
    float cranialCollar; /**< Transición de piel exclusivamente detrás del plano occipital. */
    int spine[RIG_MAX_AXIAL_JOINTS];
    size_t spineCount;
    float spineRadius[RIG_MAX_AXIAL_JOINTS]; /**< Envolvente conservadora para compensación corporal. */
    TailRig tails[RIG_MAX_TAILS];
    size_t tailCount;
    float maxJawAngle;
} Rig;
/** @brief Orienta las aristas desde rootId; rechaza ciclos/desconexiones y ordena padres primero. */
bool RigBuilder_FromAnatomy(const AnatomyGraph* anatomy, AnatomyId rootId, Rig* rig);
/** @brief Registra una cadena contigua por identidades estables, sin asumir número de patas. */
bool RigBuilder_AddLimb(Rig* rig,const AnatomyId* ids,size_t count,LimbRole role,LimbSide side,bool walking);
#endif
