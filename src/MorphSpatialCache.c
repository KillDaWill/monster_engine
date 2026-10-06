/**
 * @file MorphSpatialCache.c
 * @brief Implementación del caché espacial persistente de bloques SDF (Sparse Brick Cache).
 * @author Monster Engine Team
 * @date 2026
 */

#include "MorphSpatialCache.h"
#include "MathUtils.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MORPH_SPATIAL_INITIAL_CAPACITY 256
#define MORPH_SPATIAL_INITIAL_HASH_CAPACITY 512

static inline uint64_t BrickKey(int32_t bx, int32_t by, int32_t bz) {
    uint64_t ux = (uint32_t)bx;
    uint64_t uy = (uint32_t)by;
    uint64_t uz = (uint32_t)bz;
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

static inline AABB3D BrickBounds(int32_t bx, int32_t by, int32_t bz, float brickSize,Vector3 origin) {
    Vector3 min = Vec3_Create((float)bx * brickSize, (float)by * brickSize, (float)bz * brickSize);
    Vector3 max = Vec3_Create((float)(bx + 1) * brickSize, (float)(by + 1) * brickSize, (float)(bz + 1) * brickSize);
    return AABB_FromMinMax(Vec3_Add(origin,min),Vec3_Add(origin,max));
}

MorphSpatialCache* MorphSpatialCache_Create(float brickSize) {
    if (brickSize <= 0.001f) brickSize = 0.25f;
    MorphSpatialCache* cache = calloc(1, sizeof(MorphSpatialCache));
    if (!cache) return NULL;

    cache->brickSize = brickSize;
    cache->invBrickSize = 1.0f / brickSize;
    cache->brickCapacity = MORPH_SPATIAL_INITIAL_CAPACITY;
    cache->bricks = calloc(cache->brickCapacity, sizeof(MorphSpatialBrick));
    if (!cache->bricks) {
        free(cache);
        return NULL;
    }

    cache->hashCapacity = MORPH_SPATIAL_INITIAL_HASH_CAPACITY;
    cache->hashTable = calloc(cache->hashCapacity, sizeof(uint32_t));
    if (!cache->hashTable) {
        free(cache->bricks);
        free(cache);
        return NULL;
    }

    return cache;
}

bool MorphSpatialCache_SetGridOrigin(MorphSpatialCache* cache,Vector3 origin) {
    if(!cache||cache->brickCount||!isfinite(origin.x)||!isfinite(origin.y)||!isfinite(origin.z))return false;
    cache->gridOrigin=origin;return true;
}

void MorphSpatialCache_Destroy(MorphSpatialCache* cache) {
    if (!cache) return;
    free(cache->bricks);
    free(cache->hashTable);
    free(cache);
}

void MorphSpatialCache_Clear(MorphSpatialCache* cache) {
    if (!cache) return;
    memset(cache->hashTable, 0, cache->hashCapacity * sizeof(uint32_t));
    cache->brickCount = 0;
    cache->dirtyCount = 0;
    cache->surfaceCount = 0;
}

static bool MorphSpatialCache_Rehash(MorphSpatialCache* cache, size_t newCapacity) {
    uint32_t* newTable = calloc(newCapacity, sizeof(uint32_t));
    if (!newTable) return false;

    for (size_t i = 0; i < cache->brickCount; ++i) {
        uint64_t key = cache->bricks[i].key;
        size_t slot = HashSlot(key, newCapacity);
        while (newTable[slot] != 0) {
            slot = (slot + 1) & (newCapacity - 1);
        }
        newTable[slot] = (uint32_t)(i + 1);
    }

    free(cache->hashTable);
    cache->hashTable = newTable;
    cache->hashCapacity = newCapacity;
    return true;
}

MorphSpatialBrick* MorphSpatialCache_GetBrick(MorphSpatialCache* cache, int32_t bx, int32_t by, int32_t bz, bool createIfMissing) {
    if (!cache) return NULL;
    uint64_t key = BrickKey(bx, by, bz);
    size_t slot = HashSlot(key, cache->hashCapacity);

    while (cache->hashTable[slot] != 0) {
        uint32_t index = cache->hashTable[slot] - 1;
        if (index < cache->brickCount) {
            MorphSpatialBrick* b = &cache->bricks[index];
            if (b->key == key && b->bx == bx && b->by == by && b->bz == bz) {
                return b;
            }
        }
        slot = (slot + 1) & (cache->hashCapacity - 1);
    }

    if (!createIfMissing) return NULL;

    /* Expandir arreglo denso de ladrillos si está lleno */
    if (cache->brickCount >= cache->brickCapacity) {
        size_t nextCapacity = cache->brickCapacity * 2;
        MorphSpatialBrick* grown = realloc(cache->bricks, nextCapacity * sizeof(MorphSpatialBrick));
        if (!grown) return NULL;
        cache->bricks = grown;
        cache->brickCapacity = nextCapacity;
    }

    /* Expandir y rehashear la tabla si el factor de carga supera 0.6 */
    if (cache->brickCount * 10 >= cache->hashCapacity * 6) {
        if (!MorphSpatialCache_Rehash(cache, cache->hashCapacity * 2)) {
            return NULL;
        }
        /* Recalcular slot tras rehash */
        slot = HashSlot(key, cache->hashCapacity);
        while (cache->hashTable[slot] != 0) {
            slot = (slot + 1) & (cache->hashCapacity - 1);
        }
    }

    size_t newIndex = cache->brickCount++;
    MorphSpatialBrick* brick = &cache->bricks[newIndex];
    memset(brick, 0, sizeof(*brick));
    brick->bx = bx;
    brick->by = by;
    brick->bz = bz;
    brick->key = key;
    brick->bounds = BrickBounds(bx, by, bz, cache->brickSize,cache->gridOrigin);
    brick->isDirty = true;
    brick->dirtinessFlags = MORPH_DIRT_NONE;
    brick->lastUpdatedAge = -1.0f;
    cache->dirtyCount++;

    cache->hashTable[slot] = (uint32_t)(newIndex + 1);
    return brick;
}

const MorphSpatialBrick* MorphSpatialCache_FindBrick(const MorphSpatialCache* cache, int32_t bx, int32_t by, int32_t bz) {
    if (!cache || cache->brickCount == 0) return NULL;
    uint64_t key = BrickKey(bx, by, bz);
    size_t slot = HashSlot(key, cache->hashCapacity);

    while (cache->hashTable[slot] != 0) {
        uint32_t index = cache->hashTable[slot] - 1;
        if (index < cache->brickCount) {
            const MorphSpatialBrick* b = &cache->bricks[index];
            if (b->key == key && b->bx == bx && b->by == by && b->bz == bz) {
                return b;
            }
        }
        slot = (slot + 1) & (cache->hashCapacity - 1);
    }
    return NULL;
}

size_t MorphSpatialCache_MarkDirtyAABB(MorphSpatialCache* cache, AABB3D aabb, uint32_t dirtinessFlags) {
    if (!cache) return 0;
    int32_t minX = (int32_t)floorf((aabb.start.x-cache->gridOrigin.x) * cache->invBrickSize);
    int32_t maxX = (int32_t)floorf((aabb.end.x-cache->gridOrigin.x) * cache->invBrickSize);
    int32_t minY = (int32_t)floorf((aabb.start.y-cache->gridOrigin.y) * cache->invBrickSize);
    int32_t maxY = (int32_t)floorf((aabb.end.y-cache->gridOrigin.y) * cache->invBrickSize);
    int32_t minZ = (int32_t)floorf((aabb.start.z-cache->gridOrigin.z) * cache->invBrickSize);
    int32_t maxZ = (int32_t)floorf((aabb.end.z-cache->gridOrigin.z) * cache->invBrickSize);

    size_t count = 0;
    for (int32_t bx = minX; bx <= maxX; ++bx) {
        for (int32_t by = minY; by <= maxY; ++by) {
            for (int32_t bz = minZ; bz <= maxZ; ++bz) {
                MorphSpatialBrick* brick = MorphSpatialCache_GetBrick(cache, bx, by, bz, true);
                if (brick) {
                    if (!brick->isDirty) {
                        brick->isDirty = true;
                        cache->dirtyCount++;
                    }
                    brick->dirtinessFlags |= dirtinessFlags;
                    count++;
                }
            }
        }
    }
    return count;
}

size_t MorphSpatialCache_MarkSweptModuleDirty(MorphSpatialCache* cache, AABB3D before, AABB3D after, float padding, bool isTopology) {
    if (!cache) return 0;
    AABB3D swept = AABB_Empty();
    AABB_ExpandPoint(&swept, before.start);
    AABB_ExpandPoint(&swept, before.end);
    AABB_ExpandPoint(&swept, after.start);
    AABB_ExpandPoint(&swept, after.end);
    AABB_Pad(&swept, padding);

    uint32_t flags = isTopology ? MORPH_DIRT_TOPOLOGY : MORPH_DIRT_DEFORMATION;
    return MorphSpatialCache_MarkDirtyAABB(cache, swept, flags);
}

size_t MorphSpatialCache_UpdateDirty(MorphSpatialCache* cache, const MonsterSDF* sdf, float age) {
    if (!cache || !sdf) return 0;
    size_t updated = 0;

    for (size_t i = 0; i < cache->brickCount; ++i) {
        MorphSpatialBrick* brick = &cache->bricks[i];
        if (!brick->isDirty) continue;

        /* Compilar candidatos de conectores relevantes para el cubo volumétrico */
        brick->candidateCount = MonsterSDF_CompileConnectorCandidates(sdf, brick->bounds,
            brick->candidates, MORPH_SPATIAL_BRICK_MAX_CANDIDATES);

        /* Evaluar distancia en el centro y determinar intersección con superficie */
        Vector3 center = Vec3_Scale(Vec3_Add(brick->bounds.start, brick->bounds.end), 0.5f);
        float halfDiagonal = 0.5f * Vec3_Length(Vec3_Sub(brick->bounds.end, brick->bounds.start));
        float distCenter = MonsterSDF_EvaluateDistance(sdf, center);
        brick->centerDistance = distCenter;

        bool wasSurface = brick->hasSurface;
        brick->hasSurface = (fabsf(distCenter) <= halfDiagonal + 0.05f);
        if (brick->hasSurface && !wasSurface) cache->surfaceCount++;
        else if (!brick->hasSurface && wasSurface) cache->surfaceCount--;

        brick->isDirty = false;
        brick->dirtinessFlags = MORPH_DIRT_NONE;
        brick->lastUpdatedAge = age;
        updated++;
    }

    cache->dirtyCount = 0;
    return updated;
}

bool MorphSpatialCache_IsRegionDirty(const MorphSpatialCache* cache, AABB3D aabb, uint32_t* outDirtinessFlags) {
    if (!cache || cache->dirtyCount == 0) return false;
    int32_t minX = (int32_t)floorf((aabb.start.x-cache->gridOrigin.x) * cache->invBrickSize);
    int32_t maxX = (int32_t)floorf((aabb.end.x-cache->gridOrigin.x) * cache->invBrickSize);
    int32_t minY = (int32_t)floorf((aabb.start.y-cache->gridOrigin.y) * cache->invBrickSize);
    int32_t maxY = (int32_t)floorf((aabb.end.y-cache->gridOrigin.y) * cache->invBrickSize);
    int32_t minZ = (int32_t)floorf((aabb.start.z-cache->gridOrigin.z) * cache->invBrickSize);
    int32_t maxZ = (int32_t)floorf((aabb.end.z-cache->gridOrigin.z) * cache->invBrickSize);

    bool dirtyFound = false;
    uint32_t combinedFlags = MORPH_DIRT_NONE;

    for (int32_t bx = minX; bx <= maxX; ++bx) {
        for (int32_t by = minY; by <= maxY; ++by) {
            for (int32_t bz = minZ; bz <= maxZ; ++bz) {
                const MorphSpatialBrick* brick = MorphSpatialCache_FindBrick(cache, bx, by, bz);
                if (brick && brick->isDirty) {
                    dirtyFound = true;
                    combinedFlags |= brick->dirtinessFlags;
                }
            }
        }
    }

    if (outDirtinessFlags) *outDirtinessFlags = combinedFlags;
    return dirtyFound;
}

size_t MorphSpatialCache_GetDirtyBricks(const MorphSpatialCache* cache, const MorphSpatialBrick** outBricks, size_t maxBricks) {
    if (!cache || !outBricks || maxBricks == 0) return 0;
    size_t filled = 0;
    for (size_t i = 0; i < cache->brickCount && filled < maxBricks; ++i) {
        if (cache->bricks[i].isDirty) {
            outBricks[filled++] = &cache->bricks[i];
        }
    }
    return filled;
}

size_t MorphSpatialCache_GetTotalBrickCount(const MorphSpatialCache* cache) {
    return cache ? cache->brickCount : 0;
}

size_t MorphSpatialCache_GetDirtyBrickCount(const MorphSpatialCache* cache) {
    return cache ? cache->dirtyCount : 0;
}

size_t MorphSpatialCache_GetSurfaceBrickCount(const MorphSpatialCache* cache) {
    return cache ? cache->surfaceCount : 0;
}
