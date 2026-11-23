/**
 * @file AxialBodyAquaticFusiform.h
 * @brief Cuerpo axial hidrodinámico fusiforme reutilizable (peces pelágicos, escualos y cetáceos).
 * @author Monster Engine Team
 * @date 2026
 */

#ifndef CREATURE_AXIALBODY_AQUATIC_FUSIFORM_H
#define CREATURE_AXIALBODY_AQUATIC_FUSIFORM_H

#include "AxialBody.h"
#include "SDFPrimitives.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Resuelve estaciones axiales y ranuras de anclaje de un cuerpo acuático fusiforme. */
bool AxialBodyAquaticFusiform_Resolve(const AxialPhenotype* phenotype,
                                      uint32_t moduleInstanceId,
                                      AnatomyGraph* graph,
                                      AttachmentSlotSet* slots);

/** @brief Construye estaciones suaves de barrido elíptico SDF para el cuerpo fusiforme. */
bool AxialBodyAquaticFusiform_BuildSweepStations(const AxialPhenotype* phenotype,
                                                 const AnatomyGraph* graph,
                                                 uint32_t moduleInstanceId,
                                                 SDFSweepStation* outStations,
                                                 int maxStations,
                                                 int* outCount);

#ifdef __cplusplus
}
#endif

#endif
