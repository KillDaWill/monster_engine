/**
 * @file TerrainGenerator.h
 * @brief Generador procedural determinista de relieve (ruido simplex 2D, fBm, crestas,
 *        distorsión de dominio, perfilado de cuencas y erosión térmica acotada).
 */
#ifndef MONSTER_TERRAIN_GENERATOR_H
#define MONSTER_TERRAIN_GENERATOR_H

#include "TerrainHeightfield.h"
#include "WorldIO.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Ruido coherente 2D determinista estilo Simplex en [-1, 1] independiente de rand(). */
float TerrainNoise_Simplex2D(float x, float z, uint32_t seed);

/** @brief Ruido fractal fBm acotado aproximadamente en [-1, 1]. */
float TerrainNoise_Fbm2D(float x, float z, uint32_t seed, uint32_t octaves,
                         float persistence, float lacunarity);

/** @brief Ruido multifractal en crestas (ridged) acotado en [0, 1]. */
float TerrainNoise_Ridged2D(float x, float z, uint32_t seed, uint32_t octaves,
                            float persistence, float lacunarity);

/**
 * @brief Genera el relieve base y aplica erosión térmica determinista sobre @p tf.
 *
 * Para recetas con `recipe->flat == true`, inicializa el campo plano exacto Y=0.
 * Para relieve v3, se derivan normales, pendientes y huella durante la finalización del mundo.
 * @param outErosionMoved Opcional: suma total de material vertical transferido durante la erosión.
 */
bool TerrainGenerator_BuildBaseRelief(TerrainHeightfield *tf, const WorldRecipe *recipe,
                                      uint32_t terrainSeed, double *outErosionMoved);

#ifdef __cplusplus
}
#endif

#endif /* MONSTER_TERRAIN_GENERATOR_H */
