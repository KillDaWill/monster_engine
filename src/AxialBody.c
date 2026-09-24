/**
 * @file AxialBody.c
 * @brief Implementación del despachador axial.
 * @author Monster Engine Team
 * @date 2026
 */

#include "AxialBody.h"
#include "AxialBodySprawlingTetrapod.h"
#include "AxialBodyUprightTetrapod.h"

bool AxialBody_Supports(AxialArchetype a) {
    return a == AXIAL_ARCHETYPE_SPRAWLING_TETRAPOD || a == AXIAL_ARCHETYPE_UPRIGHT_TETRAPOD;
}

bool AxialBody_Resolve(const AxialPhenotype* phenotype,
                       uint32_t moduleInstanceId,
                       AnatomyGraph* graph,
                       AttachmentSlotSet* slots) {
    if (!phenotype) return false;
    switch (phenotype->archetype) {
    case AXIAL_ARCHETYPE_UPRIGHT_TETRAPOD:
        return AxialBodyUprightTetrapod_Resolve(phenotype, moduleInstanceId, graph, slots);
    case AXIAL_ARCHETYPE_SPRAWLING_TETRAPOD:
        return AxialBodySprawlingTetrapod_Resolve(phenotype, moduleInstanceId, graph, slots);
    default:
        return false;
    }
}
