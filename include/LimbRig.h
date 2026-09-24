/** @file LimbRig.h
 * @brief Descriptor neutral de articulaciones de una extremidad.
 */
#ifndef CREATURE_LIMB_RIG_H
#define CREATURE_LIMB_RIG_H
#include "CreaturePhenotype.h"
#include "RigBuilder.h"
#define LIMB_RIG_MAX_JOINTS 16

/** Descriptor genérico de rig para una extremidad modular. */
typedef struct LimbRigDescriptor {
    uint32_t moduleInstanceId;
    AnatomyId joints[LIMB_RIG_MAX_JOINTS];
    size_t jointCount;
    AnatomyId endEffector;
    LimbRole role;
    LimbSide side;
    LimbArchetype archetype;
    bool locomotionLimb;
    float soleHeight;
} LimbRigDescriptor;

#endif
