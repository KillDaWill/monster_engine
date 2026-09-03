#include "Mesh.h"
#include "MathUtils.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

typedef struct MeshEdge { MeshIndex a; MeshIndex b; } MeshEdge;
typedef struct MeshTriangleKey { MeshIndex a; MeshIndex b; MeshIndex c; } MeshTriangleKey;

static int CompareEdges(const void* left, const void* right) {
    const MeshEdge* a = (const MeshEdge*)left;
    const MeshEdge* b = (const MeshEdge*)right;
    if (a->a != b->a) return a->a < b->a ? -1 : 1;
    if (a->b != b->b) return a->b < b->b ? -1 : 1;
    return 0;
}

static int CompareTriangleKeys(const void* left, const void* right) {
    const MeshTriangleKey* a = (const MeshTriangleKey*)left;
    const MeshTriangleKey* b = (const MeshTriangleKey*)right;
    if (a->a != b->a) return a->a < b->a ? -1 : 1;
    if (a->b != b->b) return a->b < b->b ? -1 : 1;
    if (a->c != b->c) return a->c < b->c ? -1 : 1;
    return 0;
}

static void SortThree(MeshIndex* a, MeshIndex* b, MeshIndex* c) {
    MeshIndex t;
    if (*a > *b) { t = *a; *a = *b; *b = t; }
    if (*b > *c) { t = *b; *b = *c; *c = t; }
    if (*a > *b) { t = *a; *a = *b; *b = t; }
}

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

    if (capacity > (size_t)UINT32_MAX || capacity > SIZE_MAX / sizeof(MeshVertex)) return false;

    MeshVertex* newVerts = (MeshVertex*)realloc(mesh->vertices, capacity * sizeof(MeshVertex));
    if (!newVerts) return false;

    mesh->vertices = newVerts;
    mesh->vertexCapacity = capacity;
    return true;
}

bool Mesh_ReserveIndices(Mesh* mesh, size_t capacity) {
    if (!mesh) return false;
    if (capacity <= mesh->indexCapacity) return true;

    if (capacity > SIZE_MAX / sizeof(MeshIndex)) return false;

    MeshIndex* newInds = (MeshIndex*)realloc(mesh->indices, capacity * sizeof(MeshIndex));
    if (!newInds) return false;

    mesh->indices = newInds;
    mesh->indexCapacity = capacity;
    return true;
}

bool Mesh_AddVertex(Mesh* mesh, MeshVertex vertex, MeshIndex* outIndex) {
    if (!mesh) return false;
    if (mesh->vertexCount >= (size_t)UINT32_MAX) return false;

    if (mesh->vertexCount >= mesh->vertexCapacity) {
        size_t newCap = 0;
        if (!Math_GrowCapacity(mesh->vertexCapacity, mesh->vertexCount + 1, sizeof(MeshVertex), &newCap)) return false;
        if (!Mesh_ReserveVertices(mesh, newCap)) return false;
    }

    size_t index = mesh->vertexCount;
    mesh->vertices[mesh->vertexCount++] = vertex;
    if (outIndex) *outIndex = (MeshIndex)index;
    return true;
}

bool Mesh_AddTriangle(Mesh* mesh, MeshIndex a, MeshIndex b, MeshIndex c) {
    if (!mesh) return false;

    /* Rechazar índices fuera de rango y triángulos degenerados sin escribir nada */
    if ((size_t)a >= mesh->vertexCount || (size_t)b >= mesh->vertexCount || (size_t)c >= mesh->vertexCount) return false;
    if (a == b || b == c || a == c) return false;

    if (mesh->indexCount + 3 > mesh->indexCapacity) {
        size_t newCap = 0;
        if (!Math_GrowCapacity(mesh->indexCapacity, mesh->indexCount + 3, sizeof(MeshIndex), &newCap)) return false;
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
        if (v->material != SDF_MATERIAL_SKIN && v->material != SDF_MATERIAL_MOUTH &&
            v->material != SDF_MATERIAL_LIP && v->material != SDF_MATERIAL_UNKNOWN) {
            ++result.invalidMaterialCount;
        }
    }

    /* Índices de triángulos */
    size_t fullTriangles = mesh->indexCount / 3;
    result.invalidIndexCount = mesh->indexCount % 3; /* índices restantes sin triángulo completo */

    MeshEdge* edges = NULL;
    MeshTriangleKey* triangleKeys = NULL;
    bool* usedVertices = NULL;
    if (fullTriangles > 0 && fullTriangles <= SIZE_MAX / 3 &&
        fullTriangles <= SIZE_MAX / sizeof(MeshTriangleKey) &&
        fullTriangles * 3 <= SIZE_MAX / sizeof(MeshEdge)) {
        edges = (MeshEdge*)malloc(fullTriangles * 3 * sizeof(MeshEdge));
        triangleKeys = (MeshTriangleKey*)malloc(fullTriangles * sizeof(MeshTriangleKey));
    }
    if (mesh->vertexCount > 0) usedVertices = (bool*)calloc(mesh->vertexCount, sizeof(bool));
    size_t edgeCount = 0;
    size_t keyCount = 0;

    for (size_t t = 0; t < fullTriangles; ++t) {
        MeshIndex a = mesh->indices[t * 3 + 0];
        MeshIndex b = mesh->indices[t * 3 + 1];
        MeshIndex c = mesh->indices[t * 3 + 2];

        if ((size_t)a >= mesh->vertexCount || (size_t)b >= mesh->vertexCount || (size_t)c >= mesh->vertexCount) {
            ++result.invalidIndexCount;
            continue;
        }

        if (a == b || b == c || a == c) {
            ++result.degenerateTriangleCount;
            continue;
        }

        if (usedVertices) {
            usedVertices[a] = true;
            usedVertices[b] = true;
            usedVertices[c] = true;
        }
        if (edges && triangleKeys) {
            edges[edgeCount++] = (MeshEdge){a < b ? a : b, a < b ? b : a};
            edges[edgeCount++] = (MeshEdge){b < c ? b : c, b < c ? c : b};
            edges[edgeCount++] = (MeshEdge){c < a ? c : a, c < a ? a : c};
            SortThree(&a, &b, &c);
            triangleKeys[keyCount++] = (MeshTriangleKey){a, b, c};
        }

        Vector3 ab = Vec3_Sub(mesh->vertices[b].position, mesh->vertices[a].position);
        Vector3 ac = Vec3_Sub(mesh->vertices[c].position, mesh->vertices[a].position);
        Vector3 cross = Vec3_Cross(ab, ac);
        if (Vec3_Dot(cross, cross) <= 1e-16f) {
            ++result.zeroAreaTriangleCount;
        }
    }

    if (edges) {
        qsort(edges, edgeCount, sizeof(MeshEdge), CompareEdges);
        for (size_t i = 0; i < edgeCount;) {
            size_t j = i + 1;
            while (j < edgeCount && edges[j].a == edges[i].a && edges[j].b == edges[i].b) ++j;
            size_t incidence = j - i;
            if (incidence == 1) ++result.boundaryEdgeCount;
            else if (incidence > 2) ++result.nonManifoldEdgeCount;
            i = j;
        }
        qsort(triangleKeys, keyCount, sizeof(MeshTriangleKey), CompareTriangleKeys);
        for (size_t i = 0; i < keyCount;) {
            size_t j = i + 1;
            while (j < keyCount && triangleKeys[j].a == triangleKeys[i].a &&
                   triangleKeys[j].b == triangleKeys[i].b && triangleKeys[j].c == triangleKeys[i].c) ++j;
            if (j - i > 1) result.duplicateTriangleCount += j - i - 1;
            i = j;
        }
    }
    if (usedVertices) {
        for (size_t i = 0; i < mesh->vertexCount; ++i) if (!usedVertices[i]) ++result.isolatedVertexCount;
    }
    free(edges);
    free(triangleKeys);
    free(usedVertices);
    result.manifold = result.nonManifoldEdgeCount == 0 && result.boundaryEdgeCount == 0;
    result.watertight = result.manifold;

    result.valid = (result.invalidIndexCount == 0 &&
                    result.degenerateTriangleCount == 0 &&
                    result.zeroAreaTriangleCount == 0 &&
                    result.nonFiniteVertexCount == 0 &&
                    result.nonFiniteNormalCount == 0 &&
                    result.invalidMaterialCount == 0 &&
                    result.duplicateTriangleCount == 0);
    return result;
}
