#include "test_utils.h"
#include "MonsterVisualAsync.h"
#include "Monster.h"
#include "MonsterAger.h"
#include <unistd.h>
#include <stdio.h>

static Monster BuildLizard(void) {
    Monster lizard = Monster_Create();
    Monster_Init(&lizard);
    lizard.colorPalette = ColorPalette_CreateGradient(Color_FromRGB(16, 180, 75), Color_FromRGB(235, 195, 45), 5);

    BodyPart* head = Monster_GetHead(&lizard);
    if (head) {
        head->width = 1.8f;
        head->height = 1.2f;
        head->length = 2.0f;
        head->color.index = 0;
    }

    BodyPart chest = BodyPart_Create(0.0f, 0.0f, -2.2f, 2.2f, 2.5f, 1.5f, 0.0f);
    chest.color.index = 1;
    BodyPart abdomen = BodyPart_Create(0.0f, 0.0f, -4.8f, 2.0f, 2.5f, 1.3f, 0.0f);
    abdomen.color.index = 2;

    Monster_AddBodyPart(&lizard, chest);
    Monster_AddBodyPart(&lizard, abdomen);

    Eye leftEye = Eye_Create(0, Vec3_Create(-0.75f, 0.35f, 1.1f), Vec3_Create(0.42f, 0.4f, 0.25f), COLOR_WHITE, Color_FromRGB(20, 20, 20));
    Monster_AddEye(&lizard, leftEye);

    return lizard;
}

void test_visual_async_lifecycle_and_flush(void) {
    Monster lizard = BuildLizard();

    MonsterVisualAsyncConfig cfg = MonsterVisualAsync_DefaultConfig();
    cfg.interactiveMesherConfig.voxelSize = 0.0f;
    cfg.interactiveMesherConfig.resolutionX = 12;
    cfg.interactiveMesherConfig.resolutionY = 12;
    cfg.interactiveMesherConfig.resolutionZ = 12;

    MonsterVisualAsync* asyncMgr = MonsterVisualAsync_Create(cfg);
    TEST_ASSERT(asyncMgr != NULL, "asyncMgr no debe ser NULL");

    bool firstUpdate = MonsterVisualAsync_Update(asyncMgr, &lizard, 0.016f);
    TEST_ASSERT(!firstUpdate, "Update inicial no debe retornar true antes de que el worker procese");

    MonsterVisualAsync_Flush(asyncMgr);

    const Mesh* mesh = MonsterVisualAsync_GetDisplayMesh(asyncMgr);
    TEST_ASSERT(mesh != NULL && mesh->vertexCount > 0, "Malla expuesta tras Flush vacía");
    TEST_ASSERT(MonsterVisualAsync_GetDisplayGeneration(asyncMgr) == 1, "Generación expuesta debe ser 1 tras Flush");
    TEST_ASSERT(MonsterVisualAsync_GetDisplayEyeCount(asyncMgr) == 1, "Conteo de ojos expuesto debe ser 1");

    MonsterVisualAsyncStats stats = MonsterVisualAsync_GetStats(asyncMgr);
    TEST_ASSERT(stats.completedBuildCount >= 1, "Build count debe ser al menos 1");

    MonsterVisualAsync_Free(asyncMgr);
    Monster_Free(&lizard);

    printf("[PASS] test_visual_async_lifecycle_and_flush\n");
}

void test_visual_async_coalescing(void) {
    Monster lizard = BuildLizard();

    MonsterVisualAsyncConfig cfg = MonsterVisualAsync_DefaultConfig();
    cfg.interactiveMesherConfig.voxelSize = 0.0f;
    cfg.interactiveMesherConfig.resolutionX = 16;
    cfg.interactiveMesherConfig.resolutionY = 16;
    cfg.interactiveMesherConfig.resolutionZ = 16;

    MonsterVisualAsync* asyncMgr = MonsterVisualAsync_Create(cfg);

    /* Enviar múltiples updates rápidos sin retardo para que el worker no alcance a procesarlos todos */
    for (int i = 0; i < 20; ++i) {
        BodyPart* head = Monster_GetHead(&lizard);
        if (head) head->positionRender.x += 0.05f;
        MonsterVisualAsync_Update(asyncMgr, &lizard, 0.001f);
    }

    MonsterVisualAsync_Flush(asyncMgr);

    MonsterVisualAsyncStats stats = MonsterVisualAsync_GetStats(asyncMgr);
    TEST_ASSERT(stats.requestCount >= 20, "Múltiples solicitudes deben ser registradas");
    TEST_ASSERT(stats.coalescedCount > 0, "Solicitudes intermedias rápidas deben ser coalescidas");

    MonsterVisualAsync_Free(asyncMgr);
    Monster_Free(&lizard);

    printf("[PASS] test_visual_async_coalescing\n");
}

void test_visual_async_quality_tiers(void) {
    Monster lizard = BuildLizard();

    MonsterVisualAsyncConfig cfg = MonsterVisualAsync_DefaultConfig();
    cfg.interactiveMesherConfig.voxelSize = 0.0f;
    cfg.interactiveMesherConfig.resolutionX = 10;
    cfg.interactiveMesherConfig.resolutionY = 10;
    cfg.interactiveMesherConfig.resolutionZ = 10;

    cfg.settledMesherConfig.voxelSize = 0.0f;
    cfg.settledMesherConfig.resolutionX = 16;
    cfg.settledMesherConfig.resolutionY = 16;
    cfg.settledMesherConfig.resolutionZ = 16;
    cfg.settledDelaySec = 0.10f;

    MonsterVisualAsync* asyncMgr = MonsterVisualAsync_Create(cfg);

    /* 1. Cambio reciente -> Tier INTERACTIVE */
    MonsterVisualAsync_Update(asyncMgr, &lizard, 0.016f);
    MonsterVisualAsync_Flush(asyncMgr);

    MonsterVisualAsyncStats statsInteractive = MonsterVisualAsync_GetStats(asyncMgr);
    TEST_ASSERT(statsInteractive.activeQualityTier == MONSTER_VISUAL_QUALITY_INTERACTIVE, "Debe estar en tier INTERACTIVE tras cambio reciente");

    /* 2. Simular sin movimiento por más tiempo que settledDelaySec -> Transición a SETTLED */
    MonsterVisualAsync_Update(asyncMgr, &lizard, 0.15f);
    MonsterVisualAsync_Flush(asyncMgr);

    MonsterVisualAsyncStats statsSettled = MonsterVisualAsync_GetStats(asyncMgr);
    TEST_ASSERT(statsSettled.activeQualityTier == MONSTER_VISUAL_QUALITY_SETTLED, "Debe pasar a tier SETTLED tras settledDelaySec sin cambios");

    MonsterVisualAsync_Free(asyncMgr);
    Monster_Free(&lizard);

    printf("[PASS] test_visual_async_quality_tiers\n");
}

void run_visual_async_tests(void) {
    test_visual_async_lifecycle_and_flush();
    test_visual_async_coalescing();
    test_visual_async_quality_tiers();
}
