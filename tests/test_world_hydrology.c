/**
 * @file test_world_hydrology.c
 * @brief Pruebas deterministas de hidrología: cuenca lacustre con nivel de vertido exacto,
 *        meseta plana sin ciclos, ríos no-crecientes, profundidad física y orillas.
 */
#include "test_utils.h"
#include "TerrainGenerator.h"
#include "WorldGenerator.h"
#include "WorldHydrology.h"
#include <math.h>
#include <stdio.h>

#define CHECK_NEAR(a, b, eps, msg) TEST_ASSERT(fabsf((a) - (b)) <= (eps), (msg))

static void test_synthetic_bowl_lake_and_plateau(uint32_t version) {
    WorldStaticData *w = WorldStaticData_Alloc();
    TEST_ASSERT(w != NULL, "alloc WorldStaticData");
    w->recipe = WorldRecipe_Default();
    w->recipe.generatorVersion = version;
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

static bool sampleClippedTriangle(const WorldWaterVertex *a, const WorldWaterVertex *b,
                                  const WorldWaterVertex *c, float x, float z, float *surface) {
    double ax = a->position.x, az = a->position.z;
    double ux = (double)b->position.x - ax, uz = (double)b->position.z - az;
    double vx = (double)c->position.x - ax, vz = (double)c->position.z - az;
    double dx = (double)x - ax, dz = (double)z - az;
    double d = ux * vz - uz * vx;
    if (d >= -1e-10) return false;
    double u = (dx * vz - dz * vx) / d;
    double v = (ux * dz - uz * dx) / d;
    if (u < 0 || v < 0 || u + v > 1) return false;
    *surface = (float)((1 - u - v) * a->position.y + u * b->position.y + v * c->position.y);
    return true;
}

static void checkContinuousOccupancy(const WorldHydrology *hy, const TerrainHeightfield *tf,
                                     uint32_t x, uint32_t z, unsigned numSamples, uint32_t *random) {
    WorldWaterPatch patches[2][3];
    WorldWaterVertex polygons[2][3][WORLD_WATER_PATCH_VERTICES];
    unsigned patchCounts[2], vertices[2][3] = {{0}};
    for (unsigned t = 0; t < 2; ++t) {
        patchCounts[t] = WorldHydrology_GetWaterPatches(hy, tf, x, z, t, patches[t]);
        for (unsigned p = 0; p < patchCounts[t]; ++p) {
            WorldWaterVertex *polygon = polygons[t][p];
            vertices[t][p] = WorldHydrology_ClipWaterPatch(tf, &patches[t][p], polygon);
            const WorldWaterBody *body = WorldHydrology_GetBody(hy, patches[t][p].bodyId);
            for (unsigned k = 0; k < vertices[t][p]; ++k) {
                TEST_ASSERT(polygon[k].depth >= WORLD_WATER_DEPTH_EPSILON, "profundidad de vértice válida");
                if (body->type == WATER_BODY_LAKE)
                    CHECK_NEAR(polygon[k].position.y, body->surfaceY, 1e-5f, "plano de lago constante");
            }
        }
    }
    for (unsigned i = 0; i < numSamples; ++i) {
        *random = *random * 1664525u + 1013904223u;
        float u = (float)((*random >> 8) + 0.5) / 16777216.0f;
        *random = *random * 1664525u + 1013904223u;
        float v = (float)((*random >> 8) + 0.5) / 16777216.0f;
        /* Evitar empates (< 2e-5 unidades de rejilla) en aristas cuantizadas. */
        if (fabsf(u - v) < 2e-5f || fabsf(u - .5f) < 2e-5f || fabsf(v - .5f) < 2e-5f) continue;
        float wx = tf->originX + (x + u) * tf->spacing;
        float wz = tf->originZ + (z + v) * tf->spacing;
        unsigned t = u >= v ? 0u : 1u;
        uint16_t id = 0;
        float y = 0.0f;
        for (unsigned p = 0; p < patchCounts[t]; ++p) {
            const WorldWaterVertex *poly = polygons[t][p];
            for (unsigned k = 2; k < vertices[t][p]; ++k) {
                float hit;
                if (sampleClippedTriangle(&poly[0], &poly[k-1], &poly[k], wx, wz, &hit)) {
                    TEST_ASSERT(id == 0 || id == patches[t][p].bodyId, "cuerpos ajenos sin áreas superpuestas");
                    id = patches[t][p].bodyId;
                    y = hit;
                }
            }
        }
        WaterSample water;
        TEST_ASSERT(WorldHydrology_SampleWater(hy, tf, wx, wz, &water), "coordenada válida");
        TEST_ASSERT(water.wet == (id != 0), "ocupación de agua coincide con malla");
        if (id) {
            TEST_ASSERT(water.waterBodyId == id, "malla y consulta seleccionan el mismo cuerpo");
            CHECK_NEAR(water.surfaceY, y, 1e-4f, "cota de malla coincide con consulta");
            CHECK_NEAR(water.surfaceY - water.groundY, water.depth, 1e-4f, "profundidad firmada coherente");
        }
    }
}

static void test_continuous_water_mesh_and_version(void) {
    TerrainHeightfield tf = {0};
    TEST_ASSERT(TerrainHeightfield_Init(&tf, 17u, 16.0f), "init malla mixta");
    uint16_t owners[289] = {0};
    float flow[289] = {0};
    WorldWaterBody bodies[2] = {
        {.id = 1, .type = WATER_BODY_LAKE, .surfaceY = 1.2f},
        {.id = 2, .type = WATER_BODY_LAKE, .surfaceY = 2.7f},
    };
    uint32_t origin = 8u * 17u + 8u;
    owners[origin] = 1;
    owners[origin + 1u] = 2;
    owners[origin + 17u] = 2;
    owners[origin + 18u] = 1;
    tf.heights[origin + 1u] = 1.3f;
    tf.heights[origin + 17u] = 1.8f;
    tf.heights[origin + 18u] = 0.6f;
    TEST_ASSERT(TerrainHeightfield_RebuildDerived(&tf), "derivadas de malla mixta");
    WorldHydrology mixed = {
        .enabled = true, .waterBodyId = owners, .surfaceBodyId = owners,
        .bodies = bodies, .bodyCount = 2, .flowX = flow, .flowZ = flow, .totalWetCells = 4,
    };
    uint32_t random = 410311u;
    for (uint32_t z = 7; z < 10; ++z)
        for (uint32_t x = 7; x < 10; ++x)
            checkContinuousOccupancy(&mixed, &tf, x, z, 128u, &random);
    owners[origin + 18u] = 0;
    tf.heights[origin + 18u] = 2.1f;
    TEST_ASSERT(TerrainHeightfield_RebuildDerived(&tf), "derivadas de borde compartido");
    Vector3 diagonal[2] = {{0}};
    bool crossing[2] = {false};
    for (unsigned t = 0; t < 2; ++t) {
        WorldWaterPatch patches[3];
        unsigned n = WorldHydrology_GetWaterPatches(&mixed, &tf, 8, 8, t, patches);
        for (unsigned k = 0; k < n; ++k) {
            if (patches[k].bodyId != 1) continue;
            WorldWaterVertex v[WORLD_WATER_PATCH_VERTICES];
            unsigned count = WorldHydrology_ClipWaterPatch(&tf, &patches[k], v);
            for (unsigned j = 0; j < count; ++j) {
                if (v[j].position.x == v[j].position.z && v[j].position.x > .1f) {
                    diagonal[t] = v[j].position;
                    crossing[t] = true;
                }
            }
        }
    }
    TEST_ASSERT(crossing[0] && crossing[1] && diagonal[0].x == diagonal[1].x &&
                diagonal[0].z == diagonal[1].z && diagonal[0].y == diagonal[1].y,
                "las intersecciones de arista coinciden exactamente entre triángulos");
    uint16_t rayBody = 0;
    Vector3 hit;
    TEST_ASSERT(WorldHydrology_RaycastWater(&mixed, &tf, (Vector3){0.86f,4,0.14f},
                                            (Vector3){0.90f,0,0.16f}, &hit, &rayBody) && rayBody == 2,
                "picking oblicuo intercepta el lago propietario");
    CHECK_NEAR(hit.y, 2.7f, 1e-5f, "picking sobre lámina de lago");
    TEST_ASSERT(!WorldHydrology_RaycastWater(&mixed, &tf, (Vector3){4,4,4},
                                             (Vector3){4,-1,4}, NULL, NULL),
                "picking en tierra seca");
    TerrainHeightfield_Free(&tf);

    char err[256] = {0};
    WorldRecipe recipe;
    TEST_ASSERT(WorldRecipe_LoadFile("worlds/river_valley.world.json", &recipe, err, sizeof(err)), "receta del valle");
    recipe.generatorVersion = WORLD_GENERATOR_VERSION_CONTINUOUS;
    WorldStaticData *world = NULL, *repeat = NULL;
    TEST_ASSERT(WorldGenerator_Generate(&recipe, &world, err, sizeof(err)), "generar valle v2");
    TEST_ASSERT(WorldGenerator_Generate(&recipe, &repeat, err, sizeof(err)), "regenerar valle v2");
    TEST_ASSERT(world->generatorVersion == 2 && world->dataFingerprint == repeat->dataFingerprint,
                "determinismo de generación y huella v2");
    TEST_ASSERT(world->hydrology.riverCount > 1, "redes con salidas independientes");
    TEST_ASSERT(world->hydrology.shoreSiteCount > 100, "bancos accesibles v2");
    for (unsigned i = 0; i < 10000; ++i) {
        random = random * 1664525u + 1013904223u;
        unsigned x = (random >> 8) % (world->terrain.resolution - 1u);
        random = random * 1664525u + 1013904223u;
        unsigned z = (random >> 8) % (world->terrain.resolution - 1u);
        checkContinuousOccupancy(&world->hydrology, &world->terrain, x, z, 5, &random);
    }
    for (unsigned i = 0; i < world->hydrology.shoreSiteCount; ++i) {
        const WorldShoreSite *site = &world->hydrology.shoreSites[i];
        WaterSample contact;
        TEST_ASSERT(WorldHydrology_SampleWater(&world->hydrology, &world->terrain,
                                               site->wetContactPoint.x, site->wetContactPoint.z, &contact),
                    "contacto v2 válido");
        TEST_ASSERT(contact.wet && contact.waterBodyId == site->waterBodyId, "orilla toca el cuerpo v2");
        CHECK_NEAR(contact.surfaceY, site->wetContactPoint.y, 1e-4f, "altura de orilla v2");
    }
    printf("  [hydro v2] fp=%016llx networks=%u lakes=%u banks=%u wet=%.2f%% (%u probes + mixed bodies)\n",
           (unsigned long long)world->dataFingerprint, world->hydrology.riverCount, world->hydrology.lakeCount,
           world->hydrology.shoreSiteCount, world->hydrology.wetAreaFraction * 100.0f, 50000u);
    WorldStaticData_Release(repeat);
    WorldStaticData_Release(world);
}

static void test_v3_morphology(void) {
    char err[256] = {0};
    WorldRecipe recipe;
    TEST_ASSERT(WorldRecipe_LoadFile("worlds/lab_valley_small.world.json", &recipe, err, sizeof(err)),
                "receta del valle pequeño");
    recipe.generatorVersion = WORLD_GENERATOR_VERSION_MORPHOLOGY;
    recipe.hydrology.riverDensity = .42f;
    recipe.hydrology.lakeDensity = .40f;
    recipe.hydrology.riverWidthScale = .76f;
    recipe.hydrology.riverDepthScale = .68f;
    WorldStaticData *world = NULL, *repeat = NULL;
    TEST_ASSERT(WorldGenerator_Generate(&recipe, &world, err, sizeof(err)), "generar valle v3");
    TEST_ASSERT(WorldGenerator_Generate(&recipe, &repeat, err, sizeof(err)), "regenerar valle v3");
    const WorldHydrology *hydro = &world->hydrology;
    const TerrainHeightfield *terrain = &world->terrain;
    TEST_ASSERT(world->generatorVersion == 3 && world->dataFingerprint == repeat->dataFingerprint,
                "identidad física v3 estable");
    TEST_ASSERT(hydro->drainageAcyclic && hydro->surfaceBodyId, "drenaje acíclico y dominio firmado");
    TEST_ASSERT(hydro->lakeCount && hydro->riverCount && hydro->shoreSiteCount >= 12,
                "el valle conserva lagos, cauces y acceso");
    for (unsigned i = 0; i < terrain->resolution * terrain->resolution; ++i) {
        unsigned owner = hydro->waterBodyId[i];
        TEST_ASSERT(isfinite(terrain->heights[i]), "terreno finito");
        if (!owner) continue;
        TEST_ASSERT(owner == hydro->surfaceBodyId[i], "misma identidad en celdas húmedas");
        TEST_ASSERT(hydro->waterSurfaceY[i] > terrain->heights[i], "no hay agua bajo terreno");
        if (owner <= hydro->lakeCount) {
            CHECK_NEAR(hydro->waterSurfaceY[i], hydro->bodies[owner - 1].surfaceY,
                       1e-6f, "plano lacustre v3");
        }
    }
    uint32_t random = 317u;
    for (unsigned i = 0; i < 2000; ++i) {
        random = random * 1664525u + 1013904223u;
        unsigned x = (random >> 8) % (terrain->resolution - 1u);
        random = random * 1664525u + 1013904223u;
        unsigned z = (random >> 8) % (terrain->resolution - 1u);
        checkContinuousOccupancy(hydro, terrain, x, z, 5, &random);
    }
    WorldStaticData_Release(repeat);
    WorldStaticData_Release(world);
}

static void test_v3_river_lake_seam(void) {
    WorldRecipe recipe;
    WorldStaticData *world = NULL;
    char err[256] = {0};
    TEST_ASSERT(WorldRecipe_LoadFile("worlds/river_valley.world.json", &recipe, err, sizeof(err)),
                "receta de regresión de ribera");
    recipe.generatorVersion = WORLD_GENERATOR_VERSION_MORPHOLOGY;
    TEST_ASSERT(WorldGenerator_Generate(&recipe, &world, err, sizeof(err)), "generar encuentro río-lago");

    /* En la semilla 42 el tallado dejaba un escalón de 0,75 u entre
     * estas muestras consecutivas, aunque el relieve base sólo subía 0,10 u. */
    WaterSample lake, river;
    TEST_ASSERT(WorldHydrology_SampleWater(&world->hydrology, &world->terrain, 37.0f, 20.0f, &lake) &&
                WorldHydrology_SampleWater(&world->hydrology, &world->terrain, 37.5f, 20.0f, &river),
                "muestrear ambos lechos en el encuentro");
    TEST_ASSERT(lake.wet && river.wet && lake.type == WATER_BODY_LAKE && river.type == WATER_BODY_RIVER &&
                lake.waterBodyId != river.waterBodyId, "conservar ocupación e identidad de ambos cuerpos");
    TEST_ASSERT(fabsf(lake.groundY - river.groundY) < .24f, "no hay escalón de excavación entre los lechos");
    WorldStaticData_Release(world);
}

static void test_v4_seed42_river_valley_banks(void) {
    WorldRecipe recipe;
    WorldStaticData *world = NULL;
    char err[256] = {0};
    TEST_ASSERT(WorldRecipe_LoadFile("worlds/river_valley.world.json", &recipe, err, sizeof(err)),
                "cargar receta de orillas v4");
    TEST_ASSERT(WorldGenerator_Generate(&recipe, &world, err, sizeof(err)), "generar River Valley semilla 42");
    const WorldHydrology *hydro = &world->hydrology;
    const TerrainHeightfield *terrain = &world->terrain;
    TEST_ASSERT(world->masterSeed == 42u && hydro->drainageAcyclic && hydro->riverCount && hydro->lakeCount,
                "valle con drenaje, cauces y lagos");

    TerrainHeightfield base;
    TEST_ASSERT(TerrainHeightfield_Init(&base, terrain->resolution, terrain->span), "campo de relieve base");
    TEST_ASSERT(TerrainGenerator_BuildBaseRelief(&base, &recipe, world->terrainSeed, NULL),
                "reconstruir el relieve de referencia");
    uint32_t n = terrain->resolution;
    for (uint32_t z = 0; z < n; ++z) {
        for (uint32_t x = 0; x < n; ++x) {
            uint32_t i = z * n + x;
            const uint32_t adjacent[] = {i + 1u, i + n};
            const bool valid[] = {x + 1u < n, z + 1u < n};
            for (unsigned k = 0; k < 2; ++k) {
                if (!valid[k]) continue;
                uint32_t j = adjacent[k];
                if (hydro->waterBodyId[i] == hydro->waterBodyId[j]) continue;
                float addedRise = fabsf(terrain->heights[i] - terrain->heights[j]) -
                                  fabsf(base.heights[i] - base.heights[j]);
                TEST_ASSERT(addedRise <= .70f * terrain->spacing,
                            "el tallado no introduce un escalón abrupto de ribera");
            }
        }
    }

    /* La primera unión perdía 0,85 u de un lado al otro. El segundo
     * punto es un bajo seco excavado junto al río en la receta original. */
    WaterSample a, b;
    TEST_ASSERT(WorldHydrology_SampleWater(hydro, terrain, 15.0f, 34.5f, &a) && a.wet &&
                WorldHydrology_SampleWater(hydro, terrain, 15.5f, 34.5f, &b) && b.wet,
                "la unión conserva agua encima de las dos muestras");
    TEST_ASSERT(fabsf(a.groundY - b.groundY) < .15f, "lecho continuo en la unión de las capturas");
    TEST_ASSERT(WorldHydrology_SampleWater(hydro, terrain, 68.0f, -2.5f, &a) && !a.wet,
                "la depresión lateral sigue en tierra seca");
    float baseHeight;
    TEST_ASSERT(Terrain_SampleHeight(&base, 68.0f, -2.5f, &baseHeight), "muestrear la ribera original");
    CHECK_NEAR(a.groundY, baseHeight, .04f,
               "el socavón lateral desaparece y recupera su relieve base");
    TerrainHeightfield_Free(&base);
    WorldStaticData_Release(world);
}

static void test_v3_sloping_corridor_and_rejected_pit(void) {
    WorldRecipe recipe = WorldRecipe_Default();
    recipe.generatorVersion = WORLD_GENERATOR_VERSION_MORPHOLOGY;
    recipe.hydrology.riverDensity = .75f;
    recipe.hydrology.lakeDensity = .15f;
    recipe.size = 64;
    recipe.terrain.resolution = 129u;
    TerrainHeightfield tf;
    TEST_ASSERT(TerrainHeightfield_Init(&tf, 129, recipe.size), "campo sintético");
    uint32_t r = tf.resolution;
    for (uint32_t z = 0; z < r; ++z) {
        for (uint32_t x = 0; x < r; ++x) {
            float wx = tf.originX + x * tf.spacing;
            float wz = tf.originZ + z * tf.spacing;
            tf.heights[z*r + x] = 6.0f - .062f * wz + .014f * wx * wx
                               + .021f * sinf(3.0f * wz) * cosf(3.0f * wx);
        }
    }
    unsigned pit = 65u * r + 104u;
    tf.heights[pit] -= .28f;
    float pitHeight = tf.heights[pit];
    WorldHydrology hydro;
    TEST_ASSERT(WorldHydrology_Generate(&hydro, &tf, &recipe, 42), "excavar valle sintético v3");
    TEST_ASSERT(hydro.drainageAcyclic && hydro.riverSegmentCount > 24 && hydro.lakeCount == 0,
                "drenaje por corredor, sin cuenca espuria");
    CHECK_NEAR(tf.heights[pit], pitHeight, 1e-6f, "un microhoyo seco no se rellena a cota de vertedero");
    float previous = INFINITY;
    uint16_t body = 0;
    for (float z = 4.37f; z < 26; z += 1.0f) {
        WaterSample sample;
        TEST_ASSERT(WorldHydrology_SampleWater(&hydro, &tf, .01f, z, &sample) && sample.wet,
                    "lámina continua a lo largo del talweg");
        TEST_ASSERT(!body || body == sample.waterBodyId, "identidad del mismo corredor");
        TEST_ASSERT(sample.surfaceY < previous + 1e-4f && sample.depth > 0,
                    "lámina descendente y por encima del lecho");
        body = sample.waterBodyId;
        previous = sample.surfaceY;
        TEST_ASSERT(WorldHydrology_SampleWater(&hydro, &tf, 10.01f, z, &sample) && !sample.wet,
                    "franja exterior de tierra seca");
    }
    WorldHydrology_Free(&hydro);
    TerrainHeightfield_Free(&tf);
}

void run_world_hydrology_tests(void) {
    printf("\n--- Módulo WorldHydrology, WorldBiome & WorldGeology ---\n");
    test_synthetic_bowl_lake_and_plateau(WORLD_GENERATOR_VERSION_LEGACY);
    test_synthetic_bowl_lake_and_plateau(WORLD_GENERATOR_VERSION_MORPHOLOGY);
    test_synthetic_bowl_lake_and_plateau(WORLD_GENERATOR_VERSION_SHORELINE);
    test_procedural_valley_rivers_and_geology();
    test_continuous_water_mesh_and_version();
    test_v3_morphology();
    test_v3_sloping_corridor_and_rejected_pit();
    test_v3_river_lake_seam();
    test_v4_seed42_river_valley_banks();
}
