/** @file SurfaceMapper.h
 * @brief Mapeo genérico desde anatomía de reposo y etiquetas aportadas por la especie.
 */
#ifndef MONSTER_SURFACE_MAPPER_H
#define MONSTER_SURFACE_MAPPER_H
#include "Surface.h"
#include "Anatomy.h"
#include "Mesh.h"
#include "AnatomyDeformer.h"
/** @brief Etiquetas por identidad: independientes del orden de nodos del grafo. */
typedef struct SurfaceRegionTag { AnatomyId node; SurfaceRegion region; } SurfaceRegionTag;
typedef struct SurfaceMapping {
    Vector3 origin;
    float unitScale;
    SurfaceRegionTag tags[ANATOMY_MAX_NODES];
    size_t tagCount;
} SurfaceMapping;
/** @brief Métricas de una clasificación superficial completa. */
typedef struct SurfaceMapperStats {
    size_t verticesProcessed;
    size_t anatomySegmentCount;
    size_t candidateTests;
    size_t bruteForceTests;
    float averageCandidatesPerVertex;
    double durationMs;
} SurfaceMapperStats;
/** @brief Clasifica y fija atributos de reposo; llamar sólo al crear/remallar.
 * @param mesh Malla en reposo que recibe los atributos.
 * @param graph Anatomía en el mismo espacio; NULL permite dominio sin regiones.
 * @param mapping Etiquetas y escala; NULL usa dominio unitario. */
void SurfaceMapper_MapMesh(Mesh* mesh,const AnatomyGraph* graph,const SurfaceMapping* mapping);
/** @brief Variante instrumentada de SurfaceMapper_MapMesh. */
void SurfaceMapper_MapMeshWithStats(Mesh* mesh,const AnatomyGraph* graph,
    const SurfaceMapping* mapping,SurfaceMapperStats* stats);
/** @brief Mapea una muestra de reposo para otros productores de geometría.
 * @param position Posición en reposo.
 * @param normal Normal geométrica en reposo.
 * @param material Tejido SDF, independiente de la cobertura.
 * @param graph Anatomía opcional.
 * @param mapping Etiquetas y escala opcionales.
 * @return Coordenada inmutable de material. */
SurfaceCoordinate SurfaceMapper_MapPoint(Vector3 position,Vector3 normal,SDFMaterial material,
    const AnatomyGraph* graph,const SurfaceMapping* mapping);
/** @brief Reutiliza la vinculación de deformación para fijar el dominio material
 * en una anatomía canónica; no realiza búsquedas por vértice. */
bool SurfaceMapper_MapBoundMesh(Mesh* mesh,const AnatomyDeformer* binding,
    const AnatomyGraph* canonical,const SurfaceMapping* mapping,SurfaceMapperStats* stats);
#endif
