/**
 * @file SDFAdaptiveMesher.h
 * @brief Extracción local conformante mediante octree y tetraedros de transición.
 */
#ifndef MONSTER_SDF_ADAPTIVE_MESHER_H
#define MONSTER_SDF_ADAPTIVE_MESHER_H
#include "SDFMesher.h"
/** @brief Genera detalle local sin extender sus planos finos a todo el cuerpo. */
bool SDFAdaptiveMesher_Generate(SDFMesher* mesher, const SDFField* field,
    const SDFDetailRegion* regions, size_t regionCount, Mesh* mesh);
#endif
