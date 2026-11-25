/** @brief Cuantifica la diferencia entre la ocupación v1 renderizada y la consulta v1. */
#include "WorldGenerator.h"
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

int main(int argc, char **argv) {
    char error[256] = {0};
    WorldRecipe recipe;
    WorldStaticData *world = NULL;
    if (argc != 2) {
        fprintf(stderr, "Uso: %s worlds/receta.world.json\n", argv[0]);
        return 2;
    }
    if (!WorldRecipe_LoadFile(argv[1], &recipe, error, sizeof(error)) ||
        !WorldGenerator_Generate(&recipe, &world, error, sizeof(error))) {
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
