/**
 * @file MorphSpatialCache.h
 * @brief Caché espacial persistente de campos de distancia con signo (SDF) organizado en bloques dispersos (bricks).
 * @author Monster Engine Team
 * @date 2026
 */

#ifndef MONSTER_MORPH_SPATIAL_CACHE_H
#define MONSTER_MORPH_SPATIAL_CACHE_H

#include "AABB.h"
#include "Vector.h"
#include "MonsterSDF.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Capacidad máxima de candidatos de conectores por bloque espacial. */
#define MORPH_SPATIAL_BRICK_MAX_CANDIDATES 128

/**
 * @enum MorphDirtinessFlags
 * @brief Clasificación de la naturaleza de la modificación volumétrica en un bloque.
 */
typedef enum MorphDirtinessFlags {
    MORPH_DIRT_NONE        = 0,      /**< Bloque limpio y sincronizado con el campo SDF actual. */
    MORPH_DIRT_DEFORMATION = 1 << 0, /**< Cambio de pose, grosor o escala continua (topología invariante). */
    MORPH_DIRT_TOPOLOGY    = 1 << 1  /**< Bifurcación, brote de dígitos o cambio estructural de blueprint. */
} MorphDirtinessFlags;

/**
 * @struct MorphSpatialBrick
 * @brief Bloque volumétrico cúbico individual en la grilla espacial dispersa.
 */
/** @brief Ciclo de una versión del campo GPU; el legado sólo usa isDirty. */
typedef enum MorphBrickFieldState {
    MORPH_BRICK_CLEAN,MORPH_BRICK_DIRTY,MORPH_BRICK_SCHEDULED,MORPH_BRICK_SAMPLING,
    MORPH_BRICK_MESHING,MORPH_BRICK_READY,MORPH_BRICK_RETIRED
} MorphBrickFieldState;
typedef struct MorphSpatialBrick {
    uint64_t requestedGeneration,sampleGeneration,meshGeneration,lastSeenGeneration;
    uint32_t gpuSlot;
    MorphBrickFieldState fieldState;
    bool resident;
    int32_t bx, by, bz;                              /**< Coordenadas discretas enteras de la grilla. */
    uint64_t key;                                    /**< Clave hash 64-bit única de las coordenadas. */
    AABB3D bounds;                                   /**< Caja envolvente AABB en el espacio continuo del mundo. */
    uint16_t candidates[MORPH_SPATIAL_BRICK_MAX_CANDIDATES]; /**< Índices de conectores SDF relevantes. */
    size_t candidateCount;                           /**< Cantidad de conectores candidatos compilados. */
    float centerDistance;                            /**< Distancia SDF evaluada en el centro del bloque. */
    bool hasSurface;                                 /**< Indica si el bloque interseca la isosuperficie d=0. */
    bool isDirty;                                    /**< Flag booleano de bloque pendiente de reevaluación. */
    uint32_t dirtinessFlags;                         /**< Máscara de bits de MorphDirtinessFlags. */
    float lastUpdatedAge;                            /**< Marca temporal de edad del último snapshot evaluado. */
} MorphSpatialBrick;

/**
 * @struct MorphSpatialCache
 * @brief Tabla hash dispersa de bloques espaciales persistentes con soporte de actualización incremental.
 */
typedef struct MorphSpatialCache {
    Vector3 gridOrigin;                             /**< Origen estable; cero para el legado. */
    float brickSize;                                 /**< Tamaño del lado de cada bloque en unidades métricas. */
    float invBrickSize;                              /**< 1.0f / brickSize para conversiones rápidas. */
    MorphSpatialBrick* bricks;                       /**< Arreglo contiguo denso de bloques activos. */
    size_t brickCount;                               /**< Cantidad actual de bloques activos. */
    size_t brickCapacity;                            /**< Capacidad reservada del arreglo de bloques. */
    uint32_t* hashTable;                             /**< Tabla de índices (1-based, 0 = vacío) para búsqueda por clave. */
    size_t hashCapacity;                             /**< Tamaño de la tabla hash (potencia de 2). */
    size_t dirtyCount;                               /**< Cantidad de bloques actualmente marcados como sucios. */
    size_t surfaceCount;                             /**< Cantidad de bloques que contienen superficie. */
} MorphSpatialCache;

/**
 * @brief Crea una nueva instancia del caché espacial de bloques SDF.
 * @param brickSize Dimensión del lado de cada cubo volumétrico (por ejemplo 0.25f).
 * @return Puntero a la instancia creada o NULL si falla la asignación.
 */
/** @brief Fija el origen de un caché vacío sin cambiar su paso.
 * @param cache Caché recién creado. @param origin Origen estable del campo.
 * @return false si el caché ya contiene bricks.
 */
bool MorphSpatialCache_SetGridOrigin(MorphSpatialCache* cache,Vector3 origin);
MorphSpatialCache* MorphSpatialCache_Create(float brickSize);

/**
 * @brief Libera todos los recursos asociados a la instancia del caché espacial.
 * @param cache Puntero al caché a destruir.
 */
void MorphSpatialCache_Destroy(MorphSpatialCache* cache);

/**
 * @brief Vacía todos los bloques del caché reiniciando el estado a vacío.
 * @param cache Puntero al caché.
 */
void MorphSpatialCache_Clear(MorphSpatialCache* cache);

/**
 * @brief Obtiene o crea un bloque espacial en las coordenadas discretas especificadas.
 * @param cache Puntero al caché.
 * @param bx Coordenada X discreta de la grilla.
 * @param by Coordenada Y discreta de la grilla.
 * @param bz Coordenada Z discreta de la grilla.
 * @param createIfMissing Si es true, añade el bloque si no existía previamente.
 * @return Puntero al bloque o NULL si no existe y createIfMissing es false.
 */
MorphSpatialBrick* MorphSpatialCache_GetBrick(MorphSpatialCache* cache, int32_t bx, int32_t by, int32_t bz, bool createIfMissing);

/**
 * @brief Busca un bloque espacial existente sin crearlo.
 * @param cache Puntero constante al caché.
 * @param bx Coordenada X discreta.
 * @param by Coordenada Y discreta.
 * @param bz Coordenada Z discreta.
 * @return Puntero al bloque o NULL si no se encuentra en el caché.
 */
const MorphSpatialBrick* MorphSpatialCache_FindBrick(const MorphSpatialCache* cache, int32_t bx, int32_t by, int32_t bz);

/**
 * @brief Marca como sucios todos los bloques del caché que intersequen una región AABB.
 * @param cache Puntero al caché.
 * @param aabb Bounding box en espacio mundial a marcar.
 * @param dirtinessFlags Banderas de suciedad (deformación o topología) a combinar.
 * @return Cantidad de bloques marcados como sucios.
 */
size_t MorphSpatialCache_MarkDirtyAABB(MorphSpatialCache* cache, AABB3D aabb, uint32_t dirtinessFlags);

/**
 * @brief Marca como sucia la envolvente extendida (swept AABB) entre dos configuraciones de un módulo anatómico.
 * @param cache Puntero al caché.
 * @param before Bounds del módulo antes de la transición.
 * @param after Bounds del módulo después de la transición.
 * @param padding Margen de seguridad para capturar influencia del campo y radio de mezcla.
 * @param isTopology True si el módulo sufrió un cambio de conectividad/topología, false si es deformación.
 * @return Cantidad de bloques marcados.
 */
size_t MorphSpatialCache_MarkSweptModuleDirty(MorphSpatialCache* cache, AABB3D before, AABB3D after, float padding, bool isTopology);

/**
 * @brief Reevalúa y actualiza de forma incremental únicamente los bloques marcados como sucios contra un campo SDF.
 * @param cache Puntero al caché.
 * @param sdf Campo de distancias de la criatura para el snapshot actual.
 * @param age Edad de crecimiento/metamorfosis del snapshot.
 * @return Cantidad de bloques recalculados.
 */
size_t MorphSpatialCache_UpdateDirty(MorphSpatialCache* cache, const MonsterSDF* sdf, float age);

/**
 * @brief Consulta si una región espacial dada contiene bloques sucios o afectados por cambios topológicos.
 * @param cache Puntero constante al caché.
 * @param aabb Bounding box a consultar.
 * @param outDirtinessFlags Puntero opcional donde recibir la unión de banderas de suciedad encontradas.
 * @return True si la región interseca al menos un bloque sucio.
 */
bool MorphSpatialCache_IsRegionDirty(const MorphSpatialCache* cache, AABB3D aabb, uint32_t* outDirtinessFlags);

/**
 * @brief Obtiene una lista de punteros a todos los bloques marcados como sucios.
 * @param cache Puntero constante al caché.
 * @param outBricks Arreglo de salida para recibir los punteros a los bloques.
 * @param maxBricks Capacidad máxima del arreglo outBricks.
 * @return Cantidad de bloques sucios rellenados en outBricks.
 */
size_t MorphSpatialCache_GetDirtyBricks(const MorphSpatialCache* cache, const MorphSpatialBrick** outBricks, size_t maxBricks);

/**
 * @brief Obtiene la cantidad total de bloques registrados en el caché.
 */
size_t MorphSpatialCache_GetTotalBrickCount(const MorphSpatialCache* cache);

/**
 * @brief Obtiene la cantidad de bloques actualmente marcados como sucios.
 */
size_t MorphSpatialCache_GetDirtyBrickCount(const MorphSpatialCache* cache);

/**
 * @brief Obtiene la cantidad de bloques que intersecan la isosuperficie.
 */
size_t MorphSpatialCache_GetSurfaceBrickCount(const MorphSpatialCache* cache);

#ifdef __cplusplus
}
#endif

#endif /* MONSTER_MORPH_SPATIAL_CACHE_H */
