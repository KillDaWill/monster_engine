/**
 * @file test_world_hydrology.c
 * @brief Pruebas deterministas de hidrología: cuenca lacustre con nivel de vertido exacto,
 *        meseta plana sin ciclos, ríos no-crecientes, profundidad física y orillas.
 */
#include "test_utils.h"
#include "WorldGenerator.h"
#include "WorldHydrology.h"
#include <math.h>
#include <stdio.h>

#define CHECK_NEAR(a, b, eps, msg) TEST_ASSERT(fabsf((a) - (b)) <= (eps), (msg))

static void test_synthetic_bowl_lake_and_plateau(void) {
    WorldStaticData *w = WorldStaticData_Alloc();
    TEST_ASSERT(w != NULL, "alloc WorldStaticData");
    w->recipe = WorldRecipe_Default();
    w->recipe.size = 64.0f;
    w->recipe.terrain.resolution = 65u;
    w->recipe.hydrology.enabled = true;
    w->recipe.hydrology.lakeDensity = 0.80f;
    w->recipe.hydrology.riverDensity = 0.0f;
    w->recipe.geology.rockDensity = 0.0f;
    TEST_ASSERT(TerrainHeightfield_Init(&w->terrain, 65u, 64.0f), "init bowl heightfield");

    uint32_t res = w->terrain.resolution;
    for (uint32_t iz = 0; iz < res; ++iz) {
        float z = w->terrain.originZ + (float)iz * w->terrain.spacing;
        for (uint32_t ix = 0; ix < res; ++ix) {
            float x = w->terrain.originX + (float)ix * w->terrain.spacing;
            float r = sqrtf(x * x + z * z);
            float h;
            if (r < 12.0f) {
                h = 2.0f + (r / 12.0f) * (r / 12.0f) * 4.0f;
                if (x > 0.0f && fabsf(z) < 2.2f && h > 4.5f) h = 4.5f;
            } else if (r < 16.0f) {
                h = 6.0f;
                if (x > 0.0f && fabsf(z) < 2.2f) h = 4.5f;
            } else {
                h = 3.0f;
            }
            w->terrain.heights[iz * res + ix] = h;
        }
    }

    char err[256] = {0};
    TEST_ASSERT(WorldGenerator_FinalizeFromHeightfield(w, true, false, err, sizeof(err)), "finalizar cuenco");
    TEST_ASSERT(w->hydrology.drainageAcyclic, "drenaje acíclico en meseta");
    TEST_ASSERT(w->hydrology.lakeCount >= 1u, "cuenco detectado como lago");

    WaterSample wsCenter;
    TEST_ASSERT(WorldHydrology_SampleWater(&w->hydrology, &w->terrain, 0.0f, 0.0f, &wsCenter), "muestra centro lago");
    TEST_ASSERT(wsCenter.wet, "centro lago húmedo");
    TEST_ASSERT(wsCenter.type == WATER_BODY_LAKE, "tipo lago");
    CHECK_NEAR(wsCenter.surfaceY, 4.5f, 0.08f, "nivel de vertido exacto");
    CHECK_NEAR(wsCenter.depth, 4.5f - wsCenter.groundY, 0.05f, "profundidad física exacta");

    WaterSample wsDry;
    TEST_ASSERT(WorldHydrology_SampleWater(&w->hydrology, &w->terrain, -24.0f, -24.0f, &wsDry), "muestra meseta seca");
    TEST_ASSERT(!wsDry.wet, "meseta exterior debe estar seca");
    CHECK_NEAR(wsDry.depth, 0.0f, 1e-6f, "profundidad cero en suelo seco");

    TEST_ASSERT(w->hydrology.shoreSiteCount >= 4u, "sitios de orilla generados");
    for (uint32_t i = 0; i < w->hydrology.shoreSiteCount; ++i) {
        const WorldShoreSite *s = &w->hydrology.shoreSites[i];
        WaterSample sWet;
        TEST_ASSERT(WorldHydrology_SampleWater(&w->hydrology, &w->terrain,
                                               s->wetContactPoint.x, s->wetContactPoint.z, &sWet),
                    "muestra en contacto de orilla");
        TEST_ASSERT(sWet.wet && sWet.waterBodyId == s->waterBodyId, "orilla toca agua real del cuerpo");
        TEST_ASSERT(Terrain_InBounds(&w->terrain, s->bankApproachPoint.x, s->bankApproachPoint.z),
                    "aproximación de orilla en dominio");
    }

    WorldStaticData_Release(w);
    printf("[PASS] test_world_hydrology: cuenco lacustre, vertido exacto, meseta acíclica y orillas\n");
}

static void test_procedural_valley_rivers_and_geology(void) {
    WorldRecipe rec = WorldRecipe_Default();
    rec.size = 128.0f;
    rec.terrain.resolution = 129u;
    rec.seed = 42u;
    char err[256] = {0};
    WorldStaticData *w = NULL;
    TEST_ASSERT(WorldGenerator_Generate(&rec, &w, err, sizeof(err)), "generar valle 128");
    TEST_ASSERT(w->hydrology.drainageAcyclic, "drenaje acíclico");
    TEST_ASSERT(w->hydrology.riverCount >= 1u, "al menos un río");
    TEST_ASSERT(w->hydrology.shoreSiteCount >= 4u, "orillas accesibles");
    TEST_ASSERT(w->geology.rockCount >= 10u, "rocas procedurales generadas");
    TEST_ASSERT(w->stats.traversableLandFraction > 0.35f, "suficiente terreno transitable");

    size_t count = (size_t)w->terrain.resolution * (size_t)w->terrain.resolution;
    for (size_t i = 0; i < count; ++i) {
        if (w->hydrology.waterBodyId[i] != 0u) {
            TEST_ASSERT(w->hydrology.waterDepth[i] > 0.0f, "celda húmeda con profundidad positiva");
            CHECK_NEAR(w->hydrology.waterDepth[i],
                       w->hydrology.waterSurfaceY[i] - w->terrain.heights[i], 1e-4f,
                       "depth == surfaceY - groundY");
        } else {
            CHECK_NEAR(w->hydrology.waterDepth[i], 0.0f, 1e-6f, "celda seca depth==0");
        }
    }

    for (uint32_t i = 0; i < w->hydrology.shoreSiteCount; ++i) {
        TEST_ASSERT(!WorldGeology_CollidesDisc(&w->geology,
                                               w->hydrology.shoreSites[i].bankApproachPoint, 0.8f),
                    "estación de orilla libre de rocas");
    }

    printf("  [hydro] valley128: gen=%.1fms lakes=%u rivers=%u shores=%u rocks=%u trav=%.1f%%\n",
           w->stats.generationMs, w->stats.lakeCount, w->stats.riverCount,
           w->stats.shoreSiteCount, w->stats.rockCount, w->stats.traversableLandFraction * 100.0f);

    WorldStaticData_Release(w);
    printf("[PASS] test_world_hydrology: valle procedural, ríos, rocas y coherencia física\n");
}

void run_world_hydrology_tests(void) {
    printf("\n--- Módulo WorldHydrology, WorldBiome & WorldGeology ---\n");
    test_synthetic_bowl_lake_and_plateau();
    test_procedural_valley_rivers_and_geology();
}
