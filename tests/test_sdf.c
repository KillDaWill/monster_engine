#include "test_utils.h"
#include "MonsterSDF.h"
#include "SDFMesher.h"
#include "SDFPrimitives.h"
#include "SDFOperations.h"
#include "MonsterVisual.h"
#include "Mesh.h"
#include "Monster.h"
#include <math.h>

void test_sdf_primitives_and_ops(void) {
    Vector3 pointOutside = Vec3_Create(2.0f, 0.0f, 0.0f);
    Vector3 pointInside = Vec3_Create(0.5f, 0.0f, 0.0f);

    float dOut = SDF_Sphere(pointOutside, 1.0f);
    TEST_ASSERT(FLOAT_NEAR(dOut, 1.0f), "SDF_Sphere outside failed");

    float dIn = SDF_Sphere(pointInside, 1.0f);
    TEST_ASSERT(FLOAT_NEAR(dIn, -0.5f), "SDF_Sphere inside failed");

    /* Union suave */
    float unionDist = SDF_SmoothUnion(dOut, dIn, 0.2f);
    TEST_ASSERT(unionDist <= dIn, "SDF_SmoothUnion failed");

    printf("[PASS] test_sdf_primitives_and_ops\n");
}

void test_monster_sdf_evaluation(void) {
    Monster monster = Monster_Create();
    Monster_Init(&monster);

    MonsterSDF sdf = MonsterSDF_Create();
    bool built = MonsterSDF_Build(&sdf, &monster, MonsterSDF_DefaultConfig());
    TEST_ASSERT(built, "MonsterSDF_Build failed");

    /* Evaluación en origen del monstruo */
    Vector3 origin = Vec3_Create(0.0f, 0.0f, 0.0f);
    SDFSample sample = MonsterSDF_Evaluate(&sdf, origin);

    /* El cuerpo del monstruo debe cubrir el origen */
    TEST_ASSERT(sample.distance < 1.0f, "MonsterSDF_Evaluate origin distance failed");

    /* Punto muy lejano debe tener distancia positiva alta */
    Vector3 farPoint = Vec3_Create(100.0f, 100.0f, 100.0f);
    SDFSample farSample = MonsterSDF_Evaluate(&sdf, farPoint);
    TEST_ASSERT(farSample.distance > 50.0f, "MonsterSDF_Evaluate far point failed");

    MonsterSDF_Free(&sdf);
    Monster_Free(&monster);
    printf("[PASS] test_monster_sdf_evaluation\n");
}

void test_sdf_mesher(void) {
    Monster monster = Monster_Create();
    Monster_Init(&monster);

    MonsterSDF sdf = MonsterSDF_Create();
    MonsterSDF_Build(&sdf, &monster, MonsterSDF_DefaultConfig());

    SDFMesherConfig cfg = SDFMesher_DefaultConfig();
    cfg.resolutionX = 16;
    cfg.resolutionY = 16;
    cfg.resolutionZ = 16;

    SDFMesher mesher = SDFMesher_Create(cfg);
    Mesh mesh = Mesh_Create();

    SDFField field = MonsterSDF_GetField(&sdf);
    bool success = SDFMesher_GenerateMesh(&mesher, &field, &mesh);
    TEST_ASSERT(success, "SDFMesher_GenerateMesh returned false");
    TEST_ASSERT(mesh.vertexCount > 0, "SDFMesher generated 0 vertices");
    TEST_ASSERT(mesh.indexCount > 0, "SDFMesher generated 0 indices");

    Mesh_Free(&mesh);
    MonsterSDF_Free(&sdf);
    Monster_Free(&monster);
    printf("[PASS] test_sdf_mesher\n");
}

void test_monster_visual(void) {
    Monster monster = Monster_Create();
    Monster_Init(&monster);

    SDFMesherConfig cfg = SDFMesher_DefaultConfig();
    cfg.resolutionX = 16;
    cfg.resolutionY = 16;
    cfg.resolutionZ = 16;

    MonsterVisual visual = MonsterVisual_Create(cfg);
    bool updated = MonsterVisual_Update(&visual, &monster, 0.1f, 0.0f, MonsterSDF_DefaultConfig());
    TEST_ASSERT(updated, "MonsterVisual_Update failed to build mesh");

    const Mesh* mesh = MonsterVisual_GetMesh(&visual);
    TEST_ASSERT(mesh != NULL && mesh->vertexCount > 0, "MonsterVisual mesh invalid");

    MonsterVisual_Free(&visual);
    Monster_Free(&monster);
    printf("[PASS] test_monster_visual\n");
}

void run_sdf_tests(void) {
    test_sdf_primitives_and_ops();
    test_monster_sdf_evaluation();
    test_sdf_mesher();
    test_monster_visual();
}
