/**
 * @file LimbInsectWing.h
 * @brief Ala membranosa independiente de insecto con raíz torácica, venación y desarrollo hemimetábolo.
 */
#ifndef CREATURE_LIMB_INSECT_WING_H
#define CREATURE_LIMB_INSECT_WING_H

#include "Attachment.h"
#include "CreaturePhenotype.h"
#include "LimbRig.h"

#ifdef __cplusplus
extern "C" {
#endif

bool LimbInsectWing_Resolve(const LimbPhenotype* phenotype,
                            uint32_t moduleInstanceId,
                            const AttachmentSlot* slot,
                            AnatomyGraph* graph);

bool LimbInsectWing_BuildRigDescriptor(const LimbPhenotype* phenotype,
                                       uint32_t moduleInstanceId,
                                       const AnatomyGraph* graph,
                                       LimbRigDescriptor* out);

void LimbInsectWing_ConfigureRig(const LimbRigDescriptor* limb,
                                 const AnatomyGraph* graph,
                                 Rig* rig);

#ifdef __cplusplus
}
#endif

#endif
