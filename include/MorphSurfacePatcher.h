/** @file MorphSurfacePatcher.h
 * @brief Reconstrucción SDF transaccional de módulos que cambian de topología.
 */
#ifndef MORPH_SURFACE_PATCHER_H
#define MORPH_SURFACE_PATCHER_H

#include "AnatomyDeformer.h"
#include "CreatureMorph.h"
#include "MeshPatch.h"
#include "MonsterSDF.h"
#include "MorphSurfaceTracker.h"
#include "SDFMesher.h"

/** @brief Coste y calidad de una sustitución local. */
typedef struct MorphSurfacePatchStats {
    uint32_t moduleInstanceId;
    size_t problemTriangles,removedTriangles,newVertices;
    size_t opposedBefore,opposedAfter;
    size_t distanceEvaluations,cells;
    float cutMs,meshingMs,stitchMs;
    MeshPatchStats seam;
} MorphSurfacePatchStats;

/** @brief Elige el módulo con más caras problemáticas entre eventos reales. */
uint32_t MorphSurfacePatcher_SelectModule(const Mesh* mesh,
    const AnatomyDeformer* binding,const MorphSurfaceTracker* tracker,
    const CreatureMorphTopologyEvent* events,size_t eventCount,
    size_t* problemTriangles);

/** @brief Sustituye sólo la zona distal de un módulo, si la costura es válida.
 * `result` y `transfer` no se alteran al fallar. Ambos deben estar inicializados.
 * Las dos mallas se recortan con exactamente los mismos seis planos.
 */
bool MorphSurfacePatcher_TryModule(const Mesh* source,
    const AnatomyDeformer* binding,const Monster* target,
    const MonsterSDF* sdf,SDFMesher* mesher,uint32_t moduleInstanceId,
    Mesh* result,MeshPatchTransfer* transfer,MorphSurfacePatchStats* stats);

#endif
