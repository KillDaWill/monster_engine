/**
 * @file LimbArthropod.h
 * @brief Pata articulada exoesquelética de artrópodo (coxa, trocánter, fémur, tibia, tarso y uñas).
 */
#ifndef CREATURE_LIMB_ARTHROPOD_H
#define CREATURE_LIMB_ARTHROPOD_H

#include "Attachment.h"
#include "CreaturePhenotype.h"
#include "LimbRig.h"

#ifdef __cplusplus
extern "C" {
#endif

bool LimbArthropod_Resolve(const LimbPhenotype* phenotype,
                           uint32_t moduleInstanceId,
                           const AttachmentSlot* slot,
                           AnatomyGraph* graph);

bool LimbArthropod_BuildRigDescriptor(const LimbPhenotype* phenotype,
                                      uint32_t moduleInstanceId,
                                      const AnatomyGraph* graph,
                                      LimbRigDescriptor* out);

void LimbArthropod_ConfigureRig(const LimbRigDescriptor* limb,
                                const AnatomyGraph* graph,
                                Rig* rig);

#ifdef __cplusplus
}
#endif

#endif
