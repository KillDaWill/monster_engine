/** @file LimbMammal.h
 * @brief Extremidades parasagitales digitígradas y sus restricciones articulares.
 */
#ifndef CREATURE_LIMB_MAMMAL_H
#define CREATURE_LIMB_MAMMAL_H
#include "Attachment.h"
#include "LimbRig.h"
/** @brief Resuelve hombro/cadera, codo/rodilla, carpo/tarso, pata y falanges. */
bool LimbMammal_Resolve(const LimbPhenotype* phenotype,uint32_t module,
    const AttachmentSlot* slot,AnatomyGraph* graph);
/** @brief Construye la cadena y calcula la suela a partir de todos sus dígitos. */
bool LimbMammal_BuildRigDescriptor(const LimbPhenotype* phenotype,uint32_t module,
    const AnatomyGraph* graph,LimbRigDescriptor* out);
/** @brief Configura bisagras sagitales y articulaciones de soporte. */
void LimbMammal_ConfigureRig(const LimbRigDescriptor* limb,const AnatomyGraph* graph,Rig* rig);
#endif
