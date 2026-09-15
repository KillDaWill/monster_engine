#include "Mesh.h"
#include "MathUtils.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdatomic.h>

static _Atomic uint64_t nextMeshIdentity=1;

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
    mesh.identity=atomic_fetch_add(&nextMeshIdentity,1);
    if(!mesh.identity)mesh.identity=atomic_fetch_add(&nextMeshIdentity,1);
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
    Mesh_MarkGeometryChanged(mesh);
    Mesh_MarkSurfaceChanged(mesh);
}

void Mesh_MarkGeometryChanged(Mesh* mesh) {
    if (!mesh) return;
    if (++mesh->geometryGeneration == 0) mesh->geometryGeneration = 1;
}

void Mesh_MarkSurfaceChanged(Mesh* mesh) {
    if (!mesh) return;
    if (++mesh->surfaceGeneration == 0) mesh->surfaceGeneration = 1;
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
    Mesh_MarkGeometryChanged(mesh);
    Mesh_MarkSurfaceChanged(mesh);
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
    Mesh_MarkGeometryChanged(mesh);
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
            v->material != SDF_MATERIAL_LIP && v->material != SDF_MATERIAL_EYE_SOCKET &&
            v->material != SDF_MATERIAL_NOSTRIL && v->material != SDF_MATERIAL_TYMPANUM &&
            v->material != SDF_MATERIAL_UNKNOWN) {
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

        if (!Mesh_TriangleHasArea(mesh->vertices[a].position,mesh->vertices[b].position,mesh->vertices[c].position)) {
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

static size_t Mesh_ComponentRoot(size_t* parent, size_t index) {
    while (parent[index] != index) {
        parent[index] = parent[parent[index]];
        index = parent[index];
    }
    return index;
}

bool Mesh_KeepLargestComponent(Mesh* mesh) {
    if (!mesh || mesh->indexCount < 3 || mesh->vertexCount == 0) return mesh != NULL;
    size_t* parent = (size_t*)malloc(mesh->vertexCount * sizeof(size_t));
    size_t* triangleCounts = (size_t*)calloc(mesh->vertexCount, sizeof(size_t));
    MeshIndex* remap = (MeshIndex*)malloc(mesh->vertexCount * sizeof(MeshIndex));
    if (!parent || !triangleCounts || !remap) {
        free(parent); free(triangleCounts); free(remap);
        return false;
    }
    for (size_t i = 0; i < mesh->vertexCount; ++i) {
        parent[i] = i;
        remap[i] = UINT32_MAX;
    }
    for (size_t i = 0; i + 2 < mesh->indexCount; i += 3) {
        MeshIndex ids[3] = {mesh->indices[i], mesh->indices[i + 1], mesh->indices[i + 2]};
        if (ids[0] >= mesh->vertexCount || ids[1] >= mesh->vertexCount || ids[2] >= mesh->vertexCount) continue;
        for (size_t edge = 1; edge < 3; ++edge) {
            size_t a = Mesh_ComponentRoot(parent, ids[0]);
            size_t b = Mesh_ComponentRoot(parent, ids[edge]);
            if (a != b) parent[b] = a;
        }
    }
    size_t largestRoot = 0, largestTriangles = 0;
    for (size_t i = 0; i + 2 < mesh->indexCount; i += 3) {
        if (mesh->indices[i] >= mesh->vertexCount) continue;
        size_t root = Mesh_ComponentRoot(parent, mesh->indices[i]);
        size_t count = ++triangleCounts[root];
        if (count > largestTriangles) { largestTriangles = count; largestRoot = root; }
    }
    if (largestTriangles * 3 == mesh->indexCount) {
        free(parent); free(triangleCounts); free(remap);
        return true;
    }
    Mesh compacted = Mesh_Create();
    compacted.surfaceRecipe=mesh->surfaceRecipe; compacted.hasSurface=mesh->hasSurface;
    if (!Mesh_ReserveVertices(&compacted, mesh->vertexCount) ||
        !Mesh_ReserveIndices(&compacted, largestTriangles * 3)) {
        Mesh_Free(&compacted); free(parent); free(triangleCounts); free(remap);
        return false;
    }
    bool ok = true;
    for (size_t i = 0; ok && i + 2 < mesh->indexCount; i += 3) {
        MeshIndex source[3] = {mesh->indices[i], mesh->indices[i + 1], mesh->indices[i + 2]};
        if (source[0] >= mesh->vertexCount || Mesh_ComponentRoot(parent, source[0]) != largestRoot) continue;
        MeshIndex target[3];
        for (size_t j = 0; j < 3; ++j) {
            if (remap[source[j]] == UINT32_MAX)
                ok = Mesh_AddVertex(&compacted, mesh->vertices[source[j]], &remap[source[j]]);
            target[j] = remap[source[j]];
        }
        if (ok) ok = Mesh_AddTriangle(&compacted, target[0], target[1], target[2]);
    }
    if (ok) { Mesh old = *mesh; *mesh = compacted; Mesh_Free(&old); }
    else Mesh_Free(&compacted);
    free(parent); free(triangleCounts); free(remap);
    return ok;
}

bool Mesh_TriangleHasArea(Vector3 a,Vector3 b,Vector3 c) {
    Vector3 ab=Vec3_Sub(b,a),ac=Vec3_Sub(c,a),bc=Vec3_Sub(c,b);
    Vector3 cross=Vec3_Cross(ab,ac);
    float longest=fmaxf(Vec3_LengthSq(ab),fmaxf(Vec3_LengthSq(ac),Vec3_LengthSq(bc)));
    return longest>0 && Vec3_LengthSq(cross)>longest*longest*1e-12f;
}
