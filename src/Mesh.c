#include "Mesh.h"
#include <stdlib.h>
#include <string.h>

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

    MeshVertex* newVerts = (MeshVertex*)realloc(mesh->vertices, capacity * sizeof(MeshVertex));
    if (!newVerts) return false;

    mesh->vertices = newVerts;
    mesh->vertexCapacity = capacity;
    return true;
}

bool Mesh_ReserveIndices(Mesh* mesh, size_t capacity) {
    if (!mesh) return false;
    if (capacity <= mesh->indexCapacity) return true;

    unsigned int* newInds = (unsigned int*)realloc(mesh->indices, capacity * sizeof(unsigned int));
    if (!newInds) return false;

    mesh->indices = newInds;
    mesh->indexCapacity = capacity;
    return true;
}

size_t Mesh_AddVertex(Mesh* mesh, MeshVertex vertex) {
    if (!mesh) return 0;

    if (mesh->vertexCount >= mesh->vertexCapacity) {
        size_t newCap = (mesh->vertexCapacity == 0) ? 64 : mesh->vertexCapacity * 2;
        if (!Mesh_ReserveVertices(mesh, newCap)) return 0;
    }

    size_t index = mesh->vertexCount;
    mesh->vertices[mesh->vertexCount++] = vertex;
    return index;
}

bool Mesh_AddTriangle(Mesh* mesh, unsigned int a, unsigned int b, unsigned int c) {
    if (!mesh) return false;

    if (mesh->indexCount + 3 > mesh->indexCapacity) {
        size_t newCap = (mesh->indexCapacity == 0) ? 128 : mesh->indexCapacity * 2;
        if (!Mesh_ReserveIndices(mesh, newCap)) return false;
    }

    mesh->indices[mesh->indexCount++] = a;
    mesh->indices[mesh->indexCount++] = b;
    mesh->indices[mesh->indexCount++] = c;
    return true;
}
