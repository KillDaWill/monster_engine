/**
 * @file LimbReptile.h
 * @brief Arquetipo de extremidad reptiliana cuadrúpeda con marcha baja y dígitos articulados.
 * @author Monster Engine Team
 * @date 2026
 */

#ifndef CREATURE_LIMB_REPTILE_H
#define CREATURE_LIMB_REPTILE_H

#include "Attachment.h"
#include "CreaturePhenotype.h"
#include "Anatomy.h"
#include "LimbRig.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Resuelve la anatomía de una extremidad reptiliana (húmero/fémur, radio/tibia, carpo/tarso y dígitos).
 */
bool LimbReptile_Resolve(const LimbPhenotype* phenotype,
                         uint32_t moduleInstanceId,
                         const AttachmentSlot* slot,
                         AnatomyGraph* graph);

/**
 * @brief Construye el descriptor de articulaciones de rigging para una extremidad reptiliana.
 */
bool LimbReptile_BuildRigDescriptor(const LimbPhenotype* phenotype,
                                    uint32_t moduleInstanceId,
                                    const AnatomyGraph* graph,
                                    LimbRigDescriptor* out);

/**
 * @brief Configura las restricciones angulares de rótula y bisagra específicas de reptil en el rig.
 */
void LimbReptile_ConfigureRig(const LimbRigDescriptor* limb,
                              const AnatomyGraph* graph,
                              Rig* rig);

#ifdef __cplusplus
}
#endif

#endif
