/**
 * @file EcosystemVisual.c
 * @brief Caché compartida, LOD y reconciliación visual del ecosistema.
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
#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

typedef enum CacheState { CACHE_EMPTY, CACHE_QUEUED, CACHE_BUILDING, CACHE_PENDING, CACHE_READY, CACHE_FAILED } CacheState;

typedef struct VisualSlot {
    bool used, seen, explicitTier, monsterReady;
    uint32_t slotIndex, recipeId, stage, bucket, entitySeed;
    AIObjectId entityId;
    CreatureGenome genome;
    CreatureVisualProfile profile;
    CreatureLifeStage lifeStage;
    VisualTier tier, requestedTier;
    EcosystemVisualProjection projection;
    Vector3 position;
    float yaw, scale, animationClock, poseAlpha;
    float speciesRadius, developmentScale, meshRadius, meshLength, calibrationLength, axialTotalScale;
    float maturity;
    Vector3 previousPosition, mouthTarget;
    float previousYaw, runBlend, actionClock, restBlend;
    bool oralActive;
    CreatureAction action;
    unsigned anatomyGeneration;
    SurfacePhenotype surface;
    SurfaceRecipe surfaceRecipe;
    Monster monster;
    SkeletonPose previousPose;
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
} CacheEntry;

typedef struct VisualState {
    EcosystemWorld* world;
    VisualSlot* slots;
    size_t slotCapacity;
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
} VisualState;

static bool KeyEqual(EcosystemVisualCacheKey a, EcosystemVisualCacheKey b) {
    return a.recipeId == b.recipeId && a.stage == b.stage && a.morphologyBucket == b.morphologyBucket && a.tier == b.tier;
}

static bool KeyValid(EcosystemVisualCacheKey key) {
    return key.recipeId != 0 && key.stage < 4u && key.morphologyBucket < VISUAL_MORPHOLOGY_BUCKETS && key.tier < VISUAL_TIER_COUNT;
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
    for (size_t i = 0, p = state->queueHead; i < state->queueCount; ++i, p = (p + 1u) % ECOSYSTEM_VISUAL_QUEUE_CAPACITY) {
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
    if (state->queueCount == ECOSYSTEM_VISUAL_QUEUE_CAPACITY || (!unrestricted && state->frameEnqueues >= 1u)) return false;
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
        center[0] += position.x;
        center[1] += position.y;
        center[2] += position.z;
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
        float radial = sqrtf(radialSquared);
        radialSum += radial;
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
    /* Un segundo componente puede ser un cuerno o una cola válida. Se conserva
     * la copia completa y se deja que el mapeo anatómico certifique el tejido. */
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
    for(size_t i=0;i<monster->mouthCount;++i)monster->mouths[i].openFactor=0;
    monster->head.anatomy.oralSystem.openFactor=0;
    return true;
}

static SDFMesherConfig MeshConfig(VisualTier tier) {
    SDFMesherConfig config = SDFMesher_DefaultConfig();
    config.useAutoBounds = true;
    config.samplingThreadCount = 1;
    config.maxResolution = tier == VISUAL_TIER_NEAR ? 320 : 160;
    config.maxCells = tier == VISUAL_TIER_NEAR ? 1800000u : 500000u;
    config.voxelSize = tier == VISUAL_TIER_FAR ? .34f : tier == VISUAL_TIER_MID ? .18f : .12f;
    return config;
}

static size_t TailDetailRegions(const MonsterSDF* sdf,const AnatomyGraph* graph,
    SDFDetailRegion* regions,size_t count,size_t capacity) {
    for(size_t i=0;i<sdf->connectorCount&&count<capacity;++i) {
        const MonsterSDFConnector* c=&sdf->connectors[i];
        const AnatomyNode* a=AnatomyGraph_FindNode(graph,c->fromId);
        const AnatomyNode* b=AnatomyGraph_FindNode(graph,c->toId);
        if(!a||!b||a->region!=ANATOMY_REGION_TAIL||b->region!=ANATOMY_REGION_TAIL)continue;
        float radius=fminf(fminf(a->widthRadius,a->heightRadius),fminf(b->widthRadius,b->heightRadius));
        /* La sección terminal manda incluso en LOD lejano: una cola fina no
         * debe romperse en islas por heredar el voxel grueso del tronco. */
        regions[count++]=(SDFDetailRegion){.bounds=c->bounds,.targetVoxelSize=fmaxf(.012f,radius*.7f)};
    }
    return count;
}

static bool BuildMainMeshes(CacheEntry* entry, const EcosystemVisualCacheKey key, Monster* monster, MonsterSDF* sdf) {
    SDFMesherConfig config = MeshConfig(key.tier);
    SDFMesher mesher = SDFMesher_Create(config);
    Mesh body = Mesh_Create();
    bool ok = false;
    SDFDetailRegion regions[MONSTER_SDF_DETAIL_REGION_CAPACITY];
    /* El LOD reduce el torso, pero conserva las secciones de patas, dedos y
     * pabellones: una rejilla global gruesa podía borrar miembros completos. */
    size_t count=MonsterSDF_GetDetailRegions(sdf,key.tier==VISUAL_TIER_NEAR?1.8f:1.2f,regions,MONSTER_SDF_DETAIL_REGION_CAPACITY);
    count=TailDetailRegions(sdf,&monster->anatomyGraph,regions,count,MONSTER_SDF_DETAIL_REGION_CAPACITY);
    if(key.tier==VISUAL_TIER_NEAR)for(size_t m=0;m<sdf->mouthCount&&count+5<=MONSTER_SDF_DETAIL_REGION_CAPACITY;++m) {
        const MonsterSDFMouth* head=&sdf->mouths[m];
        float unit=monster->phenotype.axial.totalScale;
        regions[count++]=(SDFDetailRegion){.bounds=head->headBounds,.targetVoxelSize=.06f*unit};
        Vector3 points[]={head->leftNostrilCenterLocal,head->rightNostrilCenterLocal,head->leftOrbitCenterLocal,head->rightOrbitCenterLocal};
        for(unsigned feature=0;feature<4;++feature) {
            Vector3 center=Vec3_Add(head->center,points[feature]);
            Vector3 radii=Vec3_Scale(feature<2?head->nostrilRadii:head->orbitRadii,2);
            regions[count++]=(SDFDetailRegion){.bounds=AABB_FromMinMax(Vec3_Sub(center,radii),Vec3_Add(center,radii)),.targetVoxelSize=.025f*unit};
        }
    }
    mesher.config.adaptiveDetail=count>0;
    if (key.tier == VISUAL_TIER_FAR) {
        MonsterSDFBodyField context;
        SDFField field = MonsterSDF_GetBodyField(sdf, &context);
        ok=count?SDFMesher_GenerateMeshDetailed(&mesher,&field,regions,count,&body):SDFMesher_GenerateMesh(&mesher,&field,&body);
    } else {
        SDFField field = MonsterSDF_GetField(sdf);
        ok=count?SDFMesher_GenerateMeshDetailed(&mesher,&field,regions,count,&body):SDFMesher_GenerateMesh(&mesher,&field,&body);
    }
    if (ok) ok = KeepLargestWithoutAnatomyLoss(&body);
    SDFMesher_Free(&mesher);
    if (!ok) {
        Mesh_Free(&body);
        return false;
    }
    entry->body = body;
    entry->head = Mesh_Create();
    if (key.tier == VISUAL_TIER_FAR) {
        MonsterSDFHeadField context;
        SDFField field = MonsterSDF_GetHeadField(sdf, 0, &context);
        SDFMesher headMesher = SDFMesher_Create((SDFMesherConfig){
            .voxelSize = .20f, .maxResolution = 160, .maxCells = 220000u,
            .isolevel = 0.0f, .normalEps = 0.0f, .useAutoBounds = true,
            .samplingThreadCount = 1});
        headMesher.config.adaptiveDetail=count>0;
        if (sdf->mouthCount && !(count?SDFMesher_GenerateMeshDetailed(&headMesher,&field,regions,count,&entry->head):SDFMesher_GenerateMesh(&headMesher,&field,&entry->head))) ok = false;
        SDFMesher_Free(&headMesher);
        if (ok) ok = KeepLargestWithoutAnatomyLoss(&entry->head);
    }
    if (!ok) return false;
    SurfaceMapper_MapMesh(&entry->body, &monster->anatomyGraph, &monster->surfaceMapping);
    SurfaceMapper_MapMesh(&entry->head, &monster->anatomyGraph, &monster->surfaceMapping);
    ApplySurface(&entry->body, &monster->surface, monster->surfaceMapping.unitScale);
    ApplySurface(&entry->head, &monster->surface, monster->surfaceMapping.unitScale);
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

static bool BuildDeformer(CacheEntry* entry, Monster* monster,MonsterSDF* sdf) {
    if (monster->animation) entry->rig = monster->animation->rig;
    else if (!monster->anatomyGraph.nodeCount || !RigBuilder_FromAnatomy(&monster->anatomyGraph,
            monster->anatomyGraph.nodes[0].id, &entry->rig)) return false;
    if (!SkeletonPose_Init(&entry->restPose, &entry->rig.skeleton)) return false;
    entry->deformer = AnatomyDeformer_Create();
    if (!entry->deformer) return false;
    bool ok=AnatomyDeformer_BindSkeleton(entry->deformer, &entry->body, &monster->anatomyGraph, &entry->rig.skeleton) &&
        AnatomyDeformer_BindCranium(entry->deformer,&entry->rig.skeleton,entry->rig.headJoint,
            entry->rig.cranialAttachment,entry->rig.cranialAxis,entry->rig.cranialCollar,sdf);
    if(!ok)return false;
    for(size_t j=0;j<SKELETON_MAX_JOINTS;++j)entry->jointBounds[j]=AABB_Empty();
    for(size_t v=0;v<entry->deformer->vertexCount;++v) {
        const AnatomySkinBinding* binding=&entry->deformer->skinBindings[v];
        for(unsigned k=0;k<2*ANATOMY_MORPH_INFLUENCES;++k)if(binding->weights[k]>0) {
            int j=binding->joints[k];
            if(j>=0&&(size_t)j<entry->rig.skeleton.jointCount)
                AABB_ExpandPoint(&entry->jointBounds[j],entry->deformer->basePositions[v]);
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
        BuildEyesAndMouths(entry, &monster, &sdf) && BuildDeformer(entry, &monster,&sdf);
    if (ok) {
        entry->meshRadius = MeasureMeshBodyRadius(&entry->body);
        entry->meshLength = MeasureMeshLength(&entry->body);
        entry->calibrationLength = EcosystemAnatomy_ModelLength(&monster.phenotype);
        entry->axialTotalScale = monster.phenotype.axial.totalScale;
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
        bool ok = BuildCacheEntry(&state->cache[index]);
        pthread_mutex_lock(&state->mutex);
        state->cache[index].state = ok ? CACHE_PENDING : CACHE_FAILED;
        if (ok) ++state->buildCount;
        pthread_mutex_unlock(&state->mutex);
    }
}

static VisualSlot* FindSlot(VisualState* state, AIObjectId id) {
    for (size_t i = 0; i < state->slotCapacity; ++i) if (state->slots[i].used && state->slots[i].entityId == id) return &state->slots[i];
    return NULL;
}

static uint32_t StageFor(const EcosystemEntity* entity) {
    if(entity->anatomicalBody)return CREATURE_STAGE_ADULT;
    return entity->physiology.stage == LIFE_JUVENILE ? CREATURE_STAGE_JUVENILE : CREATURE_STAGE_ADULT;
}

static bool BuildSlotMonster(VisualSlot* slot) {
    const CreatureRecipe* base = CreatureRecipes_Find(slot->recipeId);
    const CreaturePhenotype* stage = base ? CreatureRecipe_GetStage(base, (CreatureStage)slot->stage) : NULL;
    if (!base || !stage) return false;
    CreatureRecipe recipe;
    CreaturePhenotype phenotype;
    if (!VisualVariation_ApplyBucket(base, stage, slot->bucket, slot->profile.pigmentSeed, &recipe, &phenotype)) return false;
    phenotype.surface = slot->surface;
    slot->monster = Monster_Create();
    if (!Creature_BuildMonster(&slot->monster, &recipe, &phenotype)) {
        Monster_Free(&slot->monster);
        return false;
    }
    slot->monsterReady = true;
    if (slot->monster.animation) slot->previousPose = slot->monster.animation->pose;
    return true;
}

static void TeardownSlot(VisualState* state, VisualSlot* slot) {
    if (!slot->used) return;
    if (slot->monsterReady) Monster_Free(&slot->monster);
    memset(slot, 0, sizeof(*slot));
    if (state->slotCount) --state->slotCount;
}

static void RefreshSlotCalibration(VisualState* state, VisualSlot* slot) {
    slot->meshRadius = 0.0f;
    slot->meshLength = 0.0f;
    slot->calibrationLength = 0.0f;
    slot->axialTotalScale = 0.0f;
    if(slot->monsterReady)slot->calibrationLength=EcosystemAnatomy_ModelLength(&slot->monster.phenotype);
    EcosystemVisualCacheKey key = {.recipeId = slot->recipeId, .stage = slot->stage,
        .morphologyBucket = slot->bucket, .tier = slot->tier};
    pthread_mutex_lock(&state->mutex);
    size_t index = CacheFindLocked(state, key);
    if (index < ECOSYSTEM_VISUAL_CACHE_CAPACITY && state->cache[index].state == CACHE_READY) {
        slot->meshRadius = state->cache[index].meshRadius;
        slot->meshLength = state->cache[index].meshLength;
        slot->calibrationLength = slot->monsterReady?EcosystemAnatomy_ModelLength(&slot->monster.phenotype):state->cache[index].calibrationLength;
        slot->axialTotalScale = state->cache[index].axialTotalScale;
    }
    pthread_mutex_unlock(&state->mutex);
}

static float CalibratedSlotScale(const VisualSlot* slot) {
    /* La referencia axial excluye la cola y no cambia al seleccionar otro LOD. */
    float calibrationFactor = slot->calibrationLength > 1e-4f ? 2.05f / slot->calibrationLength : 1.0f;
    return slot->profile.renderScale * calibrationFactor * fmaxf(0.01f, slot->developmentScale);
}

static void RebindSlot(VisualState* state, VisualSlot* slot, const EcosystemEntity* entity, uint32_t slotIndex) {
    bool wasUsed = slot->used;
    if (slot->used && slot->monsterReady) Monster_Free(&slot->monster);
    memset(slot, 0, sizeof(*slot));
    slot->used = true;
    slot->seen = true;
    slot->slotIndex = slotIndex;
    slot->entityId = entity->id;
    slot->entitySeed = entity->seed;
    slot->recipeId = state->world->species[entity->speciesIndex].recipeId;
    slot->stage = StageFor(entity);
    slot->genome = entity->genome;
    slot->profile = VisualVariation_Profile(&entity->genome, entity->seed);
    slot->bucket = slot->profile.bucket;
    slot->tier = VISUAL_TIER_FAR;
    slot->requestedTier = VISUAL_TIER_FAR;
    slot->projection = (EcosystemVisualProjection){.projectedPixels = 0.0f, .visible = true};
    slot->position = entity->body.position;
    slot->previousPosition=slot->position;
    slot->yaw = entity->body.yaw;
    slot->previousYaw=slot->yaw;
    slot->speciesRadius = state->world->species[entity->speciesIndex].radius;
    slot->developmentScale = entity->developmentScale;
    RefreshSlotCalibration(state, slot);
    slot->scale = CalibratedSlotScale(slot);
    slot->maturity = entity->physiology.stage == LIFE_ADULT ? 1.0f : .65f;
    VisualVariation_Surface(CreatureRecipes_Find(slot->recipeId), CreatureRecipe_GetStage(CreatureRecipes_Find(slot->recipeId), (CreatureStage)slot->stage),
        &slot->profile, slot->maturity, &slot->surface);
    slot->surfaceRecipe = SurfaceRecipe_CompileScaled(&slot->surface, 1.0f);
    slot->animationClock = (float)(slotIndex % 6u) * .013f;
    (void)BuildSlotMonster(slot);
    RefreshSlotCalibration(state,slot);slot->scale=CalibratedSlotScale(slot);
    if (!wasUsed) ++state->slotCount;
}

static void Reconcile(VisualState* state) {
    for (size_t i = 0; i < state->slotCapacity; ++i) state->slots[i].seen = false;
    if (!state->world || !state->world->entities) {
        for (size_t i = 0; i < state->slotCapacity; ++i) TeardownSlot(state, &state->slots[i]);
        return;
    }
    for (unsigned index = 0; index < state->world->capacity; ++index) {
        const EcosystemEntity* entity = &state->world->entities[index];
        if (!entity->id) continue;
        VisualSlot* slot = FindSlot(state, entity->id);
        bool rebind = !slot || slot->slotIndex != index || slot->recipeId != state->world->species[entity->speciesIndex].recipeId ||
            slot->stage != StageFor(entity) || slot->entitySeed != entity->seed || slot->bucket != VisualVariation_Profile(&entity->genome, entity->seed).bucket;
        if (!slot) {
            for (size_t i = 0; i < state->slotCapacity; ++i) if (!state->slots[i].used) { slot = &state->slots[i]; break; }
        }
        if (!slot) continue;
        if (rebind) RebindSlot(state, slot, entity, index);
        else {
            slot->seen = true;
            slot->position = entity->body.position;
            slot->yaw = entity->body.yaw;
            slot->speciesRadius = state->world->species[entity->speciesIndex].radius;
            slot->developmentScale = entity->developmentScale;
            RefreshSlotCalibration(state, slot);
            slot->scale = CalibratedSlotScale(slot);
            slot->maturity = entity->physiology.stage == LIFE_ADULT ? 1.0f : .65f;
        }
        slot->action=entity->mind.action;
        slot->mouthTarget=entity->body.mouth;
        slot->oralActive=entity->mind.intent.interact || entity->mind.action==ACTION_ATTACK;
    }
    for (size_t i = 0; i < state->slotCapacity; ++i) if (state->slots[i].used && !state->slots[i].seen) TeardownSlot(state, &state->slots[i]);
}

static void ApplyLod(VisualState* state) {
    size_t visible = 0;
    for (size_t i = 0; i < state->slotCapacity; ++i) visible += state->slots[i].used && state->slots[i].projection.visible;
    for (size_t i = 0; i < state->slotCapacity; ++i) {
        VisualSlot* slot = &state->slots[i];
        if (!slot->used || slot->explicitTier) continue;
        float pixels = slot->projection.projectedPixels;
        if (slot->tier == VISUAL_TIER_FAR && pixels >= 128.0f) slot->tier = VISUAL_TIER_MID;
        else if (slot->tier == VISUAL_TIER_MID && pixels < 96.0f) slot->tier = VISUAL_TIER_FAR;
        else if (slot->tier == VISUAL_TIER_MID && pixels >= 320.0f && visible <= 64u) slot->tier = VISUAL_TIER_NEAR;
        else if (slot->tier == VISUAL_TIER_NEAR && pixels < 256.0f) slot->tier = VISUAL_TIER_MID;
        if (visible > 64u && slot->tier == VISUAL_TIER_NEAR) slot->tier = VISUAL_TIER_MID;
    }
    if (visible > 64u) {
        size_t mid = 0;
        for (size_t i = 0; i < state->slotCapacity; ++i) if (state->slots[i].used && state->slots[i].projection.visible &&
            !state->slots[i].explicitTier && state->slots[i].tier == VISUAL_TIER_MID) ++mid;
        while (mid > 8u) {
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

static void PublishOne(VisualState* state) {
    pthread_mutex_lock(&state->mutex);
    for (size_t i = 0; i < ECOSYSTEM_VISUAL_CACHE_CAPACITY; ++i) if (state->cache[i].state == CACHE_PENDING) {
        state->cache[i].state = CACHE_READY;
        pthread_mutex_unlock(&state->mutex);
        return;
    }
    pthread_mutex_unlock(&state->mutex);
}

bool EcosystemVisual_Init(EcosystemVisual* visual, EcosystemWorld* world) {
    if (!visual || !world || world->capacity > ECOSYSTEM_VISUAL_MAX_SLOTS) return false;
    memset(visual, 0, sizeof(*visual));
    VisualState* state = calloc(1, sizeof(*state));
    if (!state) return false;
    state->world = world;
    state->animationSeconds=world->clock.seconds;
    state->slotCapacity = ECOSYSTEM_VISUAL_MAX_SLOTS;
    state->slots = calloc(state->slotCapacity, sizeof(*state->slots));
    state->cache = calloc(ECOSYSTEM_VISUAL_CACHE_CAPACITY, sizeof(*state->cache));
    state->queue = calloc(ECOSYSTEM_VISUAL_QUEUE_CAPACITY, sizeof(*state->queue));
    state->fallback = Mesh_Create();
    bool ok = state->slots && state->cache && state->queue &&
        PrimitiveMesh_GenerateUVSphere(&state->fallback, Vec3_Zero(), .5f, 12, 6, COLOR_WHITE) &&
        pthread_mutex_init(&state->mutex, NULL) == 0 && pthread_cond_init(&state->condition, NULL) == 0;
    if (!ok) {
        Mesh_Free(&state->fallback); free(state->slots); free(state->cache); free(state->queue); free(state); return false;
    }
    if (pthread_create(&state->worker, NULL, WorkerMain, state) != 0) {
        pthread_cond_destroy(&state->condition); pthread_mutex_destroy(&state->mutex);
        Mesh_Free(&state->fallback); free(state->slots); free(state->cache); free(state->queue); free(state); return false;
    }
    state->workerStarted = true;
    visual->state = state;
    return EcosystemVisual_Sync(visual);
}

bool EcosystemVisual_BindWorld(EcosystemVisual* visual, EcosystemWorld* world) {
    if (!visual || !visual->state || !world || world->capacity > ECOSYSTEM_VISUAL_MAX_SLOTS) return false;
    VisualState* state = visual->state;
    for (size_t i = 0; i < state->slotCapacity; ++i) TeardownSlot(state, &state->slots[i]);
    state->world = world;
    state->hasWorldClock = false;
    state->lastWorldTick = 0;
    return EcosystemVisual_Sync(visual);
}

bool EcosystemVisual_Sync(EcosystemVisual* visual) {
    if (!visual || !visual->state) return false;
    VisualState* state = visual->state;
    if (state->world && state->hasWorldClock && state->world->clock.tick < state->lastWorldTick) {
        for (size_t i = 0; i < state->slotCapacity; ++i) TeardownSlot(state, &state->slots[i]);
    }
    Reconcile(state);
    if (state->world) {
        state->lastWorldTick = state->world->clock.tick;
        state->hasWorldClock = true;
    }
    return EcosystemVisual_Validate(visual);
}

static Vector3 RotateYaw(Vector3 v,float yaw) {
    float c=cosf(yaw),s=sinf(yaw);
    return Vec3_Create(c*v.x+s*v.z,v.y,-s*v.x+c*v.z);
}

static Vector3 SlotCenter(const VisualSlot* slot) {
    if(!slot->monsterReady||!slot->monster.animation)return Vec3_Zero();
    return slot->monster.animation->animator.locomotion.supportCenter;
}

static void ApplyEcologicalAxialPose(const Rig* rig,SkeletonPose* pose,void* context) {
    const EcosystemAnatomy* body=context;if(!body)return;
    for(unsigned k=0;k<body->profile.count;++k) {
        int j=body->profile.joints[k].index;pose->joints[j].localRotation=body->pose.joints[j].localRotation;
    }
    for(size_t k=0;k<rig->spineCount;++k) {
        int j=rig->spine[k];pose->joints[j].localRotation=body->pose.joints[j].localRotation;
    }
}

static void AnimateSlot(VisualSlot* slot,const EcosystemEntity* entity, float deltaSeconds) {
    if(!slot->monsterReady||!slot->monster.animation||deltaSeconds<=0)return;
    MonsterAnimation* animation=slot->monster.animation;
    if(entity&&entity->anatomicalBody&&slot->anatomyGeneration!=entity->anatomicalBody->generation) {
        const EcosystemAnatomy* body=entity->anatomicalBody;
        slot->monster.anatomyGraph=body->graph;
        slot->monster.phenotype=body->phenotype;
        GaitProfile gait=slot->monster.animation->animator.gait;
        if(!slot->anatomyGeneration) {
            MonsterAnimation_Free(slot->monster.animation);slot->monster.animation=NULL;
            gait=slot->monster.recipe.gait==CREATURE_GAIT_FELINE_STALK?GaitPreset_FelineStalk(&body->rig):GaitPreset_BovineWalk(&body->rig);
        }
        MonsterAnimation_Configure(&slot->monster,&body->rig,gait);
        slot->anatomyGeneration=body->generation;
        slot->maturity=body->phenotype.development.colorMaturity;
        VisualVariation_Surface(&slot->monster.recipe,&body->phenotype,&slot->profile,slot->maturity,&slot->surface);
        slot->surfaceRecipe=SurfaceRecipe_CompileScaled(&slot->surface,body->phenotype.axial.totalScale);
        animation=slot->monster.animation;
        slot->calibrationLength=body->modelLength;slot->scale=CalibratedSlotScale(slot);
    }
    ProceduralAnimator* a=&animation->animator;
    a->applyAxialPose=ApplyEcologicalAxialPose;
    a->axialContext=entity?entity->anatomicalBody:NULL;
    const Rig* rig=&animation->rig;
    float scale=fmaxf(.001f,slot->scale);
    Vector3 displacement=RotateYaw(Vec3_Sub(slot->position,slot->previousPosition),-slot->yaw);
    Vector3 velocity=Vec3_Scale(displacement,1.0f/(scale*deltaSeconds));
    velocity.y=0;
    float yawDelta=slot->yaw-slot->previousYaw;
    yawDelta=atan2f(sinf(yawDelta),cosf(yawDelta));
    Locomotion_TurnFrame(&a->locomotion,yawDelta);
    a->angularVelocity=yawDelta/deltaSeconds;
    a->desiredVelocity=velocity;
    slot->previousPosition=slot->position;slot->previousYaw=slot->yaw;
    slot->actionClock+=deltaSeconds;
    bool agile=slot->monster.recipe.gait==CREATURE_GAIT_FELINE_STALK;
    float running=slot->action==ACTION_CHASE||slot->action==ACTION_ESCAPE||slot->action==ACTION_ATTACK?1:0;
    slot->runBlend+=(running-slot->runBlend)*(1-expf(-7*deltaSeconds));
    slot->restBlend+=((slot->action==ACTION_REST?1.0f:0)-slot->restBlend)*(1-expf(-3*deltaSeconds));
    float contactSpeed=Vec3_Length(velocity);
    SkeletonPose rest;SkeletonPose_Init(&rest,&rig->skeleton);
    for(size_t leg=0;leg<rig->limbCount;++leg) {
        Vector3 rel=Vec3_Sub(rest.joints[rig->limbs[leg].endEffectorJoint].worldPosition,a->locomotion.supportCenter);
        Vector3 footVelocity=Vec3_Add(velocity,Vec3_Create(a->angularVelocity*rel.z,0,-a->angularVelocity*rel.x));
        contactSpeed=fmaxf(contactSpeed,Vec3_Length(footVelocity));
    }
    float forwardFraction=fminf(1,Vec3_Length(velocity)/fmaxf(.01f,contactSpeed));
    a->gait=GaitPreset_QuadrupedMotion(rig,agile,contactSpeed,slot->runBlend*forwardFraction);
    if(entity&&entity->anatomicalBody)a->gait.supportCompression+=entity->anatomicalBody->crouch;
    for(size_t i=0;i<rig->limbCount;++i)a->locomotion.feet[i].phaseOffset=a->gait.limbPhase[i];
    a->oralReachActive=slot->oralActive&&!(entity&&entity->anatomicalBody);
    a->oralReachTarget=Vec3_Add(SlotCenter(slot),Vec3_Scale(
        RotateYaw(Vec3_Sub(slot->mouthTarget,slot->position),-slot->yaw),1.0f/scale));
    a->interactionTarget=(InteractionTarget){.valid=slot->oralActive,.position=a->oralReachTarget,
        .normal={0,1,0},.approach={0,0,1},.positionTolerance=.03f/scale,.orientationTolerance=1.57f};
    float jaw=slot->action==ACTION_ATTACK?.65f*(.5f+.5f*sinf(slot->actionClock*15)):
        slot->action==ACTION_EAT?.10f*(.5f+.5f*sinf(slot->actionClock*9)):
        slot->action==ACTION_DRINK?.045f*(.5f+.5f*sinf(slot->actionClock*12)):0;
    a->mouthOpen+=(jaw-a->mouthOpen)*(1-expf(-12*deltaSeconds));
    slot->previousPose=animation->pose;
    /* Subpasos de pose y transporte mantienen el mismo origen local en cada
     * frame. El mundo ecológico es el único que desplaza realmente al animal. */
    float remaining=fminf(deltaSeconds,.25f);
    while(remaining>1e-6f) {
        float dt=fminf(remaining,1.f/60);
        Vector3 before=a->locomotion.bodyPosition;
        MonsterAnimation_Update(&slot->monster,dt);
        Vector3 travel=Vec3_Sub(a->locomotion.bodyPosition,before);travel.y=0;
        a->locomotion.bodyPosition=Vec3_Sub(a->locomotion.bodyPosition,travel);
        for(size_t i=0;i<rig->limbCount;++i) {
            FootState* f=&a->locomotion.feet[i];
            f->plantedPosition=Vec3_Sub(f->plantedPosition,travel);
            f->targetPosition=Vec3_Sub(f->targetPosition,travel);
            f->swingStart=Vec3_Sub(f->swingStart,travel);
            f->swingTarget=Vec3_Sub(f->swingTarget,travel);
        }
        for(size_t j=0;j<rig->skeleton.jointCount;++j)if(rig->skeleton.joints[j].parentIndex<0)
            animation->pose.joints[j].localPosition=Vec3_Sub(animation->pose.joints[j].localPosition,travel);
        remaining-=dt;
    }
    /* Respiración y descanso son pequeños desplazamientos continuos de la raíz;
     * el descenso queda acotado por la holgura anatómica de alcance. */
    float breath=.003f*slot->monster.phenotype.axial.totalScale*sinf(slot->actionClock*2.2f);
    float lower=entity&&entity->anatomicalBody?0:slot->restBlend*a->reachProfile.maximumCrouch*.45f;
    for(size_t j=0;j<rig->skeleton.jointCount;++j)if(rig->skeleton.joints[j].parentIndex<0)
        animation->pose.joints[j].localPosition.y+=breath-lower;
    SkeletonPose_UpdateWorldTransforms(&animation->pose,&rig->skeleton);
    slot->poseAlpha=1;

}

bool EcosystemVisual_Update(EcosystemVisual* visual, float deltaSeconds) {
    if (!visual || !visual->state) return false;
    VisualState* state = visual->state;
    if (!EcosystemVisual_Sync(visual)) return false;
    state->frameEnqueues = 0;
    PublishOne(state);
    state->lodClock += fmaxf(0.0f, deltaSeconds);
    if (state->lodClock >= .1f) {
        state->lodClock = fmodf(state->lodClock, .1f);
        ApplyLod(state);
    }
    float animationDelta=(float)fmax(0,state->world->clock.seconds-state->animationSeconds);
    state->animationSeconds=state->world->clock.seconds;
    for (size_t i = 0; i < state->slotCapacity; ++i) if (state->slots[i].used) {
        const EcosystemEntity* e=EcosystemWorld_Get(state->world,state->slots[i].entityId);
        AnimateSlot(&state->slots[i],e, animationDelta);
    }
    for (size_t i = 0; i < state->slotCapacity; ++i) {
        VisualSlot* slot = &state->slots[i];
        if (!slot->used || !slot->projection.visible || state->frameEnqueues) continue;
        EcosystemVisualCacheKey key = {.recipeId = slot->recipeId, .stage = slot->stage,
            .morphologyBucket = slot->bucket, .tier = slot->tier};
        pthread_mutex_lock(&state->mutex);
        QueueKeyLocked(state, key, false);
        pthread_mutex_unlock(&state->mutex);
    }
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
    for (size_t i = 0; i < state->slotCapacity; ++i) TeardownSlot(state, &state->slots[i]);
    for (size_t i = 0; i < ECOSYSTEM_VISUAL_CACHE_CAPACITY; ++i) FreeCacheEntry(&state->cache[i]);
    Mesh_Free(&state->fallback);
    pthread_cond_destroy(&state->condition); pthread_mutex_destroy(&state->mutex);
    free(state->slots); free(state->cache); free(state->queue); free(state);
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
        EcosystemVisualCacheKey key = {.recipeId = slot->recipeId, .stage = slot->stage,
            .morphologyBucket = slot->bucket, .tier = slot->tier};
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
    const VisualState* state = visual->state;
    size_t written = 0;
    pthread_mutex_lock((pthread_mutex_t*)&state->mutex);
    for (size_t i = 0; i < state->slotCapacity && written < capacity; ++i) {
        const VisualSlot* slot = &state->slots[i];
        if (!slot->used || !slot->projection.visible) continue;
        EcosystemVisualDrawPacket* packet = &packets[written++];
        memset(packet, 0, sizeof(*packet));
        packet->slotIndex = slot->slotIndex; packet->entityId = slot->entityId; packet->tier = slot->tier;
        packet->bodyMesh = &state->fallback; packet->surfaceRecipe = slot->surfaceRecipe;
        size_t index = CacheFindLocked(state, (EcosystemVisualCacheKey){.recipeId = slot->recipeId, .stage = slot->stage,
            .morphologyBucket = slot->bucket, .tier = slot->tier});
        if(index==ECOSYSTEM_VISUAL_CACHE_CAPACITY||state->cache[index].state!=CACHE_READY) {
            for(size_t candidate=0;candidate<ECOSYSTEM_VISUAL_CACHE_CAPACITY;++candidate) {
                const CacheEntry* e=&state->cache[candidate];
                if(e->state==CACHE_READY&&e->key.recipeId==slot->recipeId&&e->key.stage==slot->stage&&e->key.morphologyBucket==slot->bucket) {
                    index=candidate;break;
                }
            }
        }
        if (index < ECOSYSTEM_VISUAL_CACHE_CAPACITY && state->cache[index].state == CACHE_READY) {
            const CacheEntry* entry = &state->cache[index];
            packet->tier=entry->key.tier;
            packet->ready = true; packet->bodyMesh = &entry->body;
            ++((VisualState*)state)->cacheHits;
            packet->headMesh = entry->head.vertexCount ? &entry->head : NULL;
            packet->eyes = entry->eyes; packet->eyeCount = entry->eyeCount;
            packet->mouths = entry->mouths; packet->mouthCount = entry->mouthCount;
            packet->deformer = entry->deformer; packet->skeleton = &entry->rig.skeleton;
            packet->bodyJointBounds=entry->jointBounds;
            packet->pose = slot->monsterReady && slot->monster.animation ? &slot->monster.animation->pose : &entry->restPose;
            packet->restPose=&entry->restPose;
            packet->headJoint=entry->rig.headJoint;packet->jawJoint=entry->rig.jawJoint;packet->oralPoint=entry->rig.oralPoint;
            if(slot->monsterReady&&slot->monster.animation) {
                packet->skeleton=&slot->monster.animation->rig.skeleton;
                packet->oralPoint=slot->monster.animation->rig.oralPoint;
                packet->targetGraph=&slot->monster.anatomyGraph;
            }
            packet->headScale=Vec3_Create(1,1,1);
            if(packet->targetGraph) {
                AnatomyId id=entry->rig.skeleton.joints[entry->rig.headJoint].anatomyId;
                const AnatomyNode* from=AnatomyGraph_FindNode(&entry->deformer->refGraph,id);
                const AnatomyNode* to=AnatomyGraph_FindNode(packet->targetGraph,id);
                if(from&&to)packet->headScale=Vec3_Create(to->widthRadius/from->widthRadius,to->heightRadius/from->heightRadius,to->widthRadius/from->widthRadius);
                /* La escala longitudinal y vertical conserva el mismo contacto
                 * oral que la anatomía actual, también en cabezas neonatales. */
                Vector3 a=entry->rig.oralPoint,b=packet->oralPoint;
                if(fabsf(a.y)>1e-6f&&b.y/a.y>0)packet->headScale.y=b.y/a.y;
                if(fabsf(a.z)>1e-6f&&b.z/a.z>0)packet->headScale.z=b.z/a.z;
            }
            packet->jawScale=packet->headScale;
            packet->eyeScale=1;
            const CreatureRecipe* base=CreatureRecipes_Find(slot->recipeId);
            if(slot->monsterReady&&base&&base->adult.eyes.size>0)
                packet->eyeScale=slot->monster.phenotype.eyes.size/base->adult.eyes.size;
            if(slot->monsterReady&&slot->monster.animation&&entry->rig.jawJoint>=0) {
                SkeletonPose current;SkeletonPose_Init(&current,packet->skeleton);
                Vector3 from=Vec3_Sub(Vec3_Add(entry->restPose.joints[packet->headJoint].worldPosition,entry->rig.oralPoint),entry->restPose.joints[packet->jawJoint].worldPosition);
                Vector3 to=Vec3_Sub(Vec3_Add(current.joints[packet->headJoint].worldPosition,packet->oralPoint),current.joints[packet->jawJoint].worldPosition);
                if(fabsf(from.y)>1e-6f&&to.y/from.y>0)packet->jawScale.y=to.y/from.y;
                if(fabsf(from.z)>1e-6f&&to.z/from.z>0)packet->jawScale.z=to.z/from.z;
            }
            packet->previousPose = slot->monsterReady && slot->monster.animation ? &slot->previousPose : &entry->restPose;
        }
        packet->poseAlpha = slot->poseAlpha; packet->position = slot->position;
        packet->yaw = slot->yaw; packet->scale = slot->scale;
        packet->measuredMeshRadius = slot->meshRadius;
        packet->meshRadius = slot->calibrationLength > 1e-4f ? slot->speciesRadius * slot->calibrationLength / 2.05f : 0.0f;
        packet->meshLength = slot->meshLength;
        packet->calibrationLength = slot->calibrationLength;
        packet->axialTotalScale = slot->axialTotalScale;
        packet->calibrationFactor = slot->calibrationLength > 1e-4f ? 2.05f / slot->calibrationLength : 1.0f;
        packet->gaitCadence = slot->profile.gaitCadence;
        packet->postureLean = slot->profile.postureLean;
        /* ProceduralAnimator no tiene fase ni amplitud de escaneo cefálico;
         * se publica el rasgo para un renderizador que sí pueda expresarlo. */
        packet->headScanRate = slot->profile.headScanRate;
        if (index < ECOSYSTEM_VISUAL_CACHE_CAPACITY && state->cache[index].state == CACHE_READY) {
            const CacheEntry* entry = &state->cache[index];
            packet->measuredMeshRadius = entry->meshRadius;
            packet->meshRadius=0;
            const AnatomyGraph* dimensions=packet->targetGraph?packet->targetGraph:&entry->deformer->refGraph;
            for(size_t node=0;node<dimensions->nodeCount;++node)if(dimensions->nodes[node].region==ANATOMY_REGION_TRUNK)
                packet->meshRadius=fmaxf(packet->meshRadius,dimensions->nodes[node].widthRadius);
            packet->meshLength = entry->meshLength;
            packet->calibrationLength = slot->monsterReady?EcosystemAnatomy_ModelLength(&slot->monster.phenotype):entry->calibrationLength;
            packet->axialTotalScale = entry->axialTotalScale;
            packet->calibrationFactor = 2.05f / packet->calibrationLength;
            packet->scale = slot->profile.renderScale * packet->calibrationFactor * fmaxf(0.01f, slot->developmentScale);
            packet->position=Vec3_Sub(slot->position,RotateYaw(Vec3_Scale(SlotCenter(slot),packet->scale),slot->yaw));
            if(packet->pose&&packet->headJoint>=0) {
                const JointPose* head=&packet->pose->joints[packet->headJoint];
                Vector3 reference=entry->rig.oralPoint;
                reference=(Vector3){reference.x*packet->headScale.x,reference.y*packet->headScale.y,reference.z*packet->headScale.z};
                Vector3 oral=Vec3_Add(head->worldPosition,Quat_RotateVector(head->worldRotation,reference));
                packet->renderedMouth=Vec3_Add(packet->position,RotateYaw(Vec3_Scale(oral,packet->scale),slot->yaw));
                packet->mouthError=Vec3_Distance(packet->renderedMouth,slot->mouthTarget);
            }
            if(slot->monsterReady&&slot->monster.animation)for(size_t leg=0;leg<slot->monster.animation->rig.limbCount;++leg) {
                const MonsterAnimation* animation=slot->monster.animation;
                Vector3 actual=packet->pose->joints[animation->rig.limbs[leg].endEffectorJoint].worldPosition;
                const FootState* foot=&animation->animator.locomotion.feet[leg];
                float error=Vec3_Distance(actual,foot->targetPosition)*packet->scale;
                packet->footError=fmaxf(packet->footError,error);
                if(foot->phase==FOOT_STANCE)packet->stanceFootError=fmaxf(packet->stanceFootError,error);
            }
        }
    }
    pthread_mutex_unlock((pthread_mutex_t*)&state->mutex);
    return written;
}

bool EcosystemVisual_DeformBody(const EcosystemVisualDrawPacket* packet,Mesh* output) {
    if(!packet||!output||!packet->ready||!packet->deformer||!packet->bodyMesh)return false;
    const Mesh* base=packet->bodyMesh;
    if(!Mesh_ReserveVertices(output,base->vertexCount)||!Mesh_ReserveIndices(output,base->indexCount))return false;
    memcpy(output->vertices,base->vertices,base->vertexCount*sizeof(*base->vertices));
    memcpy(output->indices,base->indices,base->indexCount*sizeof(*base->indices));
    output->vertexCount=base->vertexCount;output->indexCount=base->indexCount;
    output->surfaceRecipe=base->surfaceRecipe;output->hasSurface=base->hasSurface;
    AnatomyPosePalette palette;
    if(!AnatomyDeformer_BuildMorphPosePalette(packet->deformer,packet->skeleton,packet->pose,packet->targetGraph,&palette))return false;
    if(packet->headJoint>=0&&!AnatomyDeformer_ScaleJointPalette(packet->deformer,packet->pose,packet->headJoint,packet->headScale,&palette))return false;
    for(size_t v=0;v<base->vertexCount;++v)if(!AnatomyDeformer_EvaluatePoseVertex(packet->deformer,&palette,v,
        &output->vertices[v].position,&output->vertices[v].normal))return false;
    Mesh_MarkGeometryChanged(output);return true;
}

static bool PickMesh(const Mesh* mesh,Vector3 origin,Vector3 direction,float nearDistance,float* nearest) {
    if(!mesh)return false;
    bool hit=false;
    for(size_t i=0;i+2<mesh->indexCount;i+=3) {
        Vector3 a=mesh->vertices[mesh->indices[i]].position;
        Vector3 ab=Vec3_Sub(mesh->vertices[mesh->indices[i+1]].position,a);
        Vector3 ac=Vec3_Sub(mesh->vertices[mesh->indices[i+2]].position,a);
        Vector3 cross=Vec3_Cross(direction,ac);
        float determinant=Vec3_Dot(ab,cross);
        if(fabsf(determinant)<1e-9f)continue;
        Vector3 delta=Vec3_Sub(origin,a);
        float u=Vec3_Dot(delta,cross)/determinant;
        if(u<0||u>1)continue;
        Vector3 q=Vec3_Cross(delta,ab);
        float v=Vec3_Dot(direction,q)/determinant;
        if(v<0||u+v>1)continue;
        float distance=Vec3_Dot(ac,q)/determinant;
        if(distance>=nearDistance&&distance<*nearest){*nearest=distance;hit=true;}
    }
    return hit;
}

static bool PickBodyBounds(const EcosystemVisualDrawPacket* p,Vector3 origin,Vector3 direction,
    float nearDistance,float farDistance) {
    if(!p->bodyJointBounds)return true;
    AnatomyPosePalette palette;
    if(!AnatomyDeformer_BuildMorphPosePalette(p->deformer,p->skeleton,p->pose,p->targetGraph,&palette))return true;
    if(p->headJoint>=0&&!AnatomyDeformer_ScaleJointPalette(p->deformer,p->pose,p->headJoint,p->headScale,&palette))return true;
    AABB3D bounds=AABB_Empty();
    /* El skinning mezcla pesos positivos normalizados: la unión convexa de
     * las cajas transformadas contiene también todos sus vértices mezclados. */
    for(size_t j=0;j<palette.jointCount;++j) {
        AABB3D box=p->bodyJointBounds[j];
        if(box.start.x>box.end.x)continue;
        for(unsigned corner=0;corner<8;++corner) {
            float v[]={corner&1?box.end.x:box.start.x,corner&2?box.end.y:box.start.y,corner&4?box.end.z:box.start.z};
            float q[3];
            for(unsigned axis=0;axis<3;++axis) {
                const float* row=palette.rows[j][axis];
                q[axis]=row[0]*v[0]+row[1]*v[1]+row[2]*v[2]+row[3];
            }
            AABB_ExpandPoint(&bounds,Vec3_Create(q[0],q[1],q[2]));
        }
    }
    for(unsigned axis=0;axis<3;++axis) {
        float o=MeshAxisCoordinate(origin,axis),d=MeshAxisCoordinate(direction,axis);
        float lo=MeshAxisCoordinate(bounds.start,axis)-1e-4f,hi=MeshAxisCoordinate(bounds.end,axis)+1e-4f;
        if(fabsf(d)<1e-8f){if(o<lo||o>hi)return false;continue;}
        float a=(lo-o)/d,b=(hi-o)/d;
        nearDistance=fmaxf(nearDistance,fminf(a,b));farDistance=fminf(farDistance,fmaxf(a,b));
        if(nearDistance>farDistance)return false;
    }
    return true;
}

/* Invierte exactamente el marco afín usado para cabeza y mandíbula en GL.
 * La dirección conserva su longitud para medir el parámetro del rayo original. */
static void PickJointRay(const EcosystemVisualDrawPacket* p,int joint,Vector3 scale,
    Vector3* origin,Vector3* direction) {
    if(!p->restPose||!p->pose||joint<0||(size_t)joint>=p->pose->jointCount||
        (size_t)joint>=p->restPose->jointCount)return;
    Quaternion rotation=Quat_Multiply(p->pose->joints[joint].worldRotation,
        Quat_Inverse(p->restPose->joints[joint].worldRotation));
    *origin=Quat_RotateVector(Quat_Inverse(rotation),Vec3_Sub(*origin,p->pose->joints[joint].worldPosition));
    *direction=Quat_RotateVector(Quat_Inverse(rotation),*direction);
    origin->x/=scale.x;origin->y/=scale.y;origin->z/=scale.z;
    direction->x/=scale.x;direction->y/=scale.y;direction->z/=scale.z;
    *origin=Vec3_Add(*origin,p->restPose->joints[joint].worldPosition);
}

AIObjectId EcosystemVisual_PickRay(const EcosystemVisualDrawPacket* packets,size_t count,
    Vector3 origin,Vector3 direction,float nearDistance,float farDistance) {
    if(!packets||!isfinite(Vec3_LengthSq(origin))||!isfinite(Vec3_LengthSq(direction))||
        Vec3_LengthSq(direction)<1e-12f||!isfinite(nearDistance)||!isfinite(farDistance)||
        nearDistance<0||farDistance<=nearDistance)return 0;
    direction=Vec3_Normalize(direction);
    AIObjectId selected=0;
    Mesh deformed=Mesh_Create();
    for(size_t i=0;i<count;++i) {
        const EcosystemVisualDrawPacket* p=&packets[i];
        if(!p->entityId||!isfinite(p->scale)||p->scale<=0)continue;
        Vector3 localOrigin=Vec3_Scale(RotateYaw(Vec3_Sub(origin,p->position),-p->yaw),1/p->scale);
        Vector3 localDirection=RotateYaw(direction,-p->yaw);
        float nearest=farDistance/p->scale,nearLocal=nearDistance/p->scale;
        const Mesh* body=p->bodyMesh;
        if(p->ready&&PickBodyBounds(p,localOrigin,localDirection,nearLocal,nearest)) {
            if(!EcosystemVisual_DeformBody(p,&deformed))continue;
            body=&deformed;
        } else if(p->ready)body=NULL;
        bool hit=PickMesh(body,localOrigin,localDirection,nearLocal,&nearest);
        Vector3 headOrigin=localOrigin,headDirection=localDirection;
        PickJointRay(p,p->headJoint,p->headScale,&headOrigin,&headDirection);
        hit=PickMesh(p->headMesh,headOrigin,headDirection,nearLocal,&nearest)||hit;
        Vector3 jawOrigin=localOrigin,jawDirection=localDirection;
        PickJointRay(p,p->jawJoint,p->jawScale,&jawOrigin,&jawDirection);
        for(size_t m=0;m<p->mouthCount;++m) {
            hit=PickMesh(&p->mouths[m].jaw,jawOrigin,jawDirection,nearLocal,&nearest)||hit;
            hit=PickMesh(&p->mouths[m].hinge,headOrigin,headDirection,nearLocal,&nearest)||hit;
        }
        for(size_t e=0;e<p->eyeCount;++e)if(p->eyeScale>0) {
            Vector3 eyeOrigin=Vec3_Add(p->eyes[e].center,Vec3_Scale(Vec3_Sub(headOrigin,p->eyes[e].center),1/p->eyeScale));
            Vector3 eyeDirection=Vec3_Scale(headDirection,1/p->eyeScale);
            hit=PickMesh(&p->eyes[e].globe,eyeOrigin,eyeDirection,nearLocal,&nearest)||hit;
        }
        if(hit){farDistance=nearest*p->scale;selected=p->entityId;}
    }
    Mesh_Free(&deformed);
    return selected;
}

bool EcosystemVisual_Validate(const EcosystemVisual* visual) {
    if (!visual || !visual->state) return false;
    const VisualState* state = visual->state;
    if (!state->world || !state->world->entities) return state->slotCount == 0;
    size_t live = 0;
    for (unsigned i = 0; i < state->world->capacity; ++i) if (state->world->entities[i].id) {
        ++live;
        size_t matches = 0;
        for (size_t j = 0; j < state->slotCapacity; ++j) if (state->slots[j].used && state->slots[j].slotIndex == i &&
            state->slots[j].entityId == state->world->entities[i].id) ++matches;
        if (matches != 1u) return false;
    }
    if (live != state->slotCount) return false;
    for (size_t i = 0; i < ECOSYSTEM_VISUAL_CACHE_CAPACITY; ++i) if (state->cache[i].state != CACHE_EMPTY) {
        if (!KeyValid(state->cache[i].key)) return false;
        for (size_t j = i + 1u; j < ECOSYSTEM_VISUAL_CACHE_CAPACITY; ++j)
            if (state->cache[j].state != CACHE_EMPTY && KeyEqual(state->cache[i].key, state->cache[j].key)) return false;
    }
    return true;
}

size_t EcosystemVisual_SlotCount(const EcosystemVisual* visual) {
    return visual && visual->state ? ((const VisualState*)visual->state)->slotCount : 0;
}

size_t EcosystemVisual_CacheCount(const EcosystemVisual* visual) {
    return visual && visual->state ? ((const VisualState*)visual->state)->cacheCount : 0;
}

size_t EcosystemVisual_BuildCount(const EcosystemVisual* visual) {
    if(!visual||!visual->state)return 0;
    VisualState* state=visual->state;
    pthread_mutex_lock(&state->mutex);size_t count=state->buildCount;pthread_mutex_unlock(&state->mutex);
    return count;
}

size_t EcosystemVisual_CacheHitCount(const EcosystemVisual* visual) {
    return visual && visual->state ? ((const VisualState*)visual->state)->cacheHits : 0;
}
