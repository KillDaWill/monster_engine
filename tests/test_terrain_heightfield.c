#include "TerrainLodIndices.h"
#include <stdlib.h>
/**
 * @file test_terrain_heightfield.c
 * @brief Pruebas unitarias de Phase 1: campo de alturas, interpolación baricéntrica exacta,
 *        raycast DDA, recetas estrictas, determinismo, erosión térmica y transformaciones corporales.
 */
#include "test_utils.h"
#include "TerrainHeightfield.h"
#include "TerrainGenerator.h"
#include "WorldIO.h"
#include "WorldGenerator.h"
#include "WorldCollision.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK_NEAR(a, b, eps, msg) TEST_ASSERT(fabsf((a) - (b)) <= (eps), (msg))

static void test_flat_and_incline_triangles(void) {
    TerrainHeightfield tf;
    TEST_ASSERT(TerrainHeightfield_Init(&tf, 9u, 16.0f), "init heightfield 9x9");
    TEST_ASSERT(tf.isFlat, "heightfield inicial plano");
    SurfaceHit hit;
    TEST_ASSERT(Terrain_SampleGround(&tf, 0.0f, 0.0f, &hit), "sample centro plano");
    CHECK_NEAR(hit.position.y, 0.0f, 1e-6f, "y=0 plano");
    CHECK_NEAR(hit.normal.y, 1.0f, 1e-6f, "normal +Y plano");

    TEST_ASSERT(!Terrain_SampleGround(&tf, 8.01f, 0.0f, &hit), "fuera de dominio +X");
    TEST_ASSERT(!Terrain_SampleGround(&tf, 0.0f, -8.5f, &hit), "fuera de dominio -Z");
    TEST_ASSERT(!Terrain_SampleGround(&tf, NAN, 0.0f, &hit), "rechazar NaN");
    TEST_ASSERT(!Terrain_SampleGround(&tf, INFINITY, 0.0f, &hit), "rechazar Inf");

    /* Plano inclinado exacto: h(x, z) = 0.5*x + 0.25*z + 4.0 */
    for (uint32_t iz = 0; iz < tf.resolution; ++iz) {
        float z = tf.originZ + (float)iz * tf.spacing;
        for (uint32_t ix = 0; ix < tf.resolution; ++ix) {
            float x = tf.originX + (float)ix * tf.spacing;
            tf.heights[iz * tf.resolution + ix] = 0.5f * x + 0.25f * z + 4.0f;
        }
    }
    TEST_ASSERT(TerrainHeightfield_RebuildDerived(&tf), "rebuild derivadas rampa");
    TEST_ASSERT(!tf.isFlat, "rampa no es plana");

    Vector3 expectedN = Vec3_Normalize(Vec3_Create(-0.5f, 1.0f, -0.25f));
    for (int i = -7; i <= 7; ++i) {
        float x = (float)i * 0.93f;
        float z = (float)(-i) * 0.77f;
        TEST_ASSERT(Terrain_SampleGround(&tf, x, z, &hit), "sample rampa");
        CHECK_NEAR(hit.position.y, 0.5f * x + 0.25f * z + 4.0f, 1e-4f, "altura rampa exacta");
        CHECK_NEAR(hit.normal.x, expectedN.x, 1e-4f, "normal.x rampa");
        CHECK_NEAR(hit.normal.y, expectedN.y, 1e-4f, "normal.y rampa");
        CHECK_NEAR(hit.normal.z, expectedN.z, 1e-4f, "normal.z rampa");
    }

    /* Celda silla de montar: comprobar T0=(v00,v11,v10), T1=(v00,v01,v11) y continuidad en u=v. */
    memset(tf.heights, 0, tf.resolution * tf.resolution * sizeof(float));
    tf.heights[4 * tf.resolution + 4] = 1.0f;
    tf.heights[4 * tf.resolution + 5] = 3.0f;
    tf.heights[5 * tf.resolution + 4] = 5.0f;
    tf.heights[5 * tf.resolution + 5] = 2.0f;
    TEST_ASSERT(TerrainHeightfield_RebuildDerived(&tf), "rebuild silla");

    TEST_ASSERT(Terrain_SampleGround(&tf, 1.5f, 0.5f, &hit), "sample T0");
    CHECK_NEAR(hit.position.y, 2.25f, 1e-4f, "altura T0 exacta");
    TEST_ASSERT(Terrain_SampleGround(&tf, 0.5f, 1.5f, &hit), "sample T1");
    CHECK_NEAR(hit.position.y, 3.25f, 1e-4f, "altura T1 exacta");

    SurfaceHit hA, hB;
    TEST_ASSERT(Terrain_SampleGround(&tf, 1.001f, 0.999f, &hA), "diagonal lado T0");
    TEST_ASSERT(Terrain_SampleGround(&tf, 0.999f, 1.001f, &hB), "diagonal lado T1");
    CHECK_NEAR(hA.position.y, 1.5f, 0.01f, "continuidad diagonal A");
    CHECK_NEAR(hB.position.y, 1.5f, 0.01f, "continuidad diagonal B");
    CHECK_NEAR(hA.position.y, hB.position.y, 0.01f, "continuidad C0 diagonal");

    TerrainHeightfield_Free(&tf);
    printf("[PASS] test_terrain_heightfield: plano, rampa exacta, silla y continuidad diagonal\n");
}

static void test_ridge_raycast_and_occlusion(void) {
    TerrainHeightfield tf;
    TEST_ASSERT(TerrainHeightfield_Init(&tf, 33u, 32.0f), "init cresta 33x33");
    for (uint32_t iz = 0; iz < tf.resolution; ++iz) {
        for (uint32_t ix = 0; ix < tf.resolution; ++ix) {
            float x = tf.originX + (float)ix * tf.spacing;
            tf.heights[iz * tf.resolution + ix] = fmaxf(0.0f, 6.0f - fabsf(x) * 1.5f);
        }
    }
    TEST_ASSERT(TerrainHeightfield_RebuildDerived(&tf), "rebuild cresta");

    float tHit = 0.0f;
    Vector3 pHit = Vec3_Zero();
    TEST_ASSERT(Terrain_RaycastSegment(&tf, Vec3_Create(-8.0f, 2.0f, 0.0f),
                                       Vec3_Create(8.0f, 2.0f, 0.0f), 0.05f, &tHit, &pHit),
                "cresta debe bloquear rayo bajo");
    TEST_ASSERT(tHit > 0.1f && tHit < 0.6f, "tHit dentro de la cresta");
    CHECK_NEAR(pHit.y, 2.0f, 0.15f, "altura de impacto");

    TEST_ASSERT(!Terrain_RaycastSegment(&tf, Vec3_Create(-8.0f, 7.0f, 0.0f),
                                        Vec3_Create(8.0f, 7.0f, 0.0f), 0.05f, NULL, NULL),
                "rayo por encima de la cresta debe pasar");

    SurfaceHit gHit;
    TEST_ASSERT(Terrain_SampleGround(&tf, -6.0f, 0.0f, &gHit), "sample contacto suelo");
    TEST_ASSERT(!Terrain_RaycastSegment(&tf, Vec3_Create(-6.0f, gHit.position.y + 1.0f, 0.0f),
                                        gHit.position, 0.08f, NULL, NULL),
                "tolerancia de extremo permite contacto sobre superficie");

    TerrainHeightfield_Free(&tf);
    printf("[PASS] test_terrain_heightfield: raycast DDA sobre cresta y tolerancia de contacto\n");
}

static void test_recipe_validation_and_determinism(void) {
    char err[256] = {0};
    WorldRecipe bad = WorldRecipe_Default();
    bad.terrain.resolution = 128u;
    TEST_ASSERT(!WorldRecipe_Validate(&bad, err, sizeof(err)), "rechazar resolución par");

    const char *badJson1 = "{\"version\":99,\"id\":\"x\"}";
    TEST_ASSERT(!WorldRecipe_DeserializeJSON(badJson1, strlen(badJson1), &bad, err, sizeof(err)),
                "rechazar versión desconocida");
    const char *badJson2 = "{\"version\":1,\"id\":\"x\",\"unknown_key\":123}";
    TEST_ASSERT(!WorldRecipe_DeserializeJSON(badJson2, strlen(badJson2), &bad, err, sizeof(err)),
                "rechazar clave desconocida");

    const char *files[5] = {
        "worlds/flat.world.json",
        "worlds/river_valley.world.json",
        "worlds/mountain_lakes.world.json",
        "worlds/rocky_highlands.world.json",
        "worlds/wetlands.world.json"
    };
    for (int i = 0; i < 5; ++i) {
        WorldRecipe r1, r2;
        TEST_ASSERT(WorldRecipe_LoadFile(files[i], &r1, err, sizeof(err)), "cargar receta world.json");
        char buf[4096];
        size_t len = 0;
        TEST_ASSERT(WorldRecipe_SerializeJSON(&r1, buf, sizeof(buf), &len), "serializar receta");
        TEST_ASSERT(WorldRecipe_DeserializeJSON(buf, len, &r2, err, sizeof(err)), "round-trip receta");
        TEST_ASSERT(r1.recipeHash == r2.recipeHash, "huella estable en round-trip");
    }

    WorldRecipe rec = WorldRecipe_Default();
    rec.size = 64.0f;
    rec.terrain.resolution = 65u;
    rec.seed = 42u;
    WorldStaticData *wA = NULL, *wB = NULL, *wC = NULL;
    TEST_ASSERT(WorldGenerator_Generate(&rec, &wA, err, sizeof(err)), "generar mundo A");
    TEST_ASSERT(WorldGenerator_Generate(&rec, &wB, err, sizeof(err)), "generar mundo B");
    TEST_ASSERT(wA->dataFingerprint == wB->dataFingerprint, "determinismo exacto misma semilla");
    TEST_ASSERT(wA->stats.erosionMaterialMoved >= 0.0, "erosión no negativa");

    rec.seed = 43u;
    TEST_ASSERT(WorldGenerator_Generate(&rec, &wC, err, sizeof(err)), "generar mundo C");
    TEST_ASSERT(wA->dataFingerprint != wC->dataFingerprint, "semilla distinta produce mundo distinto");

    Vector3 nrm = Vec3_Normalize(Vec3_Create(0.25f, 0.92f, -0.30f));
    Quaternion frame = World_BuildBodyFrame(1.37f, nrm, 0.50f);
    Vector3 center = Vec3_Create(0.1f, 0.4f, -0.2f);
    Vector3 origin = Vec3_Create(12.5f, 7.3f, -4.2f);
    float scale = 1.35f;
    Vector3 localPt = Vec3_Create(0.45f, 0.92f, 1.15f);
    Vector3 worldPt = World_LocalToWorld(localPt, center, scale, frame, origin);
    Vector3 backPt = World_WorldToLocal(worldPt, center, scale, frame, origin);
    CHECK_NEAR(backPt.x, localPt.x, 1e-4f, "round-trip local/world X");
    CHECK_NEAR(backPt.y, localPt.y, 1e-4f, "round-trip local/world Y");
    CHECK_NEAR(backPt.z, localPt.z, 1e-4f, "round-trip local/world Z");

    WorldStaticData_Release(wA);
    WorldStaticData_Release(wB);
    WorldStaticData_Release(wC);
    printf("[PASS] test_terrain_heightfield: recetas JSON, determinismo, erosión y marco corporal\n");
}

static int compareLodEdges(const void *a, const void *b) {
    uint32_t lhs = *(const uint32_t *)a, rhs = *(const uint32_t *)b;
    return lhs < rhs ? -1 : (lhs > rhs);
}

static void checkLodIndices(uint32_t width, uint32_t height, uint32_t lod, uint32_t mask) {
    uint32_t *indices = NULL, count = 0;
    TerrainLodIndices_Build(width, height, lod, mask, &indices, &count);
    TEST_ASSERT(indices && count && count % 3 == 0, "índices de chunk completos");
    uint32_t *edges = malloc(count * sizeof(*edges));
    TEST_ASSERT(edges != NULL, "reservar aristas de verificación");
    int area = 0;
    uint32_t stride = width + 1u;
    for (uint32_t i = 0; i < count; i += 3) {
        int x[3], z[3];
        for (unsigned k = 0; k < 3; ++k) {
            uint32_t a = indices[i+k], b = indices[i+(k+1u)%3u];
            TEST_ASSERT(a < stride * (height + 1u), "índice dentro del chunk");
            x[k] = (int)(a % stride);
            z[k] = (int)(a / stride);
            edges[i+k] = a < b ? (a << 16) | b : (b << 16) | a;
        }
        int triArea = (z[1]-z[0]) * (x[2]-x[0]) - (x[1]-x[0]) * (z[2]-z[0]);
        TEST_ASSERT(triArea > 0, "caras CCW sin degenerados ni solapes invertidos");
        area += triArea;
    }
    TEST_ASSERT(area == (int)(2u * width * height), "malla LOD cubre toda el área del chunk");
    qsort(edges, count, sizeof(*edges), compareLodEdges);
    uint32_t cover[4] = {0};
    for (uint32_t i = 0; i < count;) {
        uint32_t next = i + 1u;
        while (next < count && edges[next] == edges[i]) ++next;
        uint32_t a = edges[i] >> 16, b = edges[i] & 65535u;
        uint32_t ax = a % stride, az = a / stride, bx = b % stride, bz = b / stride;
        int side = az == 0 && bz == 0 ? 0 : (az == height && bz == height ? 1 :
                   (ax == 0 && bx == 0 ? 2 : (ax == width && bx == width ? 3 : -1)));
        if (side < 0) {
            TEST_ASSERT(next - i == 2, "arista interior sin T-junction ni solape");
        } else {
            TEST_ASSERT(next - i == 1, "arista de borde única");
            uint32_t length = side < 2 ? width : height;
            uint32_t low = side < 2 ? ax : az, high = side < 2 ? bx : bz;
            uint32_t step = (1u << lod) * ((mask & (1u << side)) ? 2u : 1u);
            TEST_ASSERT(low % step == 0 && high == (low + step < length ? low + step : length),
                        "arista exterior coincide exactamente con vecino");
            cover[side] += high - low;
        }
        i = next;
    }
    TEST_ASSERT(cover[0] == width && cover[1] == width && cover[2] == height && cover[3] == height,
                "los cuatro bordes quedan cerrados");
    free(edges);
    free(indices);
}

static void test_lod_stitching_masks(void) {
    for (uint32_t mask = 0; mask < 16; ++mask) {
        for (uint32_t lod = 0; lod < 4; ++lod) checkLodIndices(32, 32, lod, mask);
        checkLodIndices(32, 10, 0, mask);
        checkLodIndices(17, 26, 0, mask);
    }
    printf("[PASS] TerrainLodIndices: 96 combinaciones de borde y esquina\n");
}

void run_terrain_heightfield_tests(void) {
    printf("\n--- Módulo TerrainHeightfield & WorldGenerator ---\n");
    test_flat_and_incline_triangles();
    test_ridge_raycast_and_occlusion();
    test_recipe_validation_and_determinism();
    test_lod_stitching_masks();
}
