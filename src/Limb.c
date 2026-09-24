/**
 * @file Limb.c
 * @brief Implementación del despachador modular de extremidades.
 * @author Monster Engine Team
 * @date 2026
 */

#include "Limb.h"
#include "LimbReptile.h"
#include "LimbMammal.h"

bool Limb_Supports(LimbArchetype a) {
    return a == LIMB_ARCHETYPE_REPTILE || a == LIMB_ARCHETYPE_MAMMAL;
}

uint16_t Limb_DigitLocalId(unsigned digit, unsigned station) {
    return digit < CREATURE_MAX_DIGITS && station < 7 ? (uint16_t)(32 + digit * 8 + station) : 0;
}

bool Limb_Resolve(const LimbPhenotype* phenotype, uint32_t moduleInstanceId, const AttachmentSlot* slot, AnatomyGraph* graph) {
    if (!phenotype) return false;
    switch (phenotype->archetype) {
    case LIMB_ARCHETYPE_MAMMAL:
        return LimbMammal_Resolve(phenotype, moduleInstanceId, slot, graph);
    case LIMB_ARCHETYPE_REPTILE:
        return LimbReptile_Resolve(phenotype, moduleInstanceId, slot, graph);
    default:
        return false;
    }
}

bool Limb_BuildRigDescriptor(const LimbPhenotype* phenotype, uint32_t moduleInstanceId, const AnatomyGraph* graph, LimbRigDescriptor* out) {
    if (!phenotype) return false;
    switch (phenotype->archetype) {
    case LIMB_ARCHETYPE_MAMMAL:
        return LimbMammal_BuildRigDescriptor(phenotype, moduleInstanceId, graph, out);
    case LIMB_ARCHETYPE_REPTILE:
        return LimbReptile_BuildRigDescriptor(phenotype, moduleInstanceId, graph, out);
    default:
        return false;
    }
}

void Limb_ConfigureRig(const LimbRigDescriptor* limb, const AnatomyGraph* graph, Rig* rig) {
    if (!limb) return;
    switch (limb->archetype) {
    case LIMB_ARCHETYPE_MAMMAL:
        LimbMammal_ConfigureRig(limb, graph, rig);
        break;
    case LIMB_ARCHETYPE_REPTILE:
        LimbReptile_ConfigureRig(limb, graph, rig);
        break;
    default:
        break;
    }
}
