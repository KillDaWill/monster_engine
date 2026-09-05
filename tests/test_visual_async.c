#include "test_utils.h"
#include "MonsterVisualAsync.h"
#include "Monster.h"
#include "MonsterAger.h"
#include "RenderInterfaces.h"
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

/* Contador para el renderer falso — estrecho, sólo cuenta invocaciones de renderMesh */
static int s_fakeRenderMeshCalls = 0;

static void FakeRenderMeshCounter(Renderer3D* self, const Mesh* mesh) {
    (void)mesh;
    if (self && self->user_data) {
        (*(int*)self->user_data)++;
    } else {
        s_fakeRenderMeshCalls++;
    }
}

void test_visual_async_renders_jaw_components(void) {
    Monster lizard = BuildLizard();

    /* Añadir segundo ojo para fixture 2-ojos explícito */
    Eye rightEye = Eye_Create(0, Vec3_Create(0.75f, 0.35f, 1.1f), Vec3_Create(0.42f, 0.4f, 0.25f), COLOR_WHITE, Color_FromRGB(20, 20, 20));
    Monster_AddEye(&lizard, rightEye);

    /* Una boca anclada a la cabeza: mandíbula y bisagra separadas. */
    Mouth mouth = Mouth_Create(0, Vec3_Create(0.0f, -0.20f, 0.90f), Vec3_Create(0.70f, 0.45f, 0.50f), Color_FromRGB(40, 10, 10), Color_FromRGB(200, 50, 50));
    Monster_AddMouth(&lizard, mouth);

    TEST_ASSERT(lizard.mouthCount == 1, "Fixture debe tener 1 boca");
    TEST_ASSERT(lizard.eyeCount == 2, "Fixture debe tener 2 ojos");

    MonsterVisualAsyncConfig cfg = MonsterVisualAsync_DefaultConfig();
    cfg.interactiveMesherConfig.voxelSize = 0.0f;
    cfg.interactiveMesherConfig.resolutionX = 12;
    cfg.interactiveMesherConfig.resolutionY = 12;
    cfg.interactiveMesherConfig.resolutionZ = 12;

    MonsterVisualAsync* asyncMgr = MonsterVisualAsync_Create(cfg);
    TEST_ASSERT(asyncMgr != NULL, "asyncMgr no debe ser NULL");

    /* Ciclo determinista sin sleeps: Update encola + Flush bloquea hasta que el worker termine */
    MonsterVisualAsync_Update(asyncMgr, &lizard, 0.016f);
    MonsterVisualAsync_Flush(asyncMgr);

    const Mesh* bodyMesh = MonsterVisualAsync_GetDisplayMesh(asyncMgr);
    TEST_ASSERT(bodyMesh != NULL && bodyMesh->vertexCount > 0, "Malla de cuerpo expuesta tras Flush vacía");
    TEST_ASSERT(MonsterVisualAsync_GetDisplayEyeCount(asyncMgr) == 2, "Conteo de ojos expuesto debe ser 2");

    /* Renderer falso — cuenta llamadas a renderMesh (contrato observado) */
    int renderCalls = 0;
    Renderer3D fakeRenderer;
    fakeRenderer.user_data = &renderCalls;
    fakeRenderer.beginFrame = NULL;
    fakeRenderer.endFrame = NULL;
    fakeRenderer.renderMesh = FakeRenderMeshCounter;

    bool renderOk = MonsterVisualAsync_Render(asyncMgr, &fakeRenderer);
    TEST_ASSERT(renderOk, "MonsterVisualAsync_Render debe retornar true con renderer válido");

    int expectedCalls = 1 + 2 * (int)lizard.mouthCount + 3 * (int)lizard.eyeCount;
    TEST_ASSERT(renderCalls == expectedCalls, "MonsterVisualAsync no renderiza mandíbula y bisagra");

    /* Validación explícita del faltante: sin bocas ni cabría el conteo de cuerpo+ojos */
    int bodyPlusEyes = 1 + 3 * (int)lizard.eyeCount;
    TEST_ASSERT(renderCalls != bodyPlusEyes, "Async omitió los componentes anatómicos de boca");

    MonsterVisualAsync_Free(asyncMgr);
    Monster_Free(&lizard);

    printf("[PASS] test_visual_async_renders_jaw_components\n");
}

void test_visual_async_growth_sweep(void) {
    /* Barrido determinista de crecimiento: verifica que el fix asíncrono de boca
       mantiene mandíbula y bisagra válidas a lo largo de toda la curva 0.0 -> 1.0.
       Usa dos endpoints reales (joven/adulto) con parámetros de boca
       explícitamente distintos para que la geometría interpolada cambie. */
    Monster young = BuildLizard();
    Monster adult = BuildLizard();

    /* Boca joven: pequeña y casi cerrada */
    Mouth mouthYoung = Mouth_Create(0, Vec3_Create(0.0f, -0.20f, 0.90f), Vec3_Create(0.50f, 0.30f, 0.35f),
                                   Color_FromRGB(40, 10, 10), Color_FromRGB(200, 50, 50));
    Mouth_SetOpenFactor(&mouthYoung, 0.05f);
    mouthYoung.slitThickness = 0.04f;
    mouthYoung.slitSoftness = 0.15f;
    mouthYoung.cornerRadius = 0.02f;
    Monster_AddMouth(&young, mouthYoung);

    /* Boca adulta: grande y abierta, labios más gruesos y curvatura distinta */
    Mouth mouthAdult = Mouth_Create(0, Vec3_Create(0.0f, -0.20f, 0.90f), Vec3_Create(0.95f, 0.65f, 0.65f),
                                   Color_FromRGB(80, 15, 15), Color_FromRGB(220, 80, 80));
    Mouth_SetOpenFactor(&mouthAdult, 0.85f);
    mouthAdult.slitThickness = 0.09f;
    mouthAdult.slitSoftness = 0.55f;
    mouthAdult.cornerRadius = 0.05f;
    Monster_AddMouth(&adult, mouthAdult);

    TEST_ASSERT(young.mouthCount == 1, "Endpoint joven debe tener 1 boca");
    TEST_ASSERT(adult.mouthCount == 1, "Endpoint adulto debe tener 1 boca");

    MonsterAger ager = MonsterAger_Create(&young, &adult, 0.0f);

    MonsterVisualAsyncConfig cfg = MonsterVisualAsync_DefaultConfig();
    cfg.interactiveMesherConfig.voxelSize = 0.0f;
    cfg.interactiveMesherConfig.resolutionX = 12;
    cfg.interactiveMesherConfig.resolutionY = 12;
    cfg.interactiveMesherConfig.resolutionZ = 12;
    MonsterVisualAsync* asyncMgr = MonsterVisualAsync_Create(cfg);
    TEST_ASSERT(asyncMgr != NULL, "asyncMgr no debe ser NULL en barrido de crecimiento");

    /* Edades exactas solicitadas — deterministas, sin sleeps extra */
    const float ages[] = {0.00f, 0.10f, 0.25f, 0.50f, 0.75f, 0.90f, 1.00f};
    const size_t ageCount = sizeof(ages) / sizeof(ages[0]);

    printf("[TRACE] growth sweep: edad -> mandíbula/bisagra (vertexCount)\n");
    for (size_t i = 0; i < ageCount; ++i) {
        float age = ages[i];
        MonsterAger_SetPerc(&ager, age);
        const Monster* cur = MonsterAger_GetResultConst(&ager);
        TEST_ASSERT(cur != NULL, "MonsterAger result no debe ser NULL");

        MonsterVisualAsync_Update(asyncMgr, cur, 0.016f);
        MonsterVisualAsync_Flush(asyncMgr);

        size_t mouthCount = MonsterVisualAsync_GetDisplayMouthCount(asyncMgr);
        TEST_ASSERT(mouthCount == 1, "Display mouth count debe ser 1 en cada edad del barrido");

        size_t vcs[2] = {0, 0};
        for (size_t mi = 0; mi < 2; ++mi) {
            const Mesh* m = MonsterVisualAsync_GetDisplayMouthMesh(asyncMgr, 0, mi);
            TEST_ASSERT(m != NULL, "Malla de boca expuesta no debe ser NULL");
            TEST_ASSERT(m->vertexCount > 0, "Malla de boca no debe estar vacía");
            TEST_ASSERT(m->indexCount > 0, "Malla de boca debe tener índices");
            MeshValidationResult vr = Mesh_Validate(m);
            if (!vr.valid) {
                printf("[TRACE] invalid-mouth age=%.2f mesh=%zu vertices=%zu indices=%zu invalid=%zu degenerate=%zu zero-area=%zu nonfinite-v=%zu nonfinite-n=%zu\n",
                       age, mi, m->vertexCount, m->indexCount, vr.invalidIndexCount,
                       vr.degenerateTriangleCount, vr.zeroAreaTriangleCount,
                        vr.nonFiniteVertexCount, vr.nonFiniteNormalCount);
            }
            TEST_ASSERT(vr.valid, "Mesh_Validate debe ser válido para cada malla de boca");
            vcs[mi] = m->vertexCount;
        }
        printf("[TRACE] age=%.2f v=[%zu %zu]\n", age, vcs[0], vcs[1]);
    }

    MonsterVisualAsync_Free(asyncMgr);
    MonsterAger_Free(&ager);
    Monster_Free(&young);
    Monster_Free(&adult);

    printf("[PASS] test_visual_async_growth_sweep\n");
}

static void test_visual_async_conforming_head(void) {
    Monster lizard=Monster_Create();LizardPhenotype phenotype=LizardPreset_Adult();
    TEST_ASSERT(Lizard_BuildMonster(&lizard,&phenotype),"No se construyó el adulto anatómico asíncrono");
    MonsterVisualAsyncConfig cfg=MonsterVisualAsync_DefaultConfig();cfg.settledDelaySec=.10f;
    MonsterVisualAsync* asyncMgr=MonsterVisualAsync_Create(cfg);
    TEST_ASSERT(asyncMgr!=NULL,"No se creó el gestor para cabeza local");
    MonsterVisualAsync_Update(asyncMgr,&lizard,.016f);MonsterVisualAsync_Flush(asyncMgr);
    const Mesh* body=MonsterVisualAsync_GetDisplayMesh(asyncMgr);
    const Mesh* head=MonsterVisualAsync_GetDisplayHeadMesh(asyncMgr);
    MonsterVisualAsyncStats interactive=MonsterVisualAsync_GetStats(asyncMgr);
    TEST_ASSERT(body&&body->vertexCount>0&&head&&head->vertexCount==0,"Async no publicó la superficie craneocervical única");
    TEST_ASSERT(interactive.bodyMesher.refinedCellCount>0&&interactive.bodyMesher.minimumVoxelSize<interactive.bodyMesher.effectiveVoxelSize*.5f,
                "El tier interactivo no conserva más detalle cefálico que corporal");
    MonsterVisualAsync_Update(asyncMgr,&lizard,.15f);MonsterVisualAsync_Flush(asyncMgr);
    MonsterVisualAsyncStats settled=MonsterVisualAsync_GetStats(asyncMgr);
    TEST_ASSERT(settled.activeQualityTier==MONSTER_VISUAL_QUALITY_SETTLED&&
                settled.bodyMesher.refinedCellCount>interactive.bodyMesher.refinedCellCount,
                "El tier settled no refina de forma independiente la cabeza");
    int renderCalls=0;Renderer3D renderer={0};renderer.user_data=&renderCalls;renderer.renderMesh=FakeRenderMeshCounter;
    TEST_ASSERT(MonsterVisualAsync_Render(asyncMgr,&renderer),"No se renderizó el snapshot particionado");
    TEST_ASSERT(renderCalls==1+2*(int)lizard.mouthCount+3*(int)lizard.eyeCount,
                "El renderer asíncrono omitió la cabeza local o duplicó el cuerpo");
    printf("  [debug] async cabeza interactiva %.5f/%zu settled %.5f/%zu\n",
           interactive.headMesher.effectiveVoxelSize,interactive.headMesher.cellCount,
           settled.headMesher.effectiveVoxelSize,settled.headMesher.cellCount);
    MonsterVisualAsync_Free(asyncMgr);Monster_Free(&lizard);
    printf("[PASS] test_visual_async_conforming_head\n");
}

void test_bug_async_growth_displays_old_body_with_new_jaw_hinge_snapshot_mixing(void) {
    /* REGRESIÓN: crecimiento async mezcla cuerpo/ojos viejos (T0) con mandíbula/bisagra nuevas (T1).
       Mecanismo confirmado: MonsterVisualAsync.c:392-419 intercambia bases snapshot de cuerpo/ojos/bocas
       y 442-444 siempre re-posea la mandíbula/bisagra con el Monster vivo. Un update de edad que cambia
       geometría (fingerprint distinto) debe mantener display intacto hasta el swap; si no, se muestra
       old-body/new-jaw mixing: generación sin cambiar pero vértices de jaw/hinge ya en pose T1. */

    /* Estados exactos/representativos vía MonsterAger joven/adulto con geometría distinta */
    Monster young = BuildLizard();
    Monster adult = BuildLizard();

    /* Cabeza distinta para que el fingerprint de cuerpo cambie entre edades */
    BodyPart* hy = Monster_GetHead(&young);
    BodyPart* ha = Monster_GetHead(&adult);
    if (hy) { hy->width = 1.0f; hy->height = 1.0f; hy->length = 1.0f; hy->widthRender = 1.0f; hy->heightRender = 1.0f; hy->lengthRender = 1.0f; }
    if (ha) { ha->width = 2.8f; ha->height = 2.2f; ha->length = 2.6f; ha->widthRender = 2.8f; ha->heightRender = 2.2f; ha->lengthRender = 2.6f; }

    /* Boca joven: pequeña y casi cerrada — geometría y pose T0 */
    Mouth mouthYoung = Mouth_Create(0, Vec3_Create(0.0f, -0.20f, 0.90f), Vec3_Create(0.50f, 0.30f, 0.35f),
                                   Color_FromRGB(40, 10, 10), Color_FromRGB(200, 50, 50));
    Mouth_SetOpenFactor(&mouthYoung, 0.05f);
    mouthYoung.slitThickness = 0.03f;
    mouthYoung.cornerRadius = 0.02f;
    mouthYoung.jawLength = 0.48f;
    mouthYoung.jawWidth = 0.42f;
    mouthYoung.hingeRadius = 0.07f;
    Monster_AddMouth(&young, mouthYoung);

    /* Boca adulta: grande y abierta, parámetros que cambian fingerprint — geometría y pose T1 */
    Mouth mouthAdult = Mouth_Create(0, Vec3_Create(0.0f, -0.20f, 0.90f), Vec3_Create(0.95f, 0.65f, 0.65f),
                                   Color_FromRGB(80, 15, 15), Color_FromRGB(220, 80, 80));
    Mouth_SetOpenFactor(&mouthAdult, 0.90f);
    mouthAdult.slitThickness = 0.09f;
    mouthAdult.cornerRadius = 0.06f;
    mouthAdult.jawLength = 0.95f;
    mouthAdult.jawWidth = 0.85f;
    mouthAdult.hingeRadius = 0.15f;
    Monster_AddMouth(&adult, mouthAdult);

    TEST_ASSERT(young.mouthCount == 1, "Endpoint joven debe tener 1 boca");
    TEST_ASSERT(adult.mouthCount == 1, "Endpoint adulto debe tener 1 boca");

    MonsterAger ager = MonsterAger_Create(&young, &adult, 0.0f);

    /* Configuración costosa para determinismo: worker tarda lo suficiente para que no haya swap inmediato.
       Además settledDelay grande evita cambio de tier entre los dos Updates. */
    MonsterVisualAsyncConfig cfg = MonsterVisualAsync_DefaultConfig();
    cfg.interactiveMesherConfig.voxelSize = 0.0f;
    cfg.interactiveMesherConfig.resolutionX = 32;
    cfg.interactiveMesherConfig.resolutionY = 32;
    cfg.interactiveMesherConfig.resolutionZ = 32;
    cfg.interactiveMesherConfig.maxCells = 500000;
    cfg.settledMesherConfig = cfg.interactiveMesherConfig;
    cfg.settledDelaySec = 10.0f;

    MonsterVisualAsync* asyncMgr = MonsterVisualAsync_Create(cfg);
    TEST_ASSERT(asyncMgr != NULL, "asyncMgr no debe ser NULL en test snapshot-mixing");

    /* 1) Flush de la edad conocida T0 (joven) — snapshot mostrado estable */
    MonsterAger_SetPerc(&ager, 0.0f);
    const Monster* curYoung = MonsterAger_GetResultConst(&ager);
    MonsterVisualAsync_Update(asyncMgr, curYoung, 0.016f);
    MonsterVisualAsync_Flush(asyncMgr);

    uint64_t genT0 = MonsterVisualAsync_GetDisplayGeneration(asyncMgr);
    TEST_ASSERT(genT0 != 0, "Generación tras Flush T0 debe ser no-cero");
    TEST_ASSERT(MonsterVisualAsync_GetDisplayMouthCount(asyncMgr) == 1, "Contrato two-piece: debe haber 1 boca mostrada en T0");

    const Mesh* jawT0 = MonsterVisualAsync_GetDisplayMouthMesh(asyncMgr, 0, 0);
    const Mesh* hingeT0 = MonsterVisualAsync_GetDisplayMouthMesh(asyncMgr, 0, 1);
    TEST_ASSERT(jawT0 != NULL && jawT0->vertexCount > 0, "Contrato two-piece: jaw T0 no debe ser NULL/vacía");
    TEST_ASSERT(hingeT0 != NULL && hingeT0->vertexCount > 0, "Contrato two-piece: hinge/bridge T0 no debe ser NULL/vacía");

    /* Capturar vértices representativos ligados al snapshot mostrado */
    Vector3 jawV0_T0 = jawT0->vertices[0].position;
    Vector3 hingeV0_T0 = hingeT0->vertices[0].position;
    size_t jawCountT0 = jawT0->vertexCount;
    size_t hingeCountT0 = hingeT0->vertexCount;

    /* 2) Emitir edad distinta T1 (adulta) con geometría cambiante SIN Flush — scheduling determinista */
    MonsterAger_SetPerc(&ager, 1.0f);
    const Monster* curAdult = MonsterAger_GetResultConst(&ager);
    bool swapped = MonsterVisualAsync_Update(asyncMgr, curAdult, 0.016f);

    /* Afirmar explícitamente que no hubo swap de display todavía */
    uint64_t genAfter = MonsterVisualAsync_GetDisplayGeneration(asyncMgr);
    TEST_ASSERT(genAfter == genT0, "Scheduling determinista falló: display generation cambió sin Flush; el worker ya hizo swap de snapshot (se esperaba T0 aún)");
    TEST_ASSERT(!swapped, "MonsterVisualAsync_Update no debe reportar swap de display sin Flush en este paso");

    /* 3) Mientras la generación mostrada sigue siendo T0, los vértices de jaw/hinge deben seguir ligados al snapshot T0.
       El bug re-posea con el Monster vivo T1 provocando old-body/new-jaw mixing. */
    const Mesh* jawAfter = MonsterVisualAsync_GetDisplayMouthMesh(asyncMgr, 0, 0);
    const Mesh* hingeAfter = MonsterVisualAsync_GetDisplayMouthMesh(asyncMgr, 0, 1);
    TEST_ASSERT(jawAfter != NULL && hingeAfter != NULL, "Mallas jaw/hinge no deben ser NULL tras Update sin Flush");
    /* Contrato two-piece: siguen siendo dos piezas separadas con la misma topología del snapshot T0 */
    TEST_ASSERT(jawAfter->vertexCount == jawCountT0, "Contrato two-piece: jaw vertexCount no debe cambiar sin swap de snapshot");
    TEST_ASSERT(hingeAfter->vertexCount == hingeCountT0, "Contrato two-piece: hinge vertexCount no debe cambiar sin swap de snapshot");

    Vector3 jawV0_after = jawAfter->vertices[0].position;
    Vector3 hingeV0_after = hingeAfter->vertices[0].position;

    float jawDelta = fabsf(jawV0_after.x - jawV0_T0.x) + fabsf(jawV0_after.y - jawV0_T0.y) + fabsf(jawV0_after.z - jawV0_T0.z);
    float hingeDelta = fabsf(hingeV0_after.x - hingeV0_T0.x) + fabsf(hingeV0_after.y - hingeV0_T0.y) + fabsf(hingeV0_after.z - hingeV0_T0.z);

    TEST_ASSERT(jawDelta < 1e-5f, "REGRESIÓN BUG: crecimiento async mezcla cuerpo/ojos viejos (T0) con mandíbula nueva (T1) — old-body/new-jaw mixing: display generation sigue en T0 pero jaw vertices fueron re-posed a la edad viva T1");
    TEST_ASSERT(hingeDelta < 1e-5f, "REGRESIÓN BUG: crecimiento async mezcla cuerpo/ojos viejos (T0) con bisagra/puente nuevo (T1) — old-body/new-hinge mixing: display generation sigue en T0 pero hinge/bridge vertices fueron re-posed a la edad viva T1");

    MonsterVisualAsync_Free(asyncMgr);
    MonsterAger_Free(&ager);
    Monster_Free(&young);
    Monster_Free(&adult);

    printf("[PASS] test_bug_async_growth_displays_old_body_with_new_jaw_hinge_snapshot_mixing\n");
}

static void test_visual_async_single_snapshot(void) {
    Monster m=Monster_Create();LizardPhenotype p=LizardPreset_Juvenile();
    TEST_ASSERT(Lizard_BuildMonster(&m,&p),"Snapshot juvenil inválido");
    MonsterVisualAsync* v=MonsterVisualAsync_Create(MonsterVisualAsync_DefaultConfig());
    TEST_ASSERT(v!=NULL,"No se creó el worker");
    MonsterVisualAsync_SetContinuousMotion(v,true);
    MonsterVisualAsync_Update(v,&m,0);
    for(int i=0;i<100;++i)MonsterVisualAsync_Update(v,&m,.1f);
    MonsterVisualAsync_Flush(v);
    MonsterVisualAsyncStats stats=MonsterVisualAsync_GetStats(v);
    TEST_ASSERT(stats.activeQualityTier==MONSTER_VISUAL_QUALITY_INTERACTIVE,"La animación continua disparó un asentamiento");
    TEST_ASSERT(stats.requestCount==1&&stats.completedBuildCount==1,"Se repitió un snapshot ya pendiente o en ejecución");
    TEST_ASSERT(stats.requestedFingerprint==stats.displayedFingerprint&&FLOAT_NEAR(stats.displayedScale,p.totalScale),"La telemetría no describe la malla mostrada");
    TEST_ASSERT(Mesh_Validate(MonsterVisualAsync_GetDisplayMesh(v)).watertight,"La malla unificada presenta fronteras abiertas");
    p=LizardPreset_Adult();Lizard_BuildMonster(&m,&p);MonsterVisualAsync_Update(v,&m,0);
    p=LizardPreset_Juvenile();Lizard_BuildMonster(&m,&p);MonsterVisualAsync_Update(v,&m,0);
    MonsterVisualAsync_Flush(v);stats=MonsterVisualAsync_GetStats(v);
    TEST_ASSERT(stats.requestedFingerprint==stats.displayedFingerprint&&FLOAT_NEAR(stats.displayedScale,p.totalScale),"Una generación obsoleta sustituyó el snapshot solicitado");
    MonsterVisualAsync_Free(v);Monster_Free(&m);
    printf("[PASS] test_visual_async_single_snapshot\n");
}

void run_visual_async_tests(void) {
    test_visual_async_lifecycle_and_flush();
    test_visual_async_coalescing();
    test_visual_async_quality_tiers();
    test_visual_async_renders_jaw_components();
    test_visual_async_growth_sweep();
    test_visual_async_conforming_head();
    test_visual_async_single_snapshot();
    test_bug_async_growth_displays_old_body_with_new_jaw_hinge_snapshot_mixing();
}
