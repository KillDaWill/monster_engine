/**
 * @file test_morph_chunk_manager.c
 * @brief Pruebas unitarias para el gestor de remallado local por chunks y ensamble sin costuras.
 */

#include "test_utils.h"
#include "MorphChunkManager.h"
#include "MorphSpatialCache.h"
#include "Monster.h"
#include "Creature.h"
#include "CreatureRecipes.h"
#include "MonsterSDF.h"
#include "SDFMesher.h"
#include "SDFSamplingPool.h"
#include "Mesh.h"
#include <stdio.h>

static void test_morph_chunk_manager_lifecycle(void) {
    MorphChunkManager* mgr = MorphChunkManager_Create(0.5f, 0.05f);
    TEST_ASSERT(mgr != NULL, "Creación de MorphChunkManager válida");
    TEST_ASSERT(FLOAT_NEAR(mgr->chunkSize, 0.5f), "Tamaño de chunk configurado");
    TEST_ASSERT(FLOAT_NEAR(mgr->voxelSize, 0.05f), "Tamaño de vóxel configurado");
    TEST_ASSERT(MorphChunkManager_GetTotalChunkCount(mgr) == 0, "Cero chunks iniciales");
    TEST_ASSERT(MorphChunkManager_GetDirtyChunkCount(mgr) == 0, "Cero chunks sucios");

    MorphChunkManager_Destroy(mgr);
    printf("[PASS] test_morph_chunk_manager_lifecycle\n");
}

static void test_morph_chunk_manager_chunk_access(void) {
    MorphChunkManager* mgr = MorphChunkManager_Create(0.5f, 0.05f);

    const MorphChunk* missing = MorphChunkManager_FindChunk(mgr, 1, 2, 3);
    TEST_ASSERT(missing == NULL, "Chunk inexistente retorna NULL");

    MorphChunk* c1 = MorphChunkManager_GetChunk(mgr, 1, 2, 3, true);
    TEST_ASSERT(c1 != NULL, "Chunk creado correctamente");
    TEST_ASSERT(c1->cx == 1 && c1->cy == 2 && c1->cz == 3, "Coordenadas discretas correctas");
    TEST_ASSERT(FLOAT_NEAR(c1->bounds.start.x, 0.5f) && FLOAT_NEAR(c1->bounds.end.x, 1.0f), "Bounds X correctos");
    TEST_ASSERT(FLOAT_NEAR(c1->bounds.start.y, 1.0f) && FLOAT_NEAR(c1->bounds.end.y, 1.5f), "Bounds Y correctos");
    TEST_ASSERT(FLOAT_NEAR(c1->bounds.start.z, 1.5f) && FLOAT_NEAR(c1->bounds.end.z, 2.0f), "Bounds Z correctos");
    TEST_ASSERT(c1->isDirty == true, "Chunk nuevo inicia marcado como sucio");
    TEST_ASSERT(MorphChunkManager_GetTotalChunkCount(mgr) == 1, "Total chunks = 1");

    MorphChunk* c1_again = MorphChunkManager_GetChunk(mgr, 1, 2, 3, false);
    TEST_ASSERT(c1_again == c1, "Retorna el mismo chunk existente");

    MorphChunkManager_Clear(mgr);
    TEST_ASSERT(MorphChunkManager_GetTotalChunkCount(mgr) == 0, "Chunks vaciados tras Clear");

    MorphChunkManager_Destroy(mgr);
    printf("[PASS] test_morph_chunk_manager_chunk_access\n");
}

static void test_morph_chunk_manager_dirty_sync_spatial_cache(void) {
    MorphSpatialCache* cache = MorphSpatialCache_Create(0.25f);
    MorphChunkManager* mgr = MorphChunkManager_Create(0.5f, 0.05f);

    /* Ensuciar una región en el spatial cache */
    AABB3D aabb = AABB_FromMinMax(Vec3_Create(0.1f, 0.1f, 0.1f), Vec3_Create(0.6f, 0.6f, 0.6f));
    size_t markedBricks = MorphSpatialCache_MarkDirtyAABB(cache, aabb, MORPH_DIRT_TOPOLOGY);
    TEST_ASSERT(markedBricks > 0, "Ladrillos marcados en spatial cache");

    /* Sincronizar hacia el chunk manager */
    size_t syncedChunks = MorphChunkManager_SynchronizeWithSpatialCache(mgr, cache);
    TEST_ASSERT(syncedChunks > 0, "Chunks sincronizados desde spatial cache");
    TEST_ASSERT(MorphChunkManager_GetDirtyChunkCount(mgr) > 0, "Hay chunks sucios tras sincronización");

    MorphChunkManager_Destroy(mgr);
    MorphSpatialCache_Destroy(cache);
    printf("[PASS] test_morph_chunk_manager_dirty_sync_spatial_cache\n");
}

static void test_morph_chunk_manager_local_remesh_and_assemble(void) {
    Monster dog = Monster_Create();
    CreaturePhenotype p = CreatureRecipes_Dog()->adult;
    TEST_ASSERT(Creature_BuildMonster(&dog, CreatureRecipes_Dog(), &p), "Construcción de perro");

    MonsterSDF sdf = MonsterSDF_Create();
    TEST_ASSERT(MonsterSDF_Build(&sdf, &dog, MonsterSDF_DefaultConfig()), "Construcción SDF");
    SDFField field = MonsterSDF_GetField(&sdf);

    SDFMesher mesher = SDFMesher_Create(SDFMesher_DefaultConfig());
    mesher.config.adaptiveDetail = false;
    mesher.config.voxelSize = 0.08f;

    MorphChunkManager* mgr = MorphChunkManager_Create(0.5f, 0.08f);
    AABB3D dogBounds = MonsterSDF_GetBounds(&sdf);

    size_t markedChunks = MorphChunkManager_MarkDirtyAABB(mgr, dogBounds, MORPH_DIRT_DEFORMATION);
    TEST_ASSERT(markedChunks > 0, "Chunks marcados para el perro");

    /* Actualizar y remallar los chunks sucios */
    size_t remeshed = MorphChunkManager_UpdateDirty(mgr, &mesher, &field, 1);
    TEST_ASSERT(remeshed == markedChunks, "Todos los chunks sucios fueron remallados");
    TEST_ASSERT(MorphChunkManager_GetDirtyChunkCount(mgr) == 0, "Cero chunks sucios tras UpdateDirty");
    TEST_ASSERT(MorphChunkManager_GetSurfaceChunkCount(mgr) > 0, "Chunks de superficie identificados");
    TEST_ASSERT(MorphChunkManager_GetTotalVertexCount(mgr) > 0, "Vértices generados en los chunks");
    TEST_ASSERT(MorphChunkManager_MarkResidualDirty(mgr,&field,.20f)==0,
        "La superficie recién extraída no debe marcarse como obsoleta");
    MorphChunk* corrupted=NULL;
    for(size_t i=0;i<mgr->chunkCount;++i)
        if(mgr->chunks[i].hasSurface&&mgr->chunks[i].mesh.vertexCount) {
            corrupted=&mgr->chunks[i];break;
        }
    TEST_ASSERT(corrupted,"No se halló un chunk para probar el residual");
    corrupted->mesh.vertices[0].position.x+=10.0f;
    TEST_ASSERT(MorphChunkManager_MarkResidualDirty(mgr,&field,.20f)>=1&&
        corrupted->isDirty&&(corrupted->dirtinessFlags&MORPH_DIRT_TOPOLOGY),
        "Un chunk obsoleto no se promovió a remallado topológico");
    TEST_ASSERT(MorphChunkManager_UpdateDirty(mgr,&mesher,&field,2)>=1&&
        !corrupted->isDirty,
        "El chunk residual no se regeneró y certificó");

    /* Ensamble sin costuras (seam welding) */
    Mesh assembled = Mesh_Create();
    bool assembleOk = MorphChunkManager_AssembleMesh(mgr, &assembled, true);
    TEST_ASSERT(assembleOk, "Ensamble de malla por chunks con soldadura de costuras exitoso");
    TEST_ASSERT(assembled.vertexCount > 0, "Malla ensamblada contiene vértices");
    TEST_ASSERT(assembled.indexCount > 0, "Malla ensamblada contiene triángulos");

    /* Verificación de integridad geométrica de la malla ensamblada */
    MeshValidationResult val = Mesh_Validate(&assembled);
    TEST_ASSERT(val.valid, "Malla ensamblada es geométrica y topológicamente válida");
    TEST_ASSERT(val.nonFiniteVertexCount == 0, "Cero vértices no finitos");
    TEST_ASSERT(val.nonFiniteNormalCount == 0, "Cero normales no finitas");

    Mesh_Free(&assembled);
    SDFMesher_Free(&mesher);
    MorphChunkManager_Destroy(mgr);
    MonsterSDF_Free(&sdf);
    Monster_Free(&dog);
    printf("[PASS] test_morph_chunk_manager_local_remesh_and_assemble\n");
}

static void test_morph_chunk_manager_parallel_update(void) {
    Monster dog = Monster_Create();
    const CreatureRecipe* recipe = CreatureRecipes_Dog();
    TEST_ASSERT(Creature_BuildMonster(&dog, recipe, &recipe->adult), "Construcción del perro adulto");

    MonsterSDF sdf = MonsterSDF_Create();
    TEST_ASSERT(MonsterSDF_Build(&sdf, &dog, MonsterSDF_DefaultConfig()), "Construcción del SDF del perro");
    SDFField field = MonsterSDF_GetField(&sdf);

    SDFSamplingPool* pool = SDFSamplingPool_Create(4);
    TEST_ASSERT(pool != NULL, "Creación de SDFSamplingPool con 4 hilos");

    SDFMesherConfig mesherCfg = SDFMesher_DefaultConfig();
    mesherCfg.adaptiveDetail = false;
    mesherCfg.voxelSize = 0.08f;

    MorphChunkManager* mgr = MorphChunkManager_Create(0.5f, 0.08f);
    AABB3D dogBounds = MonsterSDF_GetBounds(&sdf);

    size_t markedChunks = MorphChunkManager_MarkDirtyAABB(mgr, dogBounds, MORPH_DIRT_DEFORMATION);
    TEST_ASSERT(markedChunks > 0, "Chunks marcados para el perro");

    /* Actualizar y remallar los chunks sucios en paralelo con el pool */
    size_t remeshed = MorphChunkManager_UpdateDirtyParallel(mgr, mesherCfg, pool, &field, 1);
    TEST_ASSERT(remeshed == markedChunks, "Todos los chunks sucios fueron remallados en paralelo");
    TEST_ASSERT(MorphChunkManager_GetDirtyChunkCount(mgr) == 0, "Cero chunks sucios tras UpdateDirtyParallel");
    TEST_ASSERT(MorphChunkManager_GetSurfaceChunkCount(mgr) > 0, "Chunks de superficie identificados en paralelo");
    TEST_ASSERT(MorphChunkManager_GetTotalVertexCount(mgr) > 0, "Vértices generados en paralelo");

    /* Ensamble sin costuras (seam welding) */
    Mesh assembled = Mesh_Create();
    bool assembleOk = MorphChunkManager_AssembleMesh(mgr, &assembled, true);
    TEST_ASSERT(assembleOk, "Ensamble de malla tras remallado paralelo exitoso");
    TEST_ASSERT(assembled.vertexCount > 0, "Malla ensamblada contiene vértices");
    TEST_ASSERT(assembled.indexCount > 0, "Malla ensamblada contiene triángulos");

    MeshValidationResult val = Mesh_Validate(&assembled);
    TEST_ASSERT(val.valid, "Malla ensamblada en paralelo es válida");
    TEST_ASSERT(val.nonFiniteVertexCount == 0, "Cero vértices no finitos en paralelo");
    TEST_ASSERT(val.nonFiniteNormalCount == 0, "Cero normales no finitas en paralelo");

    Mesh_Free(&assembled);
    MorphChunkManager_Destroy(mgr);
    SDFSamplingPool_Free(pool);
    MonsterSDF_Free(&sdf);
    Monster_Free(&dog);
    printf("[PASS] test_morph_chunk_manager_parallel_update\n");
}

void run_morph_chunk_manager_tests(void) {
    printf("\n--- Módulo MorphChunkManager (Remallado Local por Chunks) ---\n");
    test_morph_chunk_manager_lifecycle();
    test_morph_chunk_manager_chunk_access();
    test_morph_chunk_manager_dirty_sync_spatial_cache();
    test_morph_chunk_manager_local_remesh_and_assemble();
    test_morph_chunk_manager_parallel_update();
}
