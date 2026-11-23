/**
 * @file AxialBodyArthropod.h
 * @brief Estrategia axial de artrópodo hexápodo: cabeza, tórax muscular, 6 patas torácicas, 4 alas y abdomen segmentado.
 */
#ifndef CREATURE_AXIALBODY_ARTHROPOD_H
#define CREATURE_AXIALBODY_ARTHROPOD_H

#include "AxialBody.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Nodos locales para segmentos abdominales individuales (20..31). */
#define AXIAL_ARTHROPOD_SEGMENT_NODE_BASE 20

bool AxialBodyArthropod_Resolve(const AxialPhenotype* phenotype,
                                uint32_t moduleInstanceId,
                                AnatomyGraph* graph,
                                AttachmentSlotSet* slots);

#ifdef __cplusplus
}
#endif

#endif
