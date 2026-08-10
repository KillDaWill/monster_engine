#include "test_utils.h"
#include "MonsterSDF.h"
#include "SDFMesher.h"
#include "SDFPrimitives.h"
#include "SDFOperations.h"
#include "MonsterVisual.h"
#include "Mesh.h"
#include "MathUtils.h"
#include "ColorPalette.h"
#include "Monster.h"
#include "MonsterAger.h"
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

void test_sdf_primitives_extended(void) {
    /* Elipsoide: dentro / superficie / fuera */
    Vector3 radii = Vec3_Create(2.0f, 1.0f, 0.5f);

    float onSurface = SDF_Ellipsoid(Vec3_Create(2.0f, 0.0f, 0.0f), radii);
    TEST_ASSERT(FLOAT_NEAR(onSurface, 0.0f), "SDF_Ellipsoid surface failed");

    float inside = SDF_Ellipsoid(Vec3_Create(0.0f, 0.0f, 0.0f), radii);
    TEST_ASSERT(inside < 0.0f, "SDF_Ellipsoid inside failed");

    float outside = SDF_Ellipsoid(Vec3_Create(0.0f, 0.0f, 2.0f), radii);
    TEST_ASSERT(outside > 0.0f, "SDF_Ellipsoid outside failed");

    /* Cápsula: punto sobre el eje -> -radio */
    float capsuleOnAxis = SDF_Capsule(Vec3_Create(0.0f, 0.0f, 0.0f),
                                      Vec3_Create(0.0f, 0.0f, -2.0f),
                                      Vec3_Create(0.0f, 0.0f, 2.0f), 0.5f);
    TEST_ASSERT(FLOAT_NEAR(capsuleOnAxis, -0.5f), "SDF_Capsule axis failed");

    /* Cápsula degenerada (a == b) se comporta como esfera */
    float capsuleDegenerate = SDF_Capsule(Vec3_Create(1.5f, 0.0f, 0.0f),
                                          Vec3_Create(0.0f, 0.0f, 0.0f),
                                          Vec3_Create(0.0f, 0.0f, 0.0f), 1.0f);
    TEST_ASSERT(FLOAT_NEAR(capsuleDegenerate, 0.5f), "SDF_Capsule degenerate failed");

    /* Caja: centro dentro (negativo), superficie ~0, fuera (positivo) */
    Vector3 halfExt = Vec3_Create(1.0f, 1.0f, 1.0f);
    TEST_ASSERT(SDF_Box(Vec3_Create(0.0f, 0.0f, 0.0f), halfExt) < 0.0f, "SDF_Box inside failed");
    TEST_ASSERT(FLOAT_NEAR(SDF_Box(Vec3_Create(1.0f, 0.5f, 0.0f), halfExt), 0.0f), "SDF_Box surface failed");
    TEST_ASSERT(SDF_Box(Vec3_Create(2.0f, 0.0f, 0.0f), halfExt) > 0.0f, "SDF_Box outside failed");

    /* Cápsula cónica (tapered): punto sobre el eje -> -radio local */
    float tapered = SDF_TaperedCapsuleApprox(Vec3_Create(0.0f, 0.0f, 0.0f),
                                             Vec3_Create(0.0f, 0.0f, -2.0f),
                                             Vec3_Create(0.0f, 0.0f, 2.0f), 1.0f, 0.5f);
    TEST_ASSERT(tapered < 0.0f, "SDF_TaperedCapsuleApprox axis failed");
    TEST_ASSERT(isfinite(tapered), "SDF_TaperedCapsuleApprox no finito");

    /* Parámetros degenerados: nunca NaN/Inf */
    float degenerateCases[] = {
        SDF_Sphere(Vec3_Create(1.0f, 0.0f, 0.0f), 0.0f),
        SDF_Ellipsoid(Vec3_Create(1.0f, 0.0f, 0.0f), Vec3_Create(0.0f, 0.0f, 0.0f)),
        SDF_Capsule(Vec3_Create(1.0f, 0.0f, 0.0f), Vec3_Create(0.0f, 0.0f, 0.0f),
                    Vec3_Create(0.0f, 0.0f, 0.0f), 0.0f),
        SDF_Box(Vec3_Create(1.0f, 0.0f, 0.0f), Vec3_Create(0.0f, 0.0f, 0.0f)),
        SDF_TaperedCapsuleApprox(Vec3_Create(1.0f, 0.0f, 0.0f), Vec3_Create(0.0f, 0.0f, 0.0f),
                                 Vec3_Create(0.0f, 0.0f, 0.0f), 0.0f, 0.0f)
    };
    for (size_t i = 0; i < sizeof(degenerateCases) / sizeof(degenerateCases[0]); ++i) {
        TEST_ASSERT(isfinite(degenerateCases[i]), "Primitiva degenerada no finita");
    }

    printf("[PASS] test_sdf_primitives_extended\n");
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
    BodyPart tail1 = BodyPart_Create(0.0f, 0.0f, -7.3f, 1.4f, 2.2f, 1.0f, 0.0f);
    tail1.color.index = 3;
    BodyPart tail2 = BodyPart_Create(0.0f, 0.0f, -9.5f, 0.7f, 2.0f, 0.6f, 0.0f);
    tail2.color.index = 4;

    Monster_AddBodyPart(&lizard, chest);
    Monster_AddBodyPart(&lizard, abdomen);
    Monster_AddBodyPart(&lizard, tail1);
    Monster_AddBodyPart(&lizard, tail2);

    Mouth lizardMouth = Mouth_Create(0, Vec3_Create(0.0f, -0.2f, 0.82f), Vec3_Create(1.0f, 0.5f, 0.8f),
                                     Color_FromRGB(80, 0, 10), Color_FromRGB(180, 40, 40));
    lizardMouth.openFactor = 0.7f;
    Monster_AddMouth(&lizard, lizardMouth);

    return lizard;
}

static float CellDiagonal(const AABB3D* bounds, float voxelSize) {
    Vector3 size = Vec3_Sub(bounds->end, bounds->start);
    size.x = Math_Max(size.x, 0.001f);
    size.y = Math_Max(size.y, 0.001f);
    size.z = Math_Max(size.z, 0.001f);
    int resX = (int)ceilf(size.x / voxelSize);
    int resY = (int)ceilf(size.y / voxelSize);
    int resZ = (int)ceilf(size.z / voxelSize);
    float dx = size.x / (float)Math_Max(resX, 2);
    float dy = size.y / (float)Math_Max(resY, 2);
    float dz = size.z / (float)Math_Max(resZ, 2);
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

static float MaxTriangleEdge(const Mesh* mesh) {
    float maxLen = 0.0f;
    for (size_t t = 0; t < mesh->indexCount / 3; ++t) {
        Vector3 pa = mesh->vertices[mesh->indices[t * 3 + 0]].position;
        Vector3 pb = mesh->vertices[mesh->indices[t * 3 + 1]].position;
        Vector3 pc = mesh->vertices[mesh->indices[t * 3 + 2]].position;
        float l1 = Vec3_Distance(pa, pb);
        float l2 = Vec3_Distance(pb, pc);
        float l3 = Vec3_Distance(pc, pa);
        maxLen = Math_Max(maxLen, Math_Max(l1, Math_Max(l2, l3)));
    }
    return maxLen;
}

static void AssertMeshWellFormed(const char* label, const Mesh* mesh, const AABB3D* bounds, float voxelSize) {
    TEST_ASSERT(mesh->vertexCount > 0 && mesh->indexCount > 0, "Malla vacía");
    TEST_ASSERT(mesh->indexCount % 3 == 0, "indexCount no múltiplo de 3");

    MeshValidationResult res = Mesh_Validate(mesh);
    TEST_ASSERT(res.valid, "Malla no pasa Mesh_Validate");

    float diagonal = CellDiagonal(bounds, voxelSize);
    float maxEdge = MaxTriangleEdge(mesh);
    TEST_ASSERT(maxEdge <= diagonal + 0.001f, "Triángulo sliver supera la diagonal de celda");

    size_t triangleCount = mesh->indexCount / 3;
    TEST_ASSERT(mesh->vertexCount < 3u * triangleCount, "Caché de aristas no compartida");
    (void)label;
}

void test_lizard_mesh_stats(void) {
    Monster lizard = BuildLizard();

    MonsterSDF sdf = MonsterSDF_Create();
    TEST_ASSERT(MonsterSDF_Build(&sdf, &lizard, MonsterSDF_DefaultConfig()), "MonsterSDF_Build falló");

    SDFMesherConfig cfg = SDFMesher_DefaultConfig();
    cfg.voxelSize = 0.15f;
    cfg.maxResolution = 128;
    cfg.normalEps = 0.0f;

    SDFMesher mesher = SDFMesher_Create(cfg);
    Mesh mesh = Mesh_Create();

    SDFField field = MonsterSDF_GetField(&sdf);
    TEST_ASSERT(SDFMesher_GenerateMesh(&mesher, &field, &mesh), "SDFMesher_GenerateMesh (lagarto) falló");

    AABB3D bounds = field.getBounds(field.context);
    AssertMeshWellFormed("lagarto", &mesh, &bounds, cfg.voxelSize);

    Mesh_Free(&mesh);
    MonsterSDF_Free(&sdf);
    Monster_Free(&lizard);
    printf("[PASS] test_lizard_mesh_stats\n");
}

void test_ager_meshes(void) {
    Monster young = Monster_Create();
    Monster_Init(&young);
    young.colorPalette = ColorPalette_CreateGradient(Color_FromRGB(40, 200, 80), Color_FromRGB(100, 230, 120), 4);
    BodyPart* headY = Monster_GetHead(&young);
    if (headY) {
        headY->width = 1.0f; headY->height = 0.8f; headY->length = 1.2f;
    }
    BodyPart chestY = BodyPart_Create(0.0f, 0.0f, -1.3f, 1.2f, 1.4f, 0.9f, 0.0f);
    BodyPart tailY = BodyPart_Create(0.0f, 0.0f, -2.8f, 0.6f, 1.5f, 0.5f, 0.0f);
    Monster_AddBodyPart(&young, chestY);
    Monster_AddBodyPart(&young, tailY);
    Mouth youngMouth = Mouth_Create(0, Vec3_Create(0.0f, -0.15f, 0.48f), Vec3_Create(0.6f, 0.3f, 0.5f),
                                    Color_FromRGB(100, 10, 10), Color_FromRGB(150, 40, 40));
    youngMouth.openFactor = 0.3f;
    Monster_AddMouth(&young, youngMouth);

    Monster adult = Monster_Create();
    Monster_Init(&adult);
    adult.colorPalette = ColorPalette_CreateGradient(Color_FromRGB(220, 40, 40), Color_FromRGB(240, 180, 30), 4);
    BodyPart* headA = Monster_GetHead(&adult);
    if (headA) {
        headA->width = 2.5f; headA->height = 1.8f; headA->length = 2.8f;
    }
    BodyPart chestA = BodyPart_Create(0.0f, 0.0f, -3.0f, 3.2f, 3.5f, 2.2f, 0.0f);
    BodyPart abdomenA = BodyPart_Create(0.0f, 0.0f, -6.6f, 2.8f, 3.2f, 1.9f, 0.0f);
    BodyPart tail1A = BodyPart_Create(0.0f, 0.0f, -10.0f, 1.8f, 3.0f, 1.4f, 0.0f);
    BodyPart tail2A = BodyPart_Create(0.0f, 0.0f, -13.1f, 0.9f, 2.5f, 0.8f, 0.0f);
    Monster_AddBodyPart(&adult, chestA);
    Monster_AddBodyPart(&adult, abdomenA);
    Monster_AddBodyPart(&adult, tail1A);
    Monster_AddBodyPart(&adult, tail2A);
    Mouth adultMouth = Mouth_Create(0, Vec3_Create(0.0f, -0.3f, 1.2f), Vec3_Create(1.6f, 0.9f, 1.2f),
                                    Color_FromRGB(60, 0, 0), Color_FromRGB(200, 30, 30));
    adultMouth.openFactor = 0.95f;
    Monster_AddMouth(&adult, adultMouth);

    MonsterAger ager = MonsterAger_Create(&young, &adult, 0.0f);

    SDFMesherConfig cfg = SDFMesher_DefaultConfig();
    cfg.voxelSize = 0.2f;
    cfg.maxResolution = 128;
    cfg.normalEps = 0.0f;
    SDFMesher mesher = SDFMesher_Create(cfg);

    const float ages[] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
    for (size_t i = 0; i < sizeof(ages) / sizeof(ages[0]); ++i) {
        MonsterAger_SetPerc(&ager, ages[i]);
        const Monster* current = MonsterAger_GetResultConst(&ager);

        MonsterSDF sdf = MonsterSDF_Create();
        TEST_ASSERT(MonsterSDF_Build(&sdf, current, MonsterSDF_DefaultConfig()), "MonsterSDF_Build (ager) falló");

        Mesh mesh = Mesh_Create();
        SDFField field = MonsterSDF_GetField(&sdf);
        TEST_ASSERT(SDFMesher_GenerateMesh(&mesher, &field, &mesh), "SDFMesher_GenerateMesh (ager) falló");

        AABB3D bounds = field.getBounds(field.context);
        AssertMeshWellFormed("ager", &mesh, &bounds, cfg.voxelSize);

        Mesh_Free(&mesh);
        MonsterSDF_Free(&sdf);
    }

    MonsterAger_Free(&ager);
    Monster_Free(&young);
    Monster_Free(&adult);
    printf("[PASS] test_ager_meshes\n");
}

void test_monster_sdf_buffer_reuse(void) {
    Monster lizard = BuildLizard();

    MonsterSDF sdf = MonsterSDF_Create();
    TEST_ASSERT(MonsterSDF_Build(&sdf, &lizard, MonsterSDF_DefaultConfig()), "Primera build falló");

    MonsterSDFBodyPart* firstParts = sdf.bodyParts;
    MonsterSDFConnector* firstConns = sdf.connectors;
    TEST_ASSERT(firstParts != NULL, "bodyParts NULL tras build");

    TEST_ASSERT(MonsterSDF_Build(&sdf, &lizard, MonsterSDF_DefaultConfig()), "Segunda build falló");
    TEST_ASSERT(sdf.bodyParts == firstParts, "bodyParts no se reutilizó entre builds");
    TEST_ASSERT(sdf.connectors == firstConns, "connectors no se reutilizó entre builds");

    MonsterSDF_Free(&sdf);
    Monster_Free(&lizard);
    printf("[PASS] test_monster_sdf_buffer_reuse\n");
}

void test_monster_visual_eyes_fingerprint(void) {
    Monster lizard = BuildLizard();

    Eye leftEye = Eye_Create(0, Vec3_Create(-0.55f, 0.25f, 0.85f), Vec3_Create(0.3f, 0.28f, 0.18f), COLOR_WHITE, Color_FromRGB(20, 20, 20));
    leftEye.pupilScale = 0.45f;
    Monster_AddEye(&lizard, leftEye);

    Eye rightEye = Eye_Create(0, Vec3_Create(0.55f, 0.25f, 0.85f), Vec3_Create(0.3f, 0.28f, 0.18f), COLOR_WHITE, Color_FromRGB(20, 20, 20));
    rightEye.pupilScale = 0.45f;
    Monster_AddEye(&lizard, rightEye);

    SDFMesherConfig cfg = SDFMesher_DefaultConfig();
    cfg.voxelSize = 0.2f;
    cfg.maxResolution = 128;

    MonsterVisual visual = MonsterVisual_Create(cfg);
    TEST_ASSERT(MonsterVisual_RebuildNow(&visual, &lizard, MonsterSDF_DefaultConfig()), "RebuildNow falló");

    TEST_ASSERT(MonsterVisual_GetEyeCount(&visual) == 2, "eyeCount incorrecto");
    const Mesh* sclera = MonsterVisual_GetEyeSclera(&visual, 0);
    const Mesh* pupil = MonsterVisual_GetEyePupil(&visual, 0);
    TEST_ASSERT(sclera != NULL && sclera->vertexCount > 0, "esclerótica vacía");
    TEST_ASSERT(pupil != NULL && pupil->vertexCount > 0, "pupila vacía");
    TEST_ASSERT(Mesh_Validate(sclera).valid && Mesh_Validate(pupil).valid, "malla de ojo inválida");

    float scleraRadius = 0.5f * Math_Max(0.3f, Math_Max(0.28f, 0.001f));
    float pupilDiameter = 0.0f;
    for (size_t i = 0; i < pupil->vertexCount; ++i) {
        for (size_t j = i + 1; j < pupil->vertexCount; ++j) {
            pupilDiameter = Math_Max(pupilDiameter, Vec3_Length(Vec3_Sub(pupil->vertices[i].position, pupil->vertices[j].position)));
        }
    }
    TEST_ASSERT(pupilDiameter < 2.0f * scleraRadius, "pupila no menor que esclerótica");
    TEST_ASSERT(pupil->vertices[0].color.r == 20 && pupil->vertices[0].color.g == 20 &&
                pupil->vertices[0].color.b == 20, "color de pupila incorrecto");

    size_t bodyVertsBefore = MonsterVisual_GetMesh(&visual)->vertexCount;

    bool unchanged = MonsterVisual_Update(&visual, &lizard, 0.0f, 0.0f, MonsterSDF_DefaultConfig());
    TEST_ASSERT(!unchanged, "Update sin cambios reconstruyó (fingerprint)");

    BodyPart* head = Monster_GetHead(&lizard);
    head->widthRender = 2.5f;
    bool changed = MonsterVisual_Update(&visual, &lizard, 0.0f, 0.0f, MonsterSDF_DefaultConfig());
    TEST_ASSERT(changed, "Update con geometría cambiada no reconstruyó");
    TEST_ASSERT(MonsterVisual_GetMesh(&visual)->vertexCount != bodyVertsBefore, "Malla no cambió tras modificar geometría");

    MonsterVisual_Free(&visual);
    Monster_Free(&lizard);
    printf("[PASS] test_monster_visual_eyes_fingerprint\n");
}

static void test_monster_visual_generation_and_transactional(void) {
    Monster lizard = BuildLizard();
    SDFMesherConfig cfg = SDFMesher_DefaultConfig();
    cfg.resolutionX = 16;
    cfg.resolutionY = 16;
    cfg.resolutionZ = 16;

    MonsterVisual visual = MonsterVisual_Create(cfg);
    TEST_ASSERT(MonsterVisual_GetGeneration(&visual) == 0, "Generación inicial debe ser 0");

    TEST_ASSERT(MonsterVisual_RebuildNow(&visual, &lizard, MonsterSDF_DefaultConfig()), "Primera reconstrucción falló");
    TEST_ASSERT(MonsterVisual_GetGeneration(&visual) == 1, "Generación debe ser 1 tras reconstrucción");

    /* Simular fallo pasando monster NULL -> RebuildNow debe retornar false y NO alterar la generación ni la malla previa */
    const Mesh* meshBefore = MonsterVisual_GetMesh(&visual);
    size_t vertsBefore = meshBefore->vertexCount;

    bool failedBuild = MonsterVisual_RebuildNow(&visual, NULL, MonsterSDF_DefaultConfig());
    TEST_ASSERT(!failedBuild, "RebuildNow con NULL debe retornar false");
    TEST_ASSERT(MonsterVisual_GetGeneration(&visual) == 1, "Generación no debe incrementar tras fallo");
    TEST_ASSERT(MonsterVisual_GetMesh(&visual)->vertexCount == vertsBefore, "Malla no debe ser alterada tras fallo");

    MonsterVisual_Free(&visual);
    Monster_Free(&lizard);
    printf("[PASS] test_monster_visual_generation_and_transactional\n");
}

static void test_mouth_bounds_isolation(void) {
    Monster lizard = BuildLizard();

    MonsterSDF sdfBase = MonsterSDF_Create();
    MonsterSDF_Build(&sdfBase, &lizard, MonsterSDF_DefaultConfig());
    AABB3D boundsBase = MonsterSDF_GetField(&sdfBase).getBounds(MonsterSDF_GetField(&sdfBase).context);

    /* Añadir una boca gigante en la cabeza */
    Mouth giantMouth = Mouth_Create(0, Vec3_Create(0.0f, 0.0f, 0.0f), Vec3_Create(100.0f, 100.0f, 100.0f), COLOR_BLACK, COLOR_WHITE);
    Monster_AddMouth(&lizard, giantMouth);

    MonsterSDF sdfMouth = MonsterSDF_Create();
    MonsterSDF_Build(&sdfMouth, &lizard, MonsterSDF_DefaultConfig());
    AABB3D boundsMouth = MonsterSDF_GetField(&sdfMouth).getBounds(MonsterSDF_GetField(&sdfMouth).context);

    /* La boca sustrativa no debe expandir el AABB en absoluto */
    TEST_ASSERT(FLOAT_NEAR(boundsBase.start.x, boundsMouth.start.x) &&
                FLOAT_NEAR(boundsBase.end.x, boundsMouth.end.x), "Boca sustrativa alteró AABB en X");
    TEST_ASSERT(FLOAT_NEAR(boundsBase.start.y, boundsMouth.start.y) &&
                FLOAT_NEAR(boundsBase.end.y, boundsMouth.end.y), "Boca sustrativa alteró AABB en Y");
    TEST_ASSERT(FLOAT_NEAR(boundsBase.start.z, boundsMouth.start.z) &&
                FLOAT_NEAR(boundsBase.end.z, boundsMouth.end.z), "Boca sustrativa alteró AABB en Z");

    MonsterSDF_Free(&sdfBase);
    MonsterSDF_Free(&sdfMouth);
    Monster_Free(&lizard);
    printf("[PASS] test_mouth_bounds_isolation\n");
}

static void test_zero_scale_eye_ager_transition(void) {
    /* Young: 0 ojos */
    Monster young = BuildLizard();
    Monster_ClearEyes(&young);

    /* Adult: 1 ojo válido */
    Monster adult = BuildLizard();
    Eye adultEye = Eye_Create(0, Vec3_Create(0.5f, 0.5f, 0.5f), Vec3_Create(0.4f, 0.4f, 0.4f), COLOR_WHITE, COLOR_BLACK);
    Monster_AddEye(&adult, adultEye);

    MonsterAger ager = MonsterAger_Create(&young, &adult, 0.0f);

    SDFMesherConfig cfg = SDFMesher_DefaultConfig();
    cfg.resolutionX = 16;
    cfg.resolutionY = 16;
    cfg.resolutionZ = 16;
    MonsterVisual visual = MonsterVisual_Create(cfg);

    /* Age 0.0 */
    MonsterAger_SetPerc(&ager, 0.0f);
    const Monster* current = MonsterAger_GetResultConst(&ager);
    TEST_ASSERT(MonsterVisual_RebuildNow(&visual, current, MonsterSDF_DefaultConfig()), "Rebuild falló a edad 0.0");
    TEST_ASSERT(MonsterVisual_GetEyeCount(&visual) == 1, "Conteo lógico de ojos debe ser 1 a edad 0.0");
    const Mesh* sclera0 = MonsterVisual_GetEyeSclera(&visual, 0);
    TEST_ASSERT(sclera0 != NULL && sclera0->vertexCount == 0, "Malla de ojo a edad 0.0 debe estar vacía");

    /* Age 0.5 */
    MonsterAger_SetPerc(&ager, 0.5f);
    current = MonsterAger_GetResultConst(&ager);
    TEST_ASSERT(MonsterVisual_RebuildNow(&visual, current, MonsterSDF_DefaultConfig()), "Rebuild falló a edad 0.5");
    TEST_ASSERT(MonsterVisual_GetEyeCount(&visual) == 1, "Conteo lógico de ojos debe ser 1 a edad 0.5");
    const Mesh* scleraHalf = MonsterVisual_GetEyeSclera(&visual, 0);
    TEST_ASSERT(scleraHalf != NULL && scleraHalf->vertexCount > 0, "Malla de ojo a edad 0.5 debe ser no vacía");

    /* Age 1.0 */
    MonsterAger_SetPerc(&ager, 1.0f);
    current = MonsterAger_GetResultConst(&ager);
    TEST_ASSERT(MonsterVisual_RebuildNow(&visual, current, MonsterSDF_DefaultConfig()), "Rebuild falló a edad 1.0");
    TEST_ASSERT(MonsterVisual_GetEyeCount(&visual) == 1, "Conteo lógico de ojos debe ser 1 a edad 1.0");
    const Mesh* scleraFull = MonsterVisual_GetEyeSclera(&visual, 0);
    const Mesh* pupilFull = MonsterVisual_GetEyePupil(&visual, 0);
    TEST_ASSERT(scleraFull != NULL && scleraFull->vertexCount > 0, "Malla de ojo a edad 1.0 debe ser no vacía");
    TEST_ASSERT(Mesh_Validate(scleraFull).valid, "Esclerótica a edad 1.0 debe ser válida");
    TEST_ASSERT(Mesh_Validate(pupilFull).valid, "Pupila a edad 1.0 debe ser válida");

    MonsterVisual_Free(&visual);
    MonsterAger_Free(&ager);
    Monster_Free(&young);
    printf("[PASS] test_zero_scale_eye_ager_transition\n");
}

void test_monster_visual_complete_fingerprint(void) {
    Monster lizard = BuildLizard();
    Eye lizardEye = Eye_Create(0, Vec3_Create(0.5f, 0.5f, 0.5f), Vec3_Create(0.4f, 0.4f, 0.4f), COLOR_WHITE, COLOR_BLACK);
    Monster_AddEye(&lizard, lizardEye);

    SDFMesherConfig cfg = SDFMesher_DefaultConfig();
    cfg.resolutionX = 16;
    cfg.resolutionY = 16;
    cfg.resolutionZ = 16;

    MonsterVisual visual = MonsterVisual_Create(cfg);
    MonsterVisual_RebuildNow(&visual, &lizard, MonsterSDF_DefaultConfig());
    uint64_t gen = MonsterVisual_GetGeneration(&visual);

    /* Update sin cambios -> NO debe incrementar generación */
    MonsterVisual_Update(&visual, &lizard, 0.016f, 0.0f, MonsterSDF_DefaultConfig());
    TEST_ASSERT(MonsterVisual_GetGeneration(&visual) == gen, "Update sin cambios no debe incrementar generación");

    /* 1. mouth.rotation */
    Mouth* m = Monster_GetMouth(&lizard, 0);
    if (m) {
        m->rotation.x += 0.5f;
        MonsterVisual_Update(&visual, &lizard, 0.016f, 0.0f, MonsterSDF_DefaultConfig());
        TEST_ASSERT(MonsterVisual_GetGeneration(&visual) == gen + 1, "mouth.rotation debe incrementar generación");
        gen++;
    }

    /* 2. palette color */
    if (lizard.colorPalette.count > 0) {
        lizard.colorPalette.colors[0].r ^= 0xFF;
        MonsterVisual_Update(&visual, &lizard, 0.016f, 0.0f, MonsterSDF_DefaultConfig());
        TEST_ASSERT(MonsterVisual_GetGeneration(&visual) == gen + 1, "palette color debe incrementar generación");
        gen++;
    }

    /* 3. eye.rotation */
    Eye* eye = Monster_GetEye(&lizard, 0);
    if (eye) {
        eye->rotation.y += 0.5f;
        MonsterVisual_Update(&visual, &lizard, 0.016f, 0.0f, MonsterSDF_DefaultConfig());
        TEST_ASSERT(MonsterVisual_GetGeneration(&visual) == gen + 1, "eye.rotation debe incrementar generación");
        gen++;

        /* 4. eye.scale */
        eye->scale.x += 0.2f;
        MonsterVisual_Update(&visual, &lizard, 0.016f, 0.0f, MonsterSDF_DefaultConfig());
        TEST_ASSERT(MonsterVisual_GetGeneration(&visual) == gen + 1, "eye.scale debe incrementar generación");
        gen++;

        /* 5. eye.offset */
        eye->offset.z += 0.1f;
        MonsterVisual_Update(&visual, &lizard, 0.016f, 0.0f, MonsterSDF_DefaultConfig());
        TEST_ASSERT(MonsterVisual_GetGeneration(&visual) == gen + 1, "eye.offset debe incrementar generación");
        gen++;

        /* 6. eye.pupilScale */
        eye->pupilScale *= 0.5f;
        MonsterVisual_Update(&visual, &lizard, 0.016f, 0.0f, MonsterSDF_DefaultConfig());
        TEST_ASSERT(MonsterVisual_GetGeneration(&visual) == gen + 1, "eye.pupilScale debe incrementar generación");
        gen++;
    }

    /* 7. SDF voxelSize */
    visual.mesher.config.voxelSize = 0.25f;
    MonsterVisual_Update(&visual, &lizard, 0.016f, 0.0f, MonsterSDF_DefaultConfig());
    TEST_ASSERT(MonsterVisual_GetGeneration(&visual) == gen + 1, "voxelSize debe incrementar generación");

    MonsterVisual_Free(&visual);
    Monster_Free(&lizard);
    printf("[PASS] test_monster_visual_complete_fingerprint\n");
}

void run_sdf_tests(void) {
    test_sdf_primitives_and_ops();
    test_sdf_primitives_extended();
    test_monster_sdf_evaluation();
    test_sdf_mesher();
    test_monster_visual();
    test_monster_visual_eyes_fingerprint();
    test_monster_visual_generation_and_transactional();
    test_mouth_bounds_isolation();
    test_zero_scale_eye_ager_transition();
    test_monster_visual_complete_fingerprint();
    test_lizard_mesh_stats();
    test_ager_meshes();
    test_monster_sdf_buffer_reuse();
}
