/**
 * @file test_morph_cross.c
 * @brief Pruebas de verificación cruzada de metamorfosis continua entre pares arbitrarios X -> Y.
 * @author Monster Engine Team
 * @date 2026
 */

#include "test_utils.h"
#include "Creature.h"
#include "CreatureRecipes.h"
#include "CreatureMorph.h"
#include "MonsterAger.h"
#include "MonsterSDF.h"
#include "MonsterVisualAsync.h"
#include "MorphSpatialCache.h"
#include "MorphChunkManager.h"
#include "AnatomyDeformer.h"
#include "SDFMesher.h"
#include "AxialBodySprawlingTetrapod.h"
#include "AxialBody.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static CreatureRecipe CreateSnakeRecipe(void) {
    CreatureRecipe r = *CreatureRecipes_Lizard();
    r.id = 901;
    r.name = "Serpiente Ápoda";
    r.gait = CREATURE_GAIT_NONE;
    r.adult.limbCount = 0;
    r.adult.tailCount = 1;
    r.adult.ornamentCount = 0;
    r.adult.axial.trunkLength = 7.0f;
    r.adult.axial.shoulderWidth = 0.6f;
    r.adult.axial.thoraxWidth = 0.7f;
    r.adult.axial.abdomenWidth = 0.7f;
    r.adult.axial.pelvicWidth = 0.6f;
    r.adult.tails[0].length = 8.0f;
    r.seed = r.larva = r.juvenile = r.adult;
    r.bodyPlan = (BodyPlan){
        .root = {.moduleInstanceId = 1, .localNodeId = AXIAL_NODE_PELVIS},
        .moduleCount = 3,
        .modules = {
            {.instanceId = 1, .kind = CREATURE_MODULE_AXIAL},
            {.instanceId = 2, .kind = CREATURE_MODULE_HEAD,
             .attachment = {.hostModuleInstanceId = 1, .slotId = AXIAL_SPRAWLING_SLOT_CERVICAL}},
            {.instanceId = 20, .kind = CREATURE_MODULE_TAIL,
             .attachment = {.hostModuleInstanceId = 1, .slotId = AXIAL_SPRAWLING_SLOT_CAUDAL}}
        }
    };
    return r;
}

static CreatureRecipe CreateBipedRecipe(void) {
    CreatureRecipe r = *CreatureRecipes_Dog();
    r.id = 902;
    r.name = "Bípedo Erecto";
    r.gait = CREATURE_GAIT_NONE;
    r.adult.limbCount = 2;
    r.adult.limbs[0] = r.adult.limbs[2];
    r.adult.limbs[1] = r.adult.limbs[3];
    r.adult.tailCount = 1;
    r.adult.ornamentCount = 0;
    r.adult.axial.withersElevation = 0.35f;
    r.seed = r.larva = r.juvenile = r.adult;
    r.bodyPlan = (BodyPlan){
        .root = {.moduleInstanceId = 1, .localNodeId = AXIAL_NODE_PELVIS},
        .moduleCount = 5,
        .modules = {
            {.instanceId = 1, .kind = CREATURE_MODULE_AXIAL},
            {.instanceId = 2, .kind = CREATURE_MODULE_HEAD,
             .attachment = {.hostModuleInstanceId = 1, .slotId = AXIAL_SLOT_CERVICAL}},
            {.instanceId = 20, .kind = CREATURE_MODULE_TAIL,
             .attachment = {.hostModuleInstanceId = 1, .slotId = AXIAL_SLOT_CAUDAL}},
            {.instanceId = 12, .kind = CREATURE_MODULE_LIMB,
             .attachment = {.hostModuleInstanceId = 1, .slotId = AXIAL_SLOT_PELVIC_LEFT}, .phenotypeIndex = 0},
            {.instanceId = 13, .kind = CREATURE_MODULE_LIMB,
             .attachment = {.hostModuleInstanceId = 1, .slotId = AXIAL_SLOT_PELVIC_RIGHT}, .phenotypeIndex = 1}
        }
    };
    return r;
}

/**
 * @brief Verifica la transición continua entre un cuadrúpedo (perro) y un ápodo (serpiente),
 * comprobando retracción de extremidades, detección de topología y remallado sin discontinuidades.
 */
static void test_morph_cross_quadruped_to_snake(void) {
    Monster dog = Monster_Create();
    Monster snake = Monster_Create();
    CreatureRecipe snakeRecipe = CreateSnakeRecipe();
    CreaturePhenotype dp = CreatureRecipes_Dog()->adult;
    CreaturePhenotype sp = snakeRecipe.adult;

    TEST_ASSERT(Creature_BuildMonster(&dog, CreatureRecipes_Dog(), &dp), "Construir perro");
    TEST_ASSERT(Creature_BuildMonster(&snake, &snakeRecipe, &sp), "Construir serpiente");

    /* 1. Recolección de eventos de topología: las 4 patas del perro deben reportarse */
    CreatureMorphTopologyEvent events[16];
    size_t eventCount = CreatureMorph_CollectTopologyEvents(&dog, &snake, events, 16);
    TEST_ASSERT(eventCount >= 4, "No se detectó la desaparición de las 4 extremidades");

    bool foundLimb10 = false, foundLimb11 = false, foundLimb12 = false, foundLimb13 = false;
    for (size_t i = 0; i < eventCount; ++i) {
        if (events[i].moduleInstanceId == 10) foundLimb10 = true;
        if (events[i].moduleInstanceId == 11) foundLimb11 = true;
        if (events[i].moduleInstanceId == 12) foundLimb12 = true;
        if (events[i].moduleInstanceId == 13) foundLimb13 = true;
    }
    TEST_ASSERT(foundLimb10 && foundLimb11 && foundLimb12 && foundLimb13,
        "No se reportaron todas las instancias de miembros en la metamorfosis");

    /* 2. Interpolación continua en MonsterAger */
    MonsterAger ager = MonsterAger_Create(&dog, &snake, 0.0f);
    MonsterSDF sdf = MonsterSDF_Create();
    const float testAges[] = {0.0f, 0.25f, 0.50f, 0.75f, 1.0f};

    for (size_t i = 0; i < sizeof(testAges)/sizeof(testAges[0]); ++i) {
        MonsterAger_SetPerc(&ager, testAges[i]);
        const Monster* intermediate = MonsterAger_GetResultConst(&ager);
        TEST_ASSERT(intermediate != NULL, "Monstruo intermedio nulo");
        TEST_ASSERT(intermediate->hasAnatomyGraph, "Falta grafo anatómico en monstruo intermedio");
        TEST_ASSERT(AnatomyGraph_Validate(&intermediate->anatomyGraph),
            "Grafo anatómico intermedio inválido");

        /* Construcción de SDF del estado intermedio */
        TEST_ASSERT(MonsterSDF_Build(&sdf, intermediate, MonsterSDF_DefaultConfig()),
            "Error al construir SDF intermedio perro -> serpiente");
        TEST_ASSERT(sdf.hasSurface, "SDF intermedio no detectó superficie");
        TEST_ASSERT(isfinite(sdf.bounds.start.x) && isfinite(sdf.bounds.end.x),
            "Límites de SDF intermedio no finitos");
    }

    MonsterSDF_Free(&sdf);

    /* 3. Comprobación del gestor asíncrono con remallado incremental por chunks */
    MonsterVisualAsyncConfig config = MonsterVisualAsync_DefaultConfig();
    config.enableChunkedRemeshing = true;
    config.chunkSize = 0.5f;
    config.morphMesherConfig.voxelSize = 0.08f;
    MonsterVisualAsync* visual = MonsterVisualAsync_Create(config);
    TEST_ASSERT(visual != NULL, "Crear MonsterVisualAsync");
    TEST_ASSERT(MonsterVisualAsync_SetMorphEndpoints(visual, &dog, &snake),
        "Configurar extremos de morph en visual asíncrono");
    MonsterVisualAsync_SetMorphMode(visual, true);

    /* Simular avance con latencia de worker */
    MonsterVisualAsync_SetWorkerDelay(visual, 100);
    MonsterAger_SetPerc(&ager, 0.5f);
    MonsterVisualAsync_UpdateWithAppearance(visual,
        MonsterAger_GetGeometryResultConst(&ager),
        MonsterAger_GetResultConst(&ager),
        0.016f);

    /* Verificar que los límites de fallback delimitan la región sin desbordamiento */
    AABB3D fallbackBox;
    if (MonsterVisualAsync_GetFallbackBounds(visual, 0.5f, 0.05f, &fallbackBox)) {
        TEST_ASSERT(isfinite(fallbackBox.start.x) && isfinite(fallbackBox.end.x),
            "AABB de fallback regional contiene valores no finitos");
        TEST_ASSERT(fallbackBox.end.x >= fallbackBox.start.x, "AABB de fallback invertido");
    }

    /* Verificar que la malla de presentación es válida y sin NaN */
    const Mesh* disp = MonsterVisualAsync_GetDisplayMesh(visual);
    if (disp && disp->vertexCount > 0) {
        for (size_t v = 0; v < disp->vertexCount; ++v) {
            TEST_ASSERT(isfinite(disp->vertices[v].position.x) &&
                        isfinite(disp->vertices[v].position.y) &&
                        isfinite(disp->vertices[v].position.z),
                        "Vértice no finito en malla durante perro -> serpiente");
        }
    }

    MonsterVisualAsync_Free(visual);
    MonsterAger_Free(&ager);
    Monster_Free(&dog);
    Monster_Free(&snake);
    printf("[PASS] test_morph_cross_quadruped_to_snake\n");
}

/**
 * @brief Verifica la transición continua entre un cuadrúpedo (perro) y un bípedo erguido,
 * comprobando que los miembros anteriores se repliegan mientras los posteriores se conservan.
 */
static void test_morph_cross_quadruped_to_biped(void) {
    Monster dog = Monster_Create();
    Monster biped = Monster_Create();
    CreatureRecipe bipedRecipe = CreateBipedRecipe();
    CreaturePhenotype dp = CreatureRecipes_Dog()->adult;
    CreaturePhenotype bp = bipedRecipe.adult;

    TEST_ASSERT(Creature_BuildMonster(&dog, CreatureRecipes_Dog(), &dp), "Construir perro");
    TEST_ASSERT(Creature_BuildMonster(&biped, &bipedRecipe, &bp), "Construir bípedo");

    /* 1. Recolección de eventos de topología: miembros anteriores (10, 11) cambian; posteriores (12, 13) persisten */
    CreatureMorphTopologyEvent events[16];
    size_t eventCount = CreatureMorph_CollectTopologyEvents(&dog, &biped, events, 16);
    TEST_ASSERT(eventCount >= 2, "Debe haber al menos 2 eventos topológicos (miembros anteriores)");

    bool anteriorChanged = false;
    for (size_t i = 0; i < eventCount; ++i) {
        if (events[i].moduleInstanceId == 10 || events[i].moduleInstanceId == 11) {
            anteriorChanged = true;
        }
    }
    TEST_ASSERT(anteriorChanged, "Los miembros anteriores no registraron evento de cambio topológico");

    /* 2. Prueba con MorphChunkManager para remallado incremental */
    MonsterAger ager = MonsterAger_Create(&dog, &biped, 0.0f);
    MonsterSDF sdf = MonsterSDF_Create();
    MonsterAger_SetPerc(&ager, 0.5f);
    const Monster* intermediate = MonsterAger_GetResultConst(&ager);
    TEST_ASSERT(MonsterSDF_Build(&sdf, intermediate, MonsterSDF_DefaultConfig()), "SDF perro -> bípedo");

    SDFField field = MonsterSDF_GetField(&sdf);
    SDFMesher mesher = SDFMesher_Create(SDFMesher_DefaultConfig());
    mesher.config.adaptiveDetail = false;
    mesher.config.voxelSize = 0.08f;

    MorphChunkManager* chunkMgr = MorphChunkManager_Create(0.5f, 0.08f);
    AABB3D bounds = MonsterSDF_GetBounds(&sdf);

    size_t marked = MorphChunkManager_MarkDirtyAABB(chunkMgr, bounds, MORPH_DIRT_DEFORMATION);
    TEST_ASSERT(marked > 0, "Chunks marcados para perro -> bípedo");

    size_t remeshed = MorphChunkManager_UpdateDirty(chunkMgr, &mesher, &field, 1);
    TEST_ASSERT(remeshed == marked, "No se remallaron chunks en el estado intermedio cuadrúpedo -> bípedo");

    /* Ensamblar con soldadura de costuras */
    Mesh assembled = Mesh_Create();
    TEST_ASSERT(MorphChunkManager_AssembleMesh(chunkMgr, &assembled, true),
        "Fallo al ensamblar malla por chunks con soldadura de costuras");
    TEST_ASSERT(assembled.vertexCount > 0 && assembled.indexCount > 0,
        "Malla ensamblada vacía");
    MeshValidationResult val = Mesh_Validate(&assembled);
    TEST_ASSERT(val.valid,
        "La malla ensamblada perro -> bípedo contiene anomalías topológicas");

    Mesh_Free(&assembled);
    MorphChunkManager_Destroy(chunkMgr);
    SDFMesher_Free(&mesher);
    MonsterSDF_Free(&sdf);
    MonsterAger_Free(&ager);
    Monster_Free(&dog);
    Monster_Free(&biped);
    printf("[PASS] test_morph_cross_quadruped_to_biped\n");
}

/**
 * @brief Verifica la reversibilidad del pipeline en la dirección serpiente -> perro (brote de extremidades).
 */
static void test_morph_cross_snake_to_quadruped_reversible(void) {
    Monster dog = Monster_Create();
    Monster snake = Monster_Create();
    CreatureRecipe snakeRecipe = CreateSnakeRecipe();
    CreaturePhenotype dp = CreatureRecipes_Dog()->adult;
    CreaturePhenotype sp = snakeRecipe.adult;

    TEST_ASSERT(Creature_BuildMonster(&dog, CreatureRecipes_Dog(), &dp), "Construir perro");
    TEST_ASSERT(Creature_BuildMonster(&snake, &snakeRecipe, &sp), "Construir serpiente");

    /* Metamorfosis inversa: Serpiente -> Perro */
    MonsterAger ager = MonsterAger_Create(&snake, &dog, 0.0f);
    MonsterSDF sdf = MonsterSDF_Create();

    const float ages[] = {0.1f, 0.5f, 0.9f};
    for (size_t i = 0; i < sizeof(ages)/sizeof(ages[0]); ++i) {
        MonsterAger_SetPerc(&ager, ages[i]);
        const Monster* intermediate = MonsterAger_GetResultConst(&ager);
        TEST_ASSERT(MonsterSDF_Build(&sdf, intermediate, MonsterSDF_DefaultConfig()),
            "Error en SDF de metamorfosis inversa serpiente -> perro");
        TEST_ASSERT(sdf.hasSurface, "SDF sin superficie en metamorfosis inversa");
    }

    MonsterSDF_Free(&sdf);
    MonsterAger_Free(&ager);
    Monster_Free(&dog);
    Monster_Free(&snake);
    printf("[PASS] test_morph_cross_snake_to_quadruped_reversible\n");
}

/**
 * @brief Verifica la compatibilidad heterocrónica: crecimiento ontogenético (larva a adulto)
 * combinado con transición de especie (larva de lagarto a perro adulto).
 */
static void test_morph_cross_ontogeny_compatibility(void) {
    Monster larvaLizard = Monster_Create();
    Monster adultDog = Monster_Create();
    CreaturePhenotype lp = CreatureRecipes_Lizard()->larva;
    CreaturePhenotype dp = CreatureRecipes_Dog()->adult;

    TEST_ASSERT(Creature_BuildMonster(&larvaLizard, CreatureRecipes_Lizard(), &lp), "Construir larva");
    TEST_ASSERT(Creature_BuildMonster(&adultDog, CreatureRecipes_Dog(), &dp), "Construir perro adulto");

    MonsterAger ager = MonsterAger_Create(&larvaLizard, &adultDog, 0.0f);
    MonsterSDF sdf = MonsterSDF_Create();

    /* Comprobar interpolación en 3 puntos */
    const float ages[] = {0.2f, 0.5f, 0.8f};
    for (size_t i = 0; i < sizeof(ages)/sizeof(ages[0]); ++i) {
        MonsterAger_SetPerc(&ager, ages[i]);
        const Monster* intermediate = MonsterAger_GetResultConst(&ager);
        TEST_ASSERT(MonsterSDF_Build(&sdf, intermediate, MonsterSDF_DefaultConfig()),
            "Error en SDF de transición larva de lagarto -> perro adulto");
        TEST_ASSERT(sdf.hasSurface, "SDF sin superficie en transición ontogenética cruzada");
    }

    MonsterSDF_Free(&sdf);
    MonsterAger_Free(&ager);
    Monster_Free(&larvaLizard);
    Monster_Free(&adultDog);
    printf("[PASS] test_morph_cross_ontogeny_compatibility\n");
}

void run_morph_cross_tests(void) {
    test_morph_cross_quadruped_to_snake();
    test_morph_cross_quadruped_to_biped();
    test_morph_cross_snake_to_quadruped_reversible();
    test_morph_cross_ontogeny_compatibility();
}
