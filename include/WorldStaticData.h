/**
 * @file WorldStaticData.h
 * @brief Propiedad inmutable con conteo atómico de referencias para terrenos, hidrología,
 *        geología, biomas y campos estáticos de navegación; incluye caché compartida thread-safe.
 */
#ifndef MONSTER_WORLD_STATIC_DATA_H
#define MONSTER_WORLD_STATIC_DATA_H

#include "WorldIO.h"
#include "TerrainHeightfield.h"
#include "WorldHydrology.h"
#include "WorldBiome.h"
#include "WorldGeology.h"
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Campos estáticos precomputados para A* topográfico (inmutables tras congelación). */
typedef struct WorldNavBaseGrid {
    uint32_t dim;
    float halfSize;
    float cellSize;
    float invCellSize;
    float *cellHeight;         /**< dim * dim altura en centro de celda. */
    float *cellMaxSlope;       /**< dim * dim pendiente máxima en la celda (radianes). */
    float *cellMaxWaterDepth;  /**< dim * dim profundidad máxima de agua en la celda. */
    float *cellRoughness;      /**< dim * dim rugosidad [0, 1]. */
    float *cellRockClearance;  /**< dim * dim holgura firmada hasta roca/borde más cercano. */
    size_t memoryBytes;
} WorldNavBaseGrid;

/** @brief Diagnósticos generados una única vez al construir el mundo estático. */
typedef struct WorldGenerationStats {
    double generationMs;
    double terrainMs;
    double hydrologyMs;
    double biomeMs;
    double geologyMs;
    double navBaseMs;
    double erosionMaterialMoved;
    size_t totalStaticBytes;
    uint32_t lakeCount;
    uint32_t riverCount;
    uint32_t shoreSiteCount;
    uint32_t rockCount;
    uint32_t collidingRockCount;
    float wetAreaFraction;
    float traversableLandFraction;
    bool drainageAcyclic;
    bool hasAccessibleWater;
} WorldGenerationStats;

/**
 * @struct WorldStaticData
 * @brief Fuente única de verdad inmutable del territorio procedural.
 *
 * Nunca almacena masas mutables de recursos, criaturas, PRNGs ni estado de render OpenGL.
 * Compartida de forma segura entre EcosystemWorld, AIWorld, snapshots triple-buffer,
 * EnvironmentRenderer y trabajadores paralelos de Brain Evolution Lab mediante
 * WorldStaticData_Retain / WorldStaticData_Release.
 */
typedef struct WorldStaticData {
    _Atomic uint32_t refCount;
    uint64_t epochId;           /**< Identificador monótono de instancia para invalidar cachés GPU. */
    uint64_t recipeHash;
    uint64_t dataFingerprint;   /**< Huella canónica de receta + terreno + hidrología + geología. */
    uint32_t generatorVersion;
    uint32_t masterSeed;
    uint32_t terrainSeed;
    uint32_t hydrologySeed;
    uint32_t geologySeed;
    uint32_t biomeSeed;
    bool isFlat;
    bool frozen;

    WorldRecipe recipe;
    TerrainHeightfield terrain;
    WorldHydrology hydrology;
    WorldBiomeField biome;
    WorldGeology geology;
    WorldNavBaseGrid navBase;
    WorldGenerationStats stats;
} WorldStaticData;

/** @brief Reserva un contenedor WorldStaticData con refCount = 1. */
WorldStaticData *WorldStaticData_Alloc(void);

/** @brief Incrementa de forma atómica el conteo de referencias (seguro con NULL). */
const WorldStaticData *WorldStaticData_Retain(const WorldStaticData *world);

/** @brief Decrementa el conteo de referencias y libera al llegar a 0 (seguro con NULL). */
void WorldStaticData_Release(const WorldStaticData *world);

/** @brief Número actual de referencias activas (diagnóstico y tests de propiedad). */
uint32_t WorldStaticData_RefCount(const WorldStaticData *world);

/** @brief Construye o adquiere desde la caché global acotada (single-flight thread-safe). */
const WorldStaticData *WorldStaticCache_Acquire(const WorldRecipe *recipe,
                                                char *error, size_t errorSize);

/** @brief Vacía la caché global liberando sus referencias retenidas (no invalida referencias externas). */
void WorldStaticCache_Clear(void);

/** @brief Estadísticas de aciertos/fallos de la caché compartida de mundos estáticos. */
void WorldStaticCache_Stats(uint64_t *outHits, uint64_t *outMisses, uint32_t *outEntries);

#ifdef __cplusplus
}
#endif

#endif /* MONSTER_WORLD_STATIC_DATA_H */
