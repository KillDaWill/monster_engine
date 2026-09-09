#include "MonsterVisualAsync.h"
#include "LizardMorph.h"
#include "SDFSamplingPool.h"
#include "PrimitiveMesh.h"
#include "MathUtils.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

static double GetTimeMs(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

static uint64_t Fnv1a64Bytes(const void* data, size_t size, uint64_t hash) {
    const unsigned char* bytes = (const unsigned char*)data;
    for (size_t i = 0; i < size; ++i) {
        hash ^= (uint64_t)bytes[i];
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

static inline uint64_t HashFloat(float v, uint64_t hash) {
    return Fnv1a64Bytes(&v, sizeof(float), hash);
}

static inline uint64_t HashVector3(Vector3 v, uint64_t hash) {
    hash = HashFloat(v.x, hash);
    hash = HashFloat(v.y, hash);
    return HashFloat(v.z, hash);
}

static inline uint64_t HashBool(bool b, uint64_t hash) {
    unsigned char v = b ? 1 : 0;
    return Fnv1a64Bytes(&v, 1, hash);
}

static inline uint64_t HashColor(Color c, uint64_t hash) {
    hash = Fnv1a64Bytes(&c.r, 1, hash);
    hash = Fnv1a64Bytes(&c.g, 1, hash);
    hash = Fnv1a64Bytes(&c.b, 1, hash);
    return Fnv1a64Bytes(&c.a, 1, hash);
}

static inline uint64_t HashSizeT(size_t v, uint64_t hash) {
    return Fnv1a64Bytes(&v, sizeof(size_t), hash);
}

static uint64_t ComputeMonsterFingerprint(const Monster* monster, MonsterSDFConfig sdfConfig, MonsterVisualQualityTier tier) {
    uint64_t hash = 0xcbf29ce484222325ULL;

    hash = HashSizeT((size_t)tier, hash);

    /* Configuración SDF */
    hash = HashFloat(sdfConfig.bodySmoothness, hash);
    hash = HashFloat(sdfConfig.connectionSmoothness, hash);
    hash = HashFloat(sdfConfig.mouthSmoothness, hash);
    hash = HashFloat(sdfConfig.connectionRadiusFactor, hash);
    hash = HashFloat(sdfConfig.boundsPadding, hash);
    hash = HashBool(sdfConfig.enableConnectorPruning, hash);

    if (!monster) return hash;
    hash = HashBool(monster->hasHead,hash);
    if(monster->hasHead) hash=Fnv1a64Bytes(&monster->head.phenotype,sizeof(monster->head.phenotype),hash);
    hash=HashBool(monster->hasAnatomyGraph,hash);
    if(monster->hasAnatomyGraph) {
        uint64_t anatomy=AnatomyGraph_Fingerprint(&monster->anatomyGraph);
        hash=Fnv1a64Bytes(&anatomy,sizeof(anatomy),hash);
    }

    /* Partes del cuerpo */
    hash = HashSizeT(monster->bodyPartCount, hash);
    for (size_t i = 0; i < monster->bodyPartCount; ++i) {
        const BodyPart* part = &monster->bodyParts[i];
        hash = HashVector3(part->positionRender, hash);
        hash = HashFloat(part->widthRender, hash);
        hash = HashFloat(part->heightRender, hash);
        hash = HashFloat(part->lengthRender, hash);
        hash = HashSizeT(part->color.index, hash);
    }

    /* Bocas */
    hash = HashSizeT(monster->mouthCount, hash);
    for (size_t i = 0; i < monster->mouthCount; ++i) {
        const Mouth* mouth = &monster->mouths[i];
        hash = HashSizeT(mouth->bodyPartIndex, hash);
        hash = HashSizeT((size_t)mouth->shape,hash);
        hash = HashVector3(mouth->offset, hash);
        hash = HashVector3(mouth->rotation, hash);
        hash = HashVector3(mouth->scale, hash);
        hash = HashFloat(mouth->slitThickness, hash);
        hash = HashFloat(mouth->slitSoftness, hash);
        hash = HashFloat(mouth->cornerRadius, hash);
        hash = HashVector3(mouth->jawPivot, hash);
        hash = HashFloat(mouth->jawLength, hash);
        hash = HashFloat(mouth->jawWidth, hash);
        hash = HashFloat(mouth->jawThickness, hash);
        hash = HashFloat(mouth->jawRearMass, hash);
        hash = HashFloat(mouth->jawMuscle, hash);
        hash = HashFloat(mouth->maxJawAngle, hash);
        hash = HashFloat(mouth->hingeRadius, hash);
        hash = HashFloat(mouth->throatRadius, hash);
        hash = HashVector3(mouth->cranium, hash);
        hash = HashVector3(mouth->snout, hash);
        hash = HashVector3(mouth->cheeks, hash);
        hash = HashVector3(mouth->brows, hash);
        hash = HashColor(mouth->insideColor, hash);
    }

    /* Ojos */
    hash = HashSizeT(monster->eyeCount, hash);
    for (size_t i = 0; i < monster->eyeCount; ++i) {
        const Eye* eye = &monster->eyes[i];
        hash = HashSizeT(eye->bodyPartIndex, hash);
        hash = HashVector3(eye->offset, hash);
        hash = HashVector3(eye->rotation, hash);
        hash = HashVector3(eye->forward, hash);
        hash = HashVector3(eye->scale, hash);
        hash = HashFloat(eye->irisScale, hash);
        hash = HashFloat(eye->pupilScale, hash);
        hash = HashFloat(eye->pupilAspect, hash);
        hash = HashColor(eye->scleraColor, hash);
        hash = HashColor(eye->irisColor, hash);
        hash = HashColor(eye->pupilColor, hash);
    }

    /* Paleta de colores */
    hash = HashSizeT(monster->colorPalette.count, hash);
    for (size_t i = 0; i < monster->colorPalette.count; ++i) {
        hash = HashColor(monster->colorPalette.colors[i], hash);
    }

    return hash;
}

MonsterVisualAsyncConfig MonsterVisualAsync_DefaultConfig(void) {
    MonsterVisualAsyncConfig cfg;
    memset(&cfg, 0, sizeof(MonsterVisualAsyncConfig));

    cfg.interactiveMesherConfig = SDFMesher_DefaultConfig();
    cfg.interactiveMesherConfig.voxelSize = 0.12f;
    cfg.interactiveMesherConfig.maxCells = 250000;

    cfg.settledMesherConfig = SDFMesher_DefaultConfig();
    cfg.settledMesherConfig.voxelSize = 0.08f;
    cfg.settledMesherConfig.maxCells = 500000;

    cfg.morphMesherConfig = SDFMesher_DefaultConfig();
    cfg.morphMesherConfig.voxelSize = 0.095f;
    cfg.morphMesherConfig.maxCells = 300000;

    cfg.interactiveHeadMesherConfig = SDFMesher_DefaultConfig();
    cfg.interactiveHeadMesherConfig.voxelSize = 0.024f;
    cfg.interactiveHeadMesherConfig.maxCells = 900000;
    cfg.interactiveHeadMesherConfig.maxResolution = 256;

    cfg.settledHeadMesherConfig = SDFMesher_DefaultConfig();
    cfg.settledHeadMesherConfig.voxelSize = 0.015f;
    cfg.settledHeadMesherConfig.maxCells = 2200000;
    cfg.settledHeadMesherConfig.maxResolution = 384;

    cfg.morphHeadMesherConfig = SDFMesher_DefaultConfig();
    cfg.morphHeadMesherConfig.voxelSize = 0.024f;
    cfg.morphHeadMesherConfig.maxCells = 500000;
    cfg.morphHeadMesherConfig.maxResolution = 256;

    cfg.sdfConfig = MonsterSDF_DefaultConfig();
    cfg.settledDelaySec = 0.15f;
    return cfg;
}

SDFMesherConfig MonsterVisualAsync_ResolveBodyConfig(const MonsterVisualAsyncConfig* c,
    MonsterVisualQualityTier tier, bool lizard) {
    SDFMesherConfig body=tier==MONSTER_VISUAL_QUALITY_SETTLED?c->settledMesherConfig:
        tier==MONSTER_VISUAL_QUALITY_MORPH?c->morphMesherConfig:c->interactiveMesherConfig;
    SDFMesherConfig head=tier==MONSTER_VISUAL_QUALITY_SETTLED?c->settledHeadMesherConfig:
        tier==MONSTER_VISUAL_QUALITY_MORPH?c->morphHeadMesherConfig:c->interactiveHeadMesherConfig;
    if(lizard) {
        body.adaptiveDetail=true;
        body.maxCells+=head.maxCells/2;
        if(body.maxResolution<head.maxResolution)body.maxResolution=head.maxResolution;
    }
    return body;
}

static void FreeEyeArray(MonsterVisualEyeAsync* eyes, size_t count) {
    if (!eyes) return;
    for (size_t i = 0; i < count; ++i) {
        Mesh_Free(&eyes[i].sclera);
        Mesh_Free(&eyes[i].iris);
        Mesh_Free(&eyes[i].pupil);
    }
    free(eyes);
}

static void FreeMouthArray(MonsterVisualMouth* mouths, size_t count) {
    if (!mouths) return;
    for (size_t i = 0; i < count; ++i) {
        MonsterVisualMouth_Free(&mouths[i]);
    }
    free(mouths);
}

static bool WorkerShouldCancel(void* context) {
    MonsterVisualAsync* asyncMgr = (MonsterVisualAsync*)context;
    if (!asyncMgr) return true;
    if (asyncMgr->shouldQuit) return true;
    if (asyncMgr->displayGeneration == 0) return false;
    if (asyncMgr->hasPendingRequest) return true;
    return false;
}

static void* WorkerThreadRoutine(void* arg) {
    MonsterVisualAsync* asyncMgr = (MonsterVisualAsync*)arg;

    MonsterSDF workerSdf = MonsterSDF_Create();
    SDFMesher workerMesher = SDFMesher_Create(asyncMgr->config.interactiveMesherConfig);
    SDFMesher workerHeadMesher = SDFMesher_Create(asyncMgr->config.interactiveHeadMesherConfig);
    SDFMesherConfig jawCfg = SDFMesher_DefaultConfig(); jawCfg.voxelSize = 0.04f; jawCfg.maxCells = 100000; jawCfg.useAutoBounds = true;
    SDFMesherConfig seamCfg = SDFMesher_DefaultConfig(); seamCfg.voxelSize = 0.03f; seamCfg.maxCells = 120000; seamCfg.useAutoBounds = true;
    SDFMesher workerJawMesher = SDFMesher_Create(jawCfg);
    SDFMesher workerSeamMesher = SDFMesher_Create(seamCfg);

    SDFMesher_SetSamplingPool(&workerMesher, asyncMgr->samplingPool);
    SDFMesher_SetSamplingPool(&workerHeadMesher, asyncMgr->samplingPool);
    SDFMesher_SetSamplingPool(&workerJawMesher, asyncMgr->samplingPool);
    SDFMesher_SetSamplingPool(&workerSeamMesher, asyncMgr->samplingPool);

    Monster workMonster = Monster_Create();
    Mesh workBodyMesh = Mesh_Create();
    Mesh workHeadMesh = Mesh_Create();
    LizardMorph* workerMorph = LizardMorph_Create();

    while (1) {
        pthread_mutex_lock(&asyncMgr->lock);
        while (!asyncMgr->hasPendingRequest && !asyncMgr->shouldQuit) {
            pthread_cond_wait(&asyncMgr->cond, &asyncMgr->lock);
        }

        if (asyncMgr->shouldQuit) {
            pthread_mutex_unlock(&asyncMgr->lock);
            break;
        }

        Monster_CopyInto(&workMonster, &asyncMgr->pendingSnapshot);

        uint64_t workFingerprint = asyncMgr->pendingFingerprint;
        MonsterVisualQualityTier workTier = asyncMgr->pendingTier;
        asyncMgr->hasPendingRequest = false;
        asyncMgr->stats.isWorkerBusy = true;
        asyncMgr->stats.workingFingerprint=workFingerprint;
        asyncMgr->stats.workingScale=workMonster.hasLizardPhenotype?workMonster.lizardPhenotype.totalScale:0;

        pthread_mutex_unlock(&asyncMgr->lock);

        /* --- TRABAJO PESADO FUERA DEL MUTEX --- */
        double tStart = GetTimeMs();

        SDFMesherConfig activeMesherCfg;
        SDFMesherConfig activeHeadCfg;
        if (workTier == MONSTER_VISUAL_QUALITY_SETTLED) {
            activeMesherCfg = asyncMgr->config.settledMesherConfig;
            activeHeadCfg = asyncMgr->config.settledHeadMesherConfig;
        } else if (workTier == MONSTER_VISUAL_QUALITY_MORPH) {
            activeMesherCfg = asyncMgr->config.morphMesherConfig;
            activeHeadCfg = asyncMgr->config.morphHeadMesherConfig;
        } else {
            activeMesherCfg = asyncMgr->config.interactiveMesherConfig;
            activeHeadCfg = asyncMgr->config.interactiveHeadMesherConfig;
        }

        if(workMonster.hasHead && !workMonster.hasLizardPhenotype) {
            float recommended=HeadAnatomy_RecommendedVoxelSize(&workMonster.head.anatomy);
            float tierTarget=workTier==MONSTER_VISUAL_QUALITY_SETTLED?recommended:recommended*1.75f;
            if(activeHeadCfg.voxelSize<=0.0001f||activeHeadCfg.voxelSize>tierTarget)
                activeHeadCfg.voxelSize=tierTarget;
        }

        activeMesherCfg=MonsterVisualAsync_ResolveBodyConfig(&asyncMgr->config,workTier,
            workMonster.hasLizardPhenotype);
        activeMesherCfg.shouldCancel = WorkerShouldCancel;
        activeMesherCfg.cancelContext = asyncMgr;
        activeHeadCfg.shouldCancel = WorkerShouldCancel;
        activeHeadCfg.cancelContext = asyncMgr;
        workerMesher.config = activeMesherCfg;
        workerHeadMesher.config = activeHeadCfg;

        bool buildOk = MonsterSDF_Build(&workerSdf, &workMonster, asyncMgr->config.sdfConfig);
        bool meshOk = false;
        bool headOk = true;

        if (buildOk) {
            MonsterSDFBodyField bodyContext;
            SDFField field = MonsterSDF_GetBodyField(&workerSdf,&bodyContext);
            Mesh_Clear(&workBodyMesh);
            SDFDetailRegion regions[MONSTER_SDF_DETAIL_REGION_CAPACITY];
            size_t regionCount=MonsterSDF_GetDetailRegions(&workerSdf,
                workTier==MONSTER_VISUAL_QUALITY_SETTLED?6.0f:3.5f,regions,MONSTER_SDF_DETAIL_REGION_CAPACITY);
            if(workerSdf.axialStationCount>1) field=MonsterSDF_GetField(&workerSdf);
            meshOk = workerSdf.axialStationCount>1 ?
                SDFMesher_GenerateMeshDetailed(&workerMesher,&field,regions,regionCount,&workBodyMesh):
                SDFMesher_GenerateMesh(&workerMesher, &field, &workBodyMesh);
            Mesh_Clear(&workHeadMesh);
            if(meshOk&&workerSdf.hasPartitionedHead&&workerSdf.axialStationCount==0) {
                MonsterSDFHeadField headContext;
                SDFField headField=MonsterSDF_GetHeadField(&workerSdf,0,&headContext);
                headOk=SDFMesher_GenerateMesh(&workerHeadMesher,&headField,&workHeadMesh)&&
                       Mesh_KeepLargestComponent(&workHeadMesh);
            }
        }

        /* Generar mallas primitivas de ojos */
        MonsterVisualEyeAsync* workEyes = NULL;
        size_t workEyeCount = workMonster.eyeCount;
        bool eyeOk = true;

        if (meshOk && headOk && workEyeCount > 0) {
            workEyes = (MonsterVisualEyeAsync*)calloc(workEyeCount, sizeof(MonsterVisualEyeAsync));
            if (workEyes) {
                for (size_t i = 0; i < workEyeCount; ++i) {
                    workEyes[i].sclera = Mesh_Create();
                    workEyes[i].iris = Mesh_Create();
                    workEyes[i].pupil = Mesh_Create();
                }
                if (!MonsterVisual_UpdateEyes(workEyes, workEyeCount, &workMonster)) {
                    eyeOk = false;
                }
            } else {
                eyeOk = false;
            }
        }

        MonsterVisualMouth* workMouths = NULL;
        size_t workMouthCount = workMonster.mouthCount;
        bool mouthOk = true;

        if (meshOk && headOk && workMouthCount > 0) {
            workMouths = (MonsterVisualMouth*)calloc(workMouthCount, sizeof(MonsterVisualMouth));
            if (!workMouths) {
                mouthOk = false;
            } else {
                for (size_t i = 0; i < workMouthCount; ++i) {
                    if (!MonsterVisual_BuildMouthMeshesFromSDFWithMeshers(&workMouths[i], &workMonster.mouths[i], &workMonster, &workerSdf, i, &workerJawMesher, &workerSeamMesher)) {
                        mouthOk = false;
                        break;
                    }
                }
            }
        }

        if (meshOk && workMonster.hasLizardPhenotype && workBodyMesh.vertexCount > 0) {
            LizardMorph_Bind(workerMorph, &workBodyMesh, &workMonster.anatomyGraph);
        }

        float workScale=workMonster.hasLizardPhenotype?workMonster.lizardPhenotype.totalScale:0;
        double tEnd = GetTimeMs();
        float durationMs = (float)(tEnd - tStart);

        /* --- DEPOSITAR RESULTADO EN READY BUFFER DENTRO DEL MUTEX --- */
        pthread_mutex_lock(&asyncMgr->lock);

        if (buildOk && meshOk && headOk && eyeOk && mouthOk && !asyncMgr->shouldQuit &&
            !(asyncMgr->displayFingerprint==asyncMgr->stats.requestedFingerprint && workFingerprint!=asyncMgr->stats.requestedFingerprint)) {
            /* Liberar readyEyes anterior */
            FreeEyeArray(asyncMgr->readyEyes, asyncMgr->readyEyeCount);

            /* Intercambiar mallas */
            Mesh tmp = asyncMgr->readyMesh;
            asyncMgr->readyMesh = workBodyMesh;
            workBodyMesh = tmp;
            Mesh tmpHead=asyncMgr->readyHeadMesh;
            asyncMgr->readyHeadMesh=workHeadMesh;
            workHeadMesh=tmpHead;

            LizardMorph* tmpMorph = asyncMgr->readyMorph;
            asyncMgr->readyMorph = workerMorph;
            workerMorph = tmpMorph;

            asyncMgr->readyEyes = workEyes;
            asyncMgr->readyEyeCount = workEyeCount;
            asyncMgr->readyEyeCapacity = workEyeCount;
            asyncMgr->readyGeneration++;
            asyncMgr->readyFingerprint = workFingerprint;
            asyncMgr->readyScale=workScale;
            asyncMgr->readyTier = workTier;
            FreeMouthArray(asyncMgr->readyMouths, asyncMgr->readyMouthCount);
            asyncMgr->readyMouths = workMouths;
            asyncMgr->readyMouthCount = workMouthCount;
            asyncMgr->readyMouthCapacity = workMouthCount;
            asyncMgr->hasReadyMesh = true;

            asyncMgr->stats.completedBuildCount++;
            asyncMgr->stats.lastBuildDurationMs = durationMs;
            asyncMgr->stats.activeQualityTier = workTier;
            asyncMgr->stats.bodyMesher=*SDFMesher_GetLastStats(&workerMesher);
            if(workerSdf.hasPartitionedHead&&workerSdf.axialStationCount==0)asyncMgr->stats.headMesher=*SDFMesher_GetLastStats(&workerHeadMesher);
            else memset(&asyncMgr->stats.headMesher,0,sizeof(asyncMgr->stats.headMesher));
            asyncMgr->readyStats=asyncMgr->stats;
        } else {
            if (workEyes) FreeEyeArray(workEyes, workEyeCount);
            if (workMouths) FreeMouthArray(workMouths, workMouthCount);
            if (!meshOk) asyncMgr->stats.cancelledBuildCount++;
        }

        asyncMgr->stats.isWorkerBusy = false;
        pthread_mutex_unlock(&asyncMgr->lock);
    }

    LizardMorph_Free(workerMorph);
    Mesh_Free(&workBodyMesh);
    Mesh_Free(&workHeadMesh);
    SDFMesher_Free(&workerMesher);
    SDFMesher_Free(&workerHeadMesher);
    SDFMesher_Free(&workerJawMesher);
    SDFMesher_Free(&workerSeamMesher);
    MonsterSDF_Free(&workerSdf);
    Monster_Free(&workMonster);

    return NULL;
}

MonsterVisualAsync* MonsterVisualAsync_Create(MonsterVisualAsyncConfig config) {
    MonsterVisualAsync* asyncMgr = (MonsterVisualAsync*)calloc(1, sizeof(MonsterVisualAsync));
    if (!asyncMgr) return NULL;

    asyncMgr->config = config;
    asyncMgr->displayMesh = Mesh_Create();
    asyncMgr->readyMesh = Mesh_Create();
    asyncMgr->displayHeadMesh = Mesh_Create();
    asyncMgr->readyHeadMesh = Mesh_Create();
    asyncMgr->pendingSnapshot = Monster_Create();
    asyncMgr->displayMorph = LizardMorph_Create();
    asyncMgr->readyMorph = LizardMorph_Create();
    asyncMgr->samplingPool = SDFSamplingPool_Create(0);

    pthread_mutex_init(&asyncMgr->lock, NULL);
    pthread_cond_init(&asyncMgr->cond, NULL);

    asyncMgr->threadRunning = true;
    if (pthread_create(&asyncMgr->workerThread, NULL, WorkerThreadRoutine, asyncMgr) != 0) {
        fprintf(stderr, "MonsterVisualAsync: error al crear hilo worker\n");
        asyncMgr->threadRunning = false;
    }

    return asyncMgr;
}

void MonsterVisualAsync_Free(MonsterVisualAsync* asyncMgr) {
    if (!asyncMgr) return;

    if (asyncMgr->threadRunning) {
        pthread_mutex_lock(&asyncMgr->lock);
        asyncMgr->shouldQuit = true;
        pthread_cond_signal(&asyncMgr->cond);
        pthread_mutex_unlock(&asyncMgr->lock);

        pthread_join(asyncMgr->workerThread, NULL);
        asyncMgr->threadRunning = false;
    }

    pthread_mutex_lock(&asyncMgr->lock);

    Monster_Free(&asyncMgr->pendingSnapshot);

    Mesh_Free(&asyncMgr->displayMesh);
    Mesh_Free(&asyncMgr->displayHeadMesh);
    FreeEyeArray(asyncMgr->displayEyes, asyncMgr->displayEyeCount);
    FreeMouthArray(asyncMgr->displayMouths, asyncMgr->displayMouthCount);

    Mesh_Free(&asyncMgr->readyMesh);
    Mesh_Free(&asyncMgr->readyHeadMesh);
    FreeEyeArray(asyncMgr->readyEyes, asyncMgr->readyEyeCount);
    FreeMouthArray(asyncMgr->readyMouths, asyncMgr->readyMouthCount);

    LizardMorph_Free(asyncMgr->displayMorph);
    LizardMorph_Free(asyncMgr->readyMorph);

    pthread_mutex_unlock(&asyncMgr->lock);

    if (asyncMgr->samplingPool) {
        SDFSamplingPool_Free(asyncMgr->samplingPool);
        asyncMgr->samplingPool = NULL;
    }

    pthread_mutex_destroy(&asyncMgr->lock);
    pthread_cond_destroy(&asyncMgr->cond);

    free(asyncMgr);
}

void MonsterVisualAsync_SetContinuousMotion(MonsterVisualAsync* asyncMgr,bool active) {
    if(!asyncMgr)return;
    if(asyncMgr->continuousMotion!=active)asyncMgr->timeSinceLastMotionSec=0;
    asyncMgr->continuousMotion=active;
}

void MonsterVisualAsync_SetMorphMode(MonsterVisualAsync* asyncMgr, bool active) {
    if(!asyncMgr)return;
    asyncMgr->morphMode = active;
}

bool MonsterVisualAsync_Update(MonsterVisualAsync* asyncMgr, const Monster* monster, float deltaTime) {
    if (!asyncMgr || !monster) return false;

    /* 1. Detectar movimiento y actualizar temporizador de tier de calidad */
    uint64_t baseGeomHash = ComputeMonsterFingerprint(monster, asyncMgr->config.sdfConfig, MONSTER_VISUAL_QUALITY_INTERACTIVE);
    if (baseGeomHash != asyncMgr->lastObservedFingerprint) {
        asyncMgr->lastObservedFingerprint = baseGeomHash;
        asyncMgr->timeSinceLastMotionSec = 0.0f;
    } else {
        asyncMgr->timeSinceLastMotionSec += deltaTime;
    }

    MonsterVisualQualityTier activeMotionTier = asyncMgr->morphMode ?
        MONSTER_VISUAL_QUALITY_MORPH : MONSTER_VISUAL_QUALITY_INTERACTIVE;
    MonsterVisualQualityTier targetTier = (asyncMgr->continuousMotion || asyncMgr->timeSinceLastMotionSec < asyncMgr->config.settledDelaySec) ?
        activeMotionTier : MONSTER_VISUAL_QUALITY_SETTLED;

    uint64_t targetFingerprint = ComputeMonsterFingerprint(monster, asyncMgr->config.sdfConfig, targetTier);

    bool updatedDisplay = false;

    pthread_mutex_lock(&asyncMgr->lock);

    /* 2. Si hay malla lista generada por el worker, swap hacia el front buffer (Display) */
    if (asyncMgr->hasReadyMesh) {
        FreeEyeArray(asyncMgr->displayEyes, asyncMgr->displayEyeCount);

        Mesh tmp = asyncMgr->displayMesh;
        asyncMgr->displayMesh = asyncMgr->readyMesh;
        asyncMgr->readyMesh = tmp;
        Mesh tmpHead=asyncMgr->displayHeadMesh;
        asyncMgr->displayHeadMesh=asyncMgr->readyHeadMesh;
        asyncMgr->readyHeadMesh=tmpHead;

        LizardMorph* tmpMorph = asyncMgr->displayMorph;
        asyncMgr->displayMorph = asyncMgr->readyMorph;
        asyncMgr->readyMorph = tmpMorph;

        asyncMgr->displayEyes = asyncMgr->readyEyes;
        asyncMgr->displayEyeCount = asyncMgr->readyEyeCount;
        asyncMgr->displayEyeCapacity = asyncMgr->readyEyeCapacity;
        asyncMgr->readyEyes = NULL;
        asyncMgr->readyEyeCount = 0;
        asyncMgr->readyEyeCapacity = 0;

        FreeMouthArray(asyncMgr->displayMouths, asyncMgr->displayMouthCount);
        asyncMgr->displayMouths = asyncMgr->readyMouths;
        asyncMgr->displayMouthCount = asyncMgr->readyMouthCount;
        asyncMgr->displayMouthCapacity = asyncMgr->readyMouthCapacity;
        asyncMgr->readyMouths = NULL;
        asyncMgr->readyMouthCount = 0;
        asyncMgr->readyMouthCapacity = 0;

        asyncMgr->displayGeneration = asyncMgr->readyGeneration;
        asyncMgr->displayFingerprint = asyncMgr->readyFingerprint;
        asyncMgr->stats.displayedFingerprint=asyncMgr->readyFingerprint;
        asyncMgr->stats.displayedScale=asyncMgr->readyScale;
        asyncMgr->displayStats=asyncMgr->readyStats;
        asyncMgr->hasReadyMesh = false;
        updatedDisplay = true;
    }

    /* 3. Comprobar si se requiere solicitar una nueva reconstrucción */
    bool isPendingMatch = asyncMgr->hasPendingRequest && (asyncMgr->pendingFingerprint == targetFingerprint);
    bool isDisplayMatch = (asyncMgr->displayFingerprint == targetFingerprint);

    bool isWorkingMatch=asyncMgr->stats.isWorkerBusy && asyncMgr->stats.workingFingerprint==targetFingerprint;
    asyncMgr->stats.requestedFingerprint=targetFingerprint;
    if((isWorkingMatch || isDisplayMatch) && asyncMgr->hasPendingRequest && !isPendingMatch) {
        asyncMgr->hasPendingRequest=false;
        asyncMgr->stats.coalescedCount++;
    }
    if (!isDisplayMatch && !isPendingMatch && !isWorkingMatch) {
        if (asyncMgr->hasPendingRequest) {
            asyncMgr->stats.coalescedCount++;
        }

        Monster_CopyInto(&asyncMgr->pendingSnapshot, monster);
        asyncMgr->pendingFingerprint = targetFingerprint;
        asyncMgr->pendingTier = targetTier;
        asyncMgr->hasPendingRequest = true;
        asyncMgr->stats.requestCount++;

        pthread_cond_signal(&asyncMgr->cond);
    }

    bool allowLiveArticulation = isDisplayMatch || asyncMgr->morphMode;

    pthread_mutex_unlock(&asyncMgr->lock);

    if (asyncMgr->morphMode && monster->hasLizardPhenotype &&
        LizardMorph_IsBound(asyncMgr->displayMorph) &&
        asyncMgr->displayMesh.vertexCount == LizardMorph_GetVertexCount(asyncMgr->displayMorph)) {
        LizardMorph_Deform(asyncMgr->displayMorph, &monster->anatomyGraph, &asyncMgr->displayMesh);
        asyncMgr->stats.displayedScale = monster->lizardPhenotype.totalScale;
    }

    /* Ojos: actualización continua de posición y escala morfológica en tiempo real */
    if (allowLiveArticulation && monster->eyeCount > 0) {
        if (asyncMgr->displayEyeCount < monster->eyeCount) {
            FreeEyeArray(asyncMgr->displayEyes, asyncMgr->displayEyeCount);
            asyncMgr->displayEyes = (MonsterVisualEyeAsync*)calloc(monster->eyeCount, sizeof(MonsterVisualEyeAsync));
            if (asyncMgr->displayEyes) {
                for (size_t i = 0; i < monster->eyeCount; ++i) {
                    asyncMgr->displayEyes[i].sclera = Mesh_Create();
                    asyncMgr->displayEyes[i].iris = Mesh_Create();
                    asyncMgr->displayEyes[i].pupil = Mesh_Create();
                }
                asyncMgr->displayEyeCount = monster->eyeCount;
                asyncMgr->displayEyeCapacity = monster->eyeCount;
            }
        }
        if (asyncMgr->displayEyes && asyncMgr->displayEyeCount >= monster->eyeCount) {
            MonsterVisual_UpdateEyes(asyncMgr->displayEyes, asyncMgr->displayEyeCount, monster);
        }
    }

    /* Mandíbula: actualización continua de articulación y escalado morfológico */
    if (allowLiveArticulation) {
        for (size_t i = 0; i < asyncMgr->displayMouthCount && i < monster->mouthCount; ++i) {
            MonsterVisual_UpdateMouthArticulation(&asyncMgr->displayMouths[i], &monster->mouths[i], monster);
        }
    }

    return updatedDisplay;
}

const Mesh* MonsterVisualAsync_GetDisplayMesh(const MonsterVisualAsync* asyncMgr) {
    return asyncMgr ? &asyncMgr->displayMesh : NULL;
}

const Mesh* MonsterVisualAsync_GetDisplayHeadMesh(const MonsterVisualAsync* asyncMgr) {
    return asyncMgr ? &asyncMgr->displayHeadMesh : NULL;
}

size_t MonsterVisualAsync_GetDisplayEyeCount(const MonsterVisualAsync* asyncMgr) {
    return asyncMgr ? asyncMgr->displayEyeCount : 0;
}

const Mesh* MonsterVisualAsync_GetDisplayEyeSclera(const MonsterVisualAsync* asyncMgr, size_t index) {
    if (!asyncMgr || index >= asyncMgr->displayEyeCount || !asyncMgr->displayEyes) return NULL;
    return &asyncMgr->displayEyes[index].sclera;
}

const Mesh* MonsterVisualAsync_GetDisplayEyeIris(const MonsterVisualAsync* asyncMgr, size_t index) {
    if (!asyncMgr || index >= asyncMgr->displayEyeCount || !asyncMgr->displayEyes) return NULL;
    return &asyncMgr->displayEyes[index].iris;
}

const Mesh* MonsterVisualAsync_GetDisplayEyePupil(const MonsterVisualAsync* asyncMgr, size_t index) {
    if (!asyncMgr || index >= asyncMgr->displayEyeCount || !asyncMgr->displayEyes) return NULL;
    return &asyncMgr->displayEyes[index].pupil;
}

size_t MonsterVisualAsync_GetDisplayMouthCount(const MonsterVisualAsync* asyncMgr) {
    return asyncMgr ? asyncMgr->displayMouthCount : 0;
}

const Mesh* MonsterVisualAsync_GetDisplayMouthMesh(
    const MonsterVisualAsync* asyncMgr,
    size_t mouthIndex,
    size_t meshIndex
) {
    if (!asyncMgr || mouthIndex >= asyncMgr->displayMouthCount || meshIndex >= 2) return NULL;
    const MonsterVisualMouth* mouth = &asyncMgr->displayMouths[mouthIndex];
    switch (meshIndex) {
        case 0: return &mouth->jaw;
        case 1: return &mouth->hinge;
        default: return NULL;
    }
}

bool MonsterVisualAsync_Render(const MonsterVisualAsync* asyncMgr, Renderer3D* renderer) {
    if (!asyncMgr || !renderer || !renderer->renderMesh) return false;

    if (asyncMgr->displayMesh.vertexCount > 0) {
        renderer->renderMesh(renderer, &asyncMgr->displayMesh);
    }
    if(asyncMgr->displayHeadMesh.vertexCount>0)renderer->renderMesh(renderer,&asyncMgr->displayHeadMesh);
    for (size_t m = 0; m < asyncMgr->displayMouthCount; ++m) {
        const MonsterVisualMouth* mouth = &asyncMgr->displayMouths[m];
        if (mouth->jaw.vertexCount > 0) renderer->renderMesh(renderer, &mouth->jaw);
        if (mouth->hinge.vertexCount > 0) renderer->renderMesh(renderer, &mouth->hinge);
    }
    for (size_t i = 0; i < asyncMgr->displayEyeCount; ++i) {
        if (asyncMgr->displayEyes[i].sclera.vertexCount > 0) {
            renderer->renderMesh(renderer, &asyncMgr->displayEyes[i].sclera);
        }
        if (asyncMgr->displayEyes[i].iris.vertexCount > 0) {
            renderer->renderMesh(renderer, &asyncMgr->displayEyes[i].iris);
        }
        if (asyncMgr->displayEyes[i].pupil.vertexCount > 0) {
            renderer->renderMesh(renderer, &asyncMgr->displayEyes[i].pupil);
        }
    }
    return true;
}

uint64_t MonsterVisualAsync_GetDisplayGeneration(const MonsterVisualAsync* asyncMgr) {
    return asyncMgr ? asyncMgr->displayGeneration : 0;
}

MonsterVisualAsyncStats MonsterVisualAsync_GetStats(const MonsterVisualAsync* asyncMgr) {
    MonsterVisualAsyncStats s;
    memset(&s, 0, sizeof(MonsterVisualAsyncStats));
    if (!asyncMgr) return s;

    pthread_mutex_lock((pthread_mutex_t*)&asyncMgr->lock);
    s = asyncMgr->stats;
    if(asyncMgr->displayGeneration>0) {
        s.lastBuildDurationMs=asyncMgr->displayStats.lastBuildDurationMs;
        s.activeQualityTier=asyncMgr->displayStats.activeQualityTier;
        s.bodyMesher=asyncMgr->displayStats.bodyMesher;
        s.headMesher=asyncMgr->displayStats.headMesher;
    }
    pthread_mutex_unlock((pthread_mutex_t*)&asyncMgr->lock);

    return s;
}

void MonsterVisualAsync_Flush(MonsterVisualAsync* asyncMgr) {
    if (!asyncMgr) return;

    while (1) {
        pthread_mutex_lock(&asyncMgr->lock);
        bool busy = asyncMgr->hasPendingRequest || asyncMgr->stats.isWorkerBusy;
        pthread_mutex_unlock(&asyncMgr->lock);

        if (!busy) break;
        usleep(1000); /* 1 ms */
    }

    /* Intercambiar si quedó algo en readyMesh */
    pthread_mutex_lock(&asyncMgr->lock);
    if (asyncMgr->hasReadyMesh) {
        FreeEyeArray(asyncMgr->displayEyes, asyncMgr->displayEyeCount);

        Mesh tmp = asyncMgr->displayMesh;
        asyncMgr->displayMesh = asyncMgr->readyMesh;
        asyncMgr->readyMesh = tmp;
        Mesh tmpHead=asyncMgr->displayHeadMesh;
        asyncMgr->displayHeadMesh=asyncMgr->readyHeadMesh;
        asyncMgr->readyHeadMesh=tmpHead;

        LizardMorph* tmpMorph = asyncMgr->displayMorph;
        asyncMgr->displayMorph = asyncMgr->readyMorph;
        asyncMgr->readyMorph = tmpMorph;

        asyncMgr->displayEyes = asyncMgr->readyEyes;
        asyncMgr->displayEyeCount = asyncMgr->readyEyeCount;
        asyncMgr->displayEyeCapacity = asyncMgr->readyEyeCapacity;
        asyncMgr->readyEyes = NULL;
        asyncMgr->readyEyeCount = 0;
        asyncMgr->readyEyeCapacity = 0;

        FreeMouthArray(asyncMgr->displayMouths, asyncMgr->displayMouthCount);
        asyncMgr->displayMouths = asyncMgr->readyMouths;
        asyncMgr->displayMouthCount = asyncMgr->readyMouthCount;
        asyncMgr->displayMouthCapacity = asyncMgr->readyMouthCapacity;
        asyncMgr->readyMouths = NULL;
        asyncMgr->readyMouthCount = 0;
        asyncMgr->readyMouthCapacity = 0;

        asyncMgr->displayGeneration = asyncMgr->readyGeneration;
        asyncMgr->displayFingerprint = asyncMgr->readyFingerprint;
        asyncMgr->stats.displayedFingerprint=asyncMgr->readyFingerprint;
        asyncMgr->stats.displayedScale=asyncMgr->readyScale;
        asyncMgr->displayStats=asyncMgr->readyStats;
        asyncMgr->hasReadyMesh = false;
    }
    pthread_mutex_unlock(&asyncMgr->lock);
}
