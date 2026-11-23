/**
 * @file AxialBodyCephalopod.h
 * @brief Estrategia axial de cefalópodo: manto muscular, collar, cabeza corta, corona circumoral y sifón.
 */
#ifndef CREATURE_AXIALBODY_CEPHALOPOD_H
#define CREATURE_AXIALBODY_CEPHALOPOD_H

#include "AxialBody.h"
#include "SDFPrimitives.h"

#ifdef __cplusplus
extern "C" {
#endif

bool AxialBodyCephalopod_Resolve(const AxialPhenotype* phenotype,
                                 uint32_t moduleInstanceId,
                                 AnatomyGraph* graph,
                                 AttachmentSlotSet* slots);

bool AxialBodyCephalopod_BuildSweepStations(const AxialPhenotype* phenotype,
                                            const AnatomyGraph* graph,
                                            uint32_t moduleInstanceId,
                                            SDFSweepStation* outStations,
                                            int maxStations,
                                            int* outCount);

#ifdef __cplusplus
}
#endif

#endif
