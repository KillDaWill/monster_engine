/**
 * @file WorldGenerator.c
 * @brief Orquestación determinista del pipeline de generación de mundos estáticos.
 */
#include "WorldGenerator.h"
#include "TerrainGenerator.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void setErr(char *err, size_t errSize, const char *fmt, ...) {
    if (!err || errSize == 0) return;
    va_list args;
    va_start(args, fmt);
    vsnprintf(err, errSize, fmt, args);
    va_end(args);
}

static double nowMs(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec * 1e-6;
}

static void fnv1aUpdate(uint64_t *h, const void *data, size_t n) {
    const unsigned char *p = (const unsigned char *)data;
    for (size_t i = 0; i < n; ++i) {
        *h ^= (uint64_t)p[i];
        *h *= UINT64_C(1099511628211);
    }
}

static bool rockBlockedCallback(const void *ctx, Vector3 pos, float radius) {
    const WorldGeology *geo = (const WorldGeology *)ctx;
    return WorldGeology_CollidesDisc(geo, pos, radius);
}

static bool buildNavBaseGrid(WorldStaticData *w) {
    const TerrainHeightfield *tf = &w->terrain;
    WorldNavBaseGrid *nb = &w->navBase;

    /* Preservar resolución física ~0.85..1.0 u.m. incluso en mundos de 256 y 512 u.m. */
    uint32_t dim = (uint32_t)ceilf(tf->span / 1.0f);
    if (dim < 48u) dim = 48u;
    if (dim > 512u) dim = 512u;
    size_t cells = (size_t)dim * (size_t)dim;

    nb->dim = dim;
    nb->halfSize = tf->halfSize;
    nb->cellSize = tf->span / (float)dim;
    nb->invCellSize = 1.0f / nb->cellSize;
    nb->cellHeight = calloc(cells, sizeof(float));
    nb->cellMaxSlope = calloc(cells, sizeof(float));
    nb->cellMaxWaterDepth = calloc(cells, sizeof(float));
    nb->cellRoughness = calloc(cells, sizeof(float));
    nb->cellRockClearance = calloc(cells, sizeof(float));
    if (!nb->cellHeight || !nb->cellMaxSlope || !nb->cellMaxWaterDepth ||
        !nb->cellRoughness || !nb->cellRockClearance) {
        return false;
    }
    nb->memoryBytes = cells * sizeof(float) * 5u;

    uint32_t travCount = 0u;
    float halfCell = nb->cellSize * 0.45f;
    for (uint32_t iz = 0; iz < dim; ++iz) {
        float cz = -nb->halfSize + ((float)iz + 0.5f) * nb->cellSize;
        for (uint32_t ix = 0; ix < dim; ++ix) {
            float cx = -nb->halfSize + ((float)ix + 0.5f) * nb->cellSize;
            size_t idx = (size_t)iz * dim + ix;

            SurfaceHit ch = {0};
            Terrain_SampleGround(tf, cx, cz, &ch);
            nb->cellHeight[idx] = ch.position.y;

            /* Pendiente y profundidad máximas conservadoras en centro + 4 subpuntos de la celda. */
            float maxSlope = acosf(fmaxf(0.0f, fminf(1.0f, ch.normal.y)));
            float maxDepth = 0.0f;
            WaterSample ws = {0};
            if (WorldHydrology_SampleWater(&w->hydrology, tf, cx, cz, &ws) && ws.wet) {
                maxDepth = ws.depth;
            }
            static const float ox[4] = {-1.0f, 1.0f, -1.0f, 1.0f};
            static const float oz[4] = {-1.0f, -1.0f, 1.0f, 1.0f};
            for (int k = 0; k < 4; ++k) {
                float sx = cx + ox[k] * halfCell;
                float sz = cz + oz[k] * halfCell;
                float sl = 0.0f;
                if (Terrain_SampleSlopeAngle(tf, sx, sz, &sl) && sl > maxSlope) maxSlope = sl;
                if (WorldHydrology_SampleWater(&w->hydrology, tf, sx, sz, &ws) && ws.wet && ws.depth > maxDepth) {
                    maxDepth = ws.depth;
                }
            }
            nb->cellMaxSlope[idx] = maxSlope;
            nb->cellMaxWaterDepth[idx] = maxDepth;

            WorldBiomeSample bs = {0};
            if (WorldBiome_Sample(&w->biome, tf, cx, cz, &bs)) {
                nb->cellRoughness[idx] = bs.roughness;
            }

            float wallClr = fminf(nb->halfSize - fabsf(cx), nb->halfSize - fabsf(cz));
            float rockClr = WorldGeology_SignedClearanceXZ(&w->geology, cx, cz, wallClr);
            nb->cellRockClearance[idx] = fminf(wallClr, rockClr);

            if (maxSlope <= 0.65f && maxDepth <= 0.35f && nb->cellRockClearance[idx] >= 0.85f) {
                ++travCount;
            }
        }
    }
    w->stats.traversableLandFraction = (float)travCount / (float)cells;
    return true;
}

bool WorldGenerator_FinalizeFromHeightfield(WorldStaticData *w, bool runHydrology,
                                            bool runGeology, char *error, size_t errorSize) {
    if (!w || !w->terrain.heights) {
        setErr(error, errorSize, "WorldStaticData sin campo de alturas inicializado");
        return false;
    }
    double t0 = nowMs();
    if (!TerrainHeightfield_RebuildDerived(&w->terrain)) {
        setErr(error, errorSize, "Fallo al calcular normales/cotas del terreno");
        return false;
    }

    double tHydro = nowMs();
    if (runHydrology) {
        if (!WorldHydrology_Generate(&w->hydrology, &w->terrain, &w->recipe, w->hydrologySeed)) {
            setErr(error, errorSize, "Fallo al generar hidrología");
            return false;
        }
    } else {
        WorldHydrology_Free(&w->hydrology);
        w->hydrology.resolution = w->terrain.resolution;
        w->hydrology.enabled = false;
        w->hydrology.drainageAcyclic = true;
    }
    w->stats.hydrologyMs = nowMs() - tHydro;

    double tBiome = nowMs();
    if (!WorldBiome_Generate(&w->biome, &w->terrain, &w->hydrology, &w->recipe, w->biomeSeed)) {
        setErr(error, errorSize, "Fallo al generar biomas/humedad/fertilidad");
        return false;
    }
    w->stats.biomeMs = nowMs() - tBiome;

    double tGeo = nowMs();
    if (runGeology) {
        if (!WorldGeology_Generate(&w->geology, &w->terrain, &w->hydrology, &w->biome,
                                   &w->recipe, w->geologySeed)) {
            setErr(error, errorSize, "Fallo al generar geología procedural");
            return false;
        }
        /* Revalidar estaciones de orilla contra las rocas colocadas. */
        if (w->hydrology.enabled) {
            WorldHydrology_RebuildShoreSites(&w->hydrology, &w->terrain, &w->recipe,
                                             rockBlockedCallback, &w->geology);
        }
    }
    w->stats.geologyMs = nowMs() - tGeo;

    double tNav = nowMs();
    if (!buildNavBaseGrid(w)) {
        setErr(error, errorSize, "Fallo al construir rejilla estática de navegación");
        return false;
    }
    w->stats.navBaseMs = nowMs() - tNav;

    /* Calcular huella canónica completa del mundo congelado. */
    uint64_t fp = UINT64_C(14695981039346656037);
    uint64_t tfFp = TerrainHeightfield_Fingerprint(&w->terrain);
    uint64_t hyFp = WorldHydrology_Fingerprint(&w->hydrology);
    uint64_t geFp = WorldGeology_Fingerprint(&w->geology);
    fnv1aUpdate(&fp, &w->generatorVersion, sizeof(w->generatorVersion));
    fnv1aUpdate(&fp, &w->recipeHash, sizeof(w->recipeHash));
    fnv1aUpdate(&fp, &tfFp, sizeof(tfFp));
    fnv1aUpdate(&fp, &hyFp, sizeof(hyFp));
    fnv1aUpdate(&fp, &geFp, sizeof(geFp));
    w->dataFingerprint = fp;
    w->isFlat = w->recipe.flat || w->terrain.isFlat;

    w->stats.lakeCount = w->hydrology.lakeCount;
    w->stats.riverCount = w->hydrology.riverCount;
    w->stats.shoreSiteCount = w->hydrology.shoreSiteCount;
    w->stats.rockCount = w->geology.rockCount;
    w->stats.collidingRockCount = w->geology.collidingRockCount;
    w->stats.wetAreaFraction = w->hydrology.wetAreaFraction;
    w->stats.drainageAcyclic = w->hydrology.drainageAcyclic;
    w->stats.hasAccessibleWater = (w->hydrology.shoreSiteCount > 0u);
    w->stats.totalStaticBytes = sizeof(WorldStaticData) + w->terrain.memoryBytes +
                                w->hydrology.memoryBytes + w->biome.memoryBytes +
                                w->geology.memoryBytes + w->navBase.memoryBytes;
    w->stats.generationMs += (nowMs() - t0);
    w->frozen = true;
    return true;
}

bool WorldGenerator_Generate(const WorldRecipe *recipe, WorldStaticData **outWorld,
                             char *error, size_t errorSize) {
    if (!recipe || !outWorld) {
        setErr(error, errorSize, "Argumentos inválidos en WorldGenerator_Generate");
        return false;
    }
    *outWorld = NULL;
    WorldRecipe validated = *recipe;
    if (!WorldRecipe_Validate(&validated, error, errorSize)) return false;
    validated.recipeHash = WorldRecipe_Fingerprint(&validated);

    double tStart = nowMs();
    WorldStaticData *w = WorldStaticData_Alloc();
    if (!w) {
        setErr(error, errorSize, "Sin memoria para WorldStaticData");
        return false;
    }

    w->recipe = validated;
    w->recipeHash = validated.recipeHash;
    w->generatorVersion = validated.generatorVersion;
    w->masterSeed = validated.seed;
    w->terrainSeed = WorldSeed_Derive(validated.seed, WORLD_SEED_TERRAIN, 0u);
    w->hydrologySeed = WorldSeed_Derive(validated.seed, WORLD_SEED_HYDROLOGY, 0u);
    w->geologySeed = WorldSeed_Derive(validated.seed, WORLD_SEED_GEOLOGY, 0u);
    w->biomeSeed = WorldSeed_Derive(validated.seed, WORLD_SEED_BIOME, 0u);

    if (!TerrainHeightfield_Init(&w->terrain, validated.terrain.resolution, validated.size)) {
        WorldStaticData_Release(w);
        setErr(error, errorSize, "No se pudo reservar campo de alturas (%ux%u)",
               validated.terrain.resolution, validated.terrain.resolution);
        return false;
    }

    double tRelief = nowMs();
    if (!TerrainGenerator_BuildBaseRelief(&w->terrain, &validated, w->terrainSeed,
                                          &w->stats.erosionMaterialMoved)) {
        WorldStaticData_Release(w);
        setErr(error, errorSize, "Error generando relieve base/erosión térmica");
        return false;
    }
    w->stats.terrainMs = nowMs() - tRelief;

    bool runHydro = (!validated.flat && validated.hydrology.enabled);
    bool runGeo = (!validated.flat && validated.geology.rockDensity > 1e-4f);
    if (!WorldGenerator_FinalizeFromHeightfield(w, runHydro, runGeo, error, errorSize)) {
        WorldStaticData_Release(w);
        return false;
    }

    w->stats.generationMs = nowMs() - tStart;
    *outWorld = w;
    return true;
}
