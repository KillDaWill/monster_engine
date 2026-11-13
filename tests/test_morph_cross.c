/**
 * @file test_morph_cross.c
 * @brief Pruebas de verificación cruzada de metamorfosis continua entre pares arbitrarios X -> Y.
 * @author Monster Engine Team
 * @date 2026
 */

#include "test_utils.h"
#include "Creature.h"
#include "CreatureRecipes.h"
#include "CreatureRecipeRegistry.h"
#include "CreatureVariation.h"
#include "CreatureMorph.h"
#include "SurfaceMapper.h"
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

static bool HasSDFMaterial(const MonsterSDF* sdf, SDFMaterial mat) {
    for (size_t i = 0; i < sdf->connectorCount; ++i) {
        if (sdf->connectors[i].material == mat) return true;
    }
    return false;
}

static void AssertNearSurfaceParity(const MonsterSDF* actual, const MonsterSDF* expected, const char* msg) {
    Vector3 ext = Vec3_Sub(expected->bounds.end, expected->bounds.start);
    float maxDiff = 0.0f;
    for (int ix = 0; ix < 6; ++ix) {
        for (int iy = 0; iy < 6; ++iy) {
            for (int iz = 0; iz < 6; ++iz) {
                Vector3 pt = {
                    expected->bounds.start.x + ext.x * ((float)ix / 5.0f),
                    expected->bounds.start.y + ext.y * ((float)iy / 5.0f),
                    expected->bounds.start.z + ext.z * ((float)iz / 5.0f)
                };
                float da = MonsterSDF_EvaluateDistance(actual, pt);
                float db = MonsterSDF_EvaluateDistance(expected, pt);
                if (fminf(fabsf(da), fabsf(db)) < 0.25f) {
                    maxDiff = fmaxf(maxDiff, fabsf(da - db));
                }
            }
        }
    }
    TEST_ASSERT(maxDiff < 0.01f, msg);
}

static void BuildRandomizedVariant(const CreatureRecipe* base, uint32_t seed, Monster* out) {
    CreatureVariation v = CreatureVariation_Create("TestVariant", seed);
    CreatureTrait candidates[6] = {
        {.type = CREATURE_TRAIT_BODY_ROBUST, .strength = 0.8f},
        {.type = CREATURE_TRAIT_LIMBS_LONG, .strength = 0.7f},
        {.type = CREATURE_TRAIT_HORNS, .strength = 0.6f, .paramA = 0.65f, .paramB = 0.14f},
        {.type = CREATURE_TRAIT_TAIL_HEAVY, .strength = 0.7f},
        {.type = CREATURE_TRAIT_COLOR_PALETTE, .strength = 0.7f,
            .colorA = Color_FromRGB((uint8_t)(60 + (seed & 127u)), (uint8_t)(50 + ((seed >> 4) & 127u)), (uint8_t)(40 + ((seed >> 8) & 127u))),
            .colorB = Color_FromRGB((uint8_t)(120 + ((seed >> 2) & 127u)), (uint8_t)(110 + ((seed >> 6) & 127u)), (uint8_t)(90 + ((seed >> 10) & 127u)))},
        {.type = CREATURE_TRAIT_PATTERN_LAYER, .strength = 0.65f, .paramA = 1.0f, .paramB = 0.4f,
            .colorA = Color_FromRGB(45, 35, 30)}
    };
    for (size_t i = 0; i < 6; ++i) {
        if (CreatureVariation_IsTraitCompatible(&candidates[i], base, &base->adult)) {
            CreatureVariation_AddTrait(&v, candidates[i]);
        }
    }
    CreatureVariant variant = {0};
    TEST_ASSERT(CreatureVariation_Apply(base, &base->adult, &v, &variant),
        "Aplicar variación aleatoria compatible");
    TEST_ASSERT(Creature_BuildMonster(out, &variant.recipe, &variant.phenotype),
        "Construir criatura aleatoria variante");
}

static void EnsureCatalogLoaded(void) {
    if (CreatureRecipes_Find(1005) != NULL) return;
    char error[256] = {0};
    TEST_ASSERT(CreatureRecipeRegistry_LoadDirectory("recipes", error, sizeof(error)),
        "Cargar catálogo recipes/");
}

/**
 * @brief Verifica que cualquier par (especie o variante) X -> Y interpola y construye SDF válido.
 */
static void test_morph_cross_all_species_matrix(void) {
    EnsureCatalogLoaded();

    const CreatureRecipeId ids[] = {1, 2, 3, 4, 1000, 1005, 1006};
    enum { SPECIES_COUNT = 7, TOTAL_CREATURES = 9 };
    Monster pool[TOTAL_CREATURES];
    const char* names[TOTAL_CREATURES] = {0};

    for (size_t i = 0; i < SPECIES_COUNT; ++i) {
        const CreatureRecipe* rec = CreatureRecipes_Find(ids[i]);
        TEST_ASSERT(rec != NULL, "Receta del catálogo encontrada");
        pool[i] = Monster_Create();
        CreaturePhenotype p = rec->adult;
        TEST_ASSERT(Creature_BuildMonster(&pool[i], rec, &p), "Construir especie");
        names[i] = rec->name ? rec->name : "Receta";
    }
    pool[7] = Monster_Create();
    pool[8] = Monster_Create();
    BuildRandomizedVariant(CreatureRecipes_Find(1), 1337u, &pool[7]);
    BuildRandomizedVariant(CreatureRecipes_Find(1005), 4242u, &pool[8]);
    names[7] = "VarianteLagarto";
    names[8] = "VarianteCaballo";

    MonsterSDF targetSdfs[TOTAL_CREATURES];
    for (size_t i = 0; i < TOTAL_CREATURES; ++i) {
        targetSdfs[i] = MonsterSDF_Create();
        TEST_ASSERT(MonsterSDF_Build(&targetSdfs[i], &pool[i], MonsterSDF_DefaultConfig()),
            "Construir SDF objetivo de referencia");
    }

    MonsterSDF sdf = MonsterSDF_Create();
    const float ages[] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
    for (size_t a = 0; a < TOTAL_CREATURES; ++a) {
        for (size_t b = 0; b < TOTAL_CREATURES; ++b) {
            if (a == b) continue;
            MonsterAger ager = MonsterAger_Create(&pool[a], &pool[b], 0.5f);
            Monster reference = Monster_Clone(MonsterAger_GetResultConst(&ager));
            for (size_t k = 0; k < sizeof(ages)/sizeof(ages[0]); ++k) {
                MonsterAger_SetPerc(&ager, ages[k]);
                const Monster* cur = MonsterAger_GetResultConst(&ager);
                TEST_ASSERT(AnatomyGraph_Validate(&cur->anatomyGraph),
                    "Grafo anatómico válido en matriz X->Y");
                for (uint32_t limbMod = 10u; limbMod <= 13u; ++limbMod) {
                    for (uint16_t local = 1u; local <= 4u; ++local) {
                        const AnatomyNode* jn = AnatomyGraph_FindNode(
                            &cur->anatomyGraph, Anatomy_MakeId(limbMod, local));
                        TEST_ASSERT(jn != NULL && jn->development > 0.1f && jn->widthRadius > 1e-3f,
                            "Articulación de pata preservada sin atrofia durante X->Y");
                    }
                }
                for (size_t ni = 0; ni < cur->anatomyGraph.nodeCount; ++ni) {
                    const AnatomyNode* jn = &cur->anatomyGraph.nodes[ni];
                    if ((jn->region == ANATOMY_REGION_FORELIMB || jn->region == ANATOMY_REGION_HINDLIMB) &&
                        jn->role == ANATOMY_ROLE_JOINT) {
                        TEST_ASSERT(jn->moduleInstanceId >= 10u && jn->moduleInstanceId <= 13u,
                            "Sin módulos duplicados de patas durante X->Y");
                    }
                }
                TEST_ASSERT(MonsterSDF_Build(&sdf, cur, MonsterSDF_DefaultConfig()),
                    "MonsterSDF_Build válido en matriz X->Y");
                TEST_ASSERT(sdf.hasSurface, "SDF sin superficie en matriz X->Y");
                TEST_ASSERT(isfinite(sdf.bounds.start.x) && isfinite(sdf.bounds.end.z),
                    "Límites finitos en matriz X->Y");
                SurfaceMorphDomain domain = {0};
                TEST_ASSERT(SurfaceMapper_BuildMorphDomain(&domain, &cur->anatomyGraph,
                    &reference.anatomyGraph, &reference.surfaceMapping),
                    "Dominio superficial de morph válido");
                if (k == 4u) {
                    TEST_ASSERT(cur->head.anatomy.surface.isBeak == pool[b].head.anatomy.surface.isBeak &&
                        cur->head.anatomy.surface.sweptSkull == pool[b].head.anatomy.surface.sweptSkull &&
                        cur->head.anatomy.surface.hasCranialProfile == pool[b].head.anatomy.surface.hasCranialProfile &&
                        cur->head.anatomy.surface.hasMandibularProfile == pool[b].head.anatomy.surface.hasMandibularProfile,
                        "Rasgos craneales convergen exactamente al destino en t=1");
                    TEST_ASSERT(cur->surface.integument.type == pool[b].surface.integument.type,
                        "Tegumento converge exactamente al destino en t=1");
                    AssertNearSurfaceParity(&sdf, &targetSdfs[b],
                        "Superficie SDF final coincide con la especie destino en t=1");
                }
            }
            (void)names;
            Monster_Free(&reference);
            MonsterAger_Free(&ager);
        }
    }

    MonsterSDF_Free(&sdf);
    for (size_t i = 0; i < TOTAL_CREATURES; ++i) {
        MonsterSDF_Free(&targetSdfs[i]);
        Monster_Free(&pool[i]);
    }
    printf("[PASS] test_morph_cross_all_species_matrix (%d pares probados)\n",
        TOTAL_CREATURES * (TOTAL_CREATURES - 1));
}

/**
 * @brief Verifica materiales (cuernos/cascos), simetría de cobertura y ausencia de saltos en 0.5.
 */
static void test_morph_cross_materials_and_continuity(void) {
    EnsureCatalogLoaded();

    const CreatureRecipe* lizardRec = CreatureRecipes_Find(1);
    const CreatureRecipe* dogRec = CreatureRecipes_Find(2);
    const CreatureRecipe* brumaRec = CreatureRecipes_Find(3);
    const CreatureRecipe* velkarRec = CreatureRecipes_Find(4);
    const CreatureRecipe* explRec = CreatureRecipes_Find(1000);
    const CreatureRecipe* horseRec = CreatureRecipes_Find(1005);
    TEST_ASSERT(lizardRec && dogRec && brumaRec && velkarRec && explRec && horseRec,
        "Recetas clave disponibles");

    Monster lizard = Monster_Create(), dog = Monster_Create(),
            bruma = Monster_Create(), velkar = Monster_Create(),
            expl = Monster_Create(), horse = Monster_Create();
    TEST_ASSERT(Creature_BuildMonster(&lizard, lizardRec, &lizardRec->adult), "Lagarto");
    TEST_ASSERT(Creature_BuildMonster(&dog, dogRec, &dogRec->adult), "Perro");
    TEST_ASSERT(Creature_BuildMonster(&bruma, brumaRec, &brumaRec->adult), "Brumacerno");
    TEST_ASSERT(Creature_BuildMonster(&velkar, velkarRec, &velkarRec->adult), "Velkar");
    TEST_ASSERT(Creature_BuildMonster(&expl, explRec, &explRec->adult), "Explorador");
    TEST_ASSERT(Creature_BuildMonster(&horse, horseRec, &horseRec->adult), "Caballo");

    Monster out = Monster_Create();
    MonsterSDF sdfA = MonsterSDF_Create(), sdfB = MonsterSDF_Create();

    /* 1. Velkar (orejas 30/31) -> Brumacerno (cuernos 30/31): debe conservar MATERIAL_HORN */
    TEST_ASSERT(CreatureMorph_Interpolate(&velkar, &bruma, 0.25f, &out) &&
        MonsterSDF_Build(&sdfA, &out, MonsterSDF_DefaultConfig()) &&
        HasSDFMaterial(&sdfA, SDF_MATERIAL_HORN),
        "Cuernos conservan material HORN al 25%% en Velkar->Brumacerno");
    TEST_ASSERT(CreatureMorph_Interpolate(&velkar, &bruma, 0.75f, &out) &&
        MonsterSDF_Build(&sdfA, &out, MonsterSDF_DefaultConfig()) &&
        HasSDFMaterial(&sdfA, SDF_MATERIAL_HORN),
        "Cuernos tras remapeo conservan material HORN al 75%% en Velkar->Brumacerno");

    /* 2. Perro -> Caballo: cascos tras remapeo deben conservar MATERIAL_HOOF */
    TEST_ASSERT(CreatureMorph_Interpolate(&dog, &horse, 0.30f, &out) &&
        MonsterSDF_Build(&sdfA, &out, MonsterSDF_DefaultConfig()) &&
        HasSDFMaterial(&sdfA, SDF_MATERIAL_HOOF),
        "Cascos conservan material HOOF al 30%% en Perro->Caballo");
    TEST_ASSERT(CreatureMorph_Interpolate(&dog, &horse, 0.75f, &out) &&
        MonsterSDF_Build(&sdfA, &out, MonsterSDF_DefaultConfig()) &&
        HasSDFMaterial(&sdfA, SDF_MATERIAL_HOOF),
        "Cascos conservan material HOOF al 75%% en Perro->Caballo");

    /* 3. Cobertura y longitud de pelo en transición escamas -> pelaje (Lagarto -> Caballo) */
    TEST_ASSERT(CreatureMorph_Interpolate(&lizard, &horse, 0.56f, &out),
        "Lagarto->Caballo al 56%%");
    TEST_ASSERT(out.surface.integument.type == INTEGUMENT_FUR &&
        out.surface.integument.coverage < 0.05f &&
        out.surface.integument.fur.length < horse.surface.integument.fur.length * 0.25f,
        "El pelo emergente crece suavemente desde raíz tras el 55%%");

    /* 4. Continuidad fina en el cruce del 50% en pares heterogéneos */
    const Monster* pairs[5][2] = {
        {&velkar, &bruma}, {&dog, &horse}, {&lizard, &horse},
        {&dog, &bruma}, {&horse, &expl}
    };
    for (size_t p = 0; p < 5; ++p) {
        Monster prev = Monster_Create();
        TEST_ASSERT(CreatureMorph_Interpolate(pairs[p][0], pairs[p][1], 0.4999f, &prev), "0.4999");
        TEST_ASSERT(CreatureMorph_Interpolate(pairs[p][0], pairs[p][1], 0.5001f, &out), "0.5001");
        TEST_ASSERT(MonsterSDF_Build(&sdfA, &prev, MonsterSDF_DefaultConfig()), "SDF 0.4999");
        TEST_ASSERT(MonsterSDF_Build(&sdfB, &out, MonsterSDF_DefaultConfig()), "SDF 0.5001");
        float maxSurface = 0.0f;
        for (int ix = -4; ix <= 4; ++ix) {
            for (int iy = -1; iy <= 6; ++iy) {
                for (int iz = -14; iz <= 6; ++iz) {
                    Vector3 pt = {(float)ix * 0.5f, (float)iy * 0.5f, (float)iz * 0.5f};
                    float d0 = MonsterSDF_EvaluateDistance(&sdfA, pt);
                    float d1 = MonsterSDF_EvaluateDistance(&sdfB, pt);
                    if (fminf(fabsf(d0), fabsf(d1)) < 0.15f) {
                        maxSurface = fmaxf(maxSurface, fabsf(d0 - d1));
                    }
                }
            }
        }
        printf("  [cruce 0.5] par %zu: maxSurface=%.5f\n", p, maxSurface);
        TEST_ASSERT(maxSurface < 0.02f, "Sin salto superficial en cruce 50%%");
        Monster_Free(&prev);
    }

    /* 5. Encadenamiento intermedio X -> Y (t=0.45) -> Z */
    Monster mid = Monster_Create();
    TEST_ASSERT(CreatureMorph_Interpolate(&lizard, &horse, 0.45f, &mid), "X -> Y al 45%%");
    MonsterAger chained = MonsterAger_Create(&mid, &bruma, 0.0f);
    TEST_ASSERT(MonsterSDF_Build(&sdfB, &bruma, MonsterSDF_DefaultConfig()), "SDF Z destino");
    for (int step = 0; step <= 4; ++step) {
        MonsterAger_SetPerc(&chained, step * 0.25f);
        const Monster* cur = MonsterAger_GetResultConst(&chained);
        TEST_ASSERT(MonsterSDF_Build(&sdfA, cur, MonsterSDF_DefaultConfig()) &&
            sdfA.hasSurface, "Cadena (X->Y) -> Z genera SDF válido");
        for (uint32_t limbMod = 10u; limbMod <= 13u; ++limbMod) {
            for (uint16_t local = 1u; local <= 4u; ++local) {
                const AnatomyNode* jn = AnatomyGraph_FindNode(
                    &cur->anatomyGraph, Anatomy_MakeId(limbMod, local));
                TEST_ASSERT(jn != NULL && jn->development > 0.1f,
                    "Patas preservadas en cadena (X->Y) -> Z");
            }
        }
        if (step == 4) {
            AssertNearSurfaceParity(&sdfA, &sdfB,
                "Cadena (X->Y) -> Z converge exactamente a Z en t=1");
        }
    }
    MonsterAger_Free(&chained);
    Monster_Free(&mid);

    MonsterSDF_Free(&sdfA); MonsterSDF_Free(&sdfB); Monster_Free(&out);
    Monster_Free(&lizard); Monster_Free(&dog); Monster_Free(&bruma);
    Monster_Free(&velkar); Monster_Free(&expl); Monster_Free(&horse);
    printf("[PASS] test_morph_cross_materials_and_continuity\n");
}

void run_morph_cross_tests(void) {
    test_morph_cross_quadruped_to_snake();
    test_morph_cross_quadruped_to_biped();
    test_morph_cross_snake_to_quadruped_reversible();
    test_morph_cross_ontogeny_compatibility();
    test_morph_cross_all_species_matrix();
    test_morph_cross_materials_and_continuity();
}
