/** @brief Cuantifica la diferencia entre la ocupación v1 renderizada y la consulta v1. */
#include "WorldGenerator.h"
#include "TerrainGenerator.h"
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct LegacyVertex {
    float height, depth;
    bool mapped;
} LegacyVertex;

static bool auditLegacyWater(const WorldStaticData *world) {
    const WorldHydrology *hy = &world->hydrology;
    const TerrainHeightfield *tf = &world->terrain;
    unsigned r = tf->resolution;
    LegacyVertex *gpu = calloc((size_t)r * r, sizeof(*gpu));
    if (!gpu) return false;

    unsigned falseWet = 0, falseDry = 0, mixed = 0, tested = 0;
    float maxHeightError = 0.0f;
    const float points[5][2] = {{.15f,.27f},{.73f,.2f},{.23f,.79f},{.82f,.91f},{.51f,.48f}};
    for (unsigned z = 0; z + 1u < r; ++z) {
        for (unsigned x = 0; x + 1u < r; ++x) {
            unsigned a = z * r + x;
            unsigned q[] = {a, a+1u, a+r, a+r+1u};
            unsigned wet = 0;
            uint16_t first = 0;
            bool mixedBody = false;
            float sum = 0.0f;
            for (unsigned i = 0; i < 4; ++i) {
                uint16_t body = hy->waterBodyId[q[i]];
                if (!body) continue;
                mixedBody |= first && first != body;
                first = body;
                ++wet;
                sum += hy->waterSurfaceY[q[i]];
            }
            if (!wet) continue;
            mixed += mixedBody;
            for (unsigned i = 0; i < 4; ++i) {
                unsigned id = q[i];
                if (gpu[id].mapped) continue;
                float y = hy->waterBodyId[id] ? hy->waterSurfaceY[id] : sum / (float)wet;
                gpu[id] = (LegacyVertex){y, fmaxf(0.0f, y - tf->heights[id]), true};
            }
            for (unsigned i = 0; i < 5; ++i) {
                float u = points[i][0], v = points[i][1];
                float wa = 1.0f - fmaxf(u,v), wb = fabsf(u-v), wc = fminf(u,v);
                unsigned b = u >= v ? q[1] : q[2];
                float height = wa * gpu[a].height + wb * gpu[b].height + wc * gpu[q[3]].height;
                float depth = wa * gpu[a].depth + wb * gpu[b].depth + wc * gpu[q[3]].depth;
                WaterSample water;
                WorldHydrology_SampleWater(hy, tf, tf->originX + (x+u)*tf->spacing,
                                           tf->originZ + (z+v)*tf->spacing, &water);
                /* Emula descarte y depth-test sobre malla LOD0. */
                bool rasterWet = depth > .002f && height > water.groundY;
                falseWet += rasterWet && !water.wet;
                falseDry += !rasterWet && water.wet;
                ++tested;
                if (rasterWet && water.wet)
                    maxHeightError = fmaxf(maxHeightError, fabsf(water.surfaceY - height));
            }
        }
    }
    printf("legacy_lod0 probes=%u false_wet=%u false_dry=%u mixed_quads=%u max_surface_error=%.6f\n",
           tested, falseWet, falseDry, mixed, maxHeightError);
    free(gpu);
    return true;
}

typedef struct TraceOutput {
    const char *prefix;
    bool ok;
} TraceOutput;

static void dumpTrace(void *context, const char *name, const void *samples,
                      size_t count, size_t sampleBytes) {
    TraceOutput *trace = context;
    char path[4096];
    int n = snprintf(path, sizeof(path), "%s-%s.bin", trace->prefix, name);
    if (n < 0 || (size_t)n >= sizeof(path)) { trace->ok = false; return; }
    FILE *f = fopen(path, "wb");
    if (!f) { trace->ok = false; return; }
    bool written = fwrite(samples, sampleBytes, count, f) == count;
    trace->ok = fclose(f) == 0 && trace->ok && written;
}

/* Binario local para el validador Python: cabecera y planos en orden de filas XZ. */
static bool dumpTerrain(const WorldStaticData *w, const char *path, const char *tracePrefix) {
    const TerrainHeightfield *tf = &w->terrain;
    const WorldHydrology *h = &w->hydrology;
    size_t count = (size_t)tf->resolution * tf->resolution;
    if (!h->enabled) return false;
    TerrainHeightfield base;
    if (!TerrainHeightfield_Init(&base, tf->resolution, tf->span)) return false;
    bool ok = TerrainGenerator_BuildBaseRelief(&base, &w->recipe, w->terrainSeed, NULL);
    FILE *f = ok ? fopen(path, "wb") : NULL;
    if (f) {
        const float header[] = {(float)tf->resolution, tf->spacing, tf->originX, tf->originZ};
        const float *fields[] = {base.heights, tf->heights, h->waterSurfaceY, h->waterDepth,
            h->flowX, h->flowZ, h->drainageAccum, w->biome.waterDistance};
        ok = fwrite(header, sizeof(float), 4, f) == 4;
        for (unsigned i = 0; i < sizeof(fields) / sizeof(fields[0]) && ok; ++i)
            ok = fwrite(fields[i], sizeof(float), count, f) == count;
        const uint16_t *domain = h->surfaceBodyId ? h->surfaceBodyId : h->waterBodyId;
        ok = ok && fwrite(domain, sizeof(uint16_t), count, f) == count;
        ok = ok && fwrite(h->waterBodyId, sizeof(uint16_t), count, f) == count;
        ok = fclose(f) == 0 && ok;
    }
    if (f && ok && tracePrefix) {
        TraceOutput output = {tracePrefix, true};
        WorldHydrology traced = {0};
        ok = WorldHydrology_GenerateTraced(&traced, &base, &w->recipe, w->hydrologySeed, dumpTrace, &output)
             && output.ok && memcmp(base.heights, tf->heights, count * sizeof(float)) == 0;
        WorldHydrology_Free(&traced);
    }
    TerrainHeightfield_Free(&base);
    return f && ok;
}

static uint32_t componentRoot(uint32_t *parents, uint32_t i) {
    while (parents[i] != i) {
        parents[i] = parents[parents[i]];
        i = parents[i];
    }
    return i;
}

/* Conectividad de la máscara genérica de navegación. Conserva los bloqueos
 * de roca y los mismos límites de pendiente y profundidad del grid base. */
static bool auditNavigation(const WorldStaticData *world) {
    const WorldNavBaseGrid *nav = &world->navBase;
    uint32_t count = nav->dim * nav->dim;
    uint32_t *parents = malloc(count * sizeof(*parents));
    uint32_t *sizes = calloc(count, sizeof(*sizes));
    uint8_t *hasWater = calloc(count, sizeof(*hasWater));
    if (!parents || !sizes || !hasWater) {
        free(parents); free(sizes); free(hasWater);
        return false;
    }
    for (uint32_t i = 0; i < count; ++i) {
        bool passable = nav->cellMaxSlope[i] <= .65f && nav->cellMaxWaterDepth[i] <= .35f &&
                        nav->cellRockClearance[i] >= .85f;
        parents[i] = passable ? i : UINT32_MAX;
        if (!passable) continue;
        if (i % nav->dim && parents[i - 1u] != UINT32_MAX)
            parents[i] = componentRoot(parents, i - 1u);
        if (i >= nav->dim && parents[i - nav->dim] != UINT32_MAX) {
            uint32_t a = componentRoot(parents, i);
            uint32_t b = componentRoot(parents, i - nav->dim);
            if (a != b) {
                if (a < b) parents[b] = a;
                else parents[a] = b;
            }
        }
    }
    for (uint32_t i = 0; i < count; ++i)
        if (parents[i] != UINT32_MAX) ++sizes[componentRoot(parents, i)];
    unsigned reachable = 0u;
    for (unsigned i = 0; i < world->hydrology.shoreSiteCount; ++i) {
        Vector3 p = world->hydrology.shoreSites[i].bankApproachPoint;
        unsigned x = (unsigned)((p.x + nav->halfSize) * nav->invCellSize);
        unsigned z = (unsigned)((p.z + nav->halfSize) * nav->invCellSize);
        if (x < nav->dim && z < nav->dim && parents[z * nav->dim + x] != UINT32_MAX) {
            hasWater[componentRoot(parents, z * nav->dim + x)] = 1;
            ++reachable;
        }
    }
    unsigned components = 0u, largest = 0u, cellsWithWater = 0u, usable = 0u;
    for (uint32_t i = 0; i < count; ++i) {
        if (!sizes[i]) continue;
        ++components;
        largest = sizes[i] > largest ? sizes[i] : largest;
        usable += sizes[i];
        if (hasWater[i]) cellsWithWater += sizes[i];
    }
    printf("nav_components=%u nav_largest_cells=%u nav_cells=%u nav_cells_with_shore=%u nav_shore_sites=%u\n",
           components, largest, usable, cellsWithWater, reachable);
    free(parents); free(sizes); free(hasWater);
    return true;
}

int main(int argc, char **argv) {
    char error[256] = {0};
    WorldRecipe recipe;
    WorldStaticData *world = NULL;
    if (argc < 2) {
        fprintf(stderr, "Uso: %s worlds/receta.world.json [--seed=N] [--generator-version=N] [--resolution=N] [--dump=PATH] [--trace=PREFIX]\n", argv[0]);
        return 2;
    }
    if (!WorldRecipe_LoadFile(argv[1], &recipe, error, sizeof(error))) {
        fprintf(stderr, "%s\n", error);
        return 1;
    }
    const char *dump = NULL, *trace = NULL;
    for (int i = 2; i < argc; ++i) {
        if (!strncmp(argv[i], "--seed=", 7)) recipe.seed = (uint32_t)strtoul(argv[i] + 7, NULL, 10);
        else if (!strncmp(argv[i], "--generator-version=", 20)) recipe.generatorVersion = (uint32_t)strtoul(argv[i] + 20, NULL, 10);
        else if (!strncmp(argv[i], "--resolution=", 13)) recipe.terrain.resolution = (uint32_t)strtoul(argv[i] + 13, NULL, 10);
        else if (!strncmp(argv[i], "--dump=", 7)) dump = argv[i] + 7;
        else if (!strncmp(argv[i], "--trace=", 8)) trace = argv[i] + 8;
        else return 2;
    }
    if (!WorldGenerator_Generate(&recipe, &world, error, sizeof(error))) {
        fprintf(stderr, "%s\n", error);
        return 1;
    }
    printf("%s v%u seed=%u fingerprint=%016llx lakes=%u rivers=%u banks=%u wet=%.3f%%\n",
           recipe.id, world->generatorVersion, world->masterSeed, (unsigned long long)world->dataFingerprint,
           world->hydrology.lakeCount, world->hydrology.riverCount,
           world->hydrology.shoreSiteCount, world->hydrology.wetAreaFraction * 100.0f);
    if (world->hydrology.surfaceBodyId) {
        const WorldHydrology *h = &world->hydrology;
        printf("segments=%u junctions=%u contour_length=%.3f major_junction=%.3f,%.3f,%.3f\n",
               h->riverSegmentCount, h->riverJunctionCount, h->shorelineLength,
               h->majorJunction.x, h->majorJunction.y, h->majorJunction.z);
    }
    if (dump) {
        printf("generation_ms=%.3f static_bytes=%zu hydrology_ms=%.3f traversable=%.5f\n",
               world->stats.generationMs, world->stats.totalStaticBytes, world->stats.hydrologyMs,
               world->stats.traversableLandFraction);
        bool ok = auditNavigation(world) && dumpTerrain(world, dump, trace);
        WorldStaticData_Release(world);
        return ok ? 0 : 1;
    }
    bool ok = !world->hydrology.totalWetCells || world->generatorVersion != WORLD_GENERATOR_VERSION_LEGACY ||
              auditLegacyWater(world);
    for (unsigned i = 0; i < world->hydrology.shoreSiteCount; ++i) {
        const WorldShoreSite *s = &world->hydrology.shoreSites[i];
        const WorldWaterBody *body = WorldHydrology_GetBody(&world->hydrology, s->waterBodyId);
        printf("site=%u %s/%u contact=%.3f,%.3f,%.3f bank=%.3f,%.3f,%.3f outward=%.3f,%.3f\n",
               s->siteId, body->type == WATER_BODY_RIVER ? "river" : "lake", body->id,
               s->wetContactPoint.x, s->wetContactPoint.y, s->wetContactPoint.z,
               s->bankApproachPoint.x, s->bankApproachPoint.y, s->bankApproachPoint.z,
               s->bankNormal.x, s->bankNormal.z);
    }
    WorldStaticData_Release(world);
    return ok ? 0 : 1;
}
