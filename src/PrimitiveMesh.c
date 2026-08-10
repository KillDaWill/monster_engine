#include "PrimitiveMesh.h"
#include "MathUtils.h"
#include <math.h>

#define RING_VERTEX(i, j) ((MeshIndex)(1 + ((size_t)(i) - 1) * (size_t)segments + (size_t)(j)))

bool PrimitiveMesh_GenerateEllipsoid(Mesh* out, Transform3D transform, unsigned int segments, unsigned int rings, Color color) {
    if (!out || transform.scale.x <= 0.0f || transform.scale.y <= 0.0f || transform.scale.z <= 0.0f || segments < 3 || rings < 2) {
        return false;
    }

    size_t interiorRings = (size_t)rings - 1;
    size_t vertexCount = 2 + interiorRings * (size_t)segments;
    size_t indexCount = 6 * (size_t)segments * interiorRings;

    Mesh_Clear(out);
    if (!Mesh_ReserveVertices(out, vertexCount) || !Mesh_ReserveIndices(out, indexCount)) {
        return false;
    }

    const float pi = 3.14159265358979323846f;
    float phiStep = pi / (float)rings;
    float thetaStep = 2.0f * pi / (float)segments;

    /* Polo Norte local: (0, ry, 0) */
    Vector3 poleNLocalPos = Vec3_Create(0.0f, transform.scale.y, 0.0f);
    Vector3 poleNLocalNorm = Vec3_Create(0.0f, 1.0f, 0.0f);
    MeshVertex poleN = {
        .position = Vec3_Add(transform.position, Transform3D_RotateVector(transform.rotation, poleNLocalPos)),
        .normal = Transform3D_RotateVector(transform.rotation, poleNLocalNorm),
        .color = color
    };
    MeshIndex poleNIndex = 0;
    if (!Mesh_AddVertex(out, poleN, &poleNIndex)) {
        return false;
    }

    /* Anillos interiores */
    for (size_t i = 1; i <= interiorRings; ++i) {
        float phi = phiStep * (float)i;
        float sinPhi = sinf(phi);
        float cosPhi = cosf(phi);
        for (size_t j = 0; j < segments; ++j) {
            float theta = thetaStep * (float)j;
            Vector3 unitDir = Vec3_Create(sinPhi * cosf(theta), cosPhi, sinPhi * sinf(theta));

            /* Posición local con escala */
            Vector3 localPos = Vec3_Create(
                unitDir.x * transform.scale.x,
                unitDir.y * transform.scale.y,
                unitDir.z * transform.scale.z
            );

            /* Normal local para elipsoide no uniforme: gradiente prop a (unitDir.x / rx, unitDir.y / ry, unitDir.z / rz) */
            Vector3 localNormUnnorm = Vec3_Create(
                unitDir.x / Math_Max(transform.scale.x, 0.0001f),
                unitDir.y / Math_Max(transform.scale.y, 0.0001f),
                unitDir.z / Math_Max(transform.scale.z, 0.0001f)
            );
            Vector3 localNorm = Vec3_Normalize(localNormUnnorm);

            MeshVertex v = {
                .position = Vec3_Add(transform.position, Transform3D_RotateVector(transform.rotation, localPos)),
                .normal = Transform3D_RotateVector(transform.rotation, localNorm),
                .color = color
            };
            MeshIndex index = 0;
            if (!Mesh_AddVertex(out, v, &index)) {
                return false;
            }
        }
    }

    /* Polo Sur local: (0, -ry, 0) */
    Vector3 poleSLocalPos = Vec3_Create(0.0f, -transform.scale.y, 0.0f);
    Vector3 poleSLocalNorm = Vec3_Create(0.0f, -1.0f, 0.0f);
    MeshVertex poleS = {
        .position = Vec3_Add(transform.position, Transform3D_RotateVector(transform.rotation, poleSLocalPos)),
        .normal = Transform3D_RotateVector(transform.rotation, poleSLocalNorm),
        .color = color
    };
    MeshIndex poleSIndex = 0;
    if (!Mesh_AddVertex(out, poleS, &poleSIndex)) {
        return false;
    }

    /* Triángulos polo norte */
    for (size_t j = 0; j < segments; ++j) {
        MeshIndex j2 = (MeshIndex)((j + 1) % segments);
        if (!Mesh_AddTriangle(out, poleNIndex, RING_VERTEX(1, j2), RING_VERTEX(1, j))) {
            return false;
        }
    }

    /* Triángulos anillos centrales */
    for (size_t i = 1; i + 1 <= interiorRings; ++i) {
        for (size_t j = 0; j < segments; ++j) {
            MeshIndex j2 = (MeshIndex)((j + 1) % segments);
            MeshIndex a = RING_VERTEX(i, j);
            MeshIndex b = RING_VERTEX(i + 1, j);
            MeshIndex c = RING_VERTEX(i + 1, j2);
            MeshIndex d = RING_VERTEX(i, j2);
            if (!Mesh_AddTriangle(out, a, c, b)) {
                return false;
            }
            if (!Mesh_AddTriangle(out, a, d, c)) {
                return false;
            }
        }
    }

    /* Triángulos polo sur */
    for (size_t j = 0; j < segments; ++j) {
        MeshIndex j2 = (MeshIndex)((j + 1) % segments);
        if (!Mesh_AddTriangle(out, poleSIndex, RING_VERTEX(interiorRings, j), RING_VERTEX(interiorRings, j2))) {
            return false;
        }
    }

    return true;
}

bool PrimitiveMesh_GenerateUVSphere(Mesh* out, Vector3 center, float radius, unsigned int segments, unsigned int rings, Color color) {
    Transform3D t = Transform3D_Create(center, Vec3_Zero(), Vec3_Create(radius, radius, radius));
    return PrimitiveMesh_GenerateEllipsoid(out, t, segments, rings, color);
}

#undef RING_VERTEX
