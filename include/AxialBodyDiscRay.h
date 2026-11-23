/**
 * @file AxialBodyDiscRay.h
 * @brief Estrategia axial batoidea de cuerpo central aplanado dorsoventralmente en disco continuo.
 */
#ifndef CREATURE_AXIALBODY_DISC_RAY_H
#define CREATURE_AXIALBODY_DISC_RAY_H

#include "AxialBody.h"
#include "SDFPrimitives.h"

#ifdef __cplusplus
extern "C" {
#endif

bool AxialBodyDiscRay_Resolve(const AxialPhenotype* phenotype,
                              uint32_t moduleInstanceId,
                              AnatomyGraph* graph,
                              AttachmentSlotSet* slots);

bool AxialBodyDiscRay_BuildSweepStations(const AxialPhenotype* phenotype,
                                         const AnatomyGraph* graph,
                                         uint32_t moduleInstanceId,
                                         SDFSweepStation* outStations,
                                         int maxStations,
                                         int* outCount);

#ifdef __cplusplus
}
#endif

#endif
