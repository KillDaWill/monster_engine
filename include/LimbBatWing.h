/**
 * @file LimbBatWing.h
 * @brief Anatomía procedural y rigging de alas quiropteras (húmero, radio, carpo, pulgar libre y falanges II-V).
 */
#ifndef CREATURE_LIMB_BAT_WING_H
#define CREATURE_LIMB_BAT_WING_H

#include "Attachment.h"
#include "CreaturePhenotype.h"
#include "LimbRig.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Resuelve los nodos anatómicos del ala y los radios digitales del patagio. */
bool LimbBatWing_Resolve(const LimbPhenotype* phenotype, uint32_t moduleInstanceId,
                         const AttachmentSlot* slot, AnatomyGraph* graph);

/** @brief Construye el descriptor de rig no ambulatorio del ala (hombro -> codo -> antebrazo -> carpo). */
bool LimbBatWing_BuildRigDescriptor(const LimbPhenotype* phenotype, uint32_t moduleInstanceId,
                                     const AnatomyGraph* graph, LimbRigDescriptor* out);

/** @brief Configura límites articulares de batida y plegado en el esqueleto del ala. */
void LimbBatWing_ConfigureRig(const LimbRigDescriptor* limb, const AnatomyGraph* graph, Rig* rig);

#ifdef __cplusplus
}
#endif

#endif /* CREATURE_LIMB_BAT_WING_H */
