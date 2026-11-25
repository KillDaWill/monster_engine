/**
 * @file WorldBiome.c
 * @brief Cálculo determinista de humedad, fertilidad, distancia al agua y pesos de material.
 */
#include "WorldBiome.h"
#include "TerrainGenerator.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static inline float clamp01(float x) {
    return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
}

void WorldBiome_Free(WorldBiomeField *bf) {
    if (!bf) return;
    free(bf->roughness);
    free(bf->waterDistance);
    free(bf->moisture);
    free(bf->fertility);
    free(bf->materialRGBA);
    free(bf->biomeId);
    memset(bf, 0, sizeof(*bf));
}

bool WorldBiome_Generate(WorldBiomeField *bf, const TerrainHeightfield *tf,
                         const WorldHydrology *hydro, const WorldRecipe *recipe,
                         uint32_t biomeSeed) {
    if (!bf || !tf || !tf->heights || !recipe) return false;
    memset(bf, 0, sizeof(*bf));

    uint32_t res = tf->resolution;
    size_t count = (size_t)res * (size_t)res;
    bf->resolution = res;
    bf->roughness = calloc(count, sizeof(float));
    bf->waterDistance = calloc(count, sizeof(float));
    bf->moisture = calloc(count, sizeof(float));
    bf->fertility = calloc(count, sizeof(float));
    bf->materialRGBA = calloc(count * 4u, sizeof(uint8_t));
    bf->biomeId = calloc(count, sizeof(uint8_t));
    if (!bf->roughness || !bf->waterDistance || !bf->moisture ||
        !bf->fertility || !bf->materialRGBA || !bf->biomeId) {
        WorldBiome_Free(bf);
        return false;
    }
    bf->memoryBytes = count * (sizeof(float) * 4u + 5u);

    /* 1) Transformada de distancia Chamfer en dos pasadas (O(N)) desde celdas húmedas. */
    const float bigDist = tf->span * 1.5f;
    float sOrtho = tf->spacing;
    float sDiag = tf->spacing * 1.41421356f;
    bool hasWater = (hydro && hydro->enabled && hydro->waterBodyId && hydro->totalWetCells > 0u);
    for (size_t i = 0; i < count; ++i) {
        bf->waterDistance[i] = (hasWater && hydro->waterBodyId[i] != 0u) ? 0.0f : bigDist;
    }
    if (hasWater) {
        /* Pasada hacia adelante */
        for (uint32_t iz = 0; iz < res; ++iz) {
            size_t row = (size_t)iz * res;
            for (uint32_t ix = 0; ix < res; ++ix) {
                float d = bf->waterDistance[row + ix];
                if (ix > 0u) d = fminf(d, bf->waterDistance[row + ix - 1u] + sOrtho);
                if (iz > 0u) {
                    size_t prow = row - res;
                    d = fminf(d, bf->waterDistance[prow + ix] + sOrtho);
                    if (ix > 0u) d = fminf(d, bf->waterDistance[prow + ix - 1u] + sDiag);
                    if (ix + 1u < res) d = fminf(d, bf->waterDistance[prow + ix + 1u] + sDiag);
                }
                bf->waterDistance[row + ix] = d;
            }
        }
        /* Pasada hacia atrás */
        for (uint32_t iz = res; iz > 0u; --iz) {
            size_t row = (size_t)(iz - 1u) * res;
            for (uint32_t ix = res; ix > 0u; --ix) {
                uint32_t cx = ix - 1u;
                float d = bf->waterDistance[row + cx];
                if (cx + 1u < res) d = fminf(d, bf->waterDistance[row + cx + 1u] + sOrtho);
                if (iz < res) {
                    size_t nrow = row + res;
                    d = fminf(d, bf->waterDistance[nrow + cx] + sOrtho);
                    if (cx > 0u) d = fminf(d, bf->waterDistance[nrow + cx - 1u] + sDiag);
                    if (cx + 1u < res) d = fminf(d, bf->waterDistance[nrow + cx + 1u] + sDiag);
                }
                bf->waterDistance[row + cx] = d;
            }
        }
    }

    /* 2) Rugosidad local, humedad, fertilidad y pesos continuos de material. */
    float altRange = fmaxf(1.0f, tf->maxHeight - tf->minHeight);
    float moistureDecay = fmaxf(14.0f, tf->span * 0.14f);

    for (uint32_t iz = 0; iz < res; ++iz) {
        float wz = tf->originZ + (float)iz * tf->spacing;
        size_t row = (size_t)iz * res;
        for (uint32_t ix = 0; ix < res; ++ix) {
            size_t idx = row + ix;
            float wx = tf->originX + (float)ix * tf->spacing;
            float h = tf->heights[idx];
            float altNorm = tf->isFlat ? 0.25f : clamp01((h - tf->minHeight) / altRange);

            /* Rugosidad local = desviación respecto a vecinos inmediatos. */
            float rough = 0.0f;
            if (!tf->isFlat && ix > 0u && iz > 0u && ix + 1u < res && iz + 1u < res) {
                float avg = 0.25f * (tf->heights[idx - 1u] + tf->heights[idx + 1u] +
                                     tf->heights[idx - res] + tf->heights[idx + res]);
                rough = clamp01(fabsf(h - avg) / fmaxf(0.15f, tf->spacing * 0.45f));
            }
            bf->roughness[idx] = rough;

            float slope = tf->slopeTan[idx];
            float slopeAngle = atanf(slope);
            float wDist = bf->waterDistance[idx];

            float microNoise = 0.5f + 0.5f * TerrainNoise_Fbm2D(wx * 0.03f, wz * 0.03f, biomeSeed, 3u, 0.5f, 2.0f);
            float waterProx = hasWater ? expf(-wDist / moistureDecay) : 0.35f;
            float valleyBonus = (1.0f - altNorm) * 0.30f;
            float moisture = clamp01(0.18f + 0.62f * waterProx + valleyBonus +
                                     0.16f * (microNoise - 0.5f) + recipe->vegetation.moistureBias);
            bf->moisture[idx] = moisture;

            /* Fertilidad: favorece pendientes suaves, humedad moderada-alta y altitud baja-media. */
            float slopeFactor = clamp01(1.0f - (slopeAngle - 0.15f) / 0.52f);
            float altFactor = clamp01(1.15f - altNorm * 0.95f);
            float wetPenalty = (hydro && hydro->waterDepth && hydro->waterDepth[idx] > 0.12f) ? 0.0f : 1.0f;
            float fertility = clamp01((0.15f + 0.85f * moisture) * slopeFactor * altFactor * wetPenalty *
                                      (0.65f + 0.35f * recipe->vegetation.density) +
                                      recipe->vegetation.fertilityBias);
            if (wetPenalty <= 0.0f) fertility = 0.0f;
            bf->fertility[idx] = fertility;

            /* Pesos continuos de 4 materiales (sin cortes bruscos). */
            float wRock = clamp01((slopeAngle - 0.36f) / 0.32f) + clamp01((altNorm - 0.72f) / 0.28f) * 0.65f;
            wRock = clamp01(wRock);

            float nearShore = hasWater ? clamp01(1.0f - wDist / 3.8f) : 0.0f;
            float isWet = (hydro && hydro->waterDepth && hydro->waterDepth[idx] > 0.01f) ? 1.0f : 0.0f;
            float wMud = clamp01(fmaxf(isWet * 0.92f, nearShore * (0.45f + 0.50f * moisture))) * (1.0f - 0.65f * wRock);

            float rem = fmaxf(0.0f, 1.0f - wRock - wMud);
            float grassPref = clamp01(fertility * 1.15f + (microNoise - 0.5f) * 0.18f);
            float wGrass = rem * grassPref;
            float wDry = rem * (1.0f - grassPref);

            float sum = wGrass + wDry + wRock + wMud;
            if (sum < 1e-6f) { wGrass = 1.0f; wDry = wRock = wMud = 0.0f; sum = 1.0f; }
            wGrass /= sum; wDry /= sum; wRock /= sum; wMud /= sum;

            uint8_t *m = &bf->materialRGBA[idx * 4u];
            int q0 = (int)lrintf(wGrass * 255.0f);
            int q1 = (int)lrintf(wDry * 255.0f);
            int q2 = (int)lrintf(wRock * 255.0f);
            int q3 = 255 - (q0 + q1 + q2);
            if (q3 < 0) { q0 += q3; q3 = 0; if (q0 < 0) { q1 += q0; q0 = 0; } }
            m[0] = (uint8_t)q0;
            m[1] = (uint8_t)q1;
            m[2] = (uint8_t)q2;
            m[3] = (uint8_t)q3;

            WorldBiomeType bt = BIOME_GRASSLAND;
            if (wRock >= 0.45f || altNorm > 0.75f) bt = BIOME_ROCKY_HIGHLAND;
            else if (wMud >= 0.35f || (hasWater && wDist < 5.0f && moisture > 0.65f)) bt = BIOME_WETLAND_SHORE;
            else if (moisture < 0.36f || fertility < 0.28f) bt = BIOME_DRYLAND;
            bf->biomeId[idx] = (uint8_t)bt;
        }
    }
    return true;
}

bool WorldBiome_Sample(const WorldBiomeField *bf, const TerrainHeightfield *tf,
                       float x, float z, WorldBiomeSample *out) {
    if (!bf || !tf || !out || !Terrain_InBounds(tf, x, z)) return false;
    memset(out, 0, sizeof(*out));
    float fx = (x - tf->originX) * tf->invSpacing;
    float fz = (z - tf->originZ) * tf->invSpacing;
    uint32_t res = tf->resolution;
    uint32_t maxCell = res - 2u;
    int ix = (int)floorf(fx);
    int iz = (int)floorf(fz);
    if (ix < 0) ix = 0; else if ((uint32_t)ix > maxCell) ix = (int)maxCell;
    if (iz < 0) iz = 0; else if ((uint32_t)iz > maxCell) iz = (int)maxCell;
    float u = fmaxf(0.0f, fminf(1.0f, fx - (float)ix));
    float v = fmaxf(0.0f, fminf(1.0f, fz - (float)iz));

    size_t i00 = (size_t)iz * res + (size_t)ix;
    size_t i10 = i00 + 1u;
    size_t i01 = i00 + res;
    size_t i11 = i01 + 1u;
    float w00 = (1.0f - u) * (1.0f - v);
    float w10 = u * (1.0f - v);
    float w01 = (1.0f - u) * v;
    float w11 = u * v;

    out->roughness = w00 * bf->roughness[i00] + w10 * bf->roughness[i10] +
                     w01 * bf->roughness[i01] + w11 * bf->roughness[i11];
    out->waterDistance = w00 * bf->waterDistance[i00] + w10 * bf->waterDistance[i10] +
                         w01 * bf->waterDistance[i01] + w11 * bf->waterDistance[i11];
    out->moisture = w00 * bf->moisture[i00] + w10 * bf->moisture[i10] +
                    w01 * bf->moisture[i01] + w11 * bf->moisture[i11];
    out->fertility = w00 * bf->fertility[i00] + w10 * bf->fertility[i10] +
                     w01 * bf->fertility[i01] + w11 * bf->fertility[i11];

    const uint8_t *m00 = &bf->materialRGBA[i00 * 4u];
    const uint8_t *m10 = &bf->materialRGBA[i10 * 4u];
    const uint8_t *m01 = &bf->materialRGBA[i01 * 4u];
    const uint8_t *m11 = &bf->materialRGBA[i11 * 4u];
    const float inv255 = 1.0f / 255.0f;
    out->matGrass   = (w00 * m00[0] + w10 * m10[0] + w01 * m01[0] + w11 * m11[0]) * inv255;
    out->matDrySoil = (w00 * m00[1] + w10 * m10[1] + w01 * m01[1] + w11 * m11[1]) * inv255;
    out->matRock    = (w00 * m00[2] + w10 * m10[2] + w01 * m01[2] + w11 * m11[2]) * inv255;
    out->matWetMud  = (w00 * m00[3] + w10 * m10[3] + w01 * m01[3] + w11 * m11[3]) * inv255;

    float h = 0.0f;
    Terrain_SampleHeight(tf, x, z, &h);
    float altRange = fmaxf(1.0f, tf->maxHeight - tf->minHeight);
    out->altitudeNorm = tf->isFlat ? 0.25f : clamp01((h - tf->minHeight) / altRange);
    Terrain_SampleSlopeAngle(tf, x, z, &out->slopeRadians);

    uint32_t nix = (uint32_t)ix + (u >= 0.5f ? 1u : 0u);
    uint32_t niz = (uint32_t)iz + (v >= 0.5f ? 1u : 0u);
    out->biome = (WorldBiomeType)bf->biomeId[(size_t)niz * res + nix];
    return true;
}
