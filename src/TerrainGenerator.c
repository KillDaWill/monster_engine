/**
 * @file TerrainGenerator.c
 * @brief Ruido coherente simplex 2D determinista, modelado de valles/cuencas y erosión térmica.
 */
#include "TerrainGenerator.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static inline uint32_t hash2D(int32_t ix, int32_t iz, uint32_t seed) {
    uint32_t h = (uint32_t)ix * 374761393u + (uint32_t)iz * 668265263u + seed * 1442695041u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

static inline float gradDot2D(int32_t ix, int32_t iz, float dx, float dz, uint32_t seed) {
    /* 16 gradientes unitarios simétricos en el círculo para isotropía sin artefactos axiales. */
    static const float gx[16] = {
         1.0000000f,  0.9238795f,  0.7071068f,  0.3826834f,
         0.0000000f, -0.3826834f, -0.7071068f, -0.9238795f,
        -1.0000000f, -0.9238795f, -0.7071068f, -0.3826834f,
         0.0000000f,  0.3826834f,  0.7071068f,  0.9238795f
    };
    static const float gz[16] = {
         0.0000000f,  0.3826834f,  0.7071068f,  0.9238795f,
         1.0000000f,  0.9238795f,  0.7071068f,  0.3826834f,
         0.0000000f, -0.3826834f, -0.7071068f, -0.9238795f,
        -1.0000000f, -0.9238795f, -0.7071068f, -0.3826834f
    };
    uint32_t idx = hash2D(ix, iz, seed) & 15u;
    return gx[idx] * dx + gz[idx] * dz;
}

float TerrainNoise_Simplex2D(float x, float z, uint32_t seed) {
    const float F2 = 0.3660254037844386f; /* (sqrt(3) - 1) / 2 */
    const float G2 = 0.2113248654051871f; /* (3 - sqrt(3)) / 6 */

    float s = (x + z) * F2;
    int32_t i = (int32_t)floorf(x + s);
    int32_t j = (int32_t)floorf(z + s);
    float t = (float)(i + j) * G2;
    float x0 = x - ((float)i - t);
    float z0 = z - ((float)j - t);

    int32_t i1, j1;
    if (x0 > z0) { i1 = 1; j1 = 0; }
    else         { i1 = 0; j1 = 1; }

    float x1 = x0 - (float)i1 + G2;
    float z1 = z0 - (float)j1 + G2;
    float x2 = x0 - 1.0f + 2.0f * G2;
    float z2 = z0 - 1.0f + 2.0f * G2;

    float n0 = 0.0f, n1 = 0.0f, n2 = 0.0f;
    float t0 = 0.5f - x0 * x0 - z0 * z0;
    if (t0 > 0.0f) {
        t0 *= t0;
        n0 = t0 * t0 * gradDot2D(i, j, x0, z0, seed);
    }
    float t1 = 0.5f - x1 * x1 - z1 * z1;
    if (t1 > 0.0f) {
        t1 *= t1;
        n1 = t1 * t1 * gradDot2D(i + i1, j + j1, x1, z1, seed);
    }
    float t2 = 0.5f - x2 * x2 - z2 * z2;
    if (t2 > 0.0f) {
        t2 *= t2;
        n2 = t2 * t2 * gradDot2D(i + 1, j + 1, x2, z2, seed);
    }

    float v = 70.0f * (n0 + n1 + n2);
    if (v > 1.0f) v = 1.0f;
    if (v < -1.0f) v = -1.0f;
    return v;
}

float TerrainNoise_Fbm2D(float x, float z, uint32_t seed, uint32_t octaves,
                         float persistence, float lacunarity) {
    float sum = 0.0f;
    float amp = 1.0f;
    float norm = 0.0f;
    float freq = 1.0f;
    for (uint32_t o = 0; o < octaves; ++o) {
        uint32_t oSeed = WorldSeed_Derive(seed, WORLD_SEED_TERRAIN, o + 1u);
        sum += amp * TerrainNoise_Simplex2D(x * freq, z * freq, oSeed);
        norm += amp;
        amp *= persistence;
        freq *= lacunarity;
    }
    return norm > 1e-6f ? (sum / norm) : 0.0f;
}

float TerrainNoise_Ridged2D(float x, float z, uint32_t seed, uint32_t octaves,
                            float persistence, float lacunarity) {
    float sum = 0.0f;
    float amp = 1.0f;
    float norm = 0.0f;
    float freq = 1.0f;
    float weight = 1.0f;
    for (uint32_t o = 0; o < octaves; ++o) {
        uint32_t oSeed = WorldSeed_Derive(seed, WORLD_SEED_TERRAIN, 100u + o);
        float n = 1.0f - fabsf(TerrainNoise_Simplex2D(x * freq, z * freq, oSeed));
        n = n * n * weight;
        weight = fmaxf(0.15f, fminf(1.0f, n * 1.8f));
        sum += amp * n;
        norm += amp;
        amp *= persistence;
        freq *= lacunarity;
    }
    return norm > 1e-6f ? (sum / norm) : 0.0f;
}

static bool applyThermalErosion(TerrainHeightfield *tf, const WorldErosionParams *er,
                                double *outMoved) {
    if (outMoved) *outMoved = 0.0;
    if (!er || er->thermalIterations == 0u || tf->isFlat) return true;

    uint32_t res = tf->resolution;
    size_t count = (size_t)res * (size_t)res;
    float *delta = malloc(count * sizeof(float));
    if (!delta) return false;

    const float degToRad = 3.14159265358979323846f / 180.0f;
    float talusSlope = tanf(er->talusAngleDegrees * degToRad);
    float talusOrtho = talusSlope * tf->spacing;
    float talusDiag = talusSlope * tf->spacing * 1.41421356f;
    float rate = er->thermalRate;

    static const int dx[8] = {-1, 1, 0, 0, -1, 1, -1, 1};
    static const int dz[8] = {0, 0, -1, 1, -1, -1, 1, 1};
    const float talusD[8] = {
        talusOrtho, talusOrtho, talusOrtho, talusOrtho,
        talusDiag, talusDiag, talusDiag, talusDiag
    };

    double totalMoved = 0.0;
    /* Erosión térmica con doble buffer determinista: cada celda redistribuye exceso
     * sobre el ángulo de talud hacia vecinos más bajos; en el borde abierto el exceso
     * puede salir del dominio de manera acotada sin generar NaN ni alturas negativas. */
    for (uint32_t iter = 0; iter < er->thermalIterations; ++iter) {
        memset(delta, 0, count * sizeof(float));
        for (uint32_t iz = 1u; iz + 1u < res; ++iz) {
            size_t row = (size_t)iz * res;
            for (uint32_t ix = 1u; ix + 1u < res; ++ix) {
                size_t idx = row + ix;
                float h = tf->heights[idx];
                float excess[8];
                float sumExcess = 0.0f;
                float maxExcess = 0.0f;
                for (int k = 0; k < 8; ++k) {
                    size_t ni = (size_t)((int)iz + dz[k]) * res + (size_t)((int)ix + dx[k]);
                    float diff = h - tf->heights[ni] - talusD[k];
                    if (diff > 0.0f) {
                        excess[k] = diff;
                        sumExcess += diff;
                        if (diff > maxExcess) maxExcess = diff;
                    } else {
                        excess[k] = 0.0f;
                    }
                }
                if (sumExcess > 1e-6f) {
                    float move = rate * maxExcess * 0.5f;
                    if (move > h) move = h;
                    delta[idx] -= move;
                    totalMoved += (double)move;
                    float invSum = move / sumExcess;
                    for (int k = 0; k < 8; ++k) {
                        if (excess[k] > 0.0f) {
                            size_t ni = (size_t)((int)iz + dz[k]) * res + (size_t)((int)ix + dx[k]);
                            delta[ni] += excess[k] * invSum;
                        }
                    }
                }
            }
        }
        for (size_t i = 0; i < count; ++i) {
            float nh = tf->heights[i] + delta[i];
            tf->heights[i] = (nh > 0.0f) ? nh : 0.0f;
        }
    }

    free(delta);
    if (outMoved) *outMoved = totalMoved;
    return true;
}

bool TerrainGenerator_BuildBaseRelief(TerrainHeightfield *tf, const WorldRecipe *recipe,
                                      uint32_t terrainSeed, double *outErosionMoved) {
    if (!tf || !tf->heights || !recipe) return false;
    if (outErosionMoved) *outErosionMoved = 0.0;

    uint32_t res = tf->resolution;
    size_t count = (size_t)res * (size_t)res;
    if (recipe->flat || recipe->terrain.heightScale <= 1e-5f) {
        memset(tf->heights, 0, count * sizeof(float));
        return TerrainHeightfield_RebuildDerived(tf);
    }

    uint32_t warpSeedX = WorldSeed_Derive(terrainSeed, WORLD_SEED_TERRAIN, 201u);
    uint32_t warpSeedZ = WorldSeed_Derive(terrainSeed, WORLD_SEED_TERRAIN, 202u);
    uint32_t macroSeed = WorldSeed_Derive(terrainSeed, WORLD_SEED_TERRAIN, 203u);
    uint32_t basinSeed = WorldSeed_Derive(terrainSeed, WORLD_SEED_TERRAIN, 204u);

    /* Ángulo de inclinación regional determinista para favorecer drenaje hacia un borde de salida. */
    float tiltAngle = ((float)(WorldSeed_Derive(terrainSeed, WORLD_SEED_TERRAIN, 205u) & 0xFFFFu) / 65535.0f)
                      * 6.28318530718f;
    float tiltDirX = cosf(tiltAngle);
    float tiltDirZ = sinf(tiltAngle);

    float freq = recipe->terrain.baseFrequency;
    float warpAmp = recipe->terrain.domainWarpStrength * (0.45f / fmaxf(0.002f, freq));
    float heightScale = recipe->terrain.heightScale;
    float ridgeW = recipe->terrain.ridgeWeight;
    float valleyW = recipe->terrain.valleyShaping;
    float invHalf = 1.0f / fmaxf(1.0f, tf->halfSize);

    /* 1) Evaluar relieve macro + fBm + crestas + perfilado de valle/cuenca. */
    float minVal = 1e30f;
    for (uint32_t iz = 0; iz < res; ++iz) {
        float wz = tf->originZ + (float)iz * tf->spacing;
        float nz = wz * invHalf;
        size_t row = (size_t)iz * res;
        for (uint32_t ix = 0; ix < res; ++ix) {
            float wx = tf->originX + (float)ix * tf->spacing;
            float nx = wx * invHalf;

            float sx = wx;
            float sz = wz;
            if (warpAmp > 1e-4f) {
                float dx = TerrainNoise_Fbm2D(wx * freq * 0.65f, wz * freq * 0.65f, warpSeedX, 2u, 0.5f, 2.0f);
                float dz = TerrainNoise_Fbm2D(wx * freq * 0.65f + 13.7f, wz * freq * 0.65f - 9.3f, warpSeedZ, 2u, 0.5f, 2.0f);
                sx += dx * warpAmp;
                sz += dz * warpAmp;
            }

            float fbm = TerrainNoise_Fbm2D(sx * freq, sz * freq, terrainSeed,
                                           recipe->terrain.octaves,
                                           recipe->terrain.persistence,
                                           recipe->terrain.lacunarity);
            float base01 = 0.5f + 0.5f * fbm;

            float ridged = 0.0f;
            if (ridgeW > 1e-4f) {
                ridged = TerrainNoise_Ridged2D(sx * freq * 0.85f, sz * freq * 0.85f,
                                               macroSeed,
                                               recipe->terrain.octaves > 4u ? 4u : recipe->terrain.octaves,
                                               0.52f, 2.05f);
            }
            float combined = base01 * (1.0f - ridgeW) + ridged * ridgeW;

            /* Perfilado de valle/cuenca natural: crea tierras bajas transitables y colinas/bordes
             * con pendiente regional suave hacia una salida de drenaje abierta. */
            if (valleyW > 1e-4f) {
                /* Eje transversal al drenaje principal para esculpir un valle con laderas suaves. */
                float across = (-tiltDirZ * nx + tiltDirX * nz);
                float along = (tiltDirX * nx + tiltDirZ * nz);
                float meander = 0.25f * TerrainNoise_Simplex2D(along * 1.4f, 2.7f, basinSeed);
                float valleyDist = fabsf(across - meander);
                float valleyProfile = fminf(1.0f, valleyDist * 1.15f);
                valleyProfile = valleyProfile * valleyProfile * (3.0f - 2.0f * valleyProfile);
                /* Pendiente macro suave en la dirección de salida (0.12 a lo largo del mapa). */
                float regionalSlope = 0.14f * (0.5f - 0.5f * along);
                /* Depresión localizada suave para favorecer lagos en cuencas si lakeDensity > 0. */
                float basinWave = 0.5f + 0.5f * TerrainNoise_Simplex2D(nx * 1.35f + 4.1f, nz * 1.35f - 2.3f, basinSeed);
                float shaped = combined * (0.45f + 0.55f * valleyProfile) + 0.22f * valleyProfile + regionalSlope
                               - 0.16f * (1.0f - basinWave) * (1.0f - valleyProfile);
                combined = combined * (1.0f - valleyW) + shaped * valleyW;
            }

            /* Curva suave para mantener llanuras y valles caminables sin destruir picos altos. */
            float h = combined * heightScale;
            tf->heights[row + ix] = h;
            if (h < minVal) minVal = h;
        }
    }

    /* Normalizar base por encima de 1.0 u.m. para dejar margen limpio de excavación fluvial. */
    float shift = (minVal < 1.5f) ? (1.5f - minVal) : 0.0f;
    for (size_t i = 0; i < count; ++i) {
        tf->heights[i] += shift;
    }

    /* 2) Erosión térmica determinista acotada. */
    if (!applyThermalErosion(tf, &recipe->erosion, outErosionMoved)) {
        return false;
    }

    return TerrainHeightfield_RebuildDerived(tf);
}
