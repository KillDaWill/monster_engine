#include "Mesh.h"
#include "MathUtils.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

Mesh Mesh_Create(void) {
    Mesh mesh;
    memset(&mesh, 0, sizeof(Mesh));
    return mesh;
}

void Mesh_Free(Mesh* mesh) {
    if (!mesh) return;

    if (mesh->vertices) {
        free(mesh->vertices);
        mesh->vertices = NULL;
    }
    if (mesh->indices) {
        free(mesh->indices);
        mesh->indices = NULL;
    }

    mesh->vertexCount = 0;
    mesh->vertexCapacity = 0;
    mesh->indexCount = 0;
    mesh->indexCapacity = 0;
}

void Mesh_Clear(Mesh* mesh) {
    if (!mesh) return;
    mesh->vertexCount = 0;
    mesh->indexCount = 0;
}

bool Mesh_ReserveVertices(Mesh* mesh, size_t capacity) {
    if (!mesh) return false;
    if (capacity <= mesh->vertexCapacity) return true;

    if (capacity > 0xFFFFFFFFu / sizeof(MeshVertex)) return false;

    MeshVertex* newVerts = (MeshVertex*)realloc(mesh->vertices, capacity * sizeof(MeshVertex));
    if (!newVerts) return false;

    mesh->vertices = newVerts;
    mesh->vertexCapacity = capacity;
    return true;
}

bool Mesh_ReserveIndices(Mesh* mesh, size_t capacity) {
    if (!mesh) return false;
    if (capacity <= mesh->indexCapacity) return true;

    if (capacity > 0xFFFFFFFFu / sizeof(unsigned int)) return false;

    unsigned int* newInds = (unsigned int*)realloc(mesh->indices, capacity * sizeof(unsigned int));
    if (!newInds) return false;

    mesh->indices = newInds;
    mesh->indexCapacity = capacity;
    return true;
}

bool Mesh_AddVertex(Mesh* mesh, MeshVertex vertex, unsigned int* outIndex) {
    if (!mesh) return false;

    if (mesh->vertexCount >= mesh->vertexCapacity) {
        size_t newCap = 0;
        if (!Math_GrowCapacity(mesh->vertexCapacity, mesh->vertexCount + 1, sizeof(MeshVertex), &newCap)) return false;
        if (!Mesh_ReserveVertices(mesh, newCap)) return false;
    }

    size_t index = mesh->vertexCount;
    mesh->vertices[mesh->vertexCount++] = vertex;
    if (outIndex) *outIndex = (unsigned int)index;
    return true;
}

bool Mesh_AddTriangle(Mesh* mesh, unsigned int a, unsigned int b, unsigned int c) {
    if (!mesh) return false;

    /* Rechazar índices fuera de rango y triángulos degenerados sin escribir nada */
    if (a >= mesh->vertexCount || b >= mesh->vertexCount || c >= mesh->vertexCount) return false;
    if (a == b || b == c || a == c) return false;

    if (mesh->indexCount + 3 > mesh->indexCapacity) {
        size_t newCap = 0;
        if (!Math_GrowCapacity(mesh->indexCapacity, mesh->indexCount + 3, sizeof(unsigned int), &newCap)) return false;
        if (!Mesh_ReserveIndices(mesh, newCap)) return false;
    }

    mesh->indices[mesh->indexCount++] = a;
    mesh->indices[mesh->indexCount++] = b;
    mesh->indices[mesh->indexCount++] = c;
    return true;
}

MeshValidationResult Mesh_Validate(const Mesh* mesh) {
    MeshValidationResult result = {0};

    if (!mesh) {
        result.valid = false;
        return result;
    }

    /* Índices de vértices */
    for (size_t i = 0; i < mesh->vertexCount; ++i) {
        const MeshVertex* v = &mesh->vertices[i];
        if (!isfinite(v->position.x) || !isfinite(v->position.y) || !isfinite(v->position.z)) {
            ++result.nonFiniteVertexCount;
        }
        if (!isfinite(v->normal.x) || !isfinite(v->normal.y) || !isfinite(v->normal.z)) {
            ++result.nonFiniteNormalCount;
        }
    }

    /* Índices de triángulos */
    size_t fullTriangles = mesh->indexCount / 3;
    result.invalidIndexCount = mesh->indexCount % 3; /* índices restantes sin triángulo completo */

    for (size_t t = 0; t < fullTriangles; ++t) {
        unsigned int a = mesh->indices[t * 3 + 0];
        unsigned int b = mesh->indices[t * 3 + 1];
        unsigned int c = mesh->indices[t * 3 + 2];

        if (a >= mesh->vertexCount) ++result.invalidIndexCount;
        if (b >= mesh->vertexCount) ++result.invalidIndexCount;
        if (c >= mesh->vertexCount) ++result.invalidIndexCount;

        if (a == b || b == c || a == c) ++result.degenerateTriangleCount;
    }

    result.valid = (result.invalidIndexCount == 0 &&
                    result.degenerateTriangleCount == 0 &&
                    result.nonFiniteVertexCount == 0 &&
                    result.nonFiniteNormalCount == 0);
    return result;
}
