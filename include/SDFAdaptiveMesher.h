/**
 * @file SDFAdaptiveMesher.h
 * @brief Extracción local conformante mediante octree y tetraedros de transición.
 */
#ifndef MONSTER_SDF_ADAPTIVE_MESHER_H
#define MONSTER_SDF_ADAPTIVE_MESHER_H
#include "SDFMesher.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SDFAdaptiveWorkspace SDFAdaptiveWorkspace;

/** @brief Crea un espacio de trabajo reutilizable para evitar alocaciones por fotograma. */
SDFAdaptiveWorkspace* SDFAdaptiveWorkspace_Create(void);

/** @brief Libera la memoria interna del espacio de trabajo adaptativo. */
void SDFAdaptiveWorkspace_Free(SDFAdaptiveWorkspace* ws);

/** @brief Genera detalle local sin extender sus planos finos a todo el cuerpo. */
bool SDFAdaptiveMesher_Generate(SDFMesher* mesher, const SDFField* field,
    const SDFDetailRegion* regions, size_t regionCount, Mesh* mesh);

#ifdef __cplusplus
}
#endif

#endif
