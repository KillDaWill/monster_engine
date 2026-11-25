/**
 * @file WorldGeology.h
 * @brief Geología procedural determinista: catálogo de variantes SDF, colocación acelerada
 *        por celdas, proxies conservadores de colisión e índice espacial estático de rocas.
 */
#ifndef MONSTER_WORLD_GEOLOGY_H
#define MONSTER_WORLD_GEOLOGY_H

#include "TerrainHeightfield.h"
#include "WorldHydrology.h"
#include "WorldBiome.h"
#include "WorldIO.h"
#include "Quaternion.h"
#include "AABB.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WORLD_MAX_ROCK_VARIANTS 32u
#define WORLD_MAX_ROCK_INSTANCES 2048u
#define ROCK_MAX_FRACTURE_PLANES 6u
#define ROCK_MAX_COMPOUND_LOBES 3u

typedef enum WorldRockFamily {
    ROCK_FAMILY_BOULDER   = 0, /**< Canto rodado/bloque redondeado. */
    ROCK_FAMILY_FRACTURED = 1, /**< Roca angular con planos de fractura. */
    ROCK_FAMILY_BEDROCK   = 2, /**< Afloramiento rocoso incrustado en ladera. */
    ROCK_FAMILY_CLUSTER   = 3  /**< Conglomerado compuesto de varios lóbulos. */
} WorldRockFamily;

/** @brief Especificación inmutable de una variante del catálogo geológico (espacio local normalizado). */
typedef struct WorldRockVariant {
    uint32_t variantId;
    uint32_t seed;
    WorldRockFamily family;
    Vector3 baseRadii;                         /**< Semiejes base (aprox. 0.6..1.2 en local). */
    uint32_t planeCount;
    Vector3 planeNormals[ROCK_MAX_FRACTURE_PLANES];
    float planeOffsets[ROCK_MAX_FRACTURE_PLANES];
    uint32_t lobeCount;
    Vector3 lobeCenters[ROCK_MAX_COMPOUND_LOBES];
    float lobeRadii[ROCK_MAX_COMPOUND_LOBES];
    float noiseAmp;                            /**< Desplazamiento superficial acotado [0.02, 0.16]. */
    float noiseFreq;
    float conservativeRadiusXZ;                /**< Radio conservador local XZ que envuelve el SDF. */
    float conservativeMinY;
    float conservativeMaxY;
} WorldRockVariant;

/** @brief Sub-esfera/cilindro conservador de colisión en coordenadas mundiales. */
typedef struct RockCollisionLobe {
    Vector3 center;
    float radiusXZ;
    float halfHeight;
} RockCollisionLobe;

/** @brief Instancia inmutable de roca colocada en el mundo estático. */
typedef struct WorldRockInstance {
    uint32_t rockId;
    uint32_t variantIndex;
    WorldRockFamily family;
    Vector3 position;          /**< Posición mundial del centro local (ya parcialmente enterrada). */
    Quaternion orientation;    /**< Orientación 3D completa. */
    Vector3 scale;             /**< Escala no uniforme acotada. */
    float burialDepth;
    float tint[3];             /**< Tinte mineral sutil [0.65..1.15]. */
    bool collides;             /**< false sólo para guijarros decorativos diminutos. */

    /* Proxies conservadores en espacio mundial (generados también en headless). */
    float boundRadiusXZ;
    float minY, maxY;
    uint8_t lobeCount;
    RockCollisionLobe lobes[ROCK_MAX_COMPOUND_LOBES];
} WorldRockInstance;

/** @brief Índice espacial estático de rejilla para consultas O(1) de rocas colisionables. */
typedef struct WorldRockGrid {
    uint32_t dim;
    float originX, originZ;
    float cellSize, invCellSize;
    uint32_t *cellOffsets;     /**< dim*dim + 1 desplazamientos CSR. */
    uint32_t *cellIndices;     /**< Índices a rocks[] por celda. */
    uint32_t totalEntries;
} WorldRockGrid;

typedef struct WorldGeology {
    WorldRockVariant *variants;
    uint32_t variantCount;
    WorldRockInstance *rocks;
    uint32_t rockCount;
    uint32_t collidingRockCount;
    WorldRockGrid grid;
    size_t memoryBytes;
} WorldGeology;

/** @brief Evalúa el campo de distancia conservador de una variante rocosa en espacio local. */
float WorldGeology_EvaluateVariantSDF(const WorldRockVariant *v, Vector3 localP);

/** @brief Genera el catálogo de variantes, coloca rocas deterministas y construye el índice espacial. */
bool WorldGeology_Generate(WorldGeology *geo, const TerrainHeightfield *tf,
                           const WorldHydrology *hydro, const WorldBiomeField *biome,
                           const WorldRecipe *recipe, uint32_t geologySeed);

/** @brief Libera catálogo, instancias e índice espacial de rocas. */
void WorldGeology_Free(WorldGeology *geo);

/** @brief Comprueba si un disco horizontal en (pos, radius) colisiona con alguna roca estática. */
bool WorldGeology_CollidesDisc(const WorldGeology *geo, Vector3 pos, float radius);

/** @brief Comprueba si un segmento barrido horizontalmente con radio @p radius toca alguna roca. */
bool WorldGeology_CollidesSegmentXZ(const WorldGeology *geo, Vector3 a, Vector3 b, float radius);

/** @brief Comprueba si un segmento 3D de visión/contacto [a, b] es ocluido por una roca. */
bool WorldGeology_OccludesSegment3D(const WorldGeology *geo, Vector3 a, Vector3 b, float radius);

/** @brief Distancia mínima firmada en XZ al proxy rocoso colisionable más cercano (capado en maxDist). */
float WorldGeology_SignedClearanceXZ(const WorldGeology *geo, float x, float z, float maxDist);

/** @brief Huella canónica cuantizada de variantes y descriptores de colisión de rocas. */
uint64_t WorldGeology_Fingerprint(const WorldGeology *geo);

#ifdef __cplusplus
}
#endif

#endif /* MONSTER_WORLD_GEOLOGY_H */
