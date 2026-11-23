/**
 * @file LimbAvian.h
 * @brief Extremidades aviares: ala articulada emplumada y pata prensil anisodáctila con garras.
 */
#ifndef CREATURE_LIMB_AVIAN_H
#define CREATURE_LIMB_AVIAN_H

#include "Attachment.h"
#include "CreaturePhenotype.h"
#include "LimbRig.h"

#ifdef __cplusplus
extern "C" {
#endif

bool LimbAvian_Resolve(const LimbPhenotype* phenotype,
                       uint32_t moduleInstanceId,
                       const AttachmentSlot* slot,
                       AnatomyGraph* graph);

bool LimbAvian_BuildRigDescriptor(const LimbPhenotype* phenotype,
                                  uint32_t moduleInstanceId,
                                  const AnatomyGraph* graph,
                                  LimbRigDescriptor* out);

void LimbAvian_ConfigureRig(const LimbRigDescriptor* limb,
                            const AnatomyGraph* graph,
                            Rig* rig);

#ifdef __cplusplus
}
#endif

#endif
