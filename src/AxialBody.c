/**
 * @file AxialBody.c
 * @brief Implementación del despachador axial.
 * @author Monster Engine Team
 * @date 2026
 */

#include "AxialBody.h"
#include "AxialBodySprawlingTetrapod.h"
#include "AxialBodyUprightTetrapod.h"
#include "AxialBodyAvian.h"
#include "AxialBodyDiscRay.h"
#include "AxialBodyCephalopod.h"
#include "AxialBodyArthropod.h"
#include "AxialBodyMedusa.h"

bool AxialBody_Supports(AxialArchetype a) {
    return a == AXIAL_ARCHETYPE_SPRAWLING_TETRAPOD ||
           a == AXIAL_ARCHETYPE_UPRIGHT_TETRAPOD ||
           a == AXIAL_ARCHETYPE_AVIAN ||
           a == AXIAL_ARCHETYPE_DISC_RAY ||
           a == AXIAL_ARCHETYPE_CEPHALOPOD ||
           a == AXIAL_ARCHETYPE_ARTHROPOD ||
           a == AXIAL_ARCHETYPE_MEDUSA_RADIAL;
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
    case AXIAL_ARCHETYPE_AVIAN:
        return AxialBodyAvian_Resolve(phenotype, moduleInstanceId, graph, slots);
    case AXIAL_ARCHETYPE_DISC_RAY:
        return AxialBodyDiscRay_Resolve(phenotype, moduleInstanceId, graph, slots);
    case AXIAL_ARCHETYPE_CEPHALOPOD:
        return AxialBodyCephalopod_Resolve(phenotype, moduleInstanceId, graph, slots);
    case AXIAL_ARCHETYPE_ARTHROPOD:
        return AxialBodyArthropod_Resolve(phenotype, moduleInstanceId, graph, slots);
    case AXIAL_ARCHETYPE_MEDUSA_RADIAL:
        return AxialBodyMedusa_Resolve(phenotype, moduleInstanceId, graph, slots);
    default:
        return false;
    }
}
