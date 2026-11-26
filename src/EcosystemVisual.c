/**
 * @file EcosystemVisual.c
 * @brief Caché compartida, LOD con presupuesto acotado, ranuras ligeras (1024+) y reconciliación por snapshots.
 */
#include "EcosystemVisual.h"
#include "Creature.h"
#include "CreatureRecipes.h"
#include "PrimitiveMesh.h"
#include "RigBuilder.h"
#include "SurfaceMapper.h"
#include "MonsterAnimation.h"
#include "GaitPresets.h"
#include "EcosystemAnatomy.h"
#include "EcosystemAnatomyCache.h"
#include "TerrainAnimationAdapter.h"
#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

typedef enum CacheState { CACHE_EMPTY, CACHE_QUEUED, CACHE_BUILDING, CACHE_PENDING, CACHE_READY, CACHE_FAILED } CacheState;

typedef struct VisualSlot {
    bool used, seen, explicitTier, monsterReady;
    uint32_t slotIndex, recipeId, stage, bucket, entitySeed, growthStep;
    AIObjectId entityId;
    CreatureGenome genome;
    CreatureVisualProfile profile;
    CreatureLifeStage lifeStage;
    VisualTier tier, requestedTier;
    EcosystemVisualProjection projection;
    Vector3 position;
    Vector3 groundNormal;
    Quaternion orientation;
    float yaw, scale, animationClock, poseAlpha;
    float speciesRadius, developmentScale, meshRadius, meshLength, calibrationLength, axialTotalScale;
    float maturity, crouch, speed;
    Vector3 previousPosition, mouthTarget, supportCenter;
    Vector3 lastContactOrigin, lastContactCenter;
    Quaternion lastContactOrientation;
    float lastContactScale;
    float previousYaw, runBlend, actionClock, restBlend;
    float gaitPhase, bobY, leanPitch;
    float midAnimAccumulator;
    bool oralActive, hasAnatomicalBody;
    CreatureAction action;
    unsigned anatomyGeneration;
    uint8_t cervicalJointCount;
    int16_t cervicalJointIndices[REACH_MAX_JOINTS];
    Quaternion cervicalRotations[REACH_MAX_JOINTS];
    Color tintColor;
    SurfacePhenotype surface;
    SurfaceRecipe surfaceRecipe;
    /* Puntero perezoso: sólo se construye para ranuras NEAR/MID (o mundos pequeños legacy). */
    Monster* monster;
    SkeletonPose* previousPose;
    /* Caché estructural por ranura para evitar recomputar escalas en GetDrawPackets. */
    uint64_t cachedScaleCacheKey;
    unsigned cachedScaleAnatomyGen;
    Vector3 cachedHeadScale;
    Vector3 cachedJawScale;
    float cachedEyeScale;
    float cachedTrunkRadius;
} VisualSlot;

typedef struct CacheEntry {
    CacheState state;
    EcosystemVisualCacheKey key;
    Mesh body, head;
    MonsterVisualEye* eyes;
    size_t eyeCount;
    MonsterVisualMouth* mouths;
    size_t mouthCount;
    AnatomyDeformer* deformer;
    AABB3D jointBounds[SKELETON_MAX_JOINTS];
    Rig rig;
    SkeletonPose restPose;
    float meshRadius, meshLength, calibrationLength, axialTotalScale;
    float trunkRadius;
    size_t approxBytes;
} CacheEntry;

typedef struct SlotHashMap {
    uint32_t* keys;
    uint32_t* values;
    uint32_t mask;
    uint32_t capacity;
} SlotHashMap;

typedef struct VisualState {
    EcosystemWorld* world;
    const WorldStaticData* staticWorld; /**< Referencia inmutable retenida para muestreo de patas 3D. */
    bool snapshotMode;
    VisualSlot* slots;
    size_t slotCapacity;
    SlotHashMap slotMap;
    uint32_t* freeSlots;
    size_t freeSlotCount;

    CacheEntry* cache;
    EcosystemVisualCacheKey* queue;
    size_t queueHead, queueTail, queueCount;
    Mesh fallback;
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    pthread_t worker;
    bool workerStarted, stop;
    bool hasWorldClock;
    uint64_t lastWorldTick;
    float lodClock;
    unsigned frameEnqueues;
    double animationSeconds;
    size_t slotCount, cacheCount, buildCount, cacheHits;
    size_t monsterInstances;
    size_t totalCacheBytes;

    /* Presupuesto de LOD configurable. */
    unsigned maxNear;
    unsigned maxMid;
    float lodBias;
    bool allowAutoNearLargePop;

    /* Contadores del último frame. */
    size_t lastVisible, lastAnimated, lastNear, lastMid, lastFar, lastCulled;
    EvolutionProfiler* profiler;
} VisualState;

/* ========================================================================== */
/* Hash O(1) entityId -> visualSlotIndex                                      */
/* ========================================================================== */

static uint32_t nextPow2(uint32_t v) {
    uint32_t p = 64u;
    while (p < v && p < (1u << 24)) p <<= 1;
    return p;
}

static inline uint32_t hashKey(uint32_t k, uint32_t mask) {
    return (k * 2654435761u) & mask;
}

static bool slotMapInit(SlotHashMap* m, size_t expected) {
    uint32_t cap = nextPow2((uint32_t)(expected * 2u + 16u));
    m->keys = calloc(cap, sizeof(uint32_t));
    m->values = calloc(cap, sizeof(uint32_t));
    if (!m->keys || !m->values) {
        free(m->keys); free(m->values); memset(m, 0, sizeof(*m));
        return false;
    }
    m->capacity = cap;
    m->mask = cap - 1u;
    return true;
}

static void slotMapFree(SlotHashMap* m) {
    free(m->keys); free(m->values); memset(m, 0, sizeof(*m));
}

static void slotMapPut(SlotHashMap* m, uint32_t id, uint32_t slotIdx) {
    if (!m->keys || !id) return;
    uint32_t pos = hashKey(id, m->mask);
    while (m->keys[pos] != 0u && m->keys[pos] != id) pos = (pos + 1u) & m->mask;
    m->keys[pos] = id;
    m->values[pos] = slotIdx;
}

static int slotMapGet(const SlotHashMap* m, uint32_t id) {
    if (!m->keys || !id) return -1;
    uint32_t pos = hashKey(id, m->mask);
    while (m->keys[pos] != 0u) {
        if (m->keys[pos] == id) return (int)m->values[pos];
        pos = (pos + 1u) & m->mask;
    }
    return -1;
}

static void slotMapRemove(SlotHashMap* m, uint32_t id) {
    if (!m->keys || !id) return;
    uint32_t mask = m->mask;
    uint32_t pos = hashKey(id, mask);
    while (m->keys[pos] != 0u) {
        if (m->keys[pos] == id) {
            uint32_t hole = pos;
            uint32_t cur = (hole + 1u) & mask;
            while (m->keys[cur] != 0u) {
                uint32_t home = hashKey(m->keys[cur], mask);
                if (((hole - home) & mask) < ((cur - home) & mask)) {
                    m->keys[hole] = m->keys[cur];
                    m->values[hole] = m->values[cur];
                    hole = cur;
                }
                cur = (cur + 1u) & mask;
            }
            m->keys[hole] = 0u; m->values[hole] = 0u;
            return;
        }
        pos = (pos + 1u) & mask;
    }
}

static bool EnsureSlotCapacity(VisualState* state, size_t needed) {
    if (needed <= state->slotCapacity) return true;
    size_t newCap = state->slotCapacity ? state->slotCapacity : 256u;
    while (newCap < needed) newCap *= 2u;
    VisualSlot* newSlots = realloc(state->slots, newCap * sizeof(*newSlots));
    if (!newSlots) return false;
    memset(newSlots + state->slotCapacity, 0, (newCap - state->slotCapacity) * sizeof(*newSlots));
    uint32_t* newFree = realloc(state->freeSlots, newCap * sizeof(*newFree));
    if (!newFree) {
        state->slots = newSlots;
        return false;
    }
    SlotHashMap nextMap = {0};
    if (!slotMapInit(&nextMap, newCap)) {
        state->slots = newSlots;
        state->freeSlots = newFree;
        return false;
    }
    for (size_t i = 0; i < state->slotCapacity; ++i) {
        if (newSlots[i].used) slotMapPut(&nextMap, newSlots[i].entityId, (uint32_t)i);
    }
    for (size_t i = newCap; i > state->slotCapacity; --i) {
        newFree[state->freeSlotCount++] = (uint32_t)(i - 1u);
    }
    slotMapFree(&state->slotMap);
    state->slotMap = nextMap;
    state->slots = newSlots;
    state->freeSlots = newFree;
    state->slotCapacity = newCap;
    return true;
}

static VisualSlot* FindSlot(VisualState* state, AIObjectId id) {
    int idx = slotMapGet(&state->slotMap, id);
    if (idx >= 0 && (size_t)idx < state->slotCapacity &&
        state->slots[idx].used && state->slots[idx].entityId == id) {
        return &state->slots[idx];
    }
    return NULL;
}

static VisualSlot* AllocSlot(VisualState* state, uint32_t* outVisIdx) {
    if (state->freeSlotCount == 0u) {
        if (!EnsureSlotCapacity(state, state->slotCapacity * 2u)) return NULL;
    }
    uint32_t idx = state->freeSlots[--state->freeSlotCount];
    if (outVisIdx) *outVisIdx = idx;
    return &state->slots[idx];
}

/* ========================================================================== */
/* Caché compartida de mallas y worker asíncrono                              */
/* ========================================================================== */

static bool KeyEqual(EcosystemVisualCacheKey a, EcosystemVisualCacheKey b) {
    return a.recipeId == b.recipeId && a.stage == b.stage &&
           a.morphologyBucket == b.morphologyBucket && a.tier == b.tier;
}

static bool KeyValid(EcosystemVisualCacheKey key) {
    return key.recipeId != 0 && key.stage < 4u &&
           key.morphologyBucket < VISUAL_MORPHOLOGY_BUCKETS && key.tier < VISUAL_TIER_COUNT;
}

uint64_t EcosystemVisual_CacheKeyHash(EcosystemVisualCacheKey key) {
    return VisualVariation_CacheKey(key.recipeId, key.stage, key.morphologyBucket, (uint32_t)key.tier);
}

static size_t CacheFindLocked(const VisualState* state, EcosystemVisualCacheKey key) {
    for (size_t i = 0; i < ECOSYSTEM_VISUAL_CACHE_CAPACITY; ++i) {
        if (state->cache[i].state != CACHE_EMPTY && KeyEqual(state->cache[i].key, key)) return i;
    }
    return ECOSYSTEM_VISUAL_CACHE_CAPACITY;
}

static size_t CacheReserveLocked(VisualState* state, EcosystemVisualCacheKey key) {
    size_t found = CacheFindLocked(state, key);
    if (found < ECOSYSTEM_VISUAL_CACHE_CAPACITY) return found;
    for (size_t i = 0; i < ECOSYSTEM_VISUAL_CACHE_CAPACITY; ++i) {
        if (state->cache[i].state == CACHE_EMPTY) {
            state->cache[i].key = key;
            state->cache[i].state = CACHE_QUEUED;
            ++state->cacheCount;
            return i;
        }
    }
    return ECOSYSTEM_VISUAL_CACHE_CAPACITY;
}

static bool QueueContainsLocked(const VisualState* state, EcosystemVisualCacheKey key) {
    for (size_t i = 0, p = state->queueHead; i < state->queueCount;
         ++i, p = (p + 1u) % ECOSYSTEM_VISUAL_QUEUE_CAPACITY) {
        if (KeyEqual(state->queue[p], key)) return true;
    }
    return false;
}

static bool QueueKeyLocked(VisualState* state, EcosystemVisualCacheKey key, bool unrestricted) {
    size_t index = CacheReserveLocked(state, key);
    if (index == ECOSYSTEM_VISUAL_CACHE_CAPACITY) return false;
    CacheEntry* entry = &state->cache[index];
    if (entry->state == CACHE_READY || entry->state == CACHE_BUILDING || entry->state == CACHE_PENDING) return true;
    if (entry->state == CACHE_QUEUED && QueueContainsLocked(state, key)) return true;
    if (state->queueCount == ECOSYSTEM_VISUAL_QUEUE_CAPACITY || (!unrestricted && state->frameEnqueues >= 2u))
        return false;
    state->queue[state->queueTail] = key;
    state->queueTail = (state->queueTail + 1u) % ECOSYSTEM_VISUAL_QUEUE_CAPACITY;
    ++state->queueCount;
    entry->state = CACHE_QUEUED;
    if (!unrestricted) ++state->frameEnqueues;
    pthread_cond_signal(&state->condition);
    return true;
}

static void FreeEyes(MonsterVisualEye* eyes, size_t count) {
    if (!eyes) return;
    for (size_t i = 0; i < count; ++i) Mesh_Free(&eyes[i].globe);
    free(eyes);
}

static void FreeMouths(MonsterVisualMouth* mouths, size_t count) {
    if (!mouths) return;
    for (size_t i = 0; i < count; ++i) MonsterVisualMouth_Free(&mouths[i]);
    free(mouths);
}

static void FreeCacheEntry(CacheEntry* entry) {
    Mesh_Free(&entry->body);
    Mesh_Free(&entry->head);
    FreeEyes(entry->eyes, entry->eyeCount);
    FreeMouths(entry->mouths, entry->mouthCount);
    AnatomyDeformer_Free(entry->deformer);
    memset(entry, 0, sizeof(*entry));
}

static bool MeshClone(const Mesh* source, Mesh* destination) {
    *destination = Mesh_Create();
    if (!Mesh_ReserveVertices(destination, source->vertexCount) || !Mesh_ReserveIndices(destination, source->indexCount)) {
        Mesh_Free(destination);
        return false;
    }
    memcpy(destination->vertices, source->vertices, source->vertexCount * sizeof(*source->vertices));
    memcpy(destination->indices, source->indices, source->indexCount * sizeof(*source->indices));
    destination->vertexCount = source->vertexCount;
    destination->indexCount = source->indexCount;
    destination->surfaceRecipe = source->surfaceRecipe;
    destination->hasSurface = source->hasSurface;
    destination->geometryGeneration = source->geometryGeneration;
    destination->surfaceGeneration = source->surfaceGeneration;
    return true;
}

static float MeshAxisCoordinate(Vector3 position, unsigned axis) {
    return axis == 0u ? position.x : axis == 1u ? position.y : position.z;
}

static float MeasureMeshBodyRadius(const Mesh* mesh) {
    if (!mesh || mesh->vertexCount < 3u) return 0.0f;
    float minimum[3] = {mesh->vertices[0].position.x, mesh->vertices[0].position.y, mesh->vertices[0].position.z};
    float maximum[3] = {minimum[0], minimum[1], minimum[2]};
    for (size_t i = 1; i < mesh->vertexCount; ++i) {
        Vector3 position = mesh->vertices[i].position;
        float values[3] = {position.x, position.y, position.z};
        for (unsigned axis = 0; axis < 3u; ++axis) {
            minimum[axis] = fminf(minimum[axis], values[axis]);
            maximum[axis] = fmaxf(maximum[axis], values[axis]);
        }
    }
    unsigned longitudinalAxis = 0u;
    for (unsigned axis = 1u; axis < 3u; ++axis) {
        if (maximum[axis] - minimum[axis] > maximum[longitudinalAxis] - minimum[longitudinalAxis]) {
            longitudinalAxis = axis;
        }
    }
    float oneThird = (maximum[longitudinalAxis] - minimum[longitudinalAxis]) / 3.0f;
    float centralStart = minimum[longitudinalAxis] + oneThird;
    float centralEnd = maximum[longitudinalAxis] - oneThird;
    float center[3] = {0.0f, 0.0f, 0.0f};
    size_t centralCount = 0;
    for (size_t i = 0; i < mesh->vertexCount; ++i) {
        Vector3 position = mesh->vertices[i].position;
        float longitudinal = MeshAxisCoordinate(position, longitudinalAxis);
        if (longitudinal < centralStart || longitudinal > centralEnd) continue;
        center[0] += position.x; center[1] += position.y; center[2] += position.z;
        ++centralCount;
    }
    if (centralCount == 0u) return 0.0f;
    for (unsigned axis = 0; axis < 3u; ++axis) center[axis] /= (float)centralCount;

    double radialSum = 0.0;
    for (size_t i = 0; i < mesh->vertexCount; ++i) {
        Vector3 position = mesh->vertices[i].position;
        float longitudinal = MeshAxisCoordinate(position, longitudinalAxis);
        if (longitudinal < centralStart || longitudinal > centralEnd) continue;
        float radialSquared = 0.0f;
        for (unsigned axis = 0; axis < 3u; ++axis) {
            if (axis == longitudinalAxis) continue;
            float delta = MeshAxisCoordinate(position, axis) - center[axis];
            radialSquared += delta * delta;
        }
        radialSum += sqrtf(radialSquared);
    }
    return (float)(radialSum / (double)centralCount);
}

static float MeasureMeshLength(const Mesh* mesh) {
    if (!mesh || !mesh->vertexCount) return 0.0f;
    float minimum[3] = {mesh->vertices[0].position.x, mesh->vertices[0].position.y, mesh->vertices[0].position.z};
    float maximum[3] = {minimum[0], minimum[1], minimum[2]};
    for (size_t i = 1; i < mesh->vertexCount; ++i) {
        Vector3 position = mesh->vertices[i].position;
        minimum[0] = fminf(minimum[0], position.x); maximum[0] = fmaxf(maximum[0], position.x);
        minimum[1] = fminf(minimum[1], position.y); maximum[1] = fmaxf(maximum[1], position.y);
        minimum[2] = fminf(minimum[2], position.z); maximum[2] = fmaxf(maximum[2], position.z);
    }
    return fmaxf(maximum[0] - minimum[0], fmaxf(maximum[1] - minimum[1], maximum[2] - minimum[2]));
}

static bool KeepLargestWithoutAnatomyLoss(Mesh* mesh) {
    if (!mesh->vertexCount || !mesh->indexCount) return true;
    size_t components = 0, largest = 0, second = 0;
    if (!Mesh_ComponentStatistics(mesh, &components, &largest, &second)) return false;
    if (components < 2u || second == 0u) return true;
    Mesh original;
    if (!MeshClone(mesh, &original)) return false;
    if (!Mesh_KeepLargestComponent(mesh)) {
        Mesh_Free(&original);
        return false;
    }
    if (mesh->vertexCount < original.vertexCount) {
        Mesh_Free(mesh);
        *mesh = original;
    } else {
        Mesh_Free(&original);
    }
    (void)largest;
    return true;
}

static void ApplySurface(Mesh* mesh, const SurfacePhenotype* surface, float unitScale) {
    mesh->surfaceRecipe = SurfaceRecipe_CompileScaled(surface, unitScale);
    mesh->surfaceRecipe.data[11][2] = unitScale;
    mesh->hasSurface = true;
    Mesh_MarkSurfaceChanged(mesh);
}

/* Hornea el albedo real de la superficie (capas de pigmento, dorsalDarkening, ventralColor,
 * oscurecimiento de raíz de pelaje y máscara de piel vs cuernos/pezuñas) en los vértices FAR
 * para que las instancias GPU coincidan cromáticamente con NEAR/MID sin coste por fotograma. */
static void BakeFarVertexSurface(Mesh* mesh, const SurfacePhenotype* surface) {
    if (!mesh || !surface || !mesh->vertexCount) return;
    float furShade = surface->integument.type == INTEGUMENT_FUR
                         ? fmaxf(0.45f, 1.0f - surface->integument.fur.rootDarkening * 0.35f)
                         : 1.0f;
    for (size_t i = 0; i < mesh->vertexCount; ++i) {
        MeshVertex* v = &mesh->vertices[i];
        float skinMask = (v->material == SDF_MATERIAL_SKIN) ? 1.0f : 0.0f;
        Vector3 baseRgb = Vec3_Create((float)v->color.r / 255.0f,
                                      (float)v->color.g / 255.0f,
                                      (float)v->color.b / 255.0f);
        Vector3 albedo = baseRgb;
        if (skinMask > 0.0f) {
            float integMask = fmaxf(0.0f, fminf(1.0f, v->surface.integumentMask));
            float localShade = 1.0f - (1.0f - furShade) * integMask;
            Vector3 pig = Vec3_Scale(Surface_EvaluatePigmentAlbedo(surface, &v->surface), localShade);
            albedo = Vec3_Lerp(baseRgb, pig, skinMask);
        }
        float r = fmaxf(0.0f, fminf(1.0f, albedo.x));
        float g = fmaxf(0.0f, fminf(1.0f, albedo.y));
        float b = fmaxf(0.0f, fminf(1.0f, albedo.z));
        v->color.r = (unsigned char)lroundf(r * 255.0f);
        v->color.g = (unsigned char)lroundf(g * 255.0f);
        v->color.b = (unsigned char)lroundf(b * 255.0f);
        /* Canal alfa almacena skinMask para que el tinte individual no manche cuernos ni pezuñas. */
        v->color.a = (unsigned char)lroundf(skinMask * 255.0f);
    }
    Mesh_MarkGeometryChanged(mesh);
}

static bool BuildCanonicalMonster(const EcosystemVisualCacheKey key, Monster* monster) {
    const CreatureRecipe* base = CreatureRecipes_Find(key.recipeId);
    const CreaturePhenotype* stage = base ? CreatureRecipe_GetStage(base, (CreatureStage)key.stage) : NULL;
    if (!base || !stage) return false;
    CreatureRecipe recipe;
    CreaturePhenotype phenotype;
    uint32_t seed = (uint32_t)EcosystemVisual_CacheKeyHash(key);
    if (!VisualVariation_ApplyBucket(base, stage, key.morphologyBucket, seed, &recipe, &phenotype)) return false;
    CreatureVisualProfile profile = VisualVariation_Profile(NULL, seed);
    SurfacePhenotype surface;
    if (!VisualVariation_Surface(&recipe, &phenotype, &profile, key.stage == CREATURE_STAGE_ADULT ? 1.0f : .65f, &surface)) return false;
    phenotype.surface = surface;
    *monster = Monster_Create();
    if (!Creature_BuildMonster(monster, &recipe, &phenotype)) {
        Monster_Free(monster);
        return false;
    }
    for (size_t i = 0; i < monster->mouthCount; ++i) monster->mouths[i].openFactor = 0;
    monster->head.anatomy.oralSystem.openFactor = 0;
    return true;
}

static SDFMesherConfig MeshConfig(VisualTier tier) {
    SDFMesherConfig config = SDFMesher_DefaultConfig();
    config.useAutoBounds = true;
    config.samplingThreadCount = 1;
    config.maxResolution = tier == VISUAL_TIER_NEAR ? 320 : (tier == VISUAL_TIER_MID ? 160 : 96);
    config.maxCells = tier == VISUAL_TIER_NEAR ? 1800000u : (tier == VISUAL_TIER_MID ? 500000u : 180000u);
    config.voxelSize = tier == VISUAL_TIER_FAR ? .24f : tier == VISUAL_TIER_MID ? .18f : .12f;
    return config;
}

static size_t TailDetailRegions(const MonsterSDF* sdf, const AnatomyGraph* graph,
    SDFDetailRegion* regions, size_t count, size_t capacity) {
    for (size_t i = 0; i < sdf->connectorCount && count < capacity; ++i) {
        const MonsterSDFConnector* c = &sdf->connectors[i];
        const AnatomyNode* a = AnatomyGraph_FindNode(graph, c->fromId);
        const AnatomyNode* b = AnatomyGraph_FindNode(graph, c->toId);
        if (!a || !b || a->region != ANATOMY_REGION_TAIL || b->region != ANATOMY_REGION_TAIL) continue;
        float radius = fminf(fminf(a->widthRadius, a->heightRadius), fminf(b->widthRadius, b->heightRadius));
        regions[count++] = (SDFDetailRegion){.bounds = c->bounds, .targetVoxelSize = fmaxf(.012f, radius * .7f)};
    }
    return count;
}

/* Tres muestras de separación normal detectan cavidades grandes de la
 * geometría de reposo. Este valor afecta sólo al transporte indirecto. */
static void BakeBodyOcclusion(Mesh *mesh, const MonsterSDF *sdf, float unit) {
    const float radii[] = {.14f, .36f, .68f};
    for (size_t i = 0; i < mesh->vertexCount; ++i) {
        MeshVertex *v = &mesh->vertices[i];
        float occlusion = 0.0f;
        for (unsigned k = 0; k < 3; ++k) {
            float radius = radii[k] * unit;
            Vector3 p = Vec3_Add(v->position, Vec3_Scale(v->normal, radius));
            float distance = MonsterSDF_EvaluateDistance(sdf, p);
            float deficit = (radius - distance) / radius;
            occlusion += fminf(1.0f, fmaxf(0.0f, (deficit - .23f) * 1.25f));
        }
        v->macroOcclusion = occlusion * (.37f / 3.0f);
    }
}

static bool BuildMainMeshes(CacheEntry* entry, const EcosystemVisualCacheKey key, Monster* monster, MonsterSDF* sdf) {
    SDFMesherConfig config = MeshConfig(key.tier);
    SDFMesher mesher = SDFMesher_Create(config);
    Mesh body = Mesh_Create();
    bool ok = false;
    SDFDetailRegion regions[MONSTER_SDF_DETAIL_REGION_CAPACITY];
    size_t count = 0;
    if (key.tier != VISUAL_TIER_FAR) {
        count = MonsterSDF_GetDetailRegions(sdf, key.tier == VISUAL_TIER_NEAR ? 1.8f : 1.2f, regions, MONSTER_SDF_DETAIL_REGION_CAPACITY);
        count = TailDetailRegions(sdf, &monster->anatomyGraph, regions, count, MONSTER_SDF_DETAIL_REGION_CAPACITY);
    } else {
        /* En FAR conservamos geometría de pezuñas/queratina con detalle local acotado (sin subdividir todo el cuerpo). */
        for (size_t i = 0; i < sdf->connectorCount && count < MONSTER_SDF_DETAIL_REGION_CAPACITY; ++i) {
            const MonsterSDFConnector* c = &sdf->connectors[i];
            if (c->material != SDF_MATERIAL_HOOF) continue;
            float r = fmaxf(c->r1, c->r2);
            regions[count++] = (SDFDetailRegion){.bounds = c->bounds, .targetVoxelSize = fmaxf(0.075f, r * 0.85f)};
        }
    }
    if (key.tier == VISUAL_TIER_NEAR) {
        for (size_t m = 0; m < sdf->mouthCount && count + 5 <= MONSTER_SDF_DETAIL_REGION_CAPACITY; ++m) {
            const MonsterSDFMouth* head = &sdf->mouths[m];
            float unit = monster->phenotype.axial.totalScale;
            regions[count++] = (SDFDetailRegion){.bounds = head->headBounds, .targetVoxelSize = .06f * unit};
            Vector3 points[] = {head->leftNostrilCenterLocal, head->rightNostrilCenterLocal, head->leftOrbitCenterLocal, head->rightOrbitCenterLocal};
            for (unsigned feature = 0; feature < 4; ++feature) {
                Vector3 center = Vec3_Add(head->center, points[feature]);
                Vector3 radii = Vec3_Scale(feature < 2 ? head->nostrilRadii : head->orbitRadii, 2);
                regions[count++] = (SDFDetailRegion){.bounds = AABB_FromMinMax(Vec3_Sub(center, radii), Vec3_Add(center, radii)), .targetVoxelSize = .025f * unit};
            }
        }
    }
    mesher.config.adaptiveDetail = count > 0;
    {
        SDFField field = MonsterSDF_GetField(sdf);
        ok = count ? SDFMesher_GenerateMeshDetailed(&mesher, &field, regions, count, &body)
                   : SDFMesher_GenerateMesh(&mesher, &field, &body);
    }
    if (ok) ok = KeepLargestWithoutAnatomyLoss(&body);
    SDFMesher_Free(&mesher);
    if (!ok) {
        Mesh_Free(&body);
        return false;
    }
    entry->body = body;
    entry->head = Mesh_Create();
    if (!ok) return false;
    SurfaceMapper_MapMesh(&entry->body, &monster->anatomyGraph, &monster->surfaceMapping);
    ApplySurface(&entry->body, &monster->surface, monster->surfaceMapping.unitScale);
    BakeBodyOcclusion(&entry->body, sdf, fmaxf(.05f, monster->surfaceMapping.unitScale));
    if (key.tier == VISUAL_TIER_FAR) {
        const CreatureRecipe* base = CreatureRecipes_Find(key.recipeId);
        const CreaturePhenotype* stage = base ? CreatureRecipe_GetStage(base, (CreatureStage)key.stage) : NULL;
        const SurfacePhenotype* bakeSurface = stage ? &stage->surface : &monster->surface;
        BakeFarVertexSurface(&entry->body, bakeSurface);
    }
    return true;
}

static bool BuildEyesAndMouths(CacheEntry* entry, Monster* monster, MonsterSDF* sdf) {
    if (monster->eyeCount) {
        entry->eyes = calloc(monster->eyeCount, sizeof(*entry->eyes));
        if (!entry->eyes) return false;
        entry->eyeCount = monster->eyeCount;
        for (size_t i = 0; i < entry->eyeCount; ++i) entry->eyes[i].globe = Mesh_Create();
        if (!MonsterVisual_UpdateEyes(entry->eyes, entry->eyeCount, monster)) return false;
    }
    if (monster->mouthCount) {
        entry->mouths = calloc(monster->mouthCount, sizeof(*entry->mouths));
        if (!entry->mouths) return false;
        entry->mouthCount = monster->mouthCount;
        SDFMesherConfig jawConfig = SDFMesher_DefaultConfig();
        jawConfig.voxelSize = .06f;
        jawConfig.maxCells = 100000u;
        jawConfig.useAutoBounds = true;
        SDFMesher jaw = SDFMesher_Create(jawConfig);
        SDFMesher seam = SDFMesher_Create(jawConfig);
        bool ok = true;
        for (size_t i = 0; i < entry->mouthCount; ++i) {
            if (!MonsterVisual_BuildMouthMeshesFromSDFWithMeshers(&entry->mouths[i], &monster->mouths[i], monster,
                    sdf, i, &jaw, &seam)) ok = false;
            ApplySurface(&entry->mouths[i].jaw, &monster->surface, monster->surfaceMapping.unitScale);
            ApplySurface(&entry->mouths[i].hinge, &monster->surface, monster->surfaceMapping.unitScale);
        }
        SDFMesher_Free(&jaw);
        SDFMesher_Free(&seam);
        if (!ok) return false;
    }
    return true;
}

static bool BuildDeformer(CacheEntry* entry, Monster* monster, MonsterSDF* sdf) {
    if (monster->animation) entry->rig = monster->animation->rig;
    else if (!monster->anatomyGraph.nodeCount || !RigBuilder_FromAnatomy(&monster->anatomyGraph,
            monster->anatomyGraph.nodes[0].id, &entry->rig)) return false;
    if (!SkeletonPose_Init(&entry->restPose, &entry->rig.skeleton)) return false;
    entry->deformer = AnatomyDeformer_Create();
    if (!entry->deformer) return false;
    bool ok = AnatomyDeformer_BindSkeleton(entry->deformer, &entry->body, &monster->anatomyGraph, &entry->rig.skeleton) &&
        AnatomyDeformer_BindCranium(entry->deformer, &entry->rig.skeleton, entry->rig.headJoint,
            entry->rig.cranialAttachment, entry->rig.cranialAxis, entry->rig.cranialCollar, sdf);
    if (!ok) return false;
    for (size_t j = 0; j < SKELETON_MAX_JOINTS; ++j) entry->jointBounds[j] = AABB_Empty();
    for (size_t v = 0; v < entry->deformer->vertexCount; ++v) {
        const AnatomySkinBinding* binding = &entry->deformer->skinBindings[v];
        for (unsigned k = 0; k < 2 * ANATOMY_MORPH_INFLUENCES; ++k) if (binding->weights[k] > 0) {
            int j = binding->joints[k];
            if (j >= 0 && (size_t)j < entry->rig.skeleton.jointCount)
                AABB_ExpandPoint(&entry->jointBounds[j], entry->deformer->basePositions[v]);
        }
    }
    entry->trunkRadius = 0.0f;
    for (size_t node = 0; node < entry->deformer->refGraph.nodeCount; ++node) {
        if (entry->deformer->refGraph.nodes[node].region == ANATOMY_REGION_TRUNK) {
            entry->trunkRadius = fmaxf(entry->trunkRadius, entry->deformer->refGraph.nodes[node].widthRadius);
        }
    }
    return true;
}

static bool BuildCacheEntry(CacheEntry* entry) {
    Monster monster;
    memset(&monster, 0, sizeof(monster));
    MonsterSDF sdf = MonsterSDF_Create();
    bool ok = BuildCanonicalMonster(entry->key, &monster) &&
        MonsterSDF_Build(&sdf, &monster, MonsterSDF_DefaultConfig()) &&
        BuildMainMeshes(entry, entry->key, &monster, &sdf) &&
        BuildEyesAndMouths(entry, &monster, &sdf) && BuildDeformer(entry, &monster, &sdf);
    if (ok) {
        entry->meshRadius = MeasureMeshBodyRadius(&entry->body);
        entry->meshLength = MeasureMeshLength(&entry->body);
        entry->calibrationLength = EcosystemAnatomy_ModelLength(&monster.phenotype);
        entry->axialTotalScale = monster.phenotype.axial.totalScale;
        entry->approxBytes = sizeof(*entry) +
            (entry->body.vertexCount + entry->head.vertexCount) * sizeof(MeshVertex) +
            (entry->body.indexCount + entry->head.indexCount) * sizeof(MeshIndex) +
            (entry->deformer ? entry->deformer->vertexCount * (sizeof(AnatomySkinBinding) + sizeof(Vector3) * 2) : 0);
        ok = entry->meshRadius > 1e-4f && entry->meshLength > 1e-4f && entry->calibrationLength > 1e-4f &&
            isfinite(entry->meshRadius) && isfinite(entry->meshLength) && isfinite(entry->calibrationLength);
    }
    MonsterSDF_Free(&sdf);
    Monster_Free(&monster);
    return ok;
}

static void* WorkerMain(void* opaque) {
    VisualState* state = opaque;
    for (;;) {
        pthread_mutex_lock(&state->mutex);
        while (!state->stop && state->queueCount == 0) pthread_cond_wait(&state->condition, &state->mutex);
        if (state->stop) {
            pthread_mutex_unlock(&state->mutex);
            return NULL;
        }
        EcosystemVisualCacheKey key = state->queue[state->queueHead];
        state->queueHead = (state->queueHead + 1u) % ECOSYSTEM_VISUAL_QUEUE_CAPACITY;
        --state->queueCount;
        size_t index = CacheFindLocked(state, key);
        if (index == ECOSYSTEM_VISUAL_CACHE_CAPACITY) {
            pthread_mutex_unlock(&state->mutex);
            continue;
        }
        state->cache[index].state = CACHE_BUILDING;
        pthread_mutex_unlock(&state->mutex);

        double tGen = EvolutionProfiler_Begin(state->profiler);
        bool ok = BuildCacheEntry(&state->cache[index]);
        EvolutionProfiler_End(state->profiler, EVO_PROF_MESH_GEN, tGen);

        pthread_mutex_lock(&state->mutex);
        state->cache[index].state = ok ? CACHE_PENDING : CACHE_FAILED;
        if (ok) {
            ++state->buildCount;
            state->totalCacheBytes += state->cache[index].approxBytes;
            if (state->profiler) EvolutionProfiler_AddAtomic(&state->profiler->counters.generatedMeshes, 1u);
        }
        pthread_mutex_unlock(&state->mutex);
    }
}

/* ========================================================================== */
/* Ranuras ligeras, construcción perezosa de Monster y calibración            */
/* ========================================================================== */

static uint32_t StageForEntity(const EcosystemEntity* entity) {
    if (entity->anatomicalBody) return CREATURE_STAGE_ADULT;
    return entity->physiology.stage == LIFE_JUVENILE ? CREATURE_STAGE_JUVENILE : CREATURE_STAGE_ADULT;
}

static void ReleaseSlotMonster(VisualState* state, VisualSlot* slot) {
    if (!slot || !slot->monsterReady) return;
    if (slot->monster) {
        Monster_Free(slot->monster);
        free(slot->monster);
        slot->monster = NULL;
    }
    free(slot->previousPose);
    slot->previousPose = NULL;
    slot->monsterReady = false;
    slot->lastContactScale = 0;
    slot->anatomyGeneration = 0u;
    if (state->monsterInstances > 0u) --state->monsterInstances;
}

static bool EnsureSlotMonster(VisualState* state, VisualSlot* slot) {
    if (slot->monsterReady && slot->monster) return true;
    const CreatureRecipe* base = CreatureRecipes_Find(slot->recipeId);
    const CreaturePhenotype* stage = base ? CreatureRecipe_GetStage(base, (CreatureStage)slot->stage) : NULL;
    if (!base || !stage) return false;
    CreatureRecipe recipe;
    CreaturePhenotype phenotype;
    if (!VisualVariation_ApplyBucket(base, stage, slot->bucket, slot->profile.pigmentSeed, &recipe, &phenotype))
        return false;
    phenotype.surface = slot->surface;
    Monster* m = calloc(1, sizeof(*m));
    SkeletonPose* prevPose = calloc(1, sizeof(*prevPose));
    if (!m || !prevPose) { free(m); free(prevPose); return false; }
    *m = Monster_Create();
    if (!Creature_BuildMonster(m, &recipe, &phenotype)) {
        Monster_Free(m); free(m); free(prevPose);
        return false;
    }
    slot->monster = m;
    slot->previousPose = prevPose;
    slot->monsterReady = true;
    slot->anatomyGeneration = 0u;
    if (m->animation) {
        *prevPose = m->animation->pose;
        slot->supportCenter = m->animation->animator.locomotion.supportCenter;
    }
    ++state->monsterInstances;
    if (state->profiler) EvolutionProfiler_AddAtomic(&state->profiler->counters.monsterConstructions, 1u);
    return true;
}

static void TeardownSlot(VisualState* state, VisualSlot* slot, size_t visIdx) {
    if (!slot->used) return;
    slotMapRemove(&state->slotMap, slot->entityId);
    ReleaseSlotMonster(state, slot);
    memset(slot, 0, sizeof(*slot));
    if (state->slotCount) --state->slotCount;
    if (state->freeSlotCount < state->slotCapacity) {
        state->freeSlots[state->freeSlotCount++] = (uint32_t)visIdx;
    }
}

static void RefreshSlotCalibration(VisualState* state, VisualSlot* slot) {
    if (slot->monsterReady && slot->monster) {
        slot->calibrationLength = EcosystemAnatomy_ModelLength(&slot->monster->phenotype);
    } else if (slot->calibrationLength <= 1e-4f) {
        const EcosystemAnatomyTemplate* tmpl = EcosystemAnatomyCache_Acquire(slot->recipeId, slot->bucket, slot->growthStep, NULL);
        if (tmpl) {
            slot->calibrationLength = tmpl->modelLength;
            slot->supportCenter = tmpl->center;
            slot->axialTotalScale = tmpl->phenotype.axial.totalScale;
            EcosystemAnatomyCache_Release(tmpl);
        }
    }
    EcosystemVisualCacheKey key = {.recipeId = slot->recipeId, .stage = slot->stage,
        .morphologyBucket = slot->bucket, .tier = slot->tier};
    pthread_mutex_lock(&state->mutex);
    size_t index = CacheFindLocked(state, key);
    if (index == ECOSYSTEM_VISUAL_CACHE_CAPACITY || state->cache[index].state != CACHE_READY) {
        for (size_t c = 0; c < ECOSYSTEM_VISUAL_CACHE_CAPACITY; ++c) {
            const CacheEntry* e = &state->cache[c];
            if (e->state == CACHE_READY && e->key.recipeId == slot->recipeId &&
                e->key.stage == slot->stage && e->key.morphologyBucket == slot->bucket) {
                index = c;
                break;
            }
        }
    }
    if (index < ECOSYSTEM_VISUAL_CACHE_CAPACITY && state->cache[index].state == CACHE_READY) {
        slot->meshRadius = state->cache[index].meshRadius;
        slot->meshLength = state->cache[index].meshLength;
        if (slot->calibrationLength <= 1e-4f) slot->calibrationLength = state->cache[index].calibrationLength;
        slot->axialTotalScale = state->cache[index].axialTotalScale;
    }
    pthread_mutex_unlock(&state->mutex);
}

static float CalibratedSlotScale(const VisualSlot* slot) {
    float calibrationFactor = slot->calibrationLength > 1e-4f ? 2.05f / slot->calibrationLength : 1.0f;
    return slot->profile.renderScale * calibrationFactor * fmaxf(0.01f, slot->developmentScale);
}

static Color ComputeSlotTint(const VisualSlot* slot) {
    /* Misma variación tonal individual que VisualVariation_Surface (negro/blanco con peso |tone|),
     * aplicada en el shader instanciado únicamente sobre skinMask (sin teñir cuernos ni pezuñas). */
    float t = isfinite(slot->maturity) ? fmaxf(0.0f, fminf(1.0f, slot->maturity)) : 1.0f;
    float tone = ((float)slot->profile.primary.r - 100.0f) / 400.0f * (0.85f + 0.15f * t);
    unsigned char target = tone < 0.0f ? 0u : 255u;
    float weight = fabsf(tone);
    if (weight > 0.35f) weight = 0.35f;
    return Color_Create(target, target, target, (unsigned char)lroundf(weight * 255.0f));
}

static void InitSlotCommon(VisualState* state, VisualSlot* slot, uint32_t visIdx,
                           AIObjectId entityId, uint32_t slotIndex, uint32_t recipeId,
                           uint32_t stage, uint32_t entitySeed, const CreatureGenome* genome,
                           Vector3 position, float yaw, float speciesRadius,
                           float developmentScale, float maturity, bool eagerMonster) {
    bool wasUsed = slot->used;
    if (wasUsed) {
        slotMapRemove(&state->slotMap, slot->entityId);
        ReleaseSlotMonster(state, slot);
    }
    memset(slot, 0, sizeof(*slot));
    slot->used = true;
    slot->seen = true;
    slot->slotIndex = slotIndex;
    slot->entityId = entityId;
    slot->entitySeed = entitySeed;
    slot->recipeId = recipeId;
    slot->stage = stage;
    slot->growthStep = ECOSYSTEM_ANATOMY_GROWTH_STEPS;
    slot->genome = *genome;
    slot->profile = VisualVariation_Profile(genome, entitySeed);
    slot->bucket = slot->profile.bucket;
    slot->tier = VISUAL_TIER_FAR;
    slot->requestedTier = VISUAL_TIER_FAR;
    slot->projection = (EcosystemVisualProjection){.projectedPixels = 0.0f, .visible = true};
    slot->position = position;
    slot->previousPosition = position;
    slot->yaw = yaw;
    slot->previousYaw = yaw;
    slot->speciesRadius = speciesRadius;
    slot->developmentScale = developmentScale;
    slot->maturity = maturity;
    slot->gaitPhase = (float)(entitySeed % 360u) * 0.0174533f;
    slot->animationClock = (float)(slotIndex % 6u) * 0.013f;

    const CreatureRecipe* base = CreatureRecipes_Find(slot->recipeId);
    const CreaturePhenotype* stagePhen = base ? CreatureRecipe_GetStage(base, (CreatureStage)slot->stage) : NULL;
    if (base && stagePhen) {
        VisualVariation_Surface(base, stagePhen, &slot->profile, slot->maturity, &slot->surface);
        slot->surfaceRecipe = SurfaceRecipe_CompileScaled(&slot->surface, 1.0f);
    }
    slot->tintColor = ComputeSlotTint(slot);
    if (eagerMonster) (void)EnsureSlotMonster(state, slot);
    RefreshSlotCalibration(state, slot);
    slot->scale = CalibratedSlotScale(slot);

    slotMapPut(&state->slotMap, entityId, visIdx);
    if (!wasUsed) ++state->slotCount;
}

static void SetStateStaticWorld(VisualState* state, const WorldStaticData* sw);

static void ReconcileWorld(VisualState* state) {
    for (size_t i = 0; i < state->slotCapacity; ++i) state->slots[i].seen = false;
    if (!state->world || !state->world->entities) {
        for (size_t i = 0; i < state->slotCapacity; ++i) TeardownSlot(state, &state->slots[i], i);
        return;
    }
    if (!EnsureSlotCapacity(state, state->world->capacity)) return;
    bool smallWorld = state->world->count <= 16u;

    for (unsigned k = 0; k < state->world->count; ++k) {
        unsigned index = state->world->activeEntitySlots[k];
        const EcosystemEntity* entity = &state->world->entities[index];
        if (!entity->id) continue;
        VisualSlot* slot = FindSlot(state, entity->id);
        uint32_t recipeId = state->world->species[entity->speciesIndex].recipeId;
        uint32_t stage = StageForEntity(entity);
        uint32_t bucket = VisualVariation_Profile(&entity->genome, entity->seed).bucket;
        bool rebind = !slot || slot->slotIndex != index || slot->recipeId != recipeId ||
            slot->stage != stage || slot->entitySeed != entity->seed || slot->bucket != bucket;
        uint32_t visIdx = 0;
        if (!slot) {
            slot = AllocSlot(state, &visIdx);
        } else {
            visIdx = (uint32_t)(slot - state->slots);
        }
        if (!slot) continue;
        if (rebind) {
            InitSlotCommon(state, slot, visIdx, entity->id, index, recipeId, stage, entity->seed,
                           &entity->genome, entity->body.position, entity->body.yaw,
                           state->world->species[entity->speciesIndex].radius,
                           entity->developmentScale,
                           entity->physiology.stage == LIFE_ADULT ? 1.0f : 0.65f,
                           smallWorld);
        } else {
            slot->seen = true;
            slot->position = entity->body.position;
            slot->yaw = entity->body.yaw;
            slot->speed = entity->body.speed;
            slot->speciesRadius = state->world->species[entity->speciesIndex].radius;
            slot->developmentScale = entity->developmentScale;
            slot->scale = CalibratedSlotScale(slot);
            slot->maturity = entity->physiology.stage == LIFE_ADULT ? 1.0f : 0.65f;
        }
        slot->action = entity->mind.action;
        bool isFlatWorld = (!state->world || !state->world->staticWorld || state->world->staticWorld->isFlat);
        slot->groundNormal = (!isFlatWorld && Vec3_LengthSq(entity->body.groundNormal) > 0.25f)
                                 ? entity->body.groundNormal : Vec3_Create(0.0f, 1.0f, 0.0f);
        if (isFlatWorld) {
            slot->orientation = Quat_FromAxisAngle(Vec3_Create(0.0f, 1.0f, 0.0f), entity->body.yaw);
        } else {
            slot->orientation = (entity->body.orientation.w * entity->body.orientation.w +
                                 entity->body.orientation.x * entity->body.orientation.x +
                                 entity->body.orientation.y * entity->body.orientation.y +
                                 entity->body.orientation.z * entity->body.orientation.z > 0.5f)
                                    ? entity->body.orientation
                                    : World_BuildBodyFrame(entity->body.yaw, slot->groundNormal, 0.60f);
        }
        slot->mouthTarget = entity->body.mouth;
        slot->oralActive = entity->mind.intent.interact || entity->mind.action == ACTION_ATTACK;
        slot->hasAnatomicalBody = (entity->anatomicalBody && entity->anatomicalBody->tmpl);
        if (slot->hasAnatomicalBody) {
            const EcosystemAnatomy* ab = entity->anatomicalBody;
            slot->growthStep = ab->growthStep;
            slot->crouch = ab->crouch;
            slot->supportCenter = ab->center;
            slot->calibrationLength = ab->modelLength;
            slot->scale = CalibratedSlotScale(slot);
            unsigned jc = ab->tmpl->profile.count;
            if (jc > REACH_MAX_JOINTS) jc = REACH_MAX_JOINTS;
            slot->cervicalJointCount = (uint8_t)jc;
            for (unsigned j = 0; j < jc; ++j) {
                slot->cervicalJointIndices[j] = (int16_t)ab->tmpl->profile.joints[j].index;
                slot->cervicalRotations[j] = ab->state.rotation[j];
            }
        }
    }
    for (size_t i = 0; i < state->slotCapacity; ++i) {
        if (state->slots[i].used && !state->slots[i].seen) TeardownSlot(state, &state->slots[i], i);
    }
    SetStateStaticWorld(state, state->world ? state->world->staticWorld : NULL);
}

static void ReconcileSnapshot(VisualState* state, const EvolutionRenderSnapshot* snap) {
    double tRec = EvolutionProfiler_Begin(state->profiler);
    for (size_t i = 0; i < state->slotCapacity; ++i) state->slots[i].seen = false;
    if (!snap) return;
    SetStateStaticWorld(state, snap->staticWorld);
    if (!EnsureSlotCapacity(state, snap->capacity > snap->entityCount ? snap->capacity : snap->entityCount + 16u))
        return;

    for (unsigned i = 0; i < snap->entityCount; ++i) {
        const EvolutionEntitySnapshot* es = &snap->entities[i];
        if (!es->id) continue;
        VisualSlot* slot = FindSlot(state, es->id);
        uint32_t stage = es->hasAnatomicalBody ? CREATURE_STAGE_ADULT
                         : (es->lifeStage == LIFE_JUVENILE ? CREATURE_STAGE_JUVENILE : CREATURE_STAGE_ADULT);
        bool rebind = !slot || slot->slotIndex != es->slotIndex || slot->recipeId != es->recipeId ||
            slot->stage != stage || slot->entitySeed != es->entitySeed || slot->bucket != es->morphologyBucket;
        uint32_t visIdx = 0;
        if (!slot) {
            slot = AllocSlot(state, &visIdx);
        } else {
            visIdx = (uint32_t)(slot - state->slots);
        }
        if (!slot) continue;
        if (rebind) {
            InitSlotCommon(state, slot, visIdx, es->id, es->slotIndex, es->recipeId, stage,
                           es->entitySeed, &es->genome, es->position, es->yaw,
                           es->speciesRadius, es->developmentScale, es->maturity, false);
        } else {
            slot->seen = true;
            slot->position = es->position;
            slot->yaw = es->yaw;
            slot->speed = es->speed;
            slot->speciesRadius = es->speciesRadius;
            slot->developmentScale = es->developmentScale;
            slot->maturity = es->maturity;
        }
        slot->lifeStage = es->lifeStage;
        slot->action = es->action;
        slot->groundNormal = Vec3_LengthSq(es->groundNormal) > 0.25f
                                 ? es->groundNormal : Vec3_Create(0.0f, 1.0f, 0.0f);
        slot->orientation = (es->orientation.w * es->orientation.w +
                             es->orientation.x * es->orientation.x +
                             es->orientation.y * es->orientation.y +
                             es->orientation.z * es->orientation.z > 0.5f)
                                ? es->orientation
                                : Quat_FromAxisAngle(Vec3_Create(0.0f, 1.0f, 0.0f), es->yaw);
        slot->mouthTarget = es->mouth;
        slot->oralActive = es->oralActive;
        slot->hasAnatomicalBody = es->hasAnatomicalBody;
        slot->growthStep = es->growthStep;
        slot->crouch = es->crouch;
        if (es->hasAnatomicalBody) {
            slot->supportCenter = es->anatomyCenter;
            if (es->modelLength > 1e-4f) slot->calibrationLength = es->modelLength;
        }
        slot->scale = CalibratedSlotScale(slot);
        slot->cervicalJointCount = es->cervicalJointCount;
        if (es->cervicalJointCount > 0u) {
            memcpy(slot->cervicalJointIndices, es->cervicalJointIndices,
                   es->cervicalJointCount * sizeof(int16_t));
            memcpy(slot->cervicalRotations, es->cervicalRotations,
                   es->cervicalJointCount * sizeof(Quaternion));
        }
    }
    for (size_t i = 0; i < state->slotCapacity; ++i) {
        if (state->slots[i].used && !state->slots[i].seen) TeardownSlot(state, &state->slots[i], i);
    }
    EvolutionProfiler_End(state->profiler, EVO_PROF_VIS_RECONCILE, tRec);
}

/* ========================================================================== */
/* Selección de LOD con histéresis y presupuestos estrictos NEAR/MID/FAR      */
/* ========================================================================== */

static void ApplyLod(VisualState* state) {
    size_t visible = 0;
    for (size_t i = 0; i < state->slotCapacity; ++i) {
        if (state->slots[i].used && state->slots[i].projection.visible) ++visible;
    }
    float bias = state->lodBias > 0.1f ? state->lodBias : 1.0f;
    float midUp = 128.0f / bias, midDown = 96.0f / bias;
    float nearUp = 320.0f / bias, nearDown = 256.0f / bias;
    bool allowAutoNear = state->allowAutoNearLargePop || (visible <= 64u);

    for (size_t i = 0; i < state->slotCapacity; ++i) {
        VisualSlot* slot = &state->slots[i];
        if (!slot->used || slot->explicitTier) continue;
        if (!slot->projection.visible) {
            slot->tier = VISUAL_TIER_FAR;
            continue;
        }
        float pixels = slot->projection.projectedPixels;
        if (slot->tier == VISUAL_TIER_FAR && pixels >= midUp) slot->tier = VISUAL_TIER_MID;
        else if (slot->tier == VISUAL_TIER_MID && pixels < midDown) slot->tier = VISUAL_TIER_FAR;
        else if (slot->tier == VISUAL_TIER_MID && pixels >= nearUp && allowAutoNear) slot->tier = VISUAL_TIER_NEAR;
        else if (slot->tier == VISUAL_TIER_NEAR && pixels < nearDown) slot->tier = VISUAL_TIER_MID;
        if (!allowAutoNear && slot->tier == VISUAL_TIER_NEAR) slot->tier = VISUAL_TIER_MID;
    }

    /* Aplicar techo estricto de NEAR automáticos (las selecciones explícitas siempre se respetan). */
    size_t autoNear = 0;
    for (size_t i = 0; i < state->slotCapacity; ++i) {
        if (state->slots[i].used && state->slots[i].projection.visible &&
            !state->slots[i].explicitTier && state->slots[i].tier == VISUAL_TIER_NEAR) ++autoNear;
    }
    while (autoNear > state->maxNear) {
        VisualSlot* candidate = NULL;
        for (size_t i = 0; i < state->slotCapacity; ++i) {
            VisualSlot* slot = &state->slots[i];
            if (!slot->used || slot->explicitTier || !slot->projection.visible || slot->tier != VISUAL_TIER_NEAR) continue;
            if (!candidate || slot->projection.projectedPixels < candidate->projection.projectedPixels) candidate = slot;
        }
        if (!candidate) break;
        candidate->tier = VISUAL_TIER_MID;
        --autoNear;
    }

    /* Aplicar techo estricto de MID (en población >64 limita al presupuesto maxMid). */
    if (visible > 64u || state->snapshotMode) {
        size_t mid = 0;
        for (size_t i = 0; i < state->slotCapacity; ++i) {
            if (state->slots[i].used && state->slots[i].projection.visible &&
                !state->slots[i].explicitTier && state->slots[i].tier == VISUAL_TIER_MID) ++mid;
        }
        while (mid > state->maxMid) {
            VisualSlot* candidate = NULL;
            for (size_t i = 0; i < state->slotCapacity; ++i) {
                VisualSlot* slot = &state->slots[i];
                if (!slot->used || slot->explicitTier || !slot->projection.visible || slot->tier != VISUAL_TIER_MID) continue;
                if (!candidate || slot->projection.projectedPixels < candidate->projection.projectedPixels) candidate = slot;
            }
            if (!candidate) break;
            candidate->tier = VISUAL_TIER_FAR;
            --mid;
        }
    }
}

static void PublishReadyCaches(VisualState* state, unsigned maxPerFrame) {
    double tPub = EvolutionProfiler_Begin(state->profiler);
    unsigned published = 0u;
    pthread_mutex_lock(&state->mutex);
    for (size_t i = 0; i < ECOSYSTEM_VISUAL_CACHE_CAPACITY && published < maxPerFrame; ++i) {
        if (state->cache[i].state == CACHE_PENDING) {
            state->cache[i].state = CACHE_READY;
            ++published;
        }
    }
    pthread_mutex_unlock(&state->mutex);
    EvolutionProfiler_End(state->profiler, EVO_PROF_MESH_PUB, tPub);
}

/* ========================================================================== */
/* Inicialización y ciclo de vida                                             */
/* ========================================================================== */

static bool InitCore(VisualState** outState, size_t initialCap, bool snapshotMode) {
    VisualState* state = calloc(1, sizeof(*state));
    if (!state) return false;
    state->snapshotMode = snapshotMode;
    state->maxNear = 12u;
    state->maxMid = 8u; /* Preserva política heredada para N>64 hasta configurar con SetBudget. */
    state->lodBias = 1.0f;
    state->allowAutoNearLargePop = false;
    state->slotCapacity = initialCap < 256u ? 256u : initialCap;
    state->slots = calloc(state->slotCapacity, sizeof(*state->slots));
    state->freeSlots = malloc(state->slotCapacity * sizeof(*state->freeSlots));
    state->cache = calloc(ECOSYSTEM_VISUAL_CACHE_CAPACITY, sizeof(*state->cache));
    state->queue = calloc(ECOSYSTEM_VISUAL_QUEUE_CAPACITY, sizeof(*state->queue));
    state->fallback = Mesh_Create();
    bool ok = state->slots && state->freeSlots && state->cache && state->queue &&
        slotMapInit(&state->slotMap, state->slotCapacity) &&
        PrimitiveMesh_GenerateUVSphere(&state->fallback, Vec3_Zero(), .5f, 12, 6, COLOR_WHITE) &&
        pthread_mutex_init(&state->mutex, NULL) == 0 && pthread_cond_init(&state->condition, NULL) == 0;
    if (!ok) {
        Mesh_Free(&state->fallback); slotMapFree(&state->slotMap);
        free(state->slots); free(state->freeSlots); free(state->cache); free(state->queue); free(state);
        return false;
    }
    for (size_t i = 0; i < state->slotCapacity; ++i) {
        state->freeSlots[i] = (uint32_t)(state->slotCapacity - 1u - i);
    }
    state->freeSlotCount = state->slotCapacity;
    if (pthread_create(&state->worker, NULL, WorkerMain, state) != 0) {
        pthread_cond_destroy(&state->condition); pthread_mutex_destroy(&state->mutex);
        Mesh_Free(&state->fallback); slotMapFree(&state->slotMap);
        free(state->slots); free(state->freeSlots); free(state->cache); free(state->queue); free(state);
        return false;
    }
    state->workerStarted = true;
    *outState = state;
    return true;
}

bool EcosystemVisual_Init(EcosystemVisual* visual, EcosystemWorld* world) {
    if (!visual || !world) return false;
    memset(visual, 0, sizeof(*visual));
    VisualState* state = NULL;
    if (!InitCore(&state, world->capacity, false)) return false;
    state->world = world;
    state->animationSeconds = world->clock.seconds;
    visual->state = state;
    return EcosystemVisual_Sync(visual);
}

bool EcosystemVisual_InitForSnapshots(EcosystemVisual* visual, unsigned initialCapacity) {
    if (!visual) return false;
    memset(visual, 0, sizeof(*visual));
    VisualState* state = NULL;
    if (!InitCore(&state, initialCapacity < 256u ? 256u : initialCapacity, true)) return false;
    state->maxNear = 12u;
    state->maxMid = 32u;
    state->allowAutoNearLargePop = true;
    visual->state = state;
    return true;
}

bool EcosystemVisual_BindWorld(EcosystemVisual* visual, EcosystemWorld* world) {
    if (!visual || !visual->state || !world) return false;
    VisualState* state = visual->state;
    for (size_t i = 0; i < state->slotCapacity; ++i) TeardownSlot(state, &state->slots[i], i);
    state->world = world;
    state->snapshotMode = false;
    state->hasWorldClock = false;
    state->lastWorldTick = 0;
    state->animationSeconds = world->clock.seconds;
    return EcosystemVisual_Sync(visual);
}

void EcosystemVisual_SetBudget(EcosystemVisual* visual, unsigned maxNear, unsigned maxMid, float lodBias) {
    if (!visual || !visual->state) return;
    VisualState* state = visual->state;
    state->maxNear = maxNear;
    state->maxMid = maxMid;
    state->lodBias = (isfinite(lodBias) && lodBias > 0.1f) ? lodBias : 1.0f;
    state->allowAutoNearLargePop = (maxNear > 0u);
}

void EcosystemVisual_SetProfiler(EcosystemVisual* visual, EvolutionProfiler* profiler) {
    if (!visual || !visual->state) return;
    ((VisualState*)visual->state)->profiler = profiler;
}

bool EcosystemVisual_Sync(EcosystemVisual* visual) {
    if (!visual || !visual->state) return false;
    VisualState* state = visual->state;
    if (state->world && state->hasWorldClock && state->world->clock.tick < state->lastWorldTick) {
        for (size_t i = 0; i < state->slotCapacity; ++i) TeardownSlot(state, &state->slots[i], i);
    }
    ReconcileWorld(state);
    if (state->world) {
        state->lastWorldTick = state->world->clock.tick;
        state->hasWorldClock = true;
    }
    return true;
}

bool EcosystemVisual_SyncSnapshot(EcosystemVisual* visual, const EvolutionRenderSnapshot* snapshot) {
    if (!visual || !visual->state || !snapshot) return false;
    VisualState* state = visual->state;
    if (state->hasWorldClock && snapshot->tick < state->lastWorldTick) {
        for (size_t i = 0; i < state->slotCapacity; ++i) TeardownSlot(state, &state->slots[i], i);
    }
    ReconcileSnapshot(state, snapshot);
    state->lastWorldTick = snapshot->tick;
    state->hasWorldClock = true;
    return true;
}

/* ========================================================================== */
/* Animación con presupuesto acotado y LOD por distancia                      */
/* ========================================================================== */

static Quaternion SlotOrientation(const VisualSlot* slot) {
    Quaternion q = slot->orientation;
    if (q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w > 0.5f) return q;
    return Quat_FromAxisAngle(Vec3_Create(0.0f, 1.0f, 0.0f), slot->yaw);
}

static void SetStateStaticWorld(VisualState* state, const WorldStaticData* sw) {
    if (!state || state->staticWorld == sw) return;
    const WorldStaticData* prev = state->staticWorld;
    state->staticWorld = WorldStaticData_Retain(sw);
    WorldStaticData_Release(prev);
}

static Vector3 RotateYaw(Vector3 v, float yaw) {
    float c = cosf(yaw), s = sinf(yaw);
    return Vec3_Create(c * v.x + s * v.z, v.y, -s * v.x + c * v.z);
}

static Vector3 SlotCenter(const VisualSlot* slot) {
    if (slot->monsterReady && slot->monster && slot->monster->animation) {
        return slot->monster->animation->animator.locomotion.supportCenter;
    }
    return slot->supportCenter;
}

static void ApplySnapshotAxialPose(const Rig* rig, SkeletonPose* pose, void* context) {
    const VisualSlot* slot = context;
    if (!slot || !pose) return;
    for (unsigned k = 0; k < slot->cervicalJointCount; ++k) {
        int j = slot->cervicalJointIndices[k];
        if (j >= 0 && (size_t)j < pose->jointCount) {
            pose->joints[j].localRotation = slot->cervicalRotations[k];
        }
    }
    if (rig) {
        for (size_t k = 0; k < rig->spineCount; ++k) {
            int j = rig->spine[k];
            bool inChain = false;
            for (unsigned c = 0; c < slot->cervicalJointCount; ++c) {
                if (slot->cervicalJointIndices[c] == j) { inChain = true; break; }
            }
            if (!inChain && j >= 0 && (size_t)j < pose->jointCount) {
                pose->joints[j].localRotation = rig->skeleton.joints[j].restRotation;
            }
        }
    }
}

static void AnimateSlotDetailed(VisualState* state, VisualSlot* slot, float deltaSeconds,
                                const EcosystemAnatomyTemplate* sharedTmpl,
                                unsigned anatomyGen) {
    if (!EnsureSlotMonster(state, slot)) return;
    if (!slot->monster || !slot->monster->animation || deltaSeconds <= 0.0f) return;

    Monster* monster = slot->monster;
    MonsterAnimation* animation = monster->animation;

    if (sharedTmpl && slot->anatomyGeneration != anatomyGen) {
        monster->anatomyGraph = sharedTmpl->graph;
        monster->phenotype = sharedTmpl->phenotype;
        GaitProfile gait = monster->animation->animator.gait;
        if (!slot->anatomyGeneration) {
            MonsterAnimation_Free(monster->animation);
            monster->animation = NULL;
            gait = monster->recipe.gait == CREATURE_GAIT_FELINE_STALK ? GaitPreset_FelineStalk(&sharedTmpl->rig) :
                   monster->recipe.gait == CREATURE_GAIT_BIPEDAL_HOP ? GaitPreset_BipedalHop(&sharedTmpl->rig) :
                   monster->recipe.gait == CREATURE_GAIT_AVIAN_FLIGHT ? GaitPreset_AvianFlight(&sharedTmpl->rig) :
                   GaitPreset_BovineWalk(&sharedTmpl->rig);
        }
        MonsterAnimation_Configure(monster, &sharedTmpl->rig, gait);
        slot->anatomyGeneration = anatomyGen ? anatomyGen : 1u;
        slot->maturity = sharedTmpl->phenotype.development.colorMaturity;
        VisualVariation_Surface(&monster->recipe, &sharedTmpl->phenotype, &slot->profile, slot->maturity, &slot->surface);
        slot->surfaceRecipe = SurfaceRecipe_CompileScaled(&slot->surface, sharedTmpl->phenotype.axial.totalScale);
        slot->tintColor = ComputeSlotTint(slot);
        animation = monster->animation;
        slot->calibrationLength = sharedTmpl->modelLength;
        slot->supportCenter = sharedTmpl->center;
        slot->scale = CalibratedSlotScale(slot);
    }

    ProceduralAnimator* a = &animation->animator;
    a->applyAxialPose = slot->hasAnatomicalBody ? ApplySnapshotAxialPose : NULL;
    a->axialContext = slot->hasAnatomicalBody ? slot : NULL;
    const Rig* rig = &animation->rig;
    float scale = fmaxf(.001f, slot->scale);

    Quaternion orient = SlotOrientation(slot);
    Vector3 displacement = World_WorldDirToLocal(Vec3_Sub(slot->position, slot->previousPosition), orient);
    Vector3 velocity = Vec3_Scale(displacement, 1.0f / (scale * deltaSeconds));
    velocity.y = 0;
    /* Acotar velocidad visual en saltos de velocidad acelerada (16x/64x). */
    float maxVisualSpeed = 6.0f / fmaxf(0.15f, scale);
    float velLen = Vec3_Length(velocity);
    if (velLen > maxVisualSpeed) velocity = Vec3_Scale(velocity, maxVisualSpeed / velLen);

    float yawDelta = slot->yaw - slot->previousYaw;
    yawDelta = atan2f(sinf(yawDelta), cosf(yawDelta));
    bool terrainContacts = state->staticWorld && !state->staticWorld->isFlat;
    if (!terrainContacts) Locomotion_TurnFrame(&a->locomotion, yawDelta);
    a->angularVelocity = fmaxf(-5.0f, fminf(5.0f, yawDelta / deltaSeconds));
    a->desiredVelocity = velocity;
    slot->previousPosition = slot->position;
    slot->previousYaw = slot->yaw;
    slot->actionClock += deltaSeconds;

    bool agile = monster->recipe.gait == CREATURE_GAIT_FELINE_STALK;
    float running = (slot->action == ACTION_CHASE || slot->action == ACTION_ESCAPE || slot->action == ACTION_ATTACK) ? 1.0f : 0.0f;
    slot->runBlend += (running - slot->runBlend) * (1.0f - expf(-7.0f * deltaSeconds));
    slot->restBlend += ((slot->action == ACTION_REST ? 1.0f : 0.0f) - slot->restBlend) * (1.0f - expf(-3.0f * deltaSeconds));

    float contactSpeed = Vec3_Length(velocity);
    const SkeletonPose* rest = sharedTmpl ? &sharedTmpl->rest : NULL;
    SkeletonPose localRest;
    if (!rest) { SkeletonPose_Init(&localRest, &rig->skeleton); rest = &localRest; }
    for (size_t leg = 0; leg < rig->limbCount; ++leg) {
        Vector3 rel = Vec3_Sub(rest->joints[rig->limbs[leg].endEffectorJoint].worldPosition, a->locomotion.supportCenter);
        Vector3 footVelocity = Vec3_Add(velocity, Vec3_Create(a->angularVelocity * rel.z, 0, -a->angularVelocity * rel.x));
        contactSpeed = fmaxf(contactSpeed, Vec3_Length(footVelocity));
    }
    float forwardFraction = fminf(1.0f, Vec3_Length(velocity) / fmaxf(.01f, contactSpeed));
    a->gait = GaitPreset_QuadrupedMotion(rig, agile, contactSpeed, slot->runBlend * forwardFraction);
    if (slot->hasAnatomicalBody) a->gait.supportCompression += slot->crouch;
    for (size_t i = 0; i < rig->limbCount; ++i) a->locomotion.feet[i].phaseOffset = a->gait.limbPhase[i];
    a->oralReachActive = slot->oralActive && !slot->hasAnatomicalBody;
    a->oralReachTarget = World_WorldToLocal(slot->mouthTarget, SlotCenter(slot), scale, orient, slot->position);
    a->interactionTarget = (InteractionTarget){
        .valid = slot->oralActive, .position = a->oralReachTarget,
        .normal = {0, 1, 0}, .approach = {0, 0, 1}, .positionTolerance = .03f / scale, .orientationTolerance = 1.57f
    };

    float jaw = 0;
    if (slot->action == ACTION_ATTACK) {
        float snap = powf(fmaxf(0, sinf(slot->actionClock * 11)), 3);
        jaw = .24f + .66f * snap;
    } else if (slot->action == ACTION_EAT) {
        jaw = .26f * (.30f + .70f * (.5f + .5f * sinf(slot->actionClock * 9)));
    } else if (slot->action == ACTION_DRINK) {
        jaw = .14f * (.25f + .75f * (.5f + .5f * sinf(slot->actionClock * 12)));
    }
    a->mouthOpen += (jaw - a->mouthOpen) * (1.0f - expf(-12.0f * deltaSeconds));
    if (slot->previousPose) *slot->previousPose = animation->pose;

    /* Los puntos de apoyo se conservan al transformar el marco completo.
     * El cuerpo ya se ha desplazado en la simulación. La locomoción visual
     * sigue usando velocidad y fase para el paso, sin integrar otro traslado. */
    TerrainAnimationAdapter terrainAdapter;
    World* prevWorld = monster->world;
    if (terrainContacts) {
        TerrainAnimationAdapter_Init(&terrainAdapter, state->staticWorld,
                                     slot->position, orient, SlotCenter(slot), scale);
        monster->world = TerrainAnimationAdapter_AsWorld(&terrainAdapter);
        float displacementWorld = Vec3_Distance(slot->lastContactOrigin, slot->position);
        if (slot->lastContactScale <= 0.0f || displacementWorld > fmaxf(.45f, a->gait.strideLength * scale)) {
            Locomotion_Init(&a->locomotion, rig, &a->gait, monster->world, a->locomotion.bodyPosition);
        } else {
            TerrainAnimationAdapter previous;
            TerrainAnimationAdapter_Init(&previous, state->staticWorld, slot->lastContactOrigin,
                                         slot->lastContactOrientation, slot->lastContactCenter, slot->lastContactScale);
            TerrainAnimationAdapter_RebaseFeet(&a->locomotion, rig, &previous, &terrainAdapter);
        }
        a->locomotion.externalBodyFrame = true;
        slot->lastContactOrigin = terrainAdapter.worldOrigin;
        slot->lastContactCenter = terrainAdapter.modelCenter;
        slot->lastContactScale = terrainAdapter.scale;
        slot->lastContactOrientation = terrainAdapter.bodyOrientation;
    }
    /* Acotar subpasos de animación de presentación (máx 2 subpasos por frame incluso a 64x). */
    float remaining = fminf(deltaSeconds, 1.0f / 30.0f);
    while (remaining > 1e-6f) {
        float dt = fminf(remaining, 1.0f / 60.0f);
        Vector3 before = a->locomotion.bodyPosition;
        MonsterAnimation_Update(monster, dt);
        Vector3 travel = Vec3_Sub(a->locomotion.bodyPosition, before);
        travel.y = 0;
        a->locomotion.bodyPosition = Vec3_Sub(a->locomotion.bodyPosition, travel);
        for (size_t i = 0; i < rig->limbCount; ++i) {
            FootState* f = &a->locomotion.feet[i];
            f->plantedPosition = Vec3_Sub(f->plantedPosition, travel);
            f->targetPosition = Vec3_Sub(f->targetPosition, travel);
            f->swingStart = Vec3_Sub(f->swingStart, travel);
            f->swingTarget = Vec3_Sub(f->swingTarget, travel);
        }
        for (size_t j = 0; j < rig->skeleton.jointCount; ++j)
            if (rig->skeleton.joints[j].parentIndex < 0)
                animation->pose.joints[j].localPosition = Vec3_Sub(animation->pose.joints[j].localPosition, travel);
        remaining -= dt;
    }
    monster->world = prevWorld;

    float breath = .003f * monster->phenotype.axial.totalScale * sinf(slot->actionClock * 2.2f);
    float lower = slot->hasAnatomicalBody ? 0.0f : slot->restBlend * a->reachProfile.maximumCrouch * .45f;
    if (!terrainContacts)
        for (size_t j = 0; j < rig->skeleton.jointCount; ++j)
            if (rig->skeleton.joints[j].parentIndex < 0)
                animation->pose.joints[j].localPosition.y += breath - lower;

    if (agile) {
        if (slot->action == ACTION_ATTACK) {
            for (size_t t = 0; t < rig->tailCount; ++t) {
                for (size_t i = 0; i < rig->tails[t].jointCount; ++i) {
                    int j = rig->tails[t].joints[i];
                    float yaw = .14f * sinf(slot->actionClock * 9.5f - (float)i * .55f);
                    float pitch = -.035f - .035f * cosf(slot->actionClock * 11.0f - (float)i * .45f);
                    Quaternion q = Quat_Multiply(Quat_FromAxisAngle(Vec3_Create(0, 1, 0), yaw),
                        Quat_FromAxisAngle(Vec3_Create(1, 0, 0), pitch));
                    animation->pose.joints[j].localRotation = Skeleton_ConstrainRotation(
                        &rig->skeleton.joints[j], Quat_Multiply(rig->skeleton.joints[j].restRotation, q));
                }
            }
        } else if (slot->restBlend > .01f) {
            for (size_t t = 0; t < rig->tailCount; ++t) {
                for (size_t i = 0; i < rig->tails[t].jointCount; ++i) {
                    int j = rig->tails[t].joints[i];
                    float frac = (float)(i + 1) / (float)(rig->tails[t].jointCount > 0 ? rig->tails[t].jointCount : 1);
                    float yaw = .055f * frac * slot->restBlend;
                    float pitch = -.025f * slot->restBlend;
                    Quaternion q = Quat_Multiply(Quat_FromAxisAngle(Vec3_Create(0, 1, 0), yaw),
                        Quat_FromAxisAngle(Vec3_Create(1, 0, 0), pitch));
                    animation->pose.joints[j].localRotation = Skeleton_ConstrainRotation(
                        &rig->skeleton.joints[j], Quat_Multiply(animation->pose.joints[j].localRotation, q));
                }
            }
        }
    }
    SkeletonPose_UpdateWorldTransforms(&animation->pose, &rig->skeleton);
    slot->poseAlpha = 1.0f;
}

static void AnimateSlotFarAnalytical(VisualSlot* slot, float dt) {
    float moveRate = fminf(4.0f, slot->speed * 1.4f + 0.35f) * slot->profile.gaitCadence;
    slot->gaitPhase = fmodf(slot->gaitPhase + dt * moveRate * 6.2831853f * 0.45f, 6.2831853f);
    float moveAmp = fminf(1.0f, slot->speed * 0.45f);
    slot->bobY = 0.025f * moveAmp * fabsf(sinf(slot->gaitPhase)) - slot->crouch * slot->scale * 0.45f;
    slot->leanPitch = (slot->action == ACTION_EAT || slot->action == ACTION_DRINK) ? 0.14f
                      : (slot->action == ACTION_REST ? -0.06f : 0.04f * moveAmp * sinf(slot->gaitPhase));
    slot->previousPosition = slot->position;
    slot->previousYaw = slot->yaw;
}

static void RunAnimationAndQueuePass(VisualState* state, float animDeltaSeconds, bool legacyWorld) {
    double tAnim = EvolutionProfiler_Begin(state->profiler);
    size_t visible = 0, animated = 0, nearC = 0, midC = 0, farC = 0, culledC = 0;
    bool smallWorld = legacyWorld && state->slotCount <= 16u;

    for (size_t i = 0; i < state->slotCapacity; ++i) {
        VisualSlot* slot = &state->slots[i];
        if (!slot->used) continue;

        if (!slot->projection.visible) {
            ++culledC;
            if (!smallWorld && slot->monsterReady && !slot->explicitTier) {
                ReleaseSlotMonster(state, slot);
            }
            continue;
        }
        ++visible;

        bool detailed = smallWorld || slot->tier == VISUAL_TIER_NEAR || slot->tier == VISUAL_TIER_MID || slot->explicitTier;
        if (slot->tier == VISUAL_TIER_NEAR) ++nearC;
        else if (slot->tier == VISUAL_TIER_MID) ++midC;
        else ++farC;

        if (detailed) {
            bool runThisFrame = true;
            float effectiveDt = animDeltaSeconds;
            if (slot->tier == VISUAL_TIER_MID && !smallWorld && !slot->explicitTier) {
                /* MID evalúa a ~20 Hz (cada 0.05 s) para acotar el presupuesto CPU. */
                slot->midAnimAccumulator += animDeltaSeconds;
                if (slot->midAnimAccumulator < 0.05f && slot->monsterReady) {
                    runThisFrame = false;
                } else {
                    effectiveDt = fminf(slot->midAnimAccumulator, 1.0f / 30.0f);
                    slot->midAnimAccumulator = 0.0f;
                }
            }
            if (runThisFrame && effectiveDt > 0.0f) {
                const EcosystemAnatomyTemplate* tmpl = NULL;
                unsigned anatGen = slot->anatomyGeneration;
                if (legacyWorld && state->world) {
                    const EcosystemEntity* e = EcosystemWorld_GetConst(state->world, slot->entityId);
                    if (e && e->anatomicalBody && e->anatomicalBody->tmpl) {
                        tmpl = e->anatomicalBody->tmpl;
                        anatGen = e->anatomicalBody->generation;
                    }
                    AnimateSlotDetailed(state, slot, effectiveDt, tmpl, anatGen);
                } else {
                    if (slot->hasAnatomicalBody) {
                        tmpl = EcosystemAnatomyCache_Acquire(slot->recipeId, slot->bucket, slot->growthStep, NULL);
                        anatGen = slot->growthStep + 1u;
                    }
                    AnimateSlotDetailed(state, slot, effectiveDt, tmpl, anatGen);
                    if (tmpl) EcosystemAnatomyCache_Release(tmpl);
                }
                ++animated;
            }
        } else {
            /* FAR: instancia ligera sin Monster ni IK de esqueleto completo. */
            if (slot->monsterReady && !slot->explicitTier) {
                ReleaseSlotMonster(state, slot);
            }
            AnimateSlotFarAnalytical(slot, animDeltaSeconds);
        }
    }

    state->lastVisible = visible;
    state->lastAnimated = animated;
    state->lastNear = nearC;
    state->lastMid = midC;
    state->lastFar = farC;
    state->lastCulled = culledC;

    if (state->profiler) {
        state->profiler->counters.activeVisualSlots = state->slotCount;
        state->profiler->counters.animatedVisualSlots = animated;
        state->profiler->counters.visibleCreatures = visible;
        state->profiler->counters.renderedNear = nearC;
        state->profiler->counters.renderedMid = midC;
        state->profiler->counters.renderedFar = farC;
    }
    EvolutionProfiler_End(state->profiler, EVO_PROF_VIS_ANIMATION, tAnim);

    /* Encolar claves de malla pendientes priorizando NEAR/seleccionadas primero. */
    for (int pass = 0; pass < 2 && state->frameEnqueues < 2u; ++pass) {
        for (size_t i = 0; i < state->slotCapacity && state->frameEnqueues < 2u; ++i) {
            VisualSlot* slot = &state->slots[i];
            if (!slot->used || !slot->projection.visible) continue;
            bool isPriority = slot->explicitTier || slot->tier == VISUAL_TIER_NEAR;
            if ((pass == 0 && !isPriority) || (pass == 1 && isPriority)) continue;
            EcosystemVisualCacheKey key = {
                .recipeId = slot->recipeId, .stage = slot->stage,
                .morphologyBucket = slot->bucket, .tier = slot->tier
            };
            pthread_mutex_lock(&state->mutex);
            QueueKeyLocked(state, key, false);
            pthread_mutex_unlock(&state->mutex);
        }
    }
}

bool EcosystemVisual_Update(EcosystemVisual* visual, float deltaSeconds) {
    if (!visual || !visual->state) return false;
    VisualState* state = visual->state;
    if (!EcosystemVisual_Sync(visual)) return false;
    state->frameEnqueues = 0;
    PublishReadyCaches(state, 1u);
    state->lodClock += fmaxf(0.0f, deltaSeconds);
    if (state->lodClock >= .1f) {
        state->lodClock = fmodf(state->lodClock, .1f);
        ApplyLod(state);
    }
    float animationDelta = (float)fmax(0.0, state->world->clock.seconds - state->animationSeconds);
    state->animationSeconds = state->world->clock.seconds;
    RunAnimationAndQueuePass(state, animationDelta, true);
    return true;
}

bool EcosystemVisual_UpdateFromSnapshot(EcosystemVisual* visual,
                                        const EvolutionRenderSnapshot* snapshot,
                                        float realDeltaSeconds) {
    if (!visual || !visual->state || !snapshot) return false;
    VisualState* state = visual->state;
    state->frameEnqueues = 0;
    PublishReadyCaches(state, 4u);
    state->lodClock += fmaxf(0.0f, realDeltaSeconds);
    if (state->lodClock >= 0.08f) {
        state->lodClock = fmodf(state->lodClock, 0.08f);
        ApplyLod(state);
    }
    /* Animación visual desacoplada de la velocidad de simulación: avanza si el reloj biológico avanzó,
     * pero siempre acotada al tiempo real de frame (<= 1/30 s) para evitar saltos a 16x/64x/MAX. */
    double simDelta = snapshot->seconds - state->animationSeconds;
    if (simDelta < 0.0) simDelta = 0.0;
    state->animationSeconds = snapshot->seconds;
    float animDt = 0.0f;
    if (simDelta > 1e-6) {
        animDt = fminf(fmaxf(0.0f, realDeltaSeconds), 1.0f / 30.0f);
        if (simDelta < (double)animDt) animDt = (float)simDelta;
    }
    RunAnimationAndQueuePass(state, animDt, false);
    return true;
}

void EcosystemVisual_Shutdown(EcosystemVisual* visual) {
    if (!visual || !visual->state) return;
    VisualState* state = visual->state;
    pthread_mutex_lock(&state->mutex);
    state->stop = true;
    pthread_cond_signal(&state->condition);
    pthread_mutex_unlock(&state->mutex);
    if (state->workerStarted) pthread_join(state->worker, NULL);
    for (size_t i = 0; i < state->slotCapacity; ++i) TeardownSlot(state, &state->slots[i], i);
    for (size_t i = 0; i < ECOSYSTEM_VISUAL_CACHE_CAPACITY; ++i) FreeCacheEntry(&state->cache[i]);
    Mesh_Free(&state->fallback);
    slotMapFree(&state->slotMap);
    WorldStaticData_Release(state->staticWorld);
    state->staticWorld = NULL;
    pthread_cond_destroy(&state->condition);
    pthread_mutex_destroy(&state->mutex);
    free(state->slots); free(state->freeSlots); free(state->cache); free(state->queue);
    free(state);
    visual->state = NULL;
}

bool EcosystemVisual_SetProjection(EcosystemVisual* visual, AIObjectId id, EcosystemVisualProjection projection) {
    if (!visual || !visual->state || !isfinite(projection.projectedPixels)) return false;
    VisualSlot* slot = FindSlot(visual->state, id);
    if (!slot) return false;
    slot->projection = projection;
    if (slot->projection.projectedPixels < 0.0f) slot->projection.projectedPixels = 0.0f;
    return true;
}

bool EcosystemVisual_RequestTier(EcosystemVisual* visual, AIObjectId id, VisualTier tier) {
    if (!visual || !visual->state || tier >= VISUAL_TIER_COUNT) return false;
    VisualSlot* slot = FindSlot(visual->state, id);
    if (!slot) return false;
    slot->requestedTier = tier; slot->tier = tier; slot->explicitTier = true;
    return true;
}

bool EcosystemVisual_ClearTierRequest(EcosystemVisual* visual, AIObjectId id) {
    if (!visual || !visual->state) return false;
    VisualSlot* slot = FindSlot(visual->state, id);
    if (!slot) return false;
    slot->explicitTier = false;
    return true;
}

size_t EcosystemVisual_PrewarmVisible(EcosystemVisual* visual) {
    if (!visual || !visual->state) return 0;
    VisualState* state = visual->state;
    size_t queued = 0;
    pthread_mutex_lock(&state->mutex);
    for (size_t i = 0; i < state->slotCapacity; ++i) {
        VisualSlot* slot = &state->slots[i];
        if (!slot->used || !slot->projection.visible) continue;
        EcosystemVisualCacheKey key = {
            .recipeId = slot->recipeId, .stage = slot->stage,
            .morphologyBucket = slot->bucket, .tier = slot->tier
        };
        size_t before = state->queueCount;
        if (QueueKeyLocked(state, key, true) && state->queueCount > before) ++queued;
    }
    pthread_mutex_unlock(&state->mutex);
    return queued;
}

const Mesh* EcosystemVisual_RequestMesh(EcosystemVisual* visual, EcosystemVisualCacheKey key) {
    if (!visual || !visual->state || !KeyValid(key)) return NULL;
    VisualState* state = visual->state;
    const Mesh* result = &state->fallback;
    pthread_mutex_lock(&state->mutex);
    size_t index = CacheFindLocked(state, key);
    if (index == ECOSYSTEM_VISUAL_CACHE_CAPACITY) index = CacheReserveLocked(state, key);
    if (index < ECOSYSTEM_VISUAL_CACHE_CAPACITY && state->cache[index].state == CACHE_READY) {
        result = &state->cache[index].body;
        ++state->cacheHits;
    } else if (index < ECOSYSTEM_VISUAL_CACHE_CAPACITY) {
        QueueKeyLocked(state, key, false);
    }
    pthread_mutex_unlock(&state->mutex);
    return result;
}

const Mesh* EcosystemVisual_FallbackMesh(const EcosystemVisual* visual) {
    if (!visual || !visual->state) return NULL;
    return &((const VisualState*)visual->state)->fallback;
}

size_t EcosystemVisual_GetDrawPackets(const EcosystemVisual* visual, EcosystemVisualDrawPacket* packets, size_t capacity) {
    if (!visual || !visual->state || !packets) return 0;
    VisualState* state = visual->state;
    size_t written = 0;
    pthread_mutex_lock(&state->mutex);
    for (size_t i = 0; i < state->slotCapacity && written < capacity; ++i) {
        VisualSlot* slot = &state->slots[i];
        if (!slot->used || !slot->projection.visible) continue;
        EcosystemVisualDrawPacket* packet = &packets[written++];
        memset(packet, 0, sizeof(*packet));
        packet->slotIndex = slot->slotIndex;
        packet->entityId = slot->entityId;
        packet->recipeId = slot->recipeId;
        packet->morphologyBucket = slot->bucket;
        packet->tier = slot->tier;
        packet->bodyMesh = &state->fallback;
        packet->surfaceRecipe = slot->surfaceRecipe;
        packet->tintColor = slot->tintColor;
        packet->bobY = slot->bobY;
        packet->leanPitch = slot->leanPitch;

        EcosystemVisualCacheKey wantedKey = {
            .recipeId = slot->recipeId, .stage = slot->stage,
            .morphologyBucket = slot->bucket, .tier = slot->tier
        };
        size_t index = CacheFindLocked(state, wantedKey);
        if (index == ECOSYSTEM_VISUAL_CACHE_CAPACITY || state->cache[index].state != CACHE_READY) {
            /* Reutilizar cualquier LOD residente compatible (preferir FAR para carga rápida). */
            for (size_t candidate = 0; candidate < ECOSYSTEM_VISUAL_CACHE_CAPACITY; ++candidate) {
                const CacheEntry* e = &state->cache[candidate];
                if (e->state == CACHE_READY && e->key.recipeId == slot->recipeId &&
                    e->key.stage == slot->stage && e->key.morphologyBucket == slot->bucket) {
                    index = candidate;
                    if (e->key.tier == VISUAL_TIER_FAR) break;
                }
            }
        }
        bool hasDetailedMonster = (slot->monsterReady && slot->monster && slot->monster->animation);
        if (index < ECOSYSTEM_VISUAL_CACHE_CAPACITY && state->cache[index].state == CACHE_READY) {
            const CacheEntry* entry = &state->cache[index];
            packet->tier = entry->key.tier;
            packet->ready = true;
            packet->bodyMesh = &entry->body;
            ++state->cacheHits;
            packet->headMesh = entry->head.vertexCount ? &entry->head : NULL;
            packet->eyes = entry->eyes; packet->eyeCount = entry->eyeCount;
            packet->mouths = entry->mouths; packet->mouthCount = entry->mouthCount;
            packet->deformer = entry->deformer;
            packet->skeleton = &entry->rig.skeleton;
            packet->bodyJointBounds = entry->jointBounds;
            packet->pose = hasDetailedMonster ? &slot->monster->animation->pose : &entry->restPose;
            packet->restPose = &entry->restPose;
            packet->headJoint = entry->rig.headJoint;
            packet->jawJoint = entry->rig.jawJoint;
            packet->oralPoint = entry->rig.oralPoint;
            packet->instancedEligible = (!hasDetailedMonster && slot->tier == VISUAL_TIER_FAR && !slot->explicitTier);

            if (hasDetailedMonster) {
                packet->skeleton = &slot->monster->animation->rig.skeleton;
                packet->oralPoint = slot->monster->animation->rig.oralPoint;
                packet->targetGraph = &slot->monster->anatomyGraph;
            }

            /* Cachear escalas estructurales de cabeza/mandíbula/ojos por ranura. */
            uint64_t scaleKey = EcosystemVisual_CacheKeyHash(entry->key);
            if (slot->cachedScaleCacheKey != scaleKey || slot->cachedScaleAnatomyGen != slot->anatomyGeneration) {
                slot->cachedHeadScale = Vec3_Create(1, 1, 1);
                if (packet->targetGraph) {
                    AnatomyId id = entry->rig.skeleton.joints[entry->rig.headJoint].anatomyId;
                    const AnatomyNode* from = AnatomyGraph_FindNode(&entry->deformer->refGraph, id);
                    const AnatomyNode* to = AnatomyGraph_FindNode(packet->targetGraph, id);
                    if (from && to) {
                        slot->cachedHeadScale = Vec3_Create(to->widthRadius / from->widthRadius,
                                                            to->heightRadius / from->heightRadius,
                                                            to->widthRadius / from->widthRadius);
                    }
                    Vector3 a = entry->rig.oralPoint, b = packet->oralPoint;
                    if (fabsf(a.y) > 1e-6f && b.y / a.y > 0) slot->cachedHeadScale.y = b.y / a.y;
                    if (fabsf(a.z) > 1e-6f && b.z / a.z > 0) slot->cachedHeadScale.z = b.z / a.z;
                }
                slot->cachedJawScale = slot->cachedHeadScale;
                slot->cachedEyeScale = 1.0f;
                const CreatureRecipe* base = CreatureRecipes_Find(slot->recipeId);
                if (hasDetailedMonster && base && base->adult.eyes.size > 0) {
                    slot->cachedEyeScale = slot->monster->phenotype.eyes.size / base->adult.eyes.size;
                }
                if (hasDetailedMonster && entry->rig.jawJoint >= 0) {
                    SkeletonPose current;
                    SkeletonPose_Init(&current, packet->skeleton);
                    Vector3 from = Vec3_Sub(Vec3_Add(entry->restPose.joints[packet->headJoint].worldPosition, entry->rig.oralPoint),
                                            entry->restPose.joints[packet->jawJoint].worldPosition);
                    Vector3 to = Vec3_Sub(Vec3_Add(current.joints[packet->headJoint].worldPosition, packet->oralPoint),
                                          current.joints[packet->jawJoint].worldPosition);
                    if (fabsf(from.y) > 1e-6f && to.y / from.y > 0) slot->cachedJawScale.y = to.y / from.y;
                    if (fabsf(from.z) > 1e-6f && to.z / from.z > 0) slot->cachedJawScale.z = to.z / from.z;
                }
                slot->cachedTrunkRadius = entry->trunkRadius;
                if (packet->targetGraph) {
                    float tr = 0.0f;
                    for (size_t node = 0; node < packet->targetGraph->nodeCount; ++node) {
                        if (packet->targetGraph->nodes[node].region == ANATOMY_REGION_TRUNK) {
                            tr = fmaxf(tr, packet->targetGraph->nodes[node].widthRadius);
                        }
                    }
                    if (tr > 0.0f) slot->cachedTrunkRadius = tr;
                }
                slot->cachedScaleCacheKey = scaleKey;
                slot->cachedScaleAnatomyGen = slot->anatomyGeneration;
            }
            packet->headScale = slot->cachedHeadScale;
            packet->jawScale = slot->cachedJawScale;
            packet->eyeScale = slot->cachedEyeScale;
            packet->previousPose = (hasDetailedMonster && slot->previousPose) ? slot->previousPose : &entry->restPose;
            packet->measuredMeshRadius = entry->meshRadius;
            packet->meshRadius = slot->cachedTrunkRadius;
            packet->meshLength = entry->meshLength;
            /* Sin deformador detallado (FAR instanciado), la malla es la adulta de entry:
             * calibrar con entry->calibrationLength para que developmentScale escale crías correctamente. */
            packet->calibrationLength = hasDetailedMonster ? EcosystemAnatomy_ModelLength(&slot->monster->phenotype)
                                                           : entry->calibrationLength;
            packet->axialTotalScale = entry->axialTotalScale;
            packet->calibrationFactor = 2.05f / fmaxf(1e-4f, packet->calibrationLength);
            packet->scale = slot->profile.renderScale * packet->calibrationFactor * fmaxf(0.01f, slot->developmentScale);
            Quaternion qOrient = SlotOrientation(slot);
            packet->orientation = qOrient;
            packet->groundNormal = Vec3_LengthSq(slot->groundNormal) > 0.25f
                                       ? slot->groundNormal : Vec3_Create(0.0f, 1.0f, 0.0f);
            packet->position = Vec3_Sub(slot->position, Quat_RotateVector(qOrient, Vec3_Scale(SlotCenter(slot), packet->scale)));
            if (hasDetailedMonster && packet->pose && packet->headJoint >= 0) {
                const JointPose* head = &packet->pose->joints[packet->headJoint];
                Vector3 reference = entry->rig.oralPoint;
                reference = (Vector3){reference.x * packet->headScale.x, reference.y * packet->headScale.y, reference.z * packet->headScale.z};
                Vector3 oral = Vec3_Add(head->worldPosition, Quat_RotateVector(head->worldRotation, reference));
                packet->renderedMouth = Vec3_Add(packet->position, Quat_RotateVector(qOrient, Vec3_Scale(oral, packet->scale)));
                packet->mouthError = Vec3_Distance(packet->renderedMouth, slot->mouthTarget);
            }
            if (hasDetailedMonster && packet->pose) {
                const MonsterAnimation* animation = slot->monster->animation;
                for (size_t leg = 0; leg < animation->rig.limbCount; ++leg) {
                    const LimbRig *limb = &animation->rig.limbs[leg];
                    Vector3 actual = packet->pose->joints[limb->endEffectorJoint].worldPosition;
                    const FootState* foot = &animation->animator.locomotion.feet[leg];
                    float error = Vec3_Distance(actual, foot->targetPosition) * packet->scale;
                    packet->footError = fmaxf(packet->footError, error);
                    if (foot->phase == FOOT_STANCE) packet->stanceFootError = fmaxf(packet->stanceFootError, error);
                    if (!limb->walking || !animation->animator.locomotion.initialized) continue;
                    Vector3 normal = Vec3_LengthSq(foot->normal) > .25f ? foot->normal : Vec3_Create(0, 1, 0);
                    Vector3 sole = Vec3_Sub(actual, Vec3_Scale(normal, limb->soleHeight));
                    unsigned contact = packet->supportCount++;
                    packet->supportPoints[contact] = Vec3_Add(packet->position,
                        Quat_RotateVector(qOrient, Vec3_Scale(sole, packet->scale)));
                    if (foot->phase == FOOT_STANCE) packet->stanceMask |= 1u << contact;
                }
            }
        } else {
            packet->position = slot->position;
            packet->orientation = SlotOrientation(slot);
            packet->groundNormal = Vec3_LengthSq(slot->groundNormal) > 0.25f
                                       ? slot->groundNormal : Vec3_Create(0.0f, 1.0f, 0.0f);
            packet->scale = slot->scale;
            packet->measuredMeshRadius = slot->meshRadius;
            packet->meshRadius = slot->calibrationLength > 1e-4f ? slot->speciesRadius * slot->calibrationLength / 2.05f : 0.0f;
            packet->meshLength = slot->meshLength;
            packet->calibrationLength = slot->calibrationLength;
            packet->axialTotalScale = slot->axialTotalScale;
            packet->calibrationFactor = slot->calibrationLength > 1e-4f ? 2.05f / slot->calibrationLength : 1.0f;
        }
        packet->presentedCenter = slot->position;
        packet->poseAlpha = slot->poseAlpha;
        packet->yaw = slot->yaw;
        packet->gaitCadence = slot->profile.gaitCadence;
        packet->postureLean = slot->profile.postureLean;
        packet->headScanRate = slot->profile.headScanRate;
    }
    pthread_mutex_unlock(&state->mutex);
    return written;
}

bool EcosystemVisual_DeformBody(const EcosystemVisualDrawPacket* packet, Mesh* output) {
    if (!packet || !output || !packet->ready || !packet->deformer || !packet->bodyMesh) return false;
    const Mesh* base = packet->bodyMesh;
    if (!Mesh_ReserveVertices(output, base->vertexCount) || !Mesh_ReserveIndices(output, base->indexCount)) return false;
    memcpy(output->vertices, base->vertices, base->vertexCount * sizeof(*base->vertices));
    memcpy(output->indices, base->indices, base->indexCount * sizeof(*base->indices));
    output->vertexCount = base->vertexCount; output->indexCount = base->indexCount;
    output->surfaceRecipe = base->surfaceRecipe; output->hasSurface = base->hasSurface;
    AnatomyPosePalette palette;
    if (!AnatomyDeformer_BuildMorphPosePalette(packet->deformer, packet->skeleton, packet->pose, packet->targetGraph, &palette)) return false;
    if (packet->headJoint >= 0 && !AnatomyDeformer_ScaleJointPalette(packet->deformer, packet->pose, packet->headJoint, packet->headScale, &palette)) return false;
    for (size_t v = 0; v < base->vertexCount; ++v)
        if (!AnatomyDeformer_EvaluatePoseVertex(packet->deformer, &palette, v, &output->vertices[v].position, &output->vertices[v].normal)) return false;
    Mesh_MarkGeometryChanged(output);
    return true;
}

static bool PickMesh(const Mesh* mesh, Vector3 origin, Vector3 direction, float nearDistance, float* nearest) {
    if (!mesh) return false;
    bool hit = false;
    for (size_t i = 0; i + 2 < mesh->indexCount; i += 3) {
        Vector3 a = mesh->vertices[mesh->indices[i]].position;
        Vector3 ab = Vec3_Sub(mesh->vertices[mesh->indices[i + 1]].position, a);
        Vector3 ac = Vec3_Sub(mesh->vertices[mesh->indices[i + 2]].position, a);
        Vector3 cross = Vec3_Cross(direction, ac);
        float determinant = Vec3_Dot(ab, cross);
        if (fabsf(determinant) < 1e-9f) continue;
        Vector3 delta = Vec3_Sub(origin, a);
        float u = Vec3_Dot(delta, cross) / determinant;
        if (u < 0 || u > 1) continue;
        Vector3 q = Vec3_Cross(delta, ab);
        float v = Vec3_Dot(direction, q) / determinant;
        if (v < 0 || u + v > 1) continue;
        float distance = Vec3_Dot(ac, q) / determinant;
        if (distance >= nearDistance && distance < *nearest) { *nearest = distance; hit = true; }
    }
    return hit;
}

static bool PickBodyBounds(const EcosystemVisualDrawPacket* p, Vector3 origin, Vector3 direction,
    float nearDistance, float farDistance) {
    if (!p->bodyJointBounds) return true;
    AnatomyPosePalette palette;
    if (!AnatomyDeformer_BuildMorphPosePalette(p->deformer, p->skeleton, p->pose, p->targetGraph, &palette)) return true;
    if (p->headJoint >= 0 && !AnatomyDeformer_ScaleJointPalette(p->deformer, p->pose, p->headJoint, p->headScale, &palette)) return true;
    AABB3D bounds = AABB_Empty();
    for (size_t j = 0; j < palette.jointCount; ++j) {
        AABB3D box = p->bodyJointBounds[j];
        if (box.start.x > box.end.x) continue;
        for (unsigned corner = 0; corner < 8; ++corner) {
            float v[] = {corner & 1 ? box.end.x : box.start.x, corner & 2 ? box.end.y : box.start.y, corner & 4 ? box.end.z : box.start.z};
            float q[3];
            for (unsigned axis = 0; axis < 3; ++axis) {
                const float* row = palette.rows[j][axis];
                q[axis] = row[0] * v[0] + row[1] * v[1] + row[2] * v[2] + row[3];
            }
            AABB_ExpandPoint(&bounds, Vec3_Create(q[0], q[1], q[2]));
        }
    }
    for (unsigned axis = 0; axis < 3; ++axis) {
        float o = MeshAxisCoordinate(origin, axis), d = MeshAxisCoordinate(direction, axis);
        float lo = MeshAxisCoordinate(bounds.start, axis) - 1e-4f, hi = MeshAxisCoordinate(bounds.end, axis) + 1e-4f;
        if (fabsf(d) < 1e-8f) { if (o < lo || o > hi) return false; continue; }
        float a = (lo - o) / d, b = (hi - o) / d;
        nearDistance = fmaxf(nearDistance, fminf(a, b));
        farDistance = fminf(farDistance, fmaxf(a, b));
        if (nearDistance > farDistance) return false;
    }
    return true;
}

static void PickJointRay(const EcosystemVisualDrawPacket* p, int joint, Vector3 scale,
    Vector3* origin, Vector3* direction) {
    if (!p->restPose || !p->pose || joint < 0 || (size_t)joint >= p->pose->jointCount ||
        (size_t)joint >= p->restPose->jointCount) return;
    Quaternion rotation = Quat_Multiply(p->pose->joints[joint].worldRotation,
        Quat_Inverse(p->restPose->joints[joint].worldRotation));
    *origin = Quat_RotateVector(Quat_Inverse(rotation), Vec3_Sub(*origin, p->pose->joints[joint].worldPosition));
    *direction = Quat_RotateVector(Quat_Inverse(rotation), *direction);
    origin->x /= scale.x; origin->y /= scale.y; origin->z /= scale.z;
    direction->x /= scale.x; direction->y /= scale.y; direction->z /= scale.z;
    *origin = Vec3_Add(*origin, p->restPose->joints[joint].worldPosition);
}

AIObjectId EcosystemVisual_PickRay(const EcosystemVisualDrawPacket* packets, size_t count,
    Vector3 origin, Vector3 direction, float nearDistance, float farDistance) {
    if (!packets || !isfinite(Vec3_LengthSq(origin)) || !isfinite(Vec3_LengthSq(direction)) ||
        Vec3_LengthSq(direction) < 1e-12f || !isfinite(nearDistance) || !isfinite(farDistance) ||
        nearDistance < 0 || farDistance <= nearDistance) return 0;
    direction = Vec3_Normalize(direction);
    AIObjectId selected = 0;
    Mesh deformed = Mesh_Create();
    for (size_t i = 0; i < count; ++i) {
        const EcosystemVisualDrawPacket* p = &packets[i];
        if (!p->entityId || !isfinite(p->scale) || p->scale <= 0) continue;

        /* Rechazo rápido por esfera envolvente conservadora antes de deformar vértices. */
        float boundR = fmaxf(2.5f, (p->meshLength > 0.1f ? p->meshLength : 3.5f) * p->scale * 1.4f);
        Vector3 toCenter = Vec3_Sub(p->position, origin);
        float proj = Vec3_Dot(toCenter, direction);
        if (proj + boundR < nearDistance || proj - boundR > farDistance) continue;
        Vector3 closest = Vec3_Add(origin, Vec3_Scale(direction, proj));
        if (Vec3_LengthSq(Vec3_Sub(closest, p->position)) > boundR * boundR) continue;

        Quaternion qP = p->orientation;
        bool hasQ = (qP.x * qP.x + qP.y * qP.y + qP.z * qP.z + qP.w * qP.w) > 0.5f;
        Vector3 localOrigin = hasQ
            ? Vec3_Scale(Quat_RotateVector(Quat_Conjugate(qP), Vec3_Sub(origin, p->position)), 1.0f / p->scale)
            : Vec3_Scale(RotateYaw(Vec3_Sub(origin, p->position), -p->yaw), 1.0f / p->scale);
        Vector3 localDirection = hasQ
            ? Quat_RotateVector(Quat_Conjugate(qP), direction)
            : RotateYaw(direction, -p->yaw);
        float nearest = farDistance / p->scale, nearLocal = nearDistance / p->scale;
        const Mesh* body = p->bodyMesh;
        if (p->ready && p->deformer && p->pose && p->pose != p->restPose) {
            if (PickBodyBounds(p, localOrigin, localDirection, nearLocal, nearest)) {
                if (!EcosystemVisual_DeformBody(p, &deformed)) continue;
                body = &deformed;
            } else {
                body = NULL;
            }
        }
        bool hit = PickMesh(body, localOrigin, localDirection, nearLocal, &nearest);
        Vector3 headOrigin = localOrigin, headDirection = localDirection;
        PickJointRay(p, p->headJoint, p->headScale, &headOrigin, &headDirection);
        hit = PickMesh(p->headMesh, headOrigin, headDirection, nearLocal, &nearest) || hit;
        Vector3 jawOrigin = localOrigin, jawDirection = localDirection;
        PickJointRay(p, p->jawJoint, p->jawScale, &jawOrigin, &jawDirection);
        for (size_t m = 0; m < p->mouthCount; ++m) {
            hit = PickMesh(&p->mouths[m].jaw, jawOrigin, jawDirection, nearLocal, &nearest) || hit;
            hit = PickMesh(&p->mouths[m].hinge, headOrigin, headDirection, nearLocal, &nearest) || hit;
        }
        for (size_t e = 0; e < p->eyeCount; ++e) if (p->eyeScale > 0) {
            Vector3 eyeOrigin = Vec3_Add(p->eyes[e].center, Vec3_Scale(Vec3_Sub(headOrigin, p->eyes[e].center), 1.0f / p->eyeScale));
            Vector3 eyeDirection = Vec3_Scale(headDirection, 1.0f / p->eyeScale);
            hit = PickMesh(&p->eyes[e].globe, eyeOrigin, eyeDirection, nearLocal, &nearest) || hit;
        }
        if (hit) { farDistance = nearest * p->scale; selected = p->entityId; }
    }
    Mesh_Free(&deformed);
    return selected;
}

bool EcosystemVisual_Validate(const EcosystemVisual* visual) {
    if (!visual || !visual->state) return false;
    const VisualState* state = visual->state;
    if (!state->snapshotMode) {
        if (!state->world || !state->world->entities) return state->slotCount == 0;
        size_t live = 0;
        for (unsigned k = 0; k < state->world->count; ++k) {
            unsigned i = state->world->activeEntitySlots[k];
            if (!state->world->entities[i].id) continue;
            ++live;
            int visIdx = slotMapGet(&state->slotMap, state->world->entities[i].id);
            if (visIdx < 0 || (size_t)visIdx >= state->slotCapacity) return false;
            const VisualSlot* s = &state->slots[visIdx];
            if (!s->used || s->slotIndex != i || s->entityId != state->world->entities[i].id) return false;
        }
        if (live != state->slotCount) return false;
    }
    for (size_t i = 0; i < ECOSYSTEM_VISUAL_CACHE_CAPACITY; ++i) if (state->cache[i].state != CACHE_EMPTY) {
        if (!KeyValid(state->cache[i].key)) return false;
        for (size_t j = i + 1u; j < ECOSYSTEM_VISUAL_CACHE_CAPACITY; ++j)
            if (state->cache[j].state != CACHE_EMPTY && KeyEqual(state->cache[i].key, state->cache[j].key)) return false;
    }
    return true;
}

EcosystemVisualStats EcosystemVisual_GetStats(const EcosystemVisual* visual) {
    EcosystemVisualStats st = {0};
    if (!visual || !visual->state) return st;
    VisualState* state = visual->state;
    pthread_mutex_lock(&state->mutex);
    st.activeSlots = state->slotCount;
    st.slotCapacity = state->slotCapacity;
    st.visibleCount = state->lastVisible;
    st.animatedCount = state->lastAnimated;
    st.nearCount = state->lastNear;
    st.midCount = state->lastMid;
    st.farCount = state->lastFar;
    st.culledCount = state->lastCulled;
    st.monsterInstances = state->monsterInstances;
    st.cacheEntries = state->cacheCount;
    st.cacheBytes = state->totalCacheBytes;
    st.pendingQueue = state->queueCount;
    st.buildCount = state->buildCount;
    st.cacheHits = state->cacheHits;
    pthread_mutex_unlock(&state->mutex);
    return st;
}

size_t EcosystemVisual_SlotCount(const EcosystemVisual* visual) {
    return visual && visual->state ? ((const VisualState*)visual->state)->slotCount : 0;
}
size_t EcosystemVisual_CacheCount(const EcosystemVisual* visual) {
    return visual && visual->state ? ((const VisualState*)visual->state)->cacheCount : 0;
}
size_t EcosystemVisual_BuildCount(const EcosystemVisual* visual) {
    if (!visual || !visual->state) return 0;
    VisualState* state = visual->state;
    pthread_mutex_lock(&state->mutex);
    size_t count = state->buildCount;
    pthread_mutex_unlock(&state->mutex);
    return count;
}
size_t EcosystemVisual_CacheHitCount(const EcosystemVisual* visual) {
    return visual && visual->state ? ((const VisualState*)visual->state)->cacheHits : 0;
}
