#ifndef FLEXIBLE_APPENDAGE_H
#define FLEXIBLE_APPENDAGE_H

#include "Attachment.h"
#include "CreaturePhenotype.h"

#define FLEXIBLE_APPENDAGE_STATIONS 20

typedef enum FlexibleAppendagePose {
    FLEXIBLE_POSE_RELAXED,
    FLEXIBLE_POSE_CURLED,
    FLEXIBLE_POSE_REACHING,
    FLEXIBLE_POSE_FEEDING,
    FLEXIBLE_POSE_COUNT
} FlexibleAppendagePose;

/** @brief Resuelve una cadena anclada de secciones elípticas y su extremo opcional. */
bool FlexibleAppendage_Resolve(const FlexibleAppendagePhenotype* phenotype, uint32_t module,
    const AttachmentSlot* slot, AnatomyGraph* graph);

/** @brief Aplica una flexión acotada respecto al reposo sin alterar la longitud de los eslabones. */
bool FlexibleAppendage_ApplyPose(uint32_t module, FlexibleAppendagePose gesture, float weight,
    const Skeleton* skeleton, SkeletonPose* pose);

#endif
