#include "test_utils.h"
#include "Mesh.h"
#include "MathUtils.h"
#include "SDFMesher.h"
#include "SDFPrimitives.h"
#include "SDFOperations.h"
#include "AABB.h"
#include <math.h>

/* ============================================================
 * Contexto SDF genérico: esfera simple (sin Monster)
 * ============================================================ */

typedef struct SphereFieldContext {
    float radius;
} SphereFieldContext;

static SDFSample SphereField_Evaluate(const void* ctx, Vector3 point) {
    const SphereFieldContext* c = (const SphereFieldContext*)ctx;
    return SDFSample_Create(SDF_Sphere(point, c->radius), COLOR_WHITE, SDF_MATERIAL_SKIN);
}

static AABB3D SphereField_Bounds(const void* ctx) {
    const SphereFieldContext* c = (const SphereFieldContext*)ctx;
    float r = c->radius;
    return AABB_FromMinMax(
        Vec3_Create(-2.0f * r, -2.0f * r, -2.0f * r),
        Vec3_Create(2.0f * r, 2.0f * r, 2.0f * r)
    );
}

static SDFField SphereField_Create(SphereFieldContext* ctx, float radius) {
    ctx->radius = radius;
    return (SDFField){
        .evaluate = SphereField_Evaluate,
        .getBounds = SphereField_Bounds,
        .context = (const void*)ctx
    };
}

/* ============================================================
 * Mesh_AddTriangle: guardas de rango y degeneración
 * ============================================================ */

static void test_add_triangle_guards(void) {
    Mesh mesh = Mesh_Create();

    MeshVertex v0 = {.position = Vec3_Create(0, 0, 0), .normal = Vec3_Create(0, 0, 1), .color = COLOR_WHITE};
    MeshVertex v1 = {.position = Vec3_Create(1, 0, 0), .normal = Vec3_Create(0, 0, 1), .color = COLOR_WHITE};
    MeshVertex v2 = {.position = Vec3_Create(0, 1, 0), .normal = Vec3_Create(0, 0, 1), .color = COLOR_WHITE};

    unsigned int i0 = 0, i1 = 0, i2 = 0;
    TEST_ASSERT(Mesh_AddVertex(&mesh, v0, &i0), "AddVertex 0 falló");
    TEST_ASSERT(Mesh_AddVertex(&mesh, v1, &i1), "AddVertex 1 falló");
    TEST_ASSERT(Mesh_AddVertex(&mesh, v2, &i2), "AddVertex 2 falló");

    /* Triángulo válido */
    TEST_ASSERT(Mesh_AddTriangle(&mesh, i0, i1, i2), "AddTriangle válido rechazado");
    TEST_ASSERT(mesh.indexCount == 3, "indexCount incorrecto tras AddTriangle válido");

    /* Índices fuera de rango: rechazo sin escritura parcial */
    size_t before = mesh.indexCount;
    TEST_ASSERT(!Mesh_AddTriangle(&mesh, i0, i1, 99u), "AddTriangle aceptó índice fuera de rango");
    TEST_ASSERT(!Mesh_AddTriangle(&mesh, 99u, i1, i2), "AddTriangle aceptó índice fuera de rango (a)");
    TEST_ASSERT(!Mesh_AddTriangle(&mesh, i0, 99u, i2), "AddTriangle aceptó índice fuera de rango (b)");
    TEST_ASSERT(mesh.indexCount == before, "AddTriangle inválido escribió índices");

    /* Índices repetidos: triángulo degenerado */
    TEST_ASSERT(!Mesh_AddTriangle(&mesh, i0, i0, i1), "AddTriangle aceptó a==b");
    TEST_ASSERT(!Mesh_AddTriangle(&mesh, i0, i1, i1), "AddTriangle aceptó b==c");
    TEST_ASSERT(!Mesh_AddTriangle(&mesh, i0, i1, i0), "AddTriangle aceptó a==c");
    TEST_ASSERT(mesh.indexCount == before, "AddTriangle degenerado escribió índices");

    Mesh_Free(&mesh);
    printf("[PASS] test_add_triangle_guards\n");
}

/* ============================================================
 * Mesh_Validate: detección de corrupción
 * ============================================================ */

static void test_mesh_validate(void) {
    /* Malla limpia */
    Mesh clean = Mesh_Create();
    MeshVertex v0 = {.position = Vec3_Create(0, 0, 0), .normal = Vec3_Create(0, 0, 1), .color = COLOR_WHITE};
    MeshVertex v1 = {.position = Vec3_Create(1, 0, 0), .normal = Vec3_Create(0, 0, 1), .color = COLOR_WHITE};
    MeshVertex v2 = {.position = Vec3_Create(0, 1, 0), .normal = Vec3_Create(0, 0, 1), .color = COLOR_WHITE};
    unsigned int a = 0, b = 0, c = 0;
    Mesh_AddVertex(&clean, v0, &a);
    Mesh_AddVertex(&clean, v1, &b);
    Mesh_AddVertex(&clean, v2, &c);
    Mesh_AddTriangle(&clean, a, b, c);

    MeshValidationResult res = Mesh_Validate(&clean);
    TEST_ASSERT(res.valid, "Mesh_Validate rechazó malla limpia");
    TEST_ASSERT(res.invalidIndexCount == 0, "invalidIndexCount != 0 en malla limpia");
    TEST_ASSERT(res.degenerateTriangleCount == 0, "degenerateTriangleCount != 0 en malla limpia");
    TEST_ASSERT(res.nonFiniteVertexCount == 0, "nonFiniteVertexCount != 0 en malla limpia");
    TEST_ASSERT(res.nonFiniteNormalCount == 0, "nonFiniteNormalCount != 0 en malla limpia");

    /* Índice fuera de rango inyectado */
    clean.indices[1] = 500u;
    res = Mesh_Validate(&clean);
    TEST_ASSERT(!res.valid, "Mesh_Validate no detectó índice fuera de rango");
    TEST_ASSERT(res.invalidIndexCount == 1, "invalidIndexCount != 1 con índice corrupto");
    clean.indices[1] = b;

    /* Triángulo degenerado inyectado */
    clean.indices[2] = clean.indices[0];
    res = Mesh_Validate(&clean);
    TEST_ASSERT(!res.valid, "Mesh_Validate no detectó triángulo degenerado");
    TEST_ASSERT(res.degenerateTriangleCount == 1, "degenerateTriangleCount != 1");
    clean.indices[2] = c;

    /* Posición no finita inyectada */
    clean.vertices[0].position.x = NAN;
    res = Mesh_Validate(&clean);
    TEST_ASSERT(!res.valid, "Mesh_Validate no detectó posición NaN");
    TEST_ASSERT(res.nonFiniteVertexCount == 1, "nonFiniteVertexCount != 1");
    clean.vertices[0].position.x = 0.0f;

    /* Normal no finita inyectada */
    clean.vertices[0].normal.y = INFINITY;
    res = Mesh_Validate(&clean);
    TEST_ASSERT(!res.valid, "Mesh_Validate no detectó normal infinita");
    TEST_ASSERT(res.nonFiniteNormalCount == 1, "nonFiniteNormalCount != 1");
    clean.vertices[0].normal.y = 0.0f;

    /* Conteo de índices no múltiplo de 3 */
    clean.indices[clean.indexCount++] = a;
    res = Mesh_Validate(&clean);
    TEST_ASSERT(!res.valid, "Mesh_Validate no detectó indexCount % 3 != 0");
    TEST_ASSERT(res.invalidIndexCount == 1, "invalidIndexCount != 1 por resto");

    /* NULL = inválida */
    res = Mesh_Validate(NULL);
    TEST_ASSERT(!res.valid, "Mesh_Validate(NULL) debería ser inválido");

    Mesh_Free(&clean);
    printf("[PASS] test_mesh_validate\n");
}

static void test_zero_area_collinear_validation(void) {
    Mesh mesh = Mesh_Create();

    /* 3 vértices colineales en el eje X */
    MeshVertex v0 = {.position = Vec3_Create(0, 0, 0), .normal = Vec3_Create(0, 1, 0), .color = COLOR_WHITE};
    MeshVertex v1 = {.position = Vec3_Create(1, 0, 0), .normal = Vec3_Create(0, 1, 0), .color = COLOR_WHITE};
    MeshVertex v2 = {.position = Vec3_Create(2, 0, 0), .normal = Vec3_Create(0, 1, 0), .color = COLOR_WHITE};

    MeshIndex a = 0, b = 0, c = 0;
    Mesh_AddVertex(&mesh, v0, &a);
    Mesh_AddVertex(&mesh, v1, &b);
    Mesh_AddVertex(&mesh, v2, &c);
    Mesh_AddTriangle(&mesh, a, b, c);

    MeshValidationResult res = Mesh_Validate(&mesh);
    TEST_ASSERT(!res.valid, "Mesh_Validate debe fallar para triángulos de área cero con vértices colineales");
    TEST_ASSERT(res.zeroAreaTriangleCount == 1, "zeroAreaTriangleCount debe ser 1");

    Mesh_Free(&mesh);
    printf("[PASS] test_zero_area_collinear_validation\n");
}

/* ============================================================
 * Pipeline E2E: esfera genérica -> SDFMesher -> Mesh
 * ============================================================ */

static float MaxTriangleEdgeLength(const Mesh* mesh, Vector3* outCellDiagonal) {
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
    (void)outCellDiagonal;
    return maxLen;
}

static void test_sphere_field_pipeline(void) {
    SphereFieldContext ctx;
    SDFField field = SphereField_Create(&ctx, 1.0f);

    SDFMesherConfig cfg = SDFMesher_DefaultConfig();
    cfg.voxelSize = 0.2f;
    cfg.maxResolution = 128;
    cfg.normalEps = 0.0f; /* forzar normalEps automático */

    SDFMesher mesher = SDFMesher_Create(cfg);
    Mesh mesh = Mesh_Create();

    bool success = SDFMesher_GenerateMesh(&mesher, &field, &mesh);
    TEST_ASSERT(success, "SDFMesher_GenerateMesh (esfera) devolvió false");
    TEST_ASSERT(mesh.vertexCount > 0 && mesh.indexCount > 0, "Malla de esfera vacía");
    TEST_ASSERT(mesh.indexCount % 3 == 0, "indexCount no múltiplo de 3");

    /* Validación estructural completa */
    MeshValidationResult res = Mesh_Validate(&mesh);
    TEST_ASSERT(res.valid, "Malla de esfera no pasó Mesh_Validate");
    TEST_ASSERT(mesh.vertices[0].material == SDF_MATERIAL_SKIN, "El material SDF de la esfera se perdió");

    /* Sin slivers: ninguna arista de triángulo supera la diagonal de celda */
    Vector3 size = Vec3_Sub(field.getBounds(field.context).end, field.getBounds(field.context).start);
    size.x = Math_Max(size.x, 0.001f);
    size.y = Math_Max(size.y, 0.001f);
    size.z = Math_Max(size.z, 0.001f);
    int resX = (int)ceilf(size.x / cfg.voxelSize);
    int resY = (int)ceilf(size.y / cfg.voxelSize);
    int resZ = (int)ceilf(size.z / cfg.voxelSize);
    float dx = size.x / (float)resX;
    float dy = size.y / (float)resY;
    float dz = size.z / (float)resZ;
    float cellDiagonal = sqrtf(dx * dx + dy * dy + dz * dz);

    float maxEdge = MaxTriangleEdgeLength(&mesh, NULL);
    TEST_ASSERT(maxEdge <= cellDiagonal + 0.001f,
                "Triángulo sliver: arista mayor que la diagonal de celda");

    /* Vértices sobre la superficie (radio 1.0) con tolerancia de interpolación */
    for (size_t i = 0; i < mesh.vertexCount; ++i) {
        float dist = fabsf(Vec3_Length(mesh.vertices[i].position) - 1.0f);
        TEST_ASSERT(dist <= 2.0f * cellDiagonal,
                    "Vértice de esfera fuera de la superficie esperada");
    }

    Mesh_Free(&mesh);
    SDFMesher_Free(&mesher);
    printf("[PASS] test_sphere_field_pipeline\n");
}

static void test_topology_audit(void) {
    Mesh mesh=Mesh_Create(); MeshVertex v={.position=Vec3_Zero(),.normal=Vec3_Create(0,1,0),.color=COLOR_WHITE,.material=SDF_MATERIAL_SKIN};
    MeshIndex ids[5]; for(size_t i=0;i<5;++i){v.position=Vec3_Create((float)i,0,0);Mesh_AddVertex(&mesh,v,&ids[i]);}
    Mesh_AddTriangle(&mesh,0,1,2); Mesh_AddTriangle(&mesh,1,0,3); Mesh_AddTriangle(&mesh,0,1,4);
    MeshValidationResult r=Mesh_Validate(&mesh);
    TEST_ASSERT(r.nonManifoldEdgeCount==1, "No se detectó la arista no-manifold");
    TEST_ASSERT(r.boundaryEdgeCount>0 && !r.watertight, "La auditoría no detectó fronteras");
    Mesh_Free(&mesh); printf("[PASS] test_topology_audit\n");
}

/* ============================================================
 * Caché de aristas compartida entre celdas vecinas
 * ============================================================ */

static void test_edge_cache_sharing(void) {
    SphereFieldContext ctx;
    SDFField field = SphereField_Create(&ctx, 1.0f);

    SDFMesherConfig cfg = SDFMesher_DefaultConfig();
    cfg.voxelSize = 0.25f;
    cfg.maxResolution = 128;

    SDFMesher mesher = SDFMesher_Create(cfg);
    Mesh mesh = Mesh_Create();

    TEST_ASSERT(SDFMesher_GenerateMesh(&mesher, &field, &mesh), "Generación de esfera falló");

    size_t triangleCount = mesh.indexCount / 3;
    TEST_ASSERT(triangleCount > 4, "Esfera generó muy pocos triángulos");

    /* Sin caché compartida, cada triángulo crearía 3 vértices nuevos:
       vertexCount == 3 * triangleCount. Con caché debe ser menor. */
    TEST_ASSERT(mesh.vertexCount < 3u * triangleCount,
                "Caché de aristas no está compartiendo vértices entre celdas");

    Mesh_Free(&mesh);
    SDFMesher_Free(&mesher);
    printf("[PASS] test_edge_cache_sharing\n");
}

static void test_keep_largest_component(void) {
    Mesh mesh=Mesh_Create();MeshIndex index;
    const Vector3 positions[]={
        {0,0,0},{1,0,0},{0,1,0},
        {3,0,0},{4,0,0},{3,1,0},{4,1,0}
    };
    for(size_t i=0;i<sizeof(positions)/sizeof(positions[0]);++i) {
        MeshVertex vertex={.position=positions[i],.normal={0,0,1},.color=COLOR_WHITE,.material=SDF_MATERIAL_SKIN};
        TEST_ASSERT(Mesh_AddVertex(&mesh,vertex,&index),"No se preparó la malla multicomponente");
    }
    TEST_ASSERT(Mesh_AddTriangle(&mesh,0,1,2)&&Mesh_AddTriangle(&mesh,3,4,5)&&Mesh_AddTriangle(&mesh,4,6,5),
                "No se prepararon los componentes de prueba");
    TEST_ASSERT(Mesh_KeepLargestComponent(&mesh),"Falló la compactación del componente mayor");
    TEST_ASSERT(mesh.vertexCount==4&&mesh.indexCount==6,"La compactación no conservó sólo el componente mayor");
    TEST_ASSERT(Mesh_Validate(&mesh).valid,"La compactación dejó índices o atributos inválidos");
    Mesh_Free(&mesh);printf("[PASS] test_keep_largest_component\n");
}

static void test_mesh_gpu_revisions(void) {
    Mesh a=Mesh_Create(),b=Mesh_Create();
    TEST_ASSERT(a.identity!=0&&b.identity!=0&&a.identity!=b.identity,"Identidades GPU únicas");
    uint64_t geometry=a.geometryGeneration,surface=a.surfaceGeneration;
    MeshVertex vertex={.position={0,0,0},.normal={0,1,0},.color=COLOR_WHITE,.material=SDF_MATERIAL_SKIN};
    TEST_ASSERT(Mesh_AddVertex(&a,vertex,NULL),"Añadir vértice para revisión");
    TEST_ASSERT(a.geometryGeneration!=geometry&&a.surfaceGeneration!=surface,"El vértice invalida ambos canales");
    geometry=a.geometryGeneration;surface=a.surfaceGeneration;
    Mesh_MarkGeometryChanged(&a);
    TEST_ASSERT(a.geometryGeneration!=geometry&&a.surfaceGeneration==surface,"Pose invalida sólo geometría");
    Mesh_MarkSurfaceChanged(&a);
    TEST_ASSERT(a.surfaceGeneration!=surface,"Remapeo invalida sólo superficie");
    Mesh_Free(&a);Mesh_Free(&b);printf("[PASS] test_mesh_gpu_revisions\n");
}

void run_mesh_tests(void) {
    test_add_triangle_guards();
    test_mesh_validate();
    test_zero_area_collinear_validation();
    test_sphere_field_pipeline();
    test_edge_cache_sharing();
    test_topology_audit();
    test_keep_largest_component();
    test_mesh_gpu_revisions();
}
