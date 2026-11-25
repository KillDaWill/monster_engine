/**
 * @file WorldBiome.h
 * @brief Campos derivados de bioma, humedad, fertilidad, rugosidad, distancia al agua
 *        y pesos continuos de material del terreno (hierba, suelo seco, roca, arena/barro).
 */
#ifndef MONSTER_WORLD_BIOME_H
#define MONSTER_WORLD_BIOME_H

#include "TerrainHeightfield.h"
#include "WorldHydrology.h"
#include "WorldIO.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum WorldBiomeType {
    BIOME_GRASSLAND      = 0,
    BIOME_WETLAND_SHORE  = 1,
    BIOME_DRYLAND        = 2,
    BIOME_ROCKY_HIGHLAND = 3
} WorldBiomeType;

typedef struct WorldBiomeSample {
    WorldBiomeType biome;
    float altitudeNorm;
    float slopeRadians;
    float roughness;
    float waterDistance;
    float moisture;
    float fertility;
    float matGrass;
    float matDrySoil;
    float matRock;
    float matWetMud;
} WorldBiomeSample;

typedef struct WorldBiomeField {
    uint32_t resolution;
    float *roughness;       /**< Variación local de altura [0, 1]. */
    float *waterDistance;   /**< Distancia horizontal aproximada (Chamfer EDT) al agua en u.m. */
    float *moisture;        /**< Proxy estático de humedad [0, 1]. */
    float *fertility;       /**< Proxy estático de fertilidad/idoneidad vegetal [0, 1]. */
    uint8_t *materialRGBA;  /**< 4 pesos continuos cuantizados (grass, drySoil, rock, wetMud) sumando 255. */
    uint8_t *biomeId;       /**< WorldBiomeType dominante por celda. */
    size_t memoryBytes;
} WorldBiomeField;

/** @brief Calcula todos los campos derivados a partir del terreno e hidrología definitivos. */
bool WorldBiome_Generate(WorldBiomeField *bf, const TerrainHeightfield *tf,
                         const WorldHydrology *hydro, const WorldRecipe *recipe,
                         uint32_t biomeSeed);

/** @brief Libera buffers propiedad del campo de biomas. */
void WorldBiome_Free(WorldBiomeField *bf);

/** @brief Muestrea humedad, fertilidad, distancia al agua y pesos de material en (x, z). */
bool WorldBiome_Sample(const WorldBiomeField *bf, const TerrainHeightfield *tf,
                       float x, float z, WorldBiomeSample *out);

#ifdef __cplusplus
}
#endif

#endif /* MONSTER_WORLD_BIOME_H */
