/**
 * @file Limb.c
 * @brief Implementación del despachador modular de extremidades.
 * @author Monster Engine Team
 * @date 2026
 */

#include "Limb.h"
#include "LimbReptile.h"
#include "LimbMammal.h"
#include "LimbBatWing.h"
#include "LimbAvian.h"
#include "LimbFin.h"
#include "LimbArthropod.h"
#include "LimbInsectWing.h"

bool Limb_Supports(LimbArchetype a) {
    return a == LIMB_ARCHETYPE_REPTILE || a == LIMB_ARCHETYPE_MAMMAL ||
           a == LIMB_ARCHETYPE_UNGULATE || a == LIMB_ARCHETYPE_WING ||
           a == LIMB_ARCHETYPE_AVIAN || a == LIMB_ARCHETYPE_FLIPPER ||
           a == LIMB_ARCHETYPE_ARTHROPOD_LEG || a == LIMB_ARCHETYPE_INSECT_WING;
}

uint16_t Limb_DigitLocalId(unsigned digit, unsigned station) {
    return digit < CREATURE_MAX_DIGITS && station < 8 ? (uint16_t)(32 + digit * 8 + station) : 0;
}

bool Limb_Resolve(const LimbPhenotype* phenotype, uint32_t moduleInstanceId, const AttachmentSlot* slot, AnatomyGraph* graph) {
    if (!phenotype) return false;
    switch (phenotype->archetype) {
    case LIMB_ARCHETYPE_MAMMAL:
    case LIMB_ARCHETYPE_UNGULATE:
        return LimbMammal_Resolve(phenotype, moduleInstanceId, slot, graph);
    case LIMB_ARCHETYPE_REPTILE:
        return LimbReptile_Resolve(phenotype, moduleInstanceId, slot, graph);
    case LIMB_ARCHETYPE_WING:
        return LimbBatWing_Resolve(phenotype, moduleInstanceId, slot, graph);
    case LIMB_ARCHETYPE_AVIAN:
        return LimbAvian_Resolve(phenotype, moduleInstanceId, slot, graph);
    case LIMB_ARCHETYPE_FLIPPER:
        return LimbFin_Resolve(phenotype, moduleInstanceId, slot, graph);
    case LIMB_ARCHETYPE_ARTHROPOD_LEG:
        return LimbArthropod_Resolve(phenotype, moduleInstanceId, slot, graph);
    case LIMB_ARCHETYPE_INSECT_WING:
        return LimbInsectWing_Resolve(phenotype, moduleInstanceId, slot, graph);
    default:
        return false;
    }
}

bool Limb_BuildRigDescriptor(const LimbPhenotype* phenotype, uint32_t moduleInstanceId, const AnatomyGraph* graph, LimbRigDescriptor* out) {
    if (!phenotype) return false;
    switch (phenotype->archetype) {
    case LIMB_ARCHETYPE_MAMMAL:
    case LIMB_ARCHETYPE_UNGULATE:
        return LimbMammal_BuildRigDescriptor(phenotype, moduleInstanceId, graph, out);
    case LIMB_ARCHETYPE_REPTILE:
        return LimbReptile_BuildRigDescriptor(phenotype, moduleInstanceId, graph, out);
    case LIMB_ARCHETYPE_WING:
        return LimbBatWing_BuildRigDescriptor(phenotype, moduleInstanceId, graph, out);
    case LIMB_ARCHETYPE_AVIAN:
        return LimbAvian_BuildRigDescriptor(phenotype, moduleInstanceId, graph, out);
    case LIMB_ARCHETYPE_FLIPPER:
        return LimbFin_BuildRigDescriptor(phenotype, moduleInstanceId, graph, out);
    case LIMB_ARCHETYPE_ARTHROPOD_LEG:
        return LimbArthropod_BuildRigDescriptor(phenotype, moduleInstanceId, graph, out);
    case LIMB_ARCHETYPE_INSECT_WING:
        return LimbInsectWing_BuildRigDescriptor(phenotype, moduleInstanceId, graph, out);
    default:
        return false;
    }
}

void Limb_ConfigureRig(const LimbRigDescriptor* limb, const AnatomyGraph* graph, Rig* rig) {
    if (!limb) return;
    switch (limb->archetype) {
    case LIMB_ARCHETYPE_MAMMAL:
    case LIMB_ARCHETYPE_UNGULATE:
        LimbMammal_ConfigureRig(limb, graph, rig);
        break;
    case LIMB_ARCHETYPE_REPTILE:
        LimbReptile_ConfigureRig(limb, graph, rig);
        break;
    case LIMB_ARCHETYPE_WING:
        LimbBatWing_ConfigureRig(limb, graph, rig);
        break;
    case LIMB_ARCHETYPE_AVIAN:
        LimbAvian_ConfigureRig(limb, graph, rig);
        break;
    case LIMB_ARCHETYPE_FLIPPER:
        LimbFin_ConfigureRig(limb, graph, rig);
        break;
    case LIMB_ARCHETYPE_ARTHROPOD_LEG:
        LimbArthropod_ConfigureRig(limb, graph, rig);
        break;
    case LIMB_ARCHETYPE_INSECT_WING:
        LimbInsectWing_ConfigureRig(limb, graph, rig);
        break;
    default:
        break;
    }
}
