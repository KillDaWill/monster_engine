/**
 * @file test_five_mammals.c
 * @brief Pruebas de regresión para la expansión de cinco mamíferos y sus sistemas anatómicos genéricos.
 */
#include "Creature.h"
#include "CreatureRecipeIO.h"
#include "CreatureRecipeRegistry.h"
#include "CreatureRecipes.h"
#include "CreatureOffspring.h"
#include "MonsterAger.h"
#include "MonsterSDF.h"
#include "MonsterVisual.h"
#include "MonsterAnimation.h"
#include "AnimatedVisual.h"
#include "Membrane.h"
#include "ArmorScales.h"
#include "PigmentPattern.h"
#include "test_utils.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static AABB3D MeshBounds(const Mesh* mesh) {
    AABB3D b = AABB_Empty();
    for (size_t i = 0; mesh && i < mesh->vertexCount; ++i)
        AABB_ExpandPoint(&b, mesh->vertices[i].position);
    return b;
}

static bool LoadSpecDoc(const char* path, CreatureRecipeDocument* doc) {
    char err[256] = {0};
    return CreatureRecipeIO_LoadFile(doc, path, err, sizeof(err));
}

static void TestAllFiveRecipesFourStagesAndAger(void) {
    const struct {
        const char* path;
        CreatureRecipeId id;
    } specs[] = {
        {"recipes/jirafa.creature.json", 1011u},
        {"recipes/ornitorrinco.creature.json", 1012u},
        {"recipes/pangolin.creature.json", 1013u},
        {"recipes/canguro.creature.json", 1014u},
        {"recipes/murcielago.creature.json", 1015u},
    };
    char err[256] = {0};
    TEST_ASSERT(CreatureRecipeRegistry_LoadDirectory("recipes", err, sizeof(err)),
                "El catálogo externo completo debe cargar las 5 especies nuevas");

    for (size_t s = 0; s < sizeof(specs) / sizeof(specs[0]); ++s) {
        CreatureRecipeDocument* doc = calloc(1, sizeof(*doc));
        TEST_ASSERT(doc != NULL && LoadSpecDoc(specs[s].path, doc), "Carga de receta nueva");
        TEST_ASSERT(doc->recipe.id == specs[s].id, "ID de receta consistente");
        const CreatureRecipe* reg = CreatureRecipes_Find(specs[s].id);
        TEST_ASSERT(reg != NULL && reg->id == specs[s].id, "Receta registrada en catálogo");

        Monster* m = malloc(sizeof(*m));
        Monster* juvenile = malloc(sizeof(*juvenile));
        Monster* adult = malloc(sizeof(*adult));
        Monster* agerOut = malloc(sizeof(*agerOut));
        TEST_ASSERT(m && juvenile && adult && agerOut, "Memoria de pruebas de cinco mamíferos");

        for (unsigned st = 0; st < 4; ++st) {
            const CreaturePhenotype* p = CreatureRecipe_GetStage(&doc->recipe, (CreatureStage)st);
            *m = Monster_Create();
            TEST_ASSERT(Creature_BuildMonster(m, &doc->recipe, p), "Construcción válida en cada etapa");
            TEST_ASSERT(AnatomyGraph_Validate(&m->anatomyGraph), "Grafo anatómico válido en las 4 etapas");
            TEST_ASSERT(m->animation != NULL, "Animación y rig válidos");
            MonsterSDF sdf = MonsterSDF_Create();
            TEST_ASSERT(MonsterSDF_Build(&sdf, m, MonsterSDF_DefaultConfig()), "SDF válido");
            Vector3 sz = AABB_Size(sdf.bounds);
            TEST_ASSERT(isfinite(sz.x) && sz.x > 0 && isfinite(sz.y) && sz.y > 0 && isfinite(sz.z) && sz.z > 0,
                        "Límites SDF finitos y positivos");
            MonsterSDF_Free(&sdf);
            Monster_Free(m);
        }

        *juvenile = Monster_Create();
        *adult = Monster_Create();
        *agerOut = Monster_Create();
        TEST_ASSERT(Creature_BuildMonster(juvenile, &doc->recipe, &doc->recipe.seed) &&
                    Creature_BuildMonster(adult, &doc->recipe, &doc->recipe.adult), "Extremos Ager válidos");
        const float ages[] = {0.0f, 0.25f, 0.50f, 0.75f, 1.0f};
        for (size_t a = 0; a < 5; ++a) {
            MonsterAger_Interpolate(juvenile, adult, ages[a], agerOut);
            TEST_ASSERT(agerOut->hasAnatomyGraph && AnatomyGraph_Validate(&agerOut->anatomyGraph),
                        "Topología coherente en edades intermedias");
        }

        Monster_Free(juvenile);
        Monster_Free(adult);
        Monster_Free(agerOut);
        free(m); free(juvenile); free(adult); free(agerOut);
        free(doc);
    }
    printf("[PASS] test_five_mammals: carga, 4 etapas, SDF y Ager para 1011..1015\n");
}

static void TestGiraffeCapabilities(void) {
    CreatureRecipeDocument* doc = calloc(1, sizeof(*doc));
    TEST_ASSERT(doc && LoadSpecDoc("recipes/jirafa.creature.json", doc), "Cargar jirafa");
    Monster* m = malloc(sizeof(*m));
    *m = Monster_Create();
    TEST_ASSERT(Creature_BuildMonster(m, &doc->recipe, &doc->recipe.adult), "Construir jirafa");
    TEST_ASSERT(m->animation->rig.cervicalChainCount >= 4, "Cadena cervical multi-segmento activa");

    /* Patrón reticulado no trivial y determinista */
    float minR = 1e6f, maxR = -1e6f;
    for (int k = 0; k < 40; ++k) {
        float val = PigmentPattern_SampleSurface(PIGMENT_PATTERN_RETICULATED,
            Vec3_Create(0.05f * (float)k, 0.9f, 0.3f), 6.0f, 0.7f, Vec3_Create(0, 1, 0), 42u);
        TEST_ASSERT(isfinite(val) && val >= 0.0f && val <= 1.0f, "Patrón reticulado acotado");
        if (val < minR) minR = val;
        if (val > maxR) maxR = val;
    }
    TEST_ASSERT((maxR - minR) > 0.25f, "Patrón reticulado varía entre polígonos y suturas");

    /* Osiconos craneales presentes en grafo */
    size_t ossicones = 0;
    for (size_t i = 0; i < m->anatomyGraph.nodeCount; ++i) {
        if (m->anatomyGraph.nodes[i].region == ANATOMY_REGION_ORNAMENT) ++ossicones;
    }
    TEST_ASSERT(ossicones >= 4, "Osiconos craneales resueltos en anatomía");
    Monster_Free(m);
    free(m);
    free(doc);
    printf("[PASS] test_five_mammals: jirafa (cadena cervical, osiconos y patrón reticulado)\n");
}

static void TestPlatypusCapabilities(void) {
    CreatureRecipeDocument* doc = calloc(1, sizeof(*doc));
    TEST_ASSERT(doc && LoadSpecDoc("recipes/ornitorrinco.creature.json", doc), "Cargar ornitorrinco");
    Monster* m = malloc(sizeof(*m));
    *m = Monster_Create();
    TEST_ASSERT(Creature_BuildMonster(m, &doc->recipe, &doc->recipe.adult), "Construir ornitorrinco");
    TEST_ASSERT(m->hasHead && m->head.anatomy.surface.isBill && !m->head.anatomy.surface.hasEars,
                "Pico anatómico de monotrema activo y sin orejas externas");
    const AnatomyNode* tailTip = AnatomyGraph_FindFirstRegion(&m->anatomyGraph, ANATOMY_REGION_TAIL);
    TEST_ASSERT(tailTip != NULL && doc->recipe.adult.tails[0].archetype == TAIL_ARCHETYPE_PADDLE,
                "Cola plana tipo remo activa");

    Mesh openWeb = Mesh_Create();
    TEST_ASSERT(Membrane_BuildMesh(m, &openWeb) && openWeb.vertexCount > 0 && openWeb.indexCount > 0,
                "Malla de membrana interdigital generada");
    CreaturePhenotype foldedPheno = doc->recipe.adult;
    foldedPheno.limbs[0].webbingFold = 0.90f;
    foldedPheno.limbs[1].webbingFold = 0.90f;
    Monster_Free(m);
    *m = Monster_Create();
    TEST_ASSERT(Creature_BuildMonster(m, &doc->recipe, &foldedPheno), "Construir ornitorrinco plegado");
    Mesh foldedWeb = Mesh_Create();
    TEST_ASSERT(Membrane_BuildMesh(m, &foldedWeb), "Membrana plegada generada");
    AABB3D bOpen = MeshBounds(&openWeb), bFold = MeshBounds(&foldedWeb);
    TEST_ASSERT(AABB_Size(bFold).x <= AABB_Size(bOpen).x + 1e-4f,
                "El plegamiento terrestre no ensancha la membrana interdigital");

    Mesh_Free(&openWeb);
    Mesh_Free(&foldedWeb);
    Monster_Free(m);
    free(m);
    free(doc);
    printf("[PASS] test_five_mammals: ornitorrinco (pico, cola paddle y membrana retráctil)\n");
}

static void TestPangolinCapabilities(void) {
    CreatureRecipeDocument* doc = calloc(1, sizeof(*doc));
    TEST_ASSERT(doc && LoadSpecDoc("recipes/pangolin.creature.json", doc), "Cargar pangolín");
    Monster* m = malloc(sizeof(*m));
    *m = Monster_Create();
    TEST_ASSERT(Creature_BuildMonster(m, &doc->recipe, &doc->recipe.adult), "Construir pangolín");
    TEST_ASSERT(m->phenotype.head.archetype == HEAD_ARCHETYPE_PHOLIDOTE, "Cabeza cónica de pangolín");

    SDFMesherConfig cfg = SDFMesher_DefaultConfig();
    cfg.voxelSize = 0.04f; cfg.maxCells = 250000; cfg.maxResolution = 128;
    MonsterVisual vis = MonsterVisual_Create(cfg);
    TEST_ASSERT(MonsterVisual_RebuildNow(&vis, m, MonsterSDF_DefaultConfig()), "Visual de pangolín construido");
    TEST_ASSERT(vis.armorMesh.vertexCount > 100 && vis.armorMesh.indexCount % 3 == 0,
                "Coraza geométrica de escamas queratínicas generada");

    AnimatedVisual bind = {0};
    TEST_ASSERT(AnimatedVisual_Bind(&bind, &vis, m) && bind.armor != NULL, "Deformer de coraza vinculado");
    AABB3D restBox = MeshBounds(&vis.armorMesh);
    float restLen = AABB_Size(restBox).z;
    for (int step = 0; step < 30; ++step) {
        m->animation->animator.defensiveCurl = 1.0f;
        MonsterAnimation_Update(m, 1.0f / 30.0f);
    }
    TEST_ASSERT(AnimatedVisual_Deform(&bind, &vis, m), "Deformación de coraza en enrollamiento");
    AABB3D curlBox = MeshBounds(&vis.armorMesh);
    float curlLen = AABB_Size(curlBox).z;
    TEST_ASSERT(isfinite(curlLen) && curlLen < restLen * 0.90f,
                "Enrollamiento defensivo reduce extensión longitudinal sin explotar malla");

    AnimatedVisual_Free(&bind);
    MonsterVisual_Free(&vis);
    Monster_Free(m);
    free(m);
    free(doc);
    printf("[PASS] test_five_mammals: pangolín (escamas geométricas imbricadas y enrollamiento)\n");
}

static void TestKangarooCapabilities(void) {
    CreatureRecipeDocument* doc = calloc(1, sizeof(*doc));
    TEST_ASSERT(doc && LoadSpecDoc("recipes/canguro.creature.json", doc), "Cargar canguro");
    Monster* m = malloc(sizeof(*m));
    *m = Monster_Create();
    TEST_ASSERT(Creature_BuildMonster(m, &doc->recipe, &doc->recipe.adult), "Construir canguro");
    TEST_ASSERT(m->recipe.gait == CREATURE_GAIT_BIPEDAL_HOP, "Marcha BIPEDAL_HOP");
    TEST_ASSERT(m->animation->rig.limbCount == 4, "4 miembros en rig");
    TEST_ASSERT(!m->animation->rig.limbs[0].walking && !m->animation->rig.limbs[1].walking,
                "Miembros anteriores no caminan en salto bípedo");
    TEST_ASSERT(m->animation->rig.limbs[2].walking && m->animation->rig.limbs[3].walking,
                "Miembros posteriores sustentan salto bípedo");

    float minHipY = 1e6f, maxHipY = -1e6f;
    bool sawFlight = false;
    m->animation->animator.desiredVelocity = Vec3_Create(0, 0, 1.5f);
    for (int step = 0; step < 90; ++step) {
        TEST_ASSERT(MonsterAnimation_Update(m, 1.0f / 30.0f), "Actualización locomotora de salto");
        int hlRoot = m->animation->rig.limbs[2].rootJoint;
        float hy = m->animation->pose.joints[hlRoot].worldPosition.y;
        if (hy < minHipY) minHipY = hy;
        if (hy > maxHipY) maxHipY = hy;
        if (m->animation->animator.locomotion.feet[2].phase == FOOT_SWING &&
            m->animation->animator.locomotion.feet[3].phase == FOOT_SWING) {
            sawFlight = true;
        }
    }
    TEST_ASSERT(sawFlight && (maxHipY - minHipY) > 0.04f,
                "Salto bípedo sincronizado produce fase aérea y arco vertical");

    Monster_Free(m);
    free(m);
    free(doc);
    printf("[PASS] test_five_mammals: canguro (macrópodo, brazos libres y salto bípedo aéreo)\n");
}

static void TestBatCapabilitiesAndOffspring(void) {
    CreatureRecipeDocument* batDoc = calloc(1, sizeof(*batDoc));
    CreatureRecipeDocument* platDoc = calloc(1, sizeof(*platDoc));
    CreatureRecipeDocument* panDoc = calloc(1, sizeof(*panDoc));
    CreatureRecipeDocument* canDoc = calloc(1, sizeof(*canDoc));
    TEST_ASSERT(batDoc && platDoc && panDoc && canDoc &&
                LoadSpecDoc("recipes/murcielago.creature.json", batDoc) &&
                LoadSpecDoc("recipes/ornitorrinco.creature.json", platDoc) &&
                LoadSpecDoc("recipes/pangolin.creature.json", panDoc) &&
                LoadSpecDoc("recipes/canguro.creature.json", canDoc), "Cargar recetas para alas y cruces");

    Monster* bat = malloc(sizeof(*bat));
    *bat = Monster_Create();
    TEST_ASSERT(Creature_BuildMonster(bat, &batDoc->recipe, &batDoc->recipe.adult), "Construir murciélago");
    TEST_ASSERT(!bat->animation->rig.limbs[0].walking && !bat->animation->rig.limbs[1].walking,
                "Las alas son no ambulatorias");

    SDFMesherConfig cfg = SDFMesher_DefaultConfig();
    cfg.voxelSize = 0.025f; cfg.maxCells = 250000; cfg.maxResolution = 128;
    MonsterVisual vis = MonsterVisual_Create(cfg);
    TEST_ASSERT(MonsterVisual_RebuildNow(&vis, bat, MonsterSDF_DefaultConfig()), "Visual de murciélago");
    TEST_ASSERT(vis.membraneMesh.vertexCount > 200 && vis.membraneMesh.indexCount % 3 == 0,
                "Patagios y dactilopatagios alares generados");

    AnimatedVisual bind = {0};
    TEST_ASSERT(AnimatedVisual_Bind(&bind, &vis, bat) && bind.membrane != NULL, "Deformer de patagios vinculado");
    AABB3D extBox = MeshBounds(&vis.membraneMesh);
    float extSpan = AABB_Size(extBox).x;
    bat->animation->animator.wingFold = 0.92f;
    TEST_ASSERT(MonsterAnimation_Update(bat, 1.0f / 30.0f) && AnimatedVisual_Deform(&bind, &vis, bat),
                "Plegado articular alar aplicado a membrana");
    AABB3D foldBox = MeshBounds(&vis.membraneMesh);
    float foldSpan = AABB_Size(foldBox).x;
    TEST_ASSERT(isfinite(foldSpan) && foldSpan < extSpan * 0.88f,
                "El plegado alar recoge realmente la envergadura del patagio");

    /* Cruces entre especies nuevas via CreatureOffspring */
    char err[256] = {0};
    CreatureOffspringConfig ocfg = CreatureOffspring_Default(2, 77u);
    CreatureOffspringBatch* cross1 = CreatureOffspring_Generate(&platDoc->recipe, &panDoc->recipe, ocfg, NULL, NULL, err, sizeof(err));
    TEST_ASSERT(cross1 != NULL && CreatureOffspring_Count(cross1) >= 1, "Cruce Ornitorrinco x Pangolín");
    const CreatureRecipe* r1 = CreatureOffspring_Get(cross1, 0, NULL);
    TEST_ASSERT(r1 && CreatureRecipe_Validate(r1, &r1->adult), "Descendiente Ornitorrinco x Pangolín válido");

    CreatureOffspringBatch* cross2 = CreatureOffspring_Generate(&canDoc->recipe, &batDoc->recipe, ocfg, NULL, NULL, err, sizeof(err));
    TEST_ASSERT(cross2 != NULL && CreatureOffspring_Count(cross2) >= 1, "Cruce Canguro x Murciélago");
    const CreatureRecipe* r2 = CreatureOffspring_Get(cross2, 0, NULL);
    TEST_ASSERT(r2 && CreatureRecipe_Validate(r2, &r2->adult), "Descendiente Canguro x Murciélago válido");

    CreatureOffspring_Destroy(cross1);
    CreatureOffspring_Destroy(cross2);
    AnimatedVisual_Free(&bind);
    MonsterVisual_Free(&vis);
    Monster_Free(bat);
    free(bat);
    free(batDoc); free(platDoc); free(panDoc); free(canDoc);
    printf("[PASS] test_five_mammals: murciélago (alas, patagios, plegado) y CreatureOffspring\n");
}

void run_five_mammals_tests(void) {
    TestAllFiveRecipesFourStagesAndAger();
    TestGiraffeCapabilities();
    TestPlatypusCapabilities();
    TestPangolinCapabilities();
    TestKangarooCapabilities();
    TestBatCapabilitiesAndOffspring();
}
