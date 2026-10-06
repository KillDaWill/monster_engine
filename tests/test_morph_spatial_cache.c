/**
 * @file test_morph_spatial_cache.c
 * @brief Pruebas unitarias para el caché espacial persistente de bloques SDF (Sparse Brick Cache).
 */

#include "test_utils.h"
#include "MorphSpatialCache.h"
#include "Monster.h"
#include "Creature.h"
#include "CreatureRecipes.h"
#include "MonsterSDF.h"
#include <stdio.h>

static void test_morph_spatial_cache_lifecycle(void) {
    MorphSpatialCache* cache = MorphSpatialCache_Create(0.25f);
    TEST_ASSERT(cache != NULL, "Creación de MorphSpatialCache válida");
    TEST_ASSERT(FLOAT_NEAR(cache->brickSize, 0.25f), "Tamaño de bloque configurado");
    TEST_ASSERT(MorphSpatialCache_GetTotalBrickCount(cache) == 0, "Caché inicial vacío");
    TEST_ASSERT(MorphSpatialCache_GetDirtyBrickCount(cache) == 0, "Cero bloques sucios");
    TEST_ASSERT(MorphSpatialCache_GetSurfaceBrickCount(cache) == 0, "Cero bloques de superficie");

    MorphSpatialCache_Destroy(cache);
    printf("[PASS] test_morph_spatial_cache_lifecycle\n");
}

static void test_morph_spatial_cache_brick_access(void) {
    MorphSpatialCache* cache = MorphSpatialCache_Create(0.5f);

    /* Búsqueda sin creación */
    const MorphSpatialBrick* missing = MorphSpatialCache_FindBrick(cache, 1, 2, 3);
    TEST_ASSERT(missing == NULL, "Bloque no existente retorna NULL");

    /* Obtención con creación */
    MorphSpatialBrick* b1 = MorphSpatialCache_GetBrick(cache, 1, 2, 3, true);
    TEST_ASSERT(b1 != NULL, "Bloque creado correctamente");
    TEST_ASSERT(b1->bx == 1 && b1->by == 2 && b1->bz == 3, "Coordenadas discretas correctas");
    TEST_ASSERT(FLOAT_NEAR(b1->bounds.start.x, 0.5f) && FLOAT_NEAR(b1->bounds.end.x, 1.0f), "Bounds X correctos");
    TEST_ASSERT(FLOAT_NEAR(b1->bounds.start.y, 1.0f) && FLOAT_NEAR(b1->bounds.end.y, 1.5f), "Bounds Y correctos");
    TEST_ASSERT(FLOAT_NEAR(b1->bounds.start.z, 1.5f) && FLOAT_NEAR(b1->bounds.end.z, 2.0f), "Bounds Z correctos");
    TEST_ASSERT(b1->isDirty == true, "Nuevo bloque inicia marcado como sucio");
    TEST_ASSERT(MorphSpatialCache_GetTotalBrickCount(cache) == 1, "Conteo total = 1");
    TEST_ASSERT(MorphSpatialCache_GetDirtyBrickCount(cache) == 1, "Conteo sucios = 1");

    /* Obtener el mismo bloque existente */
    MorphSpatialBrick* b1_again = MorphSpatialCache_GetBrick(cache, 1, 2, 3, false);
    TEST_ASSERT(b1_again == b1, "Retorna el mismo bloque existente");

    /* Segundo bloque */
    MorphSpatialBrick* b2 = MorphSpatialCache_GetBrick(cache, -2, 0, 4, true);
    TEST_ASSERT(b2 != NULL && b2 != b1, "Segundo bloque creado");
    TEST_ASSERT(MorphSpatialCache_GetTotalBrickCount(cache) == 2, "Conteo total = 2");

    MorphSpatialCache_Clear(cache);
    TEST_ASSERT(MorphSpatialCache_GetTotalBrickCount(cache) == 0, "Caché vaciado tras Clear");

    MorphSpatialCache_Destroy(cache);
    printf("[PASS] test_morph_spatial_cache_brick_access\n");
}

static void test_morph_spatial_cache_mark_dirty_aabb(void) {
    MorphSpatialCache* cache = MorphSpatialCache_Create(0.5f);

    /* Región AABB de [-0.2, -0.2, -0.2] a [0.6, 0.6, 0.6]
     * Coordenadas discretas: min = floor(-0.2 / 0.5) = -1, max = floor(0.6 / 0.5) = 1
     * Rango de 3 x 3 x 3 = 27 bloques */
    AABB3D aabb = AABB_FromMinMax(Vec3_Create(-0.2f, -0.2f, -0.2f), Vec3_Create(0.6f, 0.6f, 0.6f));
    size_t marked = MorphSpatialCache_MarkDirtyAABB(cache, aabb, MORPH_DIRT_DEFORMATION);
    TEST_ASSERT(marked == 27, "Marca exactamente 27 bloques cúbicos");
    TEST_ASSERT(MorphSpatialCache_GetDirtyBrickCount(cache) == 27, "Conteo sucios = 27");

    uint32_t flags = 0;
    bool dirty = MorphSpatialCache_IsRegionDirty(cache, aabb, &flags);
    TEST_ASSERT(dirty == true, "La región original reporta estar sucia");
    TEST_ASSERT((flags & MORPH_DIRT_DEFORMATION) != 0, "Bandera MORPH_DIRT_DEFORMATION presente");

    /* Región lejana disjunta */
    AABB3D distantAABB = AABB_FromMinMax(Vec3_Create(10.0f, 10.0f, 10.0f), Vec3_Create(11.0f, 11.0f, 11.0f));
    bool distantDirty = MorphSpatialCache_IsRegionDirty(cache, distantAABB, NULL);
    TEST_ASSERT(distantDirty == false, "Región lejana disjunta no está sucia");

    MorphSpatialCache_Destroy(cache);
    printf("[PASS] test_morph_spatial_cache_mark_dirty_aabb\n");
}

static void test_morph_spatial_cache_swept_module(void) {
    MorphSpatialCache* cache = MorphSpatialCache_Create(0.25f);

    AABB3D before = AABB_FromMinMax(Vec3_Create(0.0f, 0.0f, 0.0f), Vec3_Create(0.5f, 0.5f, 0.5f));
    AABB3D after  = AABB_FromMinMax(Vec3_Create(0.4f, 0.2f, 0.0f), Vec3_Create(0.9f, 0.7f, 0.5f));

    size_t marked = MorphSpatialCache_MarkSweptModuleDirty(cache, before, after, 0.05f, true);
    TEST_ASSERT(marked > 0, "Bloques sucios marcados en el barrido");

    uint32_t flags = 0;
    AABB3D query = AABB_FromMinMax(Vec3_Create(0.2f, 0.2f, 0.2f), Vec3_Create(0.3f, 0.3f, 0.3f));
    bool dirty = MorphSpatialCache_IsRegionDirty(cache, query, &flags);
    TEST_ASSERT(dirty == true, "Región intermedia del barrido está sucia");
    TEST_ASSERT((flags & MORPH_DIRT_TOPOLOGY) != 0, "Bandera MORPH_DIRT_TOPOLOGY presente");

    MorphSpatialCache_Destroy(cache);
    printf("[PASS] test_morph_spatial_cache_swept_module\n");
}

static void test_morph_spatial_cache_incremental_sdf_update(void) {
    Monster dog = Monster_Create();
    CreaturePhenotype p = CreatureRecipes_Dog()->adult;
    TEST_ASSERT(Creature_BuildMonster(&dog, CreatureRecipes_Dog(), &p), "Construcción de perro adulto");

    MonsterSDF sdf = MonsterSDF_Create();
    TEST_ASSERT(MonsterSDF_Build(&sdf, &dog, MonsterSDF_DefaultConfig()), "Construcción SDF");

    MorphSpatialCache* cache = MorphSpatialCache_Create(0.25f);
    AABB3D dogBounds = MonsterSDF_GetBounds(&sdf);

    /* Ensuciar todos los bloques que encierran al perro */
    size_t initialMarked = MorphSpatialCache_MarkDirtyAABB(cache, dogBounds, MORPH_DIRT_DEFORMATION);
    TEST_ASSERT(initialMarked > 0, "Bloques marcados en bounds del perro");
    TEST_ASSERT(MorphSpatialCache_GetDirtyBrickCount(cache) == initialMarked, "Bloques sucios coinciden");

    /* Actualizar todo el caché */
    size_t updated = MorphSpatialCache_UpdateDirty(cache, &sdf, 0.0f);
    TEST_ASSERT(updated == initialMarked, "Todos los bloques sucios fueron actualizados");
    TEST_ASSERT(MorphSpatialCache_GetDirtyBrickCount(cache) == 0, "Cero bloques sucios tras UpdateDirty");
    TEST_ASSERT(MorphSpatialCache_GetSurfaceBrickCount(cache) > 0, "Detectados bloques con superficie");

    /* Ensuciar de forma incremental únicamente una pata local pequeña */
    AABB3D localPaw = AABB_FromMinMax(Vec3_Create(-0.25f, -0.45f, 0.30f), Vec3_Create(-0.10f, -0.30f, 0.45f));
    size_t pawDirty = MorphSpatialCache_MarkDirtyAABB(cache, localPaw, MORPH_DIRT_TOPOLOGY);
    TEST_ASSERT(pawDirty > 0, "Pata local ensucia bloques");
    TEST_ASSERT(pawDirty < updated / 4, "La pata local ensucia una pequeña fracción del total de bloques");
    TEST_ASSERT(MorphSpatialCache_GetDirtyBrickCount(cache) == pawDirty, "Solo los bloques locales están sucios");

    /* Reevaluar de forma incremental: solo la pata se computa */
    size_t incrementalUpdated = MorphSpatialCache_UpdateDirty(cache, &sdf, 0.10f);
    TEST_ASSERT(incrementalUpdated == pawDirty, "Solo los bloques locales sucios fueron reevaluados");
    TEST_ASSERT(MorphSpatialCache_GetDirtyBrickCount(cache) == 0, "Caché limpio tras reevaluación incremental");

    Monster_Free(&dog);
    MonsterSDF_Free(&sdf);
    MorphSpatialCache_Destroy(cache);
    printf("[PASS] test_morph_spatial_cache_incremental_sdf_update\n");
}

void run_morph_spatial_cache_tests(void) {
    printf("\n--- Módulo MorphSpatialCache (Sparse Brick Cache) ---\n");
    test_morph_spatial_cache_lifecycle();
    test_morph_spatial_cache_brick_access();
    test_morph_spatial_cache_mark_dirty_aabb();
    test_morph_spatial_cache_swept_module();
    test_morph_spatial_cache_incremental_sdf_update();
}
