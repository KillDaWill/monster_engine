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
    FLEXIBLE_POSE_SPREADING,
    FLEXIBLE_POSE_CRAWLING,
    FLEXIBLE_POSE_COUNT
} FlexibleAppendagePose;

/** @brief Mezcla proporciones de la cadena y permite heredar la sección terminal por separado. */
FlexibleAppendagePhenotype FlexibleAppendage_Interpolate(const FlexibleAppendagePhenotype* a,
    const FlexibleAppendagePhenotype* b,float profileWeight,float tipWeight);

/** Longitud mínima de una prolongación nasal heredada, en unidades de receta. */
float FlexibleAppendage_MinNasalLength(const FlexibleAppendagePhenotype*,float supportHeight);

/** @brief Resuelve una cadena anclada de secciones elípticas y su extremo opcional. */
bool FlexibleAppendage_Resolve(const FlexibleAppendagePhenotype* phenotype, uint32_t module,
    const AttachmentSlot* slot, AnatomyGraph* graph);

/** @brief Aplica una flexión acotada respecto al reposo sin alterar la longitud de los eslabones. */
bool FlexibleAppendage_ApplyPose(uint32_t module, FlexibleAppendagePose gesture, float weight,
    const Skeleton* skeleton, SkeletonPose* pose);

#endif
