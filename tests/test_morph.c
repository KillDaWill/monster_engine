/**
 * @file test_morph.c
 * @brief Pruebas unitarias para el módulo de deformación morfológica continua LizardMorph.
 * @author Monster Engine Team
 * @date 2026
 */

#include "test_utils.h"
#include "LizardMorph.h"
#include "Lizard.h"
#include "Monster.h"
#include "MonsterSDF.h"
#include "SDFMesher.h"
#include "MonsterAger.h"
#include "MonsterVisualAsync.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

static double Morph_GetTimeMs(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

static unsigned Morph_CountVisibleDigits(const Mesh* mesh, float scale) {
    float lo = 0, hi = 1;
    for (unsigned i = 0; i < 24; ++i) {
        float t = (lo + hi) * 0.5f;
        LizardPhenotype p = LizardPhenotype_Interpolate(NULL, NULL, t);
        if (p.totalScale < scale) lo = t; else hi = t;
    }
    LizardPhenotype p = LizardPhenotype_Interpolate(NULL, NULL, (lo + hi) * 0.5f);
    AnatomyGraph graph;
    if (!Lizard_ResolveAnatomy(&p, &graph)) return 0;
    const unsigned formula[2][5] = {{2, 3, 4, 5, 3}, {2, 3, 4, 5, 4}};
    unsigned visible = 0;
    for (unsigned limb = 0; limb < 4; ++limb) {
        for (unsigned digit = 0; digit < 5; ++digit) {
            const AnatomyNode* n = AnatomyGraph_FindNode(&graph,
                Anatomy_DigitId(limb, digit, formula[limb / 2][digit] + 1));
            if (!n) continue;
            float nearest = 1e6f;
            for (size_t v = 0; v < mesh->vertexCount; ++v) {
                nearest = fminf(nearest, Vec3_Distance(n->center, mesh->vertices[v].position));
            }
            bool ok = nearest <= n->widthRadius * 1.8f;
            visible += ok;
        }
    }
    return visible;
}

static void test_lizard_morph_lifecycle(void) {
    LizardMorph* morph = LizardMorph_Create();
    TEST_ASSERT(morph != NULL, "LizardMorph_Create debe retornar un puntero no nulo");
    TEST_ASSERT(!LizardMorph_IsBound(morph), "Un morph recién creado no debe estar vinculado");
    TEST_ASSERT(LizardMorph_GetVertexCount(morph) == 0, "Conteo de vértices inicial debe ser 0");

    Mesh emptyMesh = Mesh_Create();
    AnatomyGraph emptyGraph;
    AnatomyGraph_Init(&emptyGraph);

    TEST_ASSERT(!LizardMorph_Bind(morph, &emptyMesh, &emptyGraph), "No debe vincular malla vacía");
    TEST_ASSERT(!LizardMorph_Deform(morph, &emptyGraph, &emptyMesh), "No debe deformar sin vinculación");

    Mesh_Free(&emptyMesh);
    LizardMorph_Free(morph);
    printf("[PASS] test_lizard_morph_lifecycle\n");
}

static void test_lizard_morph_sweep_and_perf(void) {
    Monster m = Monster_Create();
    LizardPhenotype juvP = LizardPreset_Juvenile();
    TEST_ASSERT(Lizard_BuildMonster(&m, &juvP), "Construcción de lagarto juvenil");

    MonsterSDF sdf = MonsterSDF_Create();
    TEST_ASSERT(MonsterSDF_Build(&sdf, &m, MonsterSDF_DefaultConfig()), "Construcción SDF");
    SDFField field = MonsterSDF_GetField(&sdf);

    SDFMesher mesher = SDFMesher_Create(SDFMesher_DefaultConfig());
    mesher.config.adaptiveDetail = true;
    mesher.config.voxelSize = 0.10f;
    mesher.config.maxCells = 250000;
    SDFDetailRegion regions[MONSTER_SDF_DETAIL_REGION_CAPACITY];
    size_t rCount = MonsterSDF_GetDetailRegions(&sdf, 3.5f, regions, MONSTER_SDF_DETAIL_REGION_CAPACITY);

    Mesh baseMesh = Mesh_Create();
    TEST_ASSERT(SDFMesher_GenerateMeshDetailed(&mesher, &field, regions, rCount, &baseMesh),
                "Generación de malla base adaptativa");
    TEST_ASSERT(baseMesh.vertexCount > 5000, "La malla base debe tener geometría sustancial");

    LizardMorph* morph = LizardMorph_Create();
    double tBind0 = Morph_GetTimeMs();
    TEST_ASSERT(LizardMorph_Bind(morph, &baseMesh, &m.anatomyGraph), "Vinculación de malla base");
    double tBind = Morph_GetTimeMs() - tBind0;
    TEST_ASSERT(LizardMorph_IsBound(morph), "El deformador debe quedar marcado como vinculado");
    TEST_ASSERT(LizardMorph_GetVertexCount(morph) == baseMesh.vertexCount, "Conteo de vértices debe coincidir");
    printf("  [debug] LizardMorph bind: %zu vértices en %.1f ms\n", baseMesh.vertexCount, tBind);

    Mesh morphMesh = Mesh_Create();
    morphMesh.vertices = (MeshVertex*)malloc(baseMesh.vertexCount * sizeof(MeshVertex));
    memcpy(morphMesh.vertices, baseMesh.vertices, baseMesh.vertexCount * sizeof(MeshVertex));
    morphMesh.vertexCount = baseMesh.vertexCount;
    morphMesh.vertexCapacity = baseMesh.vertexCount;
    morphMesh.indices = (MeshIndex*)malloc(baseMesh.indexCount * sizeof(MeshIndex));
    memcpy(morphMesh.indices, baseMesh.indices, baseMesh.indexCount * sizeof(MeshIndex));
    morphMesh.indexCount = baseMesh.indexCount;
    morphMesh.indexCapacity = baseMesh.indexCount;

    /* Barrido completo de ontogenia 0.0 -> 1.0 */
    float ages[] = {0.0f, 0.15f, 0.35f, 0.50f, 0.70f, 0.85f, 1.0f};
    for (size_t i = 0; i < sizeof(ages) / sizeof(ages[0]); ++i) {
        float age = ages[i];
        LizardPhenotype targetPheno = LizardPhenotype_Interpolate(NULL, NULL, age);
        AnatomyGraph targetGraph;
        TEST_ASSERT(Lizard_ResolveAnatomy(&targetPheno, &targetGraph), "Resolución de anatomía destino");

        double tDef0 = Morph_GetTimeMs();
        TEST_ASSERT(LizardMorph_Deform(morph, &targetGraph, &morphMesh), "Deformación O(V) en tiempo real");
        double tDef = Morph_GetTimeMs() - tDef0;

        /* El presupuesto se verifica en release; instrumentar accesos de memoria
         * cambia el coste por vértice y no mide el rendimiento de producción. */
#ifndef MONSTER_TEST_INSTRUMENTED
        TEST_ASSERT(tDef < 15.0, "La deformación debe ejecutarse en menos de 15 ms para soportar 60 FPS");
#endif

        unsigned visible = Morph_CountVisibleDigits(&morphMesh, targetPheno.totalScale);
        printf("  [debug] LizardMorph edad %.2f: %.2f ms, dígitos=%u/20\n", age, tDef, visible);
        TEST_ASSERT(visible == 20, "Los 20 dígitos deben permanecer visibles tras la deformación continua");
    }

    Mesh_Free(&baseMesh);
    Mesh_Free(&morphMesh);
    LizardMorph_Free(morph);
    SDFMesher_Free(&mesher);
    MonsterSDF_Free(&sdf);
    Monster_Free(&m);
    printf("[PASS] test_lizard_morph_sweep_and_perf\n");
}

static void test_lizard_morph_eye_and_jaw_growth(void) {
    Monster young = Monster_Create();
    LizardPhenotype pYoung = LizardPreset_Juvenile();
    TEST_ASSERT(Lizard_BuildMonster(&young, &pYoung), "Construcción lagarto joven");

    Monster adult = Monster_Create();
    LizardPhenotype pAdult = LizardPreset_Adult();
    TEST_ASSERT(Lizard_BuildMonster(&adult, &pAdult), "Construcción lagarto adulto");

    MonsterAger ager = MonsterAger_Create(&young, &adult, 0.0f);
    MonsterVisualAsyncConfig cfg = MonsterVisualAsync_DefaultConfig();
    MonsterVisualAsync* asyncMgr = MonsterVisualAsync_Create(cfg);
    TEST_ASSERT(asyncMgr != NULL, "Creación de MonsterVisualAsync");

    /* Paso 1: Generación base juvenil */
    const Monster* cur0 = MonsterAger_GetResultConst(&ager);
    MonsterVisualAsync_Update(asyncMgr, cur0, 0.016f);
    MonsterVisualAsync_Flush(asyncMgr);

    TEST_ASSERT(MonsterVisualAsync_GetDisplayEyeCount(asyncMgr) == 2, "Debe tener 2 ojos en display");
    TEST_ASSERT(MonsterVisualAsync_GetDisplayMouthCount(asyncMgr) == 1, "Debe tener 1 boca en display");

    const Mesh* eye0 = MonsterVisualAsync_GetDisplayEyeSclera(asyncMgr, 0);
    const Mesh* jaw0 = MonsterVisualAsync_GetDisplayMouthMesh(asyncMgr, 0, 0);
    TEST_ASSERT(eye0 != NULL && eye0->vertexCount > 0, "Malla de esclerótica ojo 0");
    TEST_ASSERT(jaw0 != NULL && jaw0->vertexCount > 0, "Malla de mandíbula 0");

    Vector3 eyeMin0 = eye0->vertices[0].position, eyeMax0 = eye0->vertices[0].position;
    for (size_t i = 1; i < eye0->vertexCount; ++i) {
        eyeMin0.x = fminf(eyeMin0.x, eye0->vertices[i].position.x);
        eyeMin0.y = fminf(eyeMin0.y, eye0->vertices[i].position.y);
        eyeMin0.z = fminf(eyeMin0.z, eye0->vertices[i].position.z);
        eyeMax0.x = fmaxf(eyeMax0.x, eye0->vertices[i].position.x);
        eyeMax0.y = fmaxf(eyeMax0.y, eye0->vertices[i].position.y);
        eyeMax0.z = fmaxf(eyeMax0.z, eye0->vertices[i].position.z);
    }
    Vector3 jawMin0 = jaw0->vertices[0].position, jawMax0 = jaw0->vertices[0].position;
    for (size_t i = 1; i < jaw0->vertexCount; ++i) {
        jawMin0.x = fminf(jawMin0.x, jaw0->vertices[i].position.x);
        jawMin0.y = fminf(jawMin0.y, jaw0->vertices[i].position.y);
        jawMin0.z = fminf(jawMin0.z, jaw0->vertices[i].position.z);
        jawMax0.x = fmaxf(jawMax0.x, jaw0->vertices[i].position.x);
        jawMax0.y = fmaxf(jawMax0.y, jaw0->vertices[i].position.y);
        jawMax0.z = fmaxf(jawMax0.z, jaw0->vertices[i].position.z);
    }
    float eyeWidth0 = eyeMax0.x - eyeMin0.x;
    float jawLength0 = jawMax0.z - jawMin0.z;
    float eyeCenterX0 = (eyeMin0.x + eyeMax0.x) * 0.5f;

    /* Paso 2: MORPH conserva los anexos juveniles hasta publicar el adulto. */
    Vector3 oldEye=eye0->vertices[0].position,oldJaw=jaw0->vertices[0].position;
    uint64_t oldGeneration=MonsterVisualAsync_GetDisplayGeneration(asyncMgr);
    MonsterVisualAsync_SetMorphMode(asyncMgr, true);
    MonsterAger_SetPerc(&ager, 1.0f);
    const Monster* adultCur = MonsterAger_GetResultConst(&ager);
    MonsterVisualAsync_Update(asyncMgr, adultCur, 0.016f);
    TEST_ASSERT(MonsterVisualAsync_GetDisplayGeneration(asyncMgr)==oldGeneration,
        "La solicitud todavía no debe haber publicado la malla adulta");
    TEST_ASSERT(Vec3_Distance(oldEye,eye0->vertices[0].position)<1e-6f&&
                Vec3_Distance(oldJaw,jaw0->vertices[0].position)<1e-6f,
        "MORPH mezcló anexos adultos con el cuerpo juvenil");
    MonsterVisualAsync_Flush(asyncMgr);
    TEST_ASSERT(MonsterVisualAsync_GetDisplayGeneration(asyncMgr)>oldGeneration,
        "El adulto debe publicarse antes de comprobar su crecimiento");

    const Mesh* eye1 = MonsterVisualAsync_GetDisplayEyeSclera(asyncMgr, 0);
    const Mesh* jaw1 = MonsterVisualAsync_GetDisplayMouthMesh(asyncMgr, 0, 0);
    Vector3 eyeMin1 = eye1->vertices[0].position, eyeMax1 = eye1->vertices[0].position;
    for (size_t i = 1; i < eye1->vertexCount; ++i) {
        eyeMin1.x = fminf(eyeMin1.x, eye1->vertices[i].position.x);
        eyeMin1.y = fminf(eyeMin1.y, eye1->vertices[i].position.y);
        eyeMin1.z = fminf(eyeMin1.z, eye1->vertices[i].position.z);
        eyeMax1.x = fmaxf(eyeMax1.x, eye1->vertices[i].position.x);
        eyeMax1.y = fmaxf(eyeMax1.y, eye1->vertices[i].position.y);
        eyeMax1.z = fmaxf(eyeMax1.z, eye1->vertices[i].position.z);
    }
    Vector3 jawMin1 = jaw1->vertices[0].position, jawMax1 = jaw1->vertices[0].position;
    for (size_t i = 1; i < jaw1->vertexCount; ++i) {
        jawMin1.x = fminf(jawMin1.x, jaw1->vertices[i].position.x);
        jawMin1.y = fminf(jawMin1.y, jaw1->vertices[i].position.y);
        jawMin1.z = fminf(jawMin1.z, jaw1->vertices[i].position.z);
        jawMax1.x = fmaxf(jawMax1.x, jaw1->vertices[i].position.x);
        jawMax1.y = fmaxf(jawMax1.y, jaw1->vertices[i].position.y);
        jawMax1.z = fmaxf(jawMax1.z, jaw1->vertices[i].position.z);
    }
    float eyeWidth1 = eyeMax1.x - eyeMin1.x;
    float jawLength1 = jawMax1.z - jawMin1.z;
    float eyeCenterX1 = (eyeMin1.x + eyeMax1.x) * 0.5f;

    /* Verificaciones de crecimiento anatómico */
    TEST_ASSERT(eyeWidth1 > eyeWidth0 * 1.35f, "El ojo debe crecer más de un 35% en anchura");
    TEST_ASSERT(eyeCenterX1 > eyeCenterX0 * 1.50f, "El ojo debe desplazarse lateralmente con el ensanchamiento craneal");
    TEST_ASSERT(jawLength1 > jawLength0 * 1.50f, "La mandíbula debe crecer más de un 50% en longitud con el hocico adulto");

    MonsterVisualAsync_Free(asyncMgr);
    MonsterAger_Free(&ager);
    Monster_Free(&young);
    Monster_Free(&adult);
    printf("[PASS] test_lizard_morph_eye_and_jaw_growth\n");
}

/* El campo directo debe contener las mismas piezas orales que el renderer
 * de mallas, incluso al desaparecer y al cambiar la articulación. */
static void test_visual_sdf_posed_mouth_bounds(void) {
    const float ages[]={0,.25f,.5f,1};
    const float openings[]={0,.1f,1};
    Monster first=Monster_Create(),last=Monster_Create();
    LizardPhenotype a=LizardPreset_Larva(),b=LizardPreset_Adult();
    TEST_ASSERT(Lizard_BuildMonster(&first,&a)&&Lizard_BuildMonster(&last,&b),"Extremos visuales inválidos");
    MonsterAger ager=MonsterAger_Create(&first,&last,0);
    MonsterSDF sdf=MonsterSDF_Create();
    for(size_t age=0;age<4;++age)for(size_t opening=0;opening<3;++opening) {
        MonsterAger_SetPerc(&ager,ages[age]);Monster* monster=MonsterAger_GetResult(&ager);
        Monster_SetHeadOpenFactor(monster,openings[opening]);
        TEST_ASSERT(MonsterSDF_Build(&sdf,monster,MonsterSDF_DefaultConfig()),"Falló el snapshot visual");
        for(size_t i=0;i<monster->mouthCount;++i) {
            MonsterVisualMouth meshes={0};
            TEST_ASSERT(MonsterVisual_BuildMouthMeshesFromSDF(&meshes,&monster->mouths[i],monster,&sdf,i),"Falló la referencia oral");
            const Mesh* parts[]={&meshes.jaw,&meshes.hinge};
            for(size_t part=0;part<2;++part)for(size_t v=0;v<parts[part]->vertexCount;++v) {
                Vector3 p=parts[part]->vertices[v].position;
                TEST_ASSERT(AABB_ContainsPoint(sdf.mouths[i].visualBounds,p),"La caja visual recorta una pieza oral articulada");
                float distance=MonsterSDF_EvaluateVisualDistance(&sdf,p);
                TEST_ASSERT(isfinite(distance)&&distance<.06f,"El campo visual pierde superficie de la referencia oral");
            }
            MonsterVisualMouth_Free(&meshes);
        }
    }
    MonsterSDF_Free(&sdf);MonsterAger_Free(&ager);Monster_Free(&first);Monster_Free(&last);
    printf("[PASS] test_visual_sdf_posed_mouth_bounds\n");
}

void run_morph_tests(void) {
    test_visual_sdf_posed_mouth_bounds();
    test_lizard_morph_lifecycle();
    test_lizard_morph_sweep_and_perf();
    test_lizard_morph_eye_and_jaw_growth();
}
