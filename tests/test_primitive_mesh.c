#include "test_utils.h"
#include "Mesh.h"
#include "PrimitiveMesh.h"
#include "Vector.h"
#include <math.h>

static void test_uv_sphere_valid(void) {
    Mesh mesh = Mesh_Create();
    Vector3 center = Vec3_Create(1.0f, 2.0f, 3.0f);
    float radius = 0.5f;

    TEST_ASSERT(PrimitiveMesh_GenerateUVSphere(&mesh, center, radius, 12, 8, COLOR_WHITE), "GenerateUVSphere falló");

    size_t expectedVertices = 2 + (8 - 1) * 12;
    size_t expectedIndices = 3 * 2 * 12 * (8 - 1);
    TEST_ASSERT(mesh.vertexCount == expectedVertices, "vertexCount incorrecto");
    TEST_ASSERT(mesh.indexCount == expectedIndices, "indexCount incorrecto");

    MeshValidationResult res = Mesh_Validate(&mesh);
    TEST_ASSERT(res.valid, "Malla UV esfera inválida");

    for (size_t i = 0; i < mesh.vertexCount; ++i) {
        Vector3 delta = Vec3_Sub(mesh.vertices[i].position, center);
        TEST_ASSERT(fabsf(Vec3_Length(delta) - radius) < 1e-4f, "Radio de vértice incorrecto");
        TEST_ASSERT(fabsf(Vec3_Length(mesh.vertices[i].normal) - 1.0f) < 1e-4f, "Normal no unitaria");
        TEST_ASSERT(mesh.vertices[i].color.r == 255 && mesh.vertices[i].color.g == 255 &&
                    mesh.vertices[i].color.b == 255 && mesh.vertices[i].color.a == 255, "Color no aplicado");
    }

    Mesh_Free(&mesh);
    printf("[PASS] test_uv_sphere_valid\n");
}

static void test_uv_sphere_winding(void) {
    Mesh mesh = Mesh_Create();
    TEST_ASSERT(PrimitiveMesh_GenerateUVSphere(&mesh, Vec3_Zero(), 1.0f, 10, 6, COLOR_WHITE), "GenerateUVSphere falló");

    for (size_t t = 0; t < mesh.indexCount / 3; ++t) {
        Vector3 a = mesh.vertices[mesh.indices[t * 3 + 0]].position;
        Vector3 b = mesh.vertices[mesh.indices[t * 3 + 1]].position;
        Vector3 c = mesh.vertices[mesh.indices[t * 3 + 2]].position;

        Vector3 faceNormal = Vec3_Cross(Vec3_Sub(b, a), Vec3_Sub(c, a));
        Vector3 centroid = Vec3_Scale(Vec3_Add(a, Vec3_Add(b, c)), 1.0f / 3.0f);

        TEST_ASSERT(Vec3_Dot(faceNormal, centroid) > 0.0f, "Triángulo con winding invertido");
    }

    Mesh_Free(&mesh);
    printf("[PASS] test_uv_sphere_winding\n");
}

static void test_uv_sphere_invalid_params(void) {
    Mesh mesh = Mesh_Create();

    TEST_ASSERT(!PrimitiveMesh_GenerateUVSphere(&mesh, Vec3_Zero(), 0.0f, 8, 4, COLOR_WHITE), "radio 0 aceptado");
    TEST_ASSERT(!PrimitiveMesh_GenerateUVSphere(&mesh, Vec3_Zero(), 1.0f, 2, 4, COLOR_WHITE), "segments < 3 aceptado");
    TEST_ASSERT(!PrimitiveMesh_GenerateUVSphere(&mesh, Vec3_Zero(), 1.0f, 8, 1, COLOR_WHITE), "rings < 2 aceptado");
    TEST_ASSERT(mesh.vertexCount == 0 && mesh.indexCount == 0, "Malla modificada con parámetros inválidos");

    Mesh_Free(&mesh);
    printf("[PASS] test_uv_sphere_invalid_params\n");
}

static void test_ellipsoid_transform(void) {
    Mesh mesh = Mesh_Create();
    Transform3D t = Transform3D_Create(
        Vec3_Create(2.0f, -1.0f, 5.0f),
        Vec3_Create(0.0f, 90.0f, 0.0f), /* Rotación 90 deg en Y */
        Vec3_Create(2.0f, 1.0f, 0.5f)   /* Escala no uniforme */
    );

    TEST_ASSERT(PrimitiveMesh_GenerateEllipsoid(&mesh, t, 16, 12, COLOR_WHITE), "GenerateEllipsoid falló");

    MeshValidationResult res = Mesh_Validate(&mesh);
    TEST_ASSERT(res.valid, "Malla de elipsoide generada es inválida");

    /* Verificar que las normales son unitarias y están adecuadamente transformadas */
    for (size_t i = 0; i < mesh.vertexCount; ++i) {
        float normLen = Vec3_Length(mesh.vertices[i].normal);
        TEST_ASSERT(fabsf(normLen - 1.0f) < 1e-3f, "Normal de elipsoide no es unitaria");
    }

    Mesh_Free(&mesh);
    printf("[PASS] test_ellipsoid_transform\n");
}

void run_primitive_mesh_tests(void) {
    test_uv_sphere_valid();
    test_uv_sphere_winding();
    test_uv_sphere_invalid_params();
    test_ellipsoid_transform();
}
