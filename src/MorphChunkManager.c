/**
 * @file MorphChunkManager.c
 * @brief Implementación del gestor de remallado local por chunks para morphing continuo.
 * @author Monster Engine Team
 * @date 2026
 */

#include "MorphChunkManager.h"
#include "SDFSamplingPool.h"
#include "MathUtils.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MORPH_CHUNK_INITIAL_CAPACITY 64
#define MORPH_CHUNK_INITIAL_HASH_CAPACITY 128

static inline uint64_t ChunkKey(int32_t cx, int32_t cy, int32_t cz) {
    uint64_t ux = (uint32_t)cx;
    uint64_t uy = (uint32_t)cy;
    uint64_t uz = (uint32_t)cz;
    return (ux * 73856093ULL) ^ (uy * 19349663ULL) ^ (uz * 83492791ULL);
}

static inline size_t HashSlot(uint64_t key, size_t capacity) {
    key ^= key >> 33;
    key *= 0xff51afd7ed558ccdULL;
    key ^= key >> 33;
    key *= 0xc4ceb9fe1a85ec53ULL;
    key ^= key >> 33;
    return (size_t)(key & (capacity - 1));
}

static inline AABB3D ChunkBounds(int32_t cx, int32_t cy, int32_t cz, float chunkSize) {
    Vector3 min = Vec3_Create((float)cx * chunkSize, (float)cy * chunkSize, (float)cz * chunkSize);
    Vector3 max = Vec3_Create((float)(cx + 1) * chunkSize, (float)(cy + 1) * chunkSize, (float)(cz + 1) * chunkSize);
    return AABB_FromMinMax(min, max);
}

MorphChunkManager* MorphChunkManager_Create(float chunkSize, float voxelSize) {
    if (chunkSize <= 0.01f) chunkSize = 0.5f;
    if (voxelSize <= 0.001f) voxelSize = 0.05f;

    MorphChunkManager* mgr = calloc(1, sizeof(MorphChunkManager));
    if (!mgr) return NULL;

    mgr->chunkSize = chunkSize;
    mgr->invChunkSize = 1.0f / chunkSize;
    mgr->voxelSize = voxelSize;
    mgr->chunkCapacity = MORPH_CHUNK_INITIAL_CAPACITY;
    mgr->chunks = calloc(mgr->chunkCapacity, sizeof(MorphChunk));
    if (!mgr->chunks) {
        free(mgr);
        return NULL;
    }

    mgr->hashCapacity = MORPH_CHUNK_INITIAL_HASH_CAPACITY;
    mgr->hashTable = calloc(mgr->hashCapacity, sizeof(uint32_t));
    if (!mgr->hashTable) {
        free(mgr->chunks);
        free(mgr);
        return NULL;
    }

    return mgr;
}

void MorphChunkManager_Destroy(MorphChunkManager* mgr) {
    if (!mgr) return;
    for (size_t i = 0; i < mgr->chunkCount; ++i) {
        Mesh_Free(&mgr->chunks[i].mesh);
    }
    free(mgr->chunks);
    free(mgr->hashTable);
    free(mgr);
}

void MorphChunkManager_Clear(MorphChunkManager* mgr) {
    if (!mgr) return;
    for (size_t i = 0; i < mgr->chunkCount; ++i) {
        Mesh_Free(&mgr->chunks[i].mesh);
    }
    memset(mgr->hashTable, 0, mgr->hashCapacity * sizeof(uint32_t));
    mgr->chunkCount = 0;
    mgr->dirtyCount = 0;
    mgr->surfaceCount = 0;
}

static bool MorphChunkManager_Rehash(MorphChunkManager* mgr, size_t newCapacity) {
    uint32_t* newTable = calloc(newCapacity, sizeof(uint32_t));
    if (!newTable) return false;

    for (size_t i = 0; i < mgr->chunkCount; ++i) {
        uint64_t key = mgr->chunks[i].key;
        size_t slot = HashSlot(key, newCapacity);
        while (newTable[slot] != 0) {
            slot = (slot + 1) & (newCapacity - 1);
        }
        newTable[slot] = (uint32_t)(i + 1);
    }

    free(mgr->hashTable);
    mgr->hashTable = newTable;
    mgr->hashCapacity = newCapacity;
    return true;
}

MorphChunk* MorphChunkManager_GetChunk(MorphChunkManager* mgr, int32_t cx, int32_t cy, int32_t cz, bool createIfMissing) {
    if (!mgr) return NULL;
    uint64_t key = ChunkKey(cx, cy, cz);
    size_t slot = HashSlot(key, mgr->hashCapacity);

    while (mgr->hashTable[slot] != 0) {
        uint32_t index = mgr->hashTable[slot] - 1;
        if (index < mgr->chunkCount) {
            MorphChunk* c = &mgr->chunks[index];
            if (c->key == key && c->cx == cx && c->cy == cy && c->cz == cz) {
                return c;
            }
        }
        slot = (slot + 1) & (mgr->hashCapacity - 1);
    }

    if (!createIfMissing) return NULL;

    if (mgr->chunkCount >= mgr->chunkCapacity) {
        size_t nextCapacity = mgr->chunkCapacity * 2;
        MorphChunk* grown = realloc(mgr->chunks, nextCapacity * sizeof(MorphChunk));
        if (!grown) return NULL;
        mgr->chunks = grown;
        mgr->chunkCapacity = nextCapacity;
    }

    if (mgr->chunkCount * 10 >= mgr->hashCapacity * 6) {
        if (!MorphChunkManager_Rehash(mgr, mgr->hashCapacity * 2)) {
            return NULL;
        }
        slot = HashSlot(key, mgr->hashCapacity);
        while (mgr->hashTable[slot] != 0) {
            slot = (slot + 1) & (mgr->hashCapacity - 1);
        }
    }

    size_t newIndex = mgr->chunkCount++;
    MorphChunk* chunk = &mgr->chunks[newIndex];
    memset(chunk, 0, sizeof(*chunk));
    chunk->cx = cx;
    chunk->cy = cy;
    chunk->cz = cz;
    chunk->key = key;
    chunk->bounds = ChunkBounds(cx, cy, cz, mgr->chunkSize);
    chunk->mesh = Mesh_Create();
    chunk->isDirty = true;
    chunk->dirtinessFlags = MORPH_DIRT_NONE;
    mgr->dirtyCount++;

    mgr->hashTable[slot] = (uint32_t)(newIndex + 1);
    return chunk;
}

const MorphChunk* MorphChunkManager_FindChunk(const MorphChunkManager* mgr, int32_t cx, int32_t cy, int32_t cz) {
    if (!mgr || mgr->chunkCount == 0) return NULL;
    uint64_t key = ChunkKey(cx, cy, cz);
    size_t slot = HashSlot(key, mgr->hashCapacity);

    while (mgr->hashTable[slot] != 0) {
        uint32_t index = mgr->hashTable[slot] - 1;
        if (index < mgr->chunkCount) {
            const MorphChunk* c = &mgr->chunks[index];
            if (c->key == key && c->cx == cx && c->cy == cy && c->cz == cz) {
                return c;
            }
        }
        slot = (slot + 1) & (mgr->hashCapacity - 1);
    }
    return NULL;
}

size_t MorphChunkManager_MarkDirtyAABB(MorphChunkManager* mgr, AABB3D aabb, uint32_t dirtinessFlags) {
    if (!mgr) return 0;
    int32_t minX = (int32_t)floorf(aabb.start.x * mgr->invChunkSize);
    int32_t maxX = (int32_t)floorf(aabb.end.x * mgr->invChunkSize);
    int32_t minY = (int32_t)floorf(aabb.start.y * mgr->invChunkSize);
    int32_t maxY = (int32_t)floorf(aabb.end.y * mgr->invChunkSize);
    int32_t minZ = (int32_t)floorf(aabb.start.z * mgr->invChunkSize);
    int32_t maxZ = (int32_t)floorf(aabb.end.z * mgr->invChunkSize);

    size_t count = 0;
    for (int32_t cx = minX; cx <= maxX; ++cx) {
        for (int32_t cy = minY; cy <= maxY; ++cy) {
            for (int32_t cz = minZ; cz <= maxZ; ++cz) {
                MorphChunk* chunk = MorphChunkManager_GetChunk(mgr, cx, cy, cz, true);
                if (chunk) {
                    if (!chunk->isDirty) {
                        chunk->isDirty = true;
                        mgr->dirtyCount++;
                    }
                    chunk->dirtinessFlags |= dirtinessFlags;
                    count++;
                }
            }
        }
    }
    return count;
}

size_t MorphChunkManager_SynchronizeWithSpatialCache(MorphChunkManager* mgr, const MorphSpatialCache* spatialCache) {
    if (!mgr || !spatialCache) return 0;
    size_t dirtyBricksCount = MorphSpatialCache_GetDirtyBrickCount(spatialCache);
    if (dirtyBricksCount == 0) return 0;

    const MorphSpatialBrick** dirtyBricks = malloc(dirtyBricksCount * sizeof(MorphSpatialBrick*));
    if (!dirtyBricks) return 0;

    size_t retrieved = MorphSpatialCache_GetDirtyBricks(spatialCache, dirtyBricks, dirtyBricksCount);
    size_t markedChunks = 0;

    for (size_t i = 0; i < retrieved; ++i) {
        const MorphSpatialBrick* b = dirtyBricks[i];
        size_t c = MorphChunkManager_MarkDirtyAABB(mgr, b->bounds, b->dirtinessFlags);
        markedChunks += c;
    }

    free(dirtyBricks);
    return markedChunks;
}

typedef struct ParallelChunkContext {
    MorphChunkManager* mgr;
    MorphChunk** dirtyList;
    const SDFField* field;
    uint64_t generation;
    SDFMesher meshers[SDF_SAMPLING_POOL_MAX_THREADS];
    size_t updatedPerThread[SDF_SAMPLING_POOL_MAX_THREADS];
} ParallelChunkContext;

static void ParallelChunkWorker(void* context, int startIndex, int endIndex, int threadIndex) {
    ParallelChunkContext* ctx = (ParallelChunkContext*)context;
    if (threadIndex < 0 || threadIndex >= SDF_SAMPLING_POOL_MAX_THREADS) return;
    SDFMesher* threadMesher = &ctx->meshers[threadIndex];
    for (int i = startIndex; i < endIndex; ++i) {
        MorphChunk* chunk = ctx->dirtyList[i];
        bool ok = SDFMesher_MeshRegion(threadMesher, ctx->field, chunk->bounds, ctx->mgr->voxelSize, &chunk->mesh);
        if (ok) {
            chunk->hasSurface = (chunk->mesh.vertexCount > 0);
            chunk->isDirty = false;
            chunk->dirtinessFlags = MORPH_DIRT_NONE;
            chunk->generation = ctx->generation;
            ctx->updatedPerThread[threadIndex]++;
        }
    }
}

size_t MorphChunkManager_UpdateDirtyParallel(
    MorphChunkManager* mgr,
    SDFMesherConfig baseConfig,
    struct SDFSamplingPool* pool,
    const SDFField* field,
    uint64_t generation
) {
    if (!mgr || !field || mgr->chunkCount == 0) return 0;

    /* Recolectar chunks sucios */
    MorphChunk** dirtyList = (MorphChunk**)malloc(mgr->chunkCount * sizeof(MorphChunk*));
    if (!dirtyList) return 0;

    size_t nDirty = 0;
    for (size_t i = 0; i < mgr->chunkCount; ++i) {
        if (mgr->chunks[i].isDirty) {
            dirtyList[nDirty++] = &mgr->chunks[i];
        }
    }

    if (nDirty == 0) {
        free(dirtyList);
        mgr->dirtyCount = 0;
        return 0;
    }

    int threadCount = (pool != NULL) ? SDFSamplingPool_GetThreadCount(pool) : 1;
    if (threadCount > SDF_SAMPLING_POOL_MAX_THREADS) threadCount = SDF_SAMPLING_POOL_MAX_THREADS;
    if (threadCount > (int)nDirty) threadCount = (int)nDirty;
    if (threadCount <= 0) threadCount = 1;

    /* Si es mono-hilo o no hay pool, ejecutar secuencialmente sin sobrecarga de hilos */
    if (threadCount <= 1 || pool == NULL) {
        SDFMesher mesher = SDFMesher_Create(baseConfig);
        mesher.borrowedPool = NULL;
        mesher.ownedPool = NULL;
        size_t updated = 0;
        for (size_t i = 0; i < nDirty; ++i) {
            MorphChunk* chunk = dirtyList[i];
            bool ok = SDFMesher_MeshRegion(&mesher, field, chunk->bounds, mgr->voxelSize, &chunk->mesh);
            if (ok) {
                bool wasSurface = chunk->hasSurface;
                chunk->hasSurface = (chunk->mesh.vertexCount > 0);
                if (chunk->hasSurface && !wasSurface) mgr->surfaceCount++;
                else if (!chunk->hasSurface && wasSurface) mgr->surfaceCount--;

                chunk->isDirty = false;
                chunk->dirtinessFlags = MORPH_DIRT_NONE;
                chunk->generation = generation;
                updated++;
            }
        }
        SDFMesher_Free(&mesher);
        free(dirtyList);
        mgr->dirtyCount = 0;
        return updated;
    }

    /* Ejecución paralela repartiendo chunks entre los hilos del pool */
    ParallelChunkContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.mgr = mgr;
    ctx.dirtyList = dirtyList;
    ctx.field = field;
    ctx.generation = generation;

    for (int t = 0; t < threadCount; ++t) {
        ctx.meshers[t] = SDFMesher_Create(baseConfig);
        ctx.meshers[t].borrowedPool = NULL;
        ctx.meshers[t].ownedPool = NULL;
    }

    SDFSamplingPool_ParallelFor(pool, (int)nDirty, ParallelChunkWorker, &ctx);

    /* Recalcular surfaceCount de forma segura tras la ejecución multihilo */
    mgr->surfaceCount = 0;
    for (size_t i = 0; i < mgr->chunkCount; ++i) {
        if (mgr->chunks[i].hasSurface) {
            mgr->surfaceCount++;
        }
    }

    size_t totalUpdated = 0;
    for (int t = 0; t < threadCount; ++t) {
        totalUpdated += ctx.updatedPerThread[t];
        SDFMesher_Free(&ctx.meshers[t]);
    }

    free(dirtyList);
    mgr->dirtyCount = 0;
    return totalUpdated;
}

size_t MorphChunkManager_UpdateDirty(MorphChunkManager* mgr, SDFMesher* mesher, const SDFField* field, uint64_t generation) {
    if (!mgr || !mesher || !field) return 0;
    struct SDFSamplingPool* pool = mesher->borrowedPool ? mesher->borrowedPool : mesher->ownedPool;
    return MorphChunkManager_UpdateDirtyParallel(mgr, mesher->config, pool, field, generation);
}

size_t MorphChunkManager_MarkResidualDirty(MorphChunkManager* mgr,
    const SDFField* field,float tolerance) {
    if(!mgr||!field||(!field->evaluateDistance&&!field->evaluate)||tolerance<=0)return 0;
    size_t marked=0;
    for(size_t i=0;i<mgr->chunkCount;++i) {
        MorphChunk* chunk=&mgr->chunks[i];
        if(chunk->isDirty||!chunk->hasSurface||!chunk->mesh.vertexCount)continue;
        size_t stride=chunk->mesh.vertexCount/64+1;
        float maximum=0.0f;
        for(size_t v=0;v<chunk->mesh.vertexCount;v+=stride) {
            Vector3 p=chunk->mesh.vertices[v].position;
            float d=field->evaluateDistance?
                field->evaluateDistance(field->context,p):
                field->evaluate(field->context,p).distance;
            if(!isfinite(d)){maximum=INFINITY;break;}
            maximum=fmaxf(maximum,fabsf(d));
        }
        if(maximum<=tolerance)continue;
        chunk->isDirty=true;
        chunk->dirtinessFlags|=maximum>tolerance*2.0f?
            MORPH_DIRT_TOPOLOGY:MORPH_DIRT_DEFORMATION;
        ++mgr->dirtyCount;
        ++marked;
    }
    return marked;
}

/* Estructura para soldadura de vértices en costuras */
typedef struct WeldVertexNode {
    Vector3 position;
    MeshIndex targetIndex;
    uint32_t next;
} WeldVertexNode;

bool MorphChunkManager_AssembleMesh(const MorphChunkManager* mgr, Mesh* outMesh, bool weldSeams) {
    if (!mgr || !outMesh) return false;
    Mesh_Clear(outMesh);

    size_t totalVertices = 0;
    size_t totalIndices = 0;
    for (size_t i = 0; i < mgr->chunkCount; ++i) {
        if (mgr->chunks[i].hasSurface) {
            totalVertices += mgr->chunks[i].mesh.vertexCount;
            totalIndices += mgr->chunks[i].mesh.indexCount;
        }
    }

    if (totalVertices == 0 || totalIndices == 0) return true;

    if (!Mesh_ReserveVertices(outMesh, totalVertices) ||
        !Mesh_ReserveIndices(outMesh, totalIndices)) {
        return false;
    }

    if (!weldSeams) {
        for (size_t i = 0; i < mgr->chunkCount; ++i) {
            const Mesh* src = &mgr->chunks[i].mesh;
            if (src->vertexCount == 0 || src->indexCount == 0) continue;

            MeshIndex baseVertex = (MeshIndex)outMesh->vertexCount;
            for (size_t v = 0; v < src->vertexCount; ++v) {
                outMesh->vertices[outMesh->vertexCount++] = src->vertices[v];
            }
            for (size_t idx = 0; idx < src->indexCount; ++idx) {
                outMesh->indices[outMesh->indexCount++] = baseVertex + src->indices[idx];
            }
        }
        Mesh_MarkGeometryChanged(outMesh);
        Mesh_MarkSurfaceChanged(outMesh);
        return true;
    }

    /* Ensamble con soldadura de costuras (seam welding) */
    size_t hashSize = 65536;
    uint32_t* head = calloc(hashSize, sizeof(uint32_t));
    WeldVertexNode* nodes = malloc(totalVertices * sizeof(WeldVertexNode));
    if (!head || !nodes) {
        free(head);
        free(nodes);
        return false;
    }
    size_t nodeCount = 0;

    for (size_t i = 0; i < mgr->chunkCount; ++i) {
        const Mesh* src = &mgr->chunks[i].mesh;
        if (src->vertexCount == 0 || src->indexCount == 0) continue;

        MeshIndex* indexMap = malloc(src->vertexCount * sizeof(MeshIndex));
        if (!indexMap) {
            free(head);
            free(nodes);
            return false;
        }

        for (size_t v = 0; v < src->vertexCount; ++v) {
            Vector3 pos = src->vertices[v].position;
            int32_t qx = (int32_t)floorf(pos.x * 2000.0f + 0.5f);
            int32_t qy = (int32_t)floorf(pos.y * 2000.0f + 0.5f);
            int32_t qz = (int32_t)floorf(pos.z * 2000.0f + 0.5f);
            uint64_t key = ((uint32_t)qx * 73856093ULL) ^ ((uint32_t)qy * 19349663ULL) ^ ((uint32_t)qz * 83492791ULL);
            size_t slot = (size_t)(key & (hashSize - 1));

            MeshIndex matchedIndex = UINT32_MAX;
            uint32_t curr = head[slot];
            while (curr != 0) {
                uint32_t nIdx = curr - 1;
                Vector3 diff = Vec3_Sub(pos, nodes[nIdx].position);
                if (Vec3_LengthSq(diff) < 1e-8f) {
                    matchedIndex = nodes[nIdx].targetIndex;
                    break;
                }
                curr = nodes[nIdx].next;
            }

            if (matchedIndex != UINT32_MAX) {
                indexMap[v] = matchedIndex;
            } else {
                MeshIndex newIdx = (MeshIndex)outMesh->vertexCount++;
                outMesh->vertices[newIdx] = src->vertices[v];
                indexMap[v] = newIdx;

                size_t nodeIdx = nodeCount++;
                nodes[nodeIdx].position = pos;
                nodes[nodeIdx].targetIndex = newIdx;
                nodes[nodeIdx].next = head[slot];
                head[slot] = (uint32_t)(nodeIdx + 1);
            }
        }

        for (size_t idx = 0; idx + 2 < src->indexCount; idx += 3) {
            MeshIndex ia = indexMap[src->indices[idx]];
            MeshIndex ib = indexMap[src->indices[idx + 1]];
            MeshIndex ic = indexMap[src->indices[idx + 2]];
            /* Omitir triángulos degenerados o de área nula resultantes de soldadura */
            if (ia != ib && ib != ic && ic != ia &&
                Mesh_TriangleHasArea(outMesh->vertices[ia].position,
                                     outMesh->vertices[ib].position,
                                     outMesh->vertices[ic].position)) {
                outMesh->indices[outMesh->indexCount++] = ia;
                outMesh->indices[outMesh->indexCount++] = ib;
                outMesh->indices[outMesh->indexCount++] = ic;
            }
        }
        free(indexMap);
    }

    free(head);
    free(nodes);
    Mesh_MarkGeometryChanged(outMesh);
    Mesh_MarkSurfaceChanged(outMesh);
    return true;
}

bool MorphChunkManager_Copy(MorphChunkManager* dest, const MorphChunkManager* src, bool copyMeshes) {
    if (!dest || !src) return false;
    MorphChunkManager_Clear(dest);
    dest->chunkSize = src->chunkSize;
    dest->invChunkSize = src->invChunkSize;
    dest->voxelSize = src->voxelSize;
    dest->dirtyCount = src->dirtyCount;
    dest->surfaceCount = src->surfaceCount;

    if (dest->chunkCapacity < src->chunkCount) {
        size_t cap = src->chunkCount < MORPH_CHUNK_INITIAL_CAPACITY ? MORPH_CHUNK_INITIAL_CAPACITY : src->chunkCount;
        MorphChunk* grown = (MorphChunk*)realloc(dest->chunks, cap * sizeof(MorphChunk));
        if (!grown) return false;
        dest->chunks = grown;
        dest->chunkCapacity = cap;
    }

    if (dest->hashCapacity < src->hashCapacity) {
        free(dest->hashTable);
        dest->hashTable = (uint32_t*)calloc(src->hashCapacity, sizeof(uint32_t));
        if (!dest->hashTable) return false;
        dest->hashCapacity = src->hashCapacity;
    }
    memcpy(dest->hashTable, src->hashTable, src->hashCapacity * sizeof(uint32_t));

    dest->chunkCount = src->chunkCount;
    for (size_t i = 0; i < src->chunkCount; ++i) {
        const MorphChunk* s = &src->chunks[i];
        MorphChunk* d = &dest->chunks[i];
        d->cx = s->cx;
        d->cy = s->cy;
        d->cz = s->cz;
        d->key = s->key;
        d->bounds = s->bounds;
        d->isDirty = s->isDirty;
        d->hasSurface = s->hasSurface;
        d->generation = s->generation;
        d->dirtinessFlags = s->dirtinessFlags;
        d->mesh = Mesh_Create();
        if (copyMeshes && s->mesh.vertexCount > 0) {
            if (Mesh_ReserveVertices(&d->mesh, s->mesh.vertexCount) &&
                Mesh_ReserveIndices(&d->mesh, s->mesh.indexCount)) {
                memcpy(d->mesh.vertices, s->mesh.vertices, s->mesh.vertexCount * sizeof(MeshVertex));
                memcpy(d->mesh.indices, s->mesh.indices, s->mesh.indexCount * sizeof(MeshIndex));
                d->mesh.vertexCount = s->mesh.vertexCount;
                d->mesh.indexCount = s->mesh.indexCount;
            }
        }
    }
    return true;
}

size_t MorphChunkManager_GetTotalChunkCount(const MorphChunkManager* mgr) {
    return mgr ? mgr->chunkCount : 0;
}

size_t MorphChunkManager_GetDirtyChunkCount(const MorphChunkManager* mgr) {
    return mgr ? mgr->dirtyCount : 0;
}

size_t MorphChunkManager_GetSurfaceChunkCount(const MorphChunkManager* mgr) {
    return mgr ? mgr->surfaceCount : 0;
}

size_t MorphChunkManager_GetTotalVertexCount(const MorphChunkManager* mgr) {
    if (!mgr) return 0;
    size_t count = 0;
    for (size_t i = 0; i < mgr->chunkCount; ++i) {
        count += mgr->chunks[i].mesh.vertexCount;
    }
    return count;
}

size_t MorphChunkManager_GetTotalTriangleCount(const MorphChunkManager* mgr) {
    if (!mgr) return 0;
    size_t count = 0;
    for (size_t i = 0; i < mgr->chunkCount; ++i) {
        count += mgr->chunks[i].mesh.indexCount / 3;
    }
    return count;
}
