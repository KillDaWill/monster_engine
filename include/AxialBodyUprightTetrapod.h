/** @file AxialBodyUprightTetrapod.h
 * @brief Tronco erecto con caja torácica profunda y retracción abdominal.
 */
#ifndef CREATURE_AXIAL_UPRIGHT_H
#define CREATURE_AXIAL_UPRIGHT_H
#include "AxialBody.h"
#include "SDFPrimitives.h"

/** @brief Publica seis estaciones semánticas canónicas y anclajes parasagitales. @return Éxito. */
bool AxialBodyUprightTetrapod_Resolve(const AxialPhenotype* phenotype,
    uint32_t module, AnatomyGraph* graph, AttachmentSlotSet* slots);

/**
 * @brief Construye la secuencia continua de estaciones de barrido axial para el tetrápodo erecto,
 *        resolviendo C0 (cervical superior), C1 (cervical medio con curvatura/masa nucal),
 *        C2 (cervical inferior ensanchada), W (cruz/withers escapular), Pectoral,
 *        Tórax Anterior/Posterior, Abdomen y Pelvis.
 * @param phenotype Fenotipo axial con proporciones y parámetros cervicales.
 * @param graph Grafo anatómico resuelto.
 * @param module Identificador de módulo axial.
 * @param outStations Búfer de estaciones de barrido.
 * @param maxStations Capacidad del búfer (mínimo 9).
 * @param outCount Número de estaciones generadas.
 * @return true si se generaron las estaciones; false si falló.
 */
bool AxialBodyUprightTetrapod_BuildSweepStations(const AxialPhenotype* phenotype,
    const AnatomyGraph* graph, uint32_t module,
    SDFSweepStation* outStations, int maxStations, int* outCount);

#endif
