#include "PrimitiveMesh.h"
#include <math.h>

#define RING_VERTEX(i, j) ((unsigned int)(1 + ((size_t)(i) - 1) * (size_t)segments + (size_t)(j)))

bool PrimitiveMesh_GenerateUVSphere(Mesh* out, Vector3 center, float radius, unsigned int segments, unsigned int rings, Color color) {
    if (!out || radius <= 0.0f || segments < 3 || rings < 2) {
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

    MeshVertex poleN = {
        .position = Vec3_Create(center.x, center.y + radius, center.z),
        .normal = Vec3_Create(0.0f, 1.0f, 0.0f),
        .color = color
    };
    unsigned int poleNIndex = 0;
    if (!Mesh_AddVertex(out, poleN, &poleNIndex)) {
        return false;
    }

    for (size_t i = 1; i <= interiorRings; ++i) {
        float phi = phiStep * (float)i;
        float sinPhi = sinf(phi);
        float cosPhi = cosf(phi);
        for (size_t j = 0; j < segments; ++j) {
            float theta = thetaStep * (float)j;
            Vector3 offset = Vec3_Create(sinPhi * cosf(theta), cosPhi, sinPhi * sinf(theta));
            MeshVertex v = {
                .position = Vec3_Add(center, Vec3_Scale(offset, radius)),
                .normal = offset,
                .color = color
            };
            unsigned int index = 0;
            if (!Mesh_AddVertex(out, v, &index)) {
                return false;
            }
        }
    }

    MeshVertex poleS = {
        .position = Vec3_Create(center.x, center.y - radius, center.z),
        .normal = Vec3_Create(0.0f, -1.0f, 0.0f),
        .color = color
    };
    unsigned int poleSIndex = 0;
    if (!Mesh_AddVertex(out, poleS, &poleSIndex)) {
        return false;
    }

    for (size_t j = 0; j < segments; ++j) {
        unsigned int j2 = (unsigned int)((j + 1) % segments);
        if (!Mesh_AddTriangle(out, poleNIndex, RING_VERTEX(1, j2), RING_VERTEX(1, j))) {
            return false;
        }
    }

    for (size_t i = 1; i + 1 <= interiorRings; ++i) {
        for (size_t j = 0; j < segments; ++j) {
            unsigned int j2 = (unsigned int)((j + 1) % segments);
            unsigned int a = RING_VERTEX(i, j);
            unsigned int b = RING_VERTEX(i + 1, j);
            unsigned int c = RING_VERTEX(i + 1, j2);
            unsigned int d = RING_VERTEX(i, j2);
            if (!Mesh_AddTriangle(out, a, c, b)) {
                return false;
            }
            if (!Mesh_AddTriangle(out, a, d, c)) {
                return false;
            }
        }
    }

    for (size_t j = 0; j < segments; ++j) {
        unsigned int j2 = (unsigned int)((j + 1) % segments);
        if (!Mesh_AddTriangle(out, poleSIndex, RING_VERTEX(interiorRings, j), RING_VERTEX(interiorRings, j2))) {
            return false;
        }
    }

    return true;
}

#undef RING_VERTEX
