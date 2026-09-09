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
#include "Mesh.h"
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
            visible += nearest <= n->widthRadius * 1.8f;
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
    SDFDetailRegion regions[16];
    size_t rCount = MonsterSDF_GetDetailRegions(&sdf, 3.5f, regions, 16);

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

        /* Comprobar presupuesto de tiempo para >= 60 FPS (16.6 ms) */
        TEST_ASSERT(tDef < 15.0, "La deformación debe ejecutarse en menos de 15 ms para soportar 60 FPS");

        /* Comprobar visibilidad íntegra de los 20 extremos de dígitos */
        unsigned visible = Morph_CountVisibleDigits(&morphMesh, targetPheno.totalScale);
        TEST_ASSERT(visible == 20, "Los 20 dígitos deben permanecer visibles tras la deformación continua");
        printf("  [debug] LizardMorph edad %.2f: %.2f ms, dígitos=%u/20\n", age, tDef, visible);
    }

    Mesh_Free(&baseMesh);
    Mesh_Free(&morphMesh);
    LizardMorph_Free(morph);
    SDFMesher_Free(&mesher);
    MonsterSDF_Free(&sdf);
    Monster_Free(&m);
    printf("[PASS] test_lizard_morph_sweep_and_perf\n");
}

void run_morph_tests(void) {
    test_lizard_morph_lifecycle();
    test_lizard_morph_sweep_and_perf();
}
