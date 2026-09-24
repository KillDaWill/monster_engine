/**
 * @file Limb.h
 * @brief Despacho de resolución de anatomía y rigging modular para extremidades.
 * @author Monster Engine Team
 * @date 2026
 */

#ifndef CREATURE_LIMB_H
#define CREATURE_LIMB_H

#include "Attachment.h"
#include "CreaturePhenotype.h"
#include "LimbRig.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    LIMB_NODE_ROOT = 1,
    LIMB_NODE_MIDDLE,
    LIMB_NODE_DISTAL,
    LIMB_NODE_AUTOPOD
};

/** @brief Indica si el módulo implementa anatomía y rig del arquetipo. */
bool Limb_Supports(LimbArchetype archetype);

/** @return ID digital local, o cero para argumentos fuera de rango. */
uint16_t Limb_DigitLocalId(unsigned digit, unsigned station);

/** @brief Despacha la resolución de la extremidad según su arquetipo fenotípico. @return Éxito. */
bool Limb_Resolve(const LimbPhenotype* phenotype, uint32_t moduleInstanceId, const AttachmentSlot* slot, AnatomyGraph* graph);

/** @brief Despacha la construcción del descriptor de rigging según el arquetipo. @return Éxito. */
bool Limb_BuildRigDescriptor(const LimbPhenotype* phenotype, uint32_t moduleInstanceId, const AnatomyGraph* graph, LimbRigDescriptor* out);

/** @brief Despacha la configuración de restricciones del miembro en el rig. */
void Limb_ConfigureRig(const LimbRigDescriptor* limb, const AnatomyGraph* graph, Rig* rig);

#ifdef __cplusplus
}
#endif

#endif
