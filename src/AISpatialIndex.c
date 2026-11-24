/**
 * @file AISpatialIndex.c
 * @brief Implementación de rejilla espacial uniforme 2D XZ y mapa hash O(1).
 */
#include "AISpatialIndex.h"
#include "AIWorld.h"
#include "EcosystemResources.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static uint32_t nextPow2(uint32_t v) {
    uint32_t p = 16u;
    while (p < v && p < (1u << 28)) p <<= 1;
    return p;
}

static inline uint32_t hashId(uint32_t id, uint32_t mask) {
    return (id * 2654435761u) & mask;
}

static inline int clampCell(int c, int dim) {
    if (c < 0) return 0;
    if (c >= dim) return dim - 1;
    return c;
}

static inline int coordToCell(const AISpatialIndex *idx, float v) {
    int c = (int)floorf((v + idx->halfSize) * idx->invCellSize);
    return clampCell(c, idx->gridDim);
}

static inline int cellIndexFor(const AISpatialIndex *idx, Vector3 pos) {
    int cx = coordToCell(idx, pos.x);
    int cz = coordToCell(idx, pos.z);
    return cz * idx->gridDim + cx;
}

static void unlinkNode(AISpatialIndex *idx, uint32_t slot) {
    AISpatialNode *n = &idx->nodes[slot];
    if (!n->inGrid) return;
    AISpatialGridLayer *layer = &idx->layers[n->layer];
    if (n->prev >= 0) {
        idx->nodes[n->prev].next = n->next;
    } else if (n->cellIndex >= 0 && (unsigned)n->cellIndex < idx->cellCount) {
        layer->cellHeads[n->cellIndex] = n->next;
    }
    if (n->next >= 0) {
        idx->nodes[n->next].prev = n->prev;
    }
    n->prev = -1;
    n->next = -1;
    n->cellIndex = -1;
    n->inGrid = false;
    if (layer->count > 0u) --layer->count;
}

static void linkNode(AISpatialIndex *idx, uint32_t slot, AISpatialLayer layerId,
                     int cellIndex, float radius) {
    AISpatialNode *n = &idx->nodes[slot];
    AISpatialGridLayer *layer = &idx->layers[layerId];
    int head = layer->cellHeads[cellIndex];
    n->prev = -1;
    n->next = head;
    n->cellIndex = cellIndex;
    n->layer = (uint8_t)layerId;
    n->inGrid = true;
    if (head >= 0) {
        idx->nodes[head].prev = (int)slot;
    }
    layer->cellHeads[cellIndex] = (int)slot;
    ++layer->count;
    if (radius > layer->maxRadius) layer->maxRadius = radius;
}

static void hashInsert(AISpatialIndex *idx, uint32_t id, uint32_t slot) {
    uint32_t pos = hashId(id, idx->hashMask);
    while (idx->hashKeys[pos] != 0u && idx->hashKeys[pos] != id) {
        pos = (pos + 1u) & idx->hashMask;
    }
    idx->hashKeys[pos] = id;
    idx->hashSlots[pos] = slot;
}

/* Borrado con desplazamiento hacia atrás (backward-shift): mantiene el invariante sin lápidas. */
static void hashRemove(AISpatialIndex *idx, uint32_t id) {
    uint32_t mask = idx->hashMask;
    uint32_t pos = hashId(id, mask);
    while (idx->hashKeys[pos] != 0u) {
        if (idx->hashKeys[pos] == id) {
            uint32_t hole = pos;
            uint32_t cur = (hole + 1u) & mask;
            while (idx->hashKeys[cur] != 0u) {
                uint32_t home = hashId(idx->hashKeys[cur], mask);
                uint32_t distCur = (cur - home) & mask;
                uint32_t distHole = (hole - home) & mask;
                if (distHole < distCur) {
                    idx->hashKeys[hole] = idx->hashKeys[cur];
                    idx->hashSlots[hole] = idx->hashSlots[cur];
                    hole = cur;
                }
                cur = (cur + 1u) & mask;
            }
            idx->hashKeys[hole] = 0u;
            idx->hashSlots[hole] = 0u;
            return;
        }
        pos = (pos + 1u) & mask;
    }
}

AISpatialIndex *AISpatialIndex_Create(unsigned capacity, float halfSize, float cellSize) {
    if (!capacity || !isfinite(halfSize) || halfSize <= 0.0f) return NULL;
    if (!isfinite(cellSize) || cellSize < 0.5f) cellSize = 4.0f;

    AISpatialIndex *idx = calloc(1, sizeof(*idx));
    if (!idx) return NULL;
    idx->capacity = capacity;
    idx->halfSize = halfSize;
    idx->cellSize = cellSize;
    idx->invCellSize = 1.0f / cellSize;

    int dim = (int)ceilf((2.0f * halfSize) / cellSize);
    if (dim < 1) dim = 1;
    if (dim > 256) {
        dim = 256;
        idx->cellSize = (2.0f * halfSize) / (float)dim;
        idx->invCellSize = 1.0f / idx->cellSize;
    }
    idx->gridDim = dim;
    idx->cellCount = (unsigned)(dim * dim);

    for (unsigned l = 0; l < AI_SPATIAL_LAYER_COUNT; ++l) {
        idx->layers[l].cellHeads = malloc(idx->cellCount * sizeof(int));
        if (!idx->layers[l].cellHeads) {
            AISpatialIndex_Destroy(idx);
            return NULL;
        }
        for (unsigned c = 0; c < idx->cellCount; ++c) idx->layers[l].cellHeads[c] = -1;
        idx->layers[l].maxRadius = 0.0f;
        idx->layers[l].count = 0u;
    }

    idx->nodes = malloc(capacity * sizeof(*idx->nodes));
    idx->hashCapacity = nextPow2(capacity * 2u + 8u);
    idx->hashMask = idx->hashCapacity - 1u;
    idx->hashKeys = calloc(idx->hashCapacity, sizeof(uint32_t));
    idx->hashSlots = calloc(idx->hashCapacity, sizeof(uint32_t));
    idx->freeSlots = malloc(capacity * sizeof(uint32_t));
    idx->activeSlots = malloc(capacity * sizeof(uint32_t));
    idx->slotToActive = malloc(capacity * sizeof(uint32_t));
    if (!idx->nodes || !idx->hashKeys || !idx->hashSlots ||
        !idx->freeSlots || !idx->activeSlots || !idx->slotToActive) {
        AISpatialIndex_Destroy(idx);
        return NULL;
    }

    for (unsigned i = 0; i < capacity; ++i) {
        idx->nodes[i] = (AISpatialNode){.prev = -1, .next = -1, .cellIndex = -1, .layer = 0, .inGrid = false};
        /* Orden inverso para que las primeras ranuras entregadas sean 0, 1, 2... (compatibilidad). */
        idx->freeSlots[i] = capacity - 1u - i;
        idx->slotToActive[i] = UINT32_MAX;
    }
    idx->freeCount = capacity;
    idx->activeCount = 0u;
    return idx;
}

void AISpatialIndex_Destroy(AISpatialIndex *idx) {
    if (!idx) return;
    for (unsigned l = 0; l < AI_SPATIAL_LAYER_COUNT; ++l) {
        free(idx->layers[l].cellHeads);
    }
    free(idx->nodes);
    free(idx->hashKeys);
    free(idx->hashSlots);
    free(idx->freeSlots);
    free(idx->activeSlots);
    free(idx->slotToActive);
    free(idx);
}

bool AISpatialIndex_SetCellSize(AISpatialIndex *idx, float cellSize,
                                const struct AIWorldObject *objects) {
    if (!idx || !isfinite(cellSize) || cellSize < 0.5f) return false;
    int dim = (int)ceilf((2.0f * idx->halfSize) / cellSize);
    if (dim < 1) dim = 1;
    if (dim > 256) {
        dim = 256;
        cellSize = (2.0f * idx->halfSize) / (float)dim;
    }
    unsigned newCells = (unsigned)(dim * dim);
    int *newHeads[AI_SPATIAL_LAYER_COUNT] = {0};
    for (unsigned l = 0; l < AI_SPATIAL_LAYER_COUNT; ++l) {
        newHeads[l] = malloc(newCells * sizeof(int));
        if (!newHeads[l]) {
            for (unsigned k = 0; k < l; ++k) free(newHeads[k]);
            return false;
        }
        for (unsigned c = 0; c < newCells; ++c) newHeads[l][c] = -1;
    }
    for (unsigned l = 0; l < AI_SPATIAL_LAYER_COUNT; ++l) {
        free(idx->layers[l].cellHeads);
        idx->layers[l].cellHeads = newHeads[l];
        idx->layers[l].count = 0u;
        idx->layers[l].maxRadius = 0.0f;
    }
    idx->cellSize = cellSize;
    idx->invCellSize = 1.0f / cellSize;
    idx->gridDim = dim;
    idx->cellCount = newCells;
    for (unsigned i = 0; i < idx->capacity; ++i) {
        idx->nodes[i].prev = -1;
        idx->nodes[i].next = -1;
        idx->nodes[i].cellIndex = -1;
        idx->nodes[i].inGrid = false;
    }
    if (objects) {
        for (unsigned k = 0; k < idx->activeCount; ++k) {
            uint32_t slot = idx->activeSlots[k];
            const AIWorldObject *o = &objects[slot];
            if (!o->active) continue;
            AISpatialLayer layer = AISpatialIndex_LayerForObject(o);
            linkNode(idx, slot, layer, cellIndexFor(idx, o->position), o->radius);
        }
    }
    return true;
}

AISpatialLayer AISpatialIndex_LayerForObject(const AIWorldObject *obj) {
    if (!obj) return AI_SPATIAL_LAYER_RESOURCES;
    if (obj->type == AI_CREATURE) return AI_SPATIAL_LAYER_CREATURES;
    if (obj->type == AI_OBSTACLE || obj->type == AI_WATER || !obj->walkable || obj->occludes) {
        return AI_SPATIAL_LAYER_STATIC;
    }
    if (obj->type == AI_FOOD && obj->foodCategory == ECO_MEAT) {
        return AI_SPATIAL_LAYER_CARCASSES;
    }
    return AI_SPATIAL_LAYER_RESOURCES;
}

bool AISpatialIndex_AllocSlot(AISpatialIndex *idx, uint32_t *outSlot) {
    if (!idx || !outSlot || idx->freeCount == 0u) return false;
    *outSlot = idx->freeSlots[--idx->freeCount];
    return true;
}

void AISpatialIndex_Insert(AISpatialIndex *idx, uint32_t slot, uint32_t id,
                           AISpatialLayer layer, Vector3 pos, float radius) {
    if (!idx || slot >= idx->capacity || !id) return;
    hashInsert(idx, id, slot);
    if (idx->slotToActive[slot] == UINT32_MAX) {
        uint32_t aIdx = idx->activeCount++;
        idx->activeSlots[aIdx] = slot;
        idx->slotToActive[slot] = aIdx;
    }
    if (idx->nodes[slot].inGrid) unlinkNode(idx, slot);
    linkNode(idx, slot, layer, cellIndexFor(idx, pos), radius);
}

void AISpatialIndex_Update(AISpatialIndex *idx, uint32_t slot,
                           AISpatialLayer layer, Vector3 pos, float radius) {
    if (!idx || slot >= idx->capacity) return;
    int newCell = cellIndexFor(idx, pos);
    AISpatialNode *n = &idx->nodes[slot];
    if (n->inGrid && n->layer == (uint8_t)layer && n->cellIndex == newCell) {
        if (radius > idx->layers[layer].maxRadius) idx->layers[layer].maxRadius = radius;
        return;
    }
    if (n->inGrid) unlinkNode(idx, slot);
    linkNode(idx, slot, layer, newCell, radius);
}

void AISpatialIndex_Remove(AISpatialIndex *idx, uint32_t slot, uint32_t id) {
    if (!idx || slot >= idx->capacity) return;
    if (id) hashRemove(idx, id);
    if (idx->nodes[slot].inGrid) unlinkNode(idx, slot);
    uint32_t aIdx = idx->slotToActive[slot];
    if (aIdx != UINT32_MAX) {
        uint32_t lastSlot = idx->activeSlots[--idx->activeCount];
        if (aIdx < idx->activeCount) {
            idx->activeSlots[aIdx] = lastSlot;
            idx->slotToActive[lastSlot] = aIdx;
        }
        idx->slotToActive[slot] = UINT32_MAX;
    }
    if (idx->freeCount < idx->capacity) {
        idx->freeSlots[idx->freeCount++] = slot;
    }
}

int AISpatialIndex_Lookup(const AISpatialIndex *idx, uint32_t id) {
    if (!idx || !id) return -1;
    uint32_t mask = idx->hashMask;
    uint32_t pos = hashId(id, mask);
    while (idx->hashKeys[pos] != 0u) {
        if (idx->hashKeys[pos] == id) return (int)idx->hashSlots[pos];
        pos = (pos + 1u) & mask;
    }
    return -1;
}

unsigned AISpatialIndex_QueryAABB(const AISpatialIndex *idx,
                                  uint32_t layerMask,
                                  float minX, float minZ, float maxX, float maxZ,
                                  uint32_t *outSlots, unsigned capacity, bool *overflowed) {
    if (overflowed) *overflowed = false;
    if (!idx || !outSlots || !capacity) return 0u;
    unsigned written = 0u;
    int dim = idx->gridDim;

    for (unsigned l = 0; l < AI_SPATIAL_LAYER_COUNT; ++l) {
        if (!(layerMask & (1u << l))) continue;
        const AISpatialGridLayer *layer = &idx->layers[l];
        if (layer->count == 0u) continue;
        float pad = layer->maxRadius + 1e-4f;
        int x0 = coordToCell(idx, minX - pad);
        int x1 = coordToCell(idx, maxX + pad);
        int z0 = coordToCell(idx, minZ - pad);
        int z1 = coordToCell(idx, maxZ + pad);

        for (int cz = z0; cz <= z1; ++cz) {
            int rowBase = cz * dim;
            for (int cx = x0; cx <= x1; ++cx) {
                int slot = layer->cellHeads[rowBase + cx];
                while (slot >= 0) {
                    if (written < capacity) {
                        outSlots[written++] = (uint32_t)slot;
                    } else {
                        if (overflowed) *overflowed = true;
                        return written;
                    }
                    slot = idx->nodes[slot].next;
                }
            }
        }
    }
    return written;
}

unsigned AISpatialIndex_QueryRadius(const AISpatialIndex *idx,
                                    uint32_t layerMask, Vector3 center, float radius,
                                    uint32_t *outSlots, unsigned capacity, bool *overflowed) {
    if (!isfinite(radius) || radius < 0.0f) radius = 0.0f;
    return AISpatialIndex_QueryAABB(idx, layerMask,
                                    center.x - radius, center.z - radius,
                                    center.x + radius, center.z + radius,
                                    outSlots, capacity, overflowed);
}

unsigned AISpatialIndex_QuerySegment(const AISpatialIndex *idx,
                                     uint32_t layerMask,
                                     Vector3 a, Vector3 b, float radius,
                                     uint32_t *outSlots, unsigned capacity, bool *overflowed) {
    if (!isfinite(radius) || radius < 0.0f) radius = 0.0f;
    float minX = fminf(a.x, b.x) - radius;
    float maxX = fmaxf(a.x, b.x) + radius;
    float minZ = fminf(a.z, b.z) - radius;
    float maxZ = fmaxf(a.z, b.z) + radius;
    return AISpatialIndex_QueryAABB(idx, layerMask, minX, minZ, maxX, maxZ,
                                    outSlots, capacity, overflowed);
}

void AISpatialIndex_SortSlotsById(const AIWorldObject *objects,
                                  uint32_t *slots, unsigned count) {
    if (!objects || !slots || count < 2u) return;
    /* Ordenación por inserción para listas pequeñas; Shell sort determinista para mayores. */
    if (count <= 32u) {
        for (unsigned i = 1u; i < count; ++i) {
            uint32_t s = slots[i];
            uint32_t key = objects[s].id;
            unsigned j = i;
            while (j > 0u && objects[slots[j - 1u]].id > key) {
                slots[j] = slots[j - 1u];
                --j;
            }
            slots[j] = s;
        }
        return;
    }
    for (unsigned gap = count / 2u; gap > 0u; gap /= 2u) {
        for (unsigned i = gap; i < count; ++i) {
            uint32_t s = slots[i];
            uint32_t key = objects[s].id;
            unsigned j = i;
            while (j >= gap && objects[slots[j - gap]].id > key) {
                slots[j] = slots[j - gap];
                j -= gap;
            }
            slots[j] = s;
        }
    }
}
