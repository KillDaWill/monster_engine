/** @file GpuSDFSurfaceCache.h
 * @brief Plan de rejilla estable para extracción SDF GPU; no contiene dependencias GL.
 */
#ifndef MONSTER_GPU_SDF_SURFACE_CACHE_H
#define MONSTER_GPU_SDF_SURFACE_CACHE_H
#include "AABB.h"
#include "MorphSpatialCache.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
/** @brief Retícula de objeto inmutable durante un ciclo entre extremos. */
typedef struct GpuSDFSurfaceGrid {
    Vector3 origin;
    float voxelSize;
    uint32_t cells[3];
    size_t sampleCount,cellCount;
    unsigned refinementSteps;
} GpuSDFSurfaceGrid;
/** @brief Define la retícula una vez a partir de una envolvente certificada.
 * @return false ante datos inválidos o índices que excedan 32 bits.
 */
bool GpuSDFSurfaceGrid_Configure(GpuSDFSurfaceGrid* grid,AABB3D bounds,float step,unsigned refinement);
/** @brief Comprueba que un campo actual cabe en la retícula, sin moverla. */
bool GpuSDFSurfaceGrid_Contains(const GpuSDFSurfaceGrid* grid,AABB3D bounds);
/** @brief Plan denso de almacenamiento reutilizable por franjas; procesa todo el volumen. */
typedef struct GpuSDFSurfacePlan {
    uint32_t slabDepth,slabCount;
    size_t maxSamples,maxCells,updatedSamples;
} GpuSDFSurfacePlan;
/** @brief Acota buffers sin mover la retícula ni reducir el detalle geométrico.
 * @param grid Retícula global ya configurada.
 * @param sampleLimit Capacidad de esquinas del buffer temporal.
 * @param cellLimit Capacidad de casos del buffer temporal.
 * @param requestedDepth Máximo solicitado de planos de celdas por franja.
 * @param plan Plan resultante; se conserva ante un fallo.
 * @return false si no cabe ni una franja o los índices no son válidos.
 */
bool GpuSDFSurfaceGrid_PlanDense(const GpuSDFSurfaceGrid* grid,size_t sampleLimit,
    size_t cellLimit,unsigned requestedDepth,GpuSDFSurfacePlan* plan);
/** @brief Telemetría diferida de etapas GPU, sin descargar geometría. */
typedef struct GpuSDFSurfaceStats {
    double certificationCPUms;size_t certifiedEmptyBricks;
    double uploadCPUms,samplingGPUms,classificationGPUms,emissionGPUms,renderGPUms,totalGPUms;
    size_t samples,cells,gpuBytes;
    size_t sampleRequests; /**< Esquinas solicitadas por bricks antes de compartir valores globales. */
    size_t candidateBricks,dirtyBricks,newBricks,activeBricks,residentSampleBricks,reusedSamples,publishedIndices,uniqueEdges,updatedEdges,exactNormals;
    unsigned slabs;
    uint64_t generation,timingGeneration;
} GpuSDFSurfaceStats;
/** @brief Orden CPU de metadatos; no contiene muestras ni vértices CPU. */
typedef struct GpuSDFBrickJob {uint32_t x,y,z,slot;} GpuSDFBrickJob;
/** @brief Control persistente de residencia GPU sobre MorphSpatialCache. */
typedef struct GpuSDFSurfaceCache {
    GpuSDFSurfaceGrid grid;
    unsigned brickCells;
    bool cachedNormals;
    MorphSpatialCache* spatial;
    MonsterSDFSurfaceFeature* features;size_t featureCount,featureCapacity;
    MonsterSDFSurfaceFeature* featureScratch;size_t featureScratchCapacity;
    GpuSDFBrickJob* dirty;size_t dirtyCount,dirtyCapacity;
    uint32_t* retired;size_t retiredCount,retiredCapacity;
    uint32_t* freeSlots;size_t freeCount,freeCapacity;
    uint32_t nextSlot;
    size_t residentCount,newCount,reusedSamples;
    uint64_t generation,submittedGeneration,completedGeneration;
} GpuSDFSurfaceCache;
/** @brief Crea control puro C; bricks uniformes de 8 o 16 celdas.
 * @param grid Rejilla estable. @param brickCells Tamaño configurable.
 * @return Caché o NULL ante un plan inválido.
 */
GpuSDFSurfaceCache* GpuSDFSurfaceCache_Create(const GpuSDFSurfaceGrid* grid,unsigned brickCells);
/** @brief Libera metadatos. @param cache Control del campo. */
void GpuSDFSurfaceCache_Destroy(GpuSDFSurfaceCache* cache);
/** @brief Siembra features actuales y cambios barridos, sin evaluar SDF CPU.
 * @param cache Control. @param sdf Descripción actual.
 * @return false ante error; no se debe presentar geometría antigua en ese caso.
 */
bool GpuSDFSurfaceCache_Update(GpuSDFSurfaceCache* cache,const MonsterSDF* sdf);
/** @brief Registra trabajo enviado, todavía sin certificar su finalización.
 * @param cache Control. @param generation Versión enviada.
 */
void GpuSDFSurfaceCache_Submit(GpuSDFSurfaceCache* cache,uint64_t generation);
/** @brief Certifica una generación tras comprobar finalización y ausencia de overflow.
 * @param cache Control. @param generation Versión completada; no limpia solicitudes posteriores.
 */
void GpuSDFSurfaceCache_Commit(GpuSDFSurfaceCache* cache,uint64_t generation);
#endif
