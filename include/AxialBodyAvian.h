/**
 * @file AxialBodyAvian.h
 * @brief Estrategia axial aviar con quilla pectoral, hombros dorsales altos y uropigio compacto.
 */
#ifndef CREATURE_AXIALBODY_AVIAN_H
#define CREATURE_AXIALBODY_AVIAN_H

#include "AxialBody.h"
#include "SDFPrimitives.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Resuelve las estaciones anatómicas y ranuras de un torso aviar aerodinámico. */
bool AxialBodyAvian_Resolve(const AxialPhenotype* phenotype,
                            uint32_t moduleInstanceId,
                            AnatomyGraph* graph,
                            AttachmentSlotSet* slots);

/** @brief Genera estaciones de barrido elíptico continuo con pecho aquillado y transición cervical suave. */
bool AxialBodyAvian_BuildSweepStations(const AxialPhenotype* phenotype,
                                       const AnatomyGraph* graph,
                                       uint32_t moduleInstanceId,
                                       SDFSweepStation* outStations,
                                       int maxStations,
                                       int* outCount);

#ifdef __cplusplus
}
#endif

#endif
