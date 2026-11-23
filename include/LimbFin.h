/**
 * @file LimbFin.h
 * @brief Aletas natatorias continuas batoideas (pectorales en disco y pélvicas) con deformación ondulatoria.
 */
#ifndef CREATURE_LIMB_FIN_H
#define CREATURE_LIMB_FIN_H

#include "Attachment.h"
#include "CreaturePhenotype.h"
#include "LimbRig.h"

#ifdef __cplusplus
extern "C" {
#endif

bool LimbFin_Resolve(const LimbPhenotype* phenotype,
                     uint32_t moduleInstanceId,
                     const AttachmentSlot* slot,
                     AnatomyGraph* graph);

bool LimbFin_BuildRigDescriptor(const LimbPhenotype* phenotype,
                                uint32_t moduleInstanceId,
                                const AnatomyGraph* graph,
                                LimbRigDescriptor* out);

void LimbFin_ConfigureRig(const LimbRigDescriptor* limb,
                          const AnatomyGraph* graph,
                          Rig* rig);

#ifdef __cplusplus
}
#endif

#endif
