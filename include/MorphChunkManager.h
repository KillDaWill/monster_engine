/**
 * @file MorphChunkManager.h
 * @brief Gestor de remallado volumétrico por bloques (chunks) locales para morphing continuo.
 * @author Monster Engine Team
 * @date 2026
 */

#ifndef MONSTER_MORPH_CHUNK_MANAGER_H
#define MONSTER_MORPH_CHUNK_MANAGER_H

#include "Mesh.h"
#include "AABB.h"
#include "SDFMesher.h"
#include "MorphSpatialCache.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @struct MorphChunk
 * @brief Chunk volumétrico discreto con su submalla local triangulada.
 */
typedef struct MorphChunk {
    int32_t cx, cy, cz;             /**< Coordenadas enteras del chunk en la grilla. */
    uint64_t key;                   /**< Clave hash única de las coordenadas. */
    AABB3D bounds;                  /**< Bounding box métrico en coordenadas continuas. */
    Mesh mesh;                      /**< Sub-malla triangular generada localmente. */
    bool isDirty;                   /**< Flag de chunk pendiente de remallado. */
    bool hasSurface;                /**< Indica si el chunk contiene triángulos o corta la isosuperficie. */
    uint64_t generation;            /**< Versión/generación temporal de la última actualización. */
    uint32_t dirtinessFlags;        /**< Banderas de suciedad (MorphDirtinessFlags). */
} MorphChunk;

/**
 * @struct MorphChunkManager
 * @brief Gestor desacoplado de colección de chunks volumétricos con soporte de ensamble sin costuras.
 */
typedef struct MorphChunkManager {
    float chunkSize;                /**< Dimensión del lado del chunk métrico. */
    float invChunkSize;             /**< 1.0f / chunkSize. */
    float voxelSize;                /**< Tamaño de celda de Marching Cubes para remallado. */
    MorphChunk* chunks;             /**< Arreglo denso de chunks activos. */
    size_t chunkCount;              /**< Cantidad de chunks creados. */
    size_t chunkCapacity;           /**< Capacidad reservada de chunks. */
    uint32_t* hashTable;            /**< Tabla hash abierta para acceso rápido por clave. */
    size_t hashCapacity;            /**< Capacidad de la tabla hash. */
    size_t dirtyCount;              /**< Cantidad de chunks actualmente sucios. */
    size_t surfaceCount;            /**< Cantidad de chunks con geometría superficial activa. */
} MorphChunkManager;

/**
 * @brief Crea una nueva instancia del gestor de chunks.
 * @param chunkSize Tamaño de cada bloque métrico (ej. 0.5f).
 * @param voxelSize Tamaño de vóxel para el mesher (ej. 0.05f).
 * @return Puntero al nuevo gestor o NULL si falla la asignación.
 */
MorphChunkManager* MorphChunkManager_Create(float chunkSize, float voxelSize);

/**
 * @brief Destruye el gestor de chunks y libera todas las submallas asociadas.
 * @param mgr Puntero al gestor a destruir.
 */
void MorphChunkManager_Destroy(MorphChunkManager* mgr);

/**
 * @brief Vacía todos los chunks registrados y reinicia las estadísticas.
 * @param mgr Puntero al gestor.
 */
void MorphChunkManager_Clear(MorphChunkManager* mgr);

/**
 * @brief Obtiene o crea un chunk en las coordenadas discretas especificadas.
 * @param mgr Puntero al gestor.
 * @param cx Coordenada X discreta de la grilla de chunks.
 * @param cy Coordenada Y discreta.
 * @param cz Coordenada Z discreta.
 * @param createIfMissing Si es true, instancia el chunk si no existía.
 * @return Puntero al chunk o NULL si no existe y createIfMissing es false.
 */
MorphChunk* MorphChunkManager_GetChunk(MorphChunkManager* mgr, int32_t cx, int32_t cy, int32_t cz, bool createIfMissing);

/**
 * @brief Busca un chunk existente sin crearlo.
 */
const MorphChunk* MorphChunkManager_FindChunk(const MorphChunkManager* mgr, int32_t cx, int32_t cy, int32_t cz);

/**
 * @brief Marca como sucios todos los chunks que intersecan una región AABB.
 * @param mgr Puntero al gestor.
 * @param aabb Bounding box en espacio continuo.
 * @param dirtinessFlags Banderas de suciedad a acumular.
 * @return Cantidad de chunks marcados como sucios.
 */
size_t MorphChunkManager_MarkDirtyAABB(MorphChunkManager* mgr, AABB3D aabb, uint32_t dirtinessFlags);

/**
 * @brief Sincroniza el estado de suciedad de los chunks a partir de un MorphSpatialCache.
 * Marca únicamente los chunks que encierran ladrillos espaciales sucios.
 * @param mgr Puntero al gestor de chunks.
 * @param spatialCache Puntero al caché de bloques SDF.
 * @return Cantidad de chunks marcados como sucios.
 */
size_t MorphChunkManager_SynchronizeWithSpatialCache(MorphChunkManager* mgr, const MorphSpatialCache* spatialCache);

struct SDFSamplingPool;

/**
 * @brief Remalla en paralelo los chunks sucios utilizando un pool de muestreo.
 * @param mgr Puntero al gestor.
 * @param baseConfig Configuración base del mesher para los chunks.
 * @param pool Pool persistente de hilos para ejecución concurrente (si es NULL o mono-hilo, ejecuta secuencial).
 * @param field Campo SDF a muestrear.
 * @param generation Generación asignada a los chunks actualizados.
 * @return Cantidad de chunks remallados con éxito.
 */
size_t MorphChunkManager_UpdateDirtyParallel(
    MorphChunkManager* mgr,
    SDFMesherConfig baseConfig,
    struct SDFSamplingPool* pool,
    const SDFField* field,
    uint64_t generation
);

/**
 * @brief Remalla localmente y de forma incremental únicamente los chunks marcados como sucios.
 * Si el mesher cuenta con un SDFSamplingPool asociado, delega automáticamente a MorphChunkManager_UpdateDirtyParallel.
 * @param mgr Puntero al gestor.
 * @param mesher Instancia reutilizable de SDFMesher.
 * @param field Campo SDF a muestrear.
 * @param generation Marca de versión temporal asignada a los chunks actualizados.
 * @return Cantidad de chunks remallados con éxito.
 */
size_t MorphChunkManager_UpdateDirty(MorphChunkManager* mgr, SDFMesher* mesher, const SDFField* field, uint64_t generation);

/** @brief Detecta chunks publicados cuya superficie ya no coincide con el campo actual.
 * Muestrea vértices de cada chunk y promueve errores severos a suciedad topológica.
 */
size_t MorphChunkManager_MarkResidualDirty(MorphChunkManager* mgr,
    const SDFField* field,float tolerance);

/**
 * @brief Ensambla todas las submallas de los chunks activos en una única malla combinada.
 * @param mgr Puntero al gestor.
 * @param outMesh Malla receptora.
 * @param weldSeams Si es true, une (welds) vértices coincidentes en las costuras entre chunks contiguos.
 * @return true si el ensamble se completó con éxito.
 */
bool MorphChunkManager_AssembleMesh(const MorphChunkManager* mgr, Mesh* outMesh, bool weldSeams);

/**
 * @brief Duplica la colección y estado de chunks hacia otro gestor.
 * @param dest Gestor destino receptor.
 * @param src Gestor origen a duplicar.
 * @param copyMeshes Si es true, copia también los arreglos de vértices e índices de cada chunk.
 *                   Si es false, copia únicamente las envolventes AABB y banderas de estado.
 * @return true si la copia se completó con éxito.
 */
bool MorphChunkManager_Copy(MorphChunkManager* dest, const MorphChunkManager* src, bool copyMeshes);

/**
 * @brief Obtiene la cantidad total de chunks registrados.
 */
size_t MorphChunkManager_GetTotalChunkCount(const MorphChunkManager* mgr);

/**
 * @brief Obtiene la cantidad de chunks actualmente marcados como sucios.
 */
size_t MorphChunkManager_GetDirtyChunkCount(const MorphChunkManager* mgr);

/**
 * @brief Obtiene la cantidad de chunks que contienen geometría activa.
 */
size_t MorphChunkManager_GetSurfaceChunkCount(const MorphChunkManager* mgr);

/**
 * @brief Conteo total acumulado de vértices entre todos los chunks.
 */
size_t MorphChunkManager_GetTotalVertexCount(const MorphChunkManager* mgr);

/**
 * @brief Conteo total acumulado de triángulos entre todos los chunks.
 */
size_t MorphChunkManager_GetTotalTriangleCount(const MorphChunkManager* mgr);

#ifdef __cplusplus
}
#endif

#endif /* MONSTER_MORPH_CHUNK_MANAGER_H */
