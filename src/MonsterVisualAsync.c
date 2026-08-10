#include "MonsterVisualAsync.h"
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

    if (!monster) return hash;

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
        hash = HashVector3(mouth->offset, hash);
        hash = HashVector3(mouth->rotation, hash);
        hash = HashVector3(mouth->scale, hash);
        hash = HashFloat(mouth->openFactor, hash);
        hash = HashColor(mouth->insideColor, hash);
        hash = HashColor(mouth->lipColor, hash);
    }

    /* Ojos */
    hash = HashSizeT(monster->eyeCount, hash);
    for (size_t i = 0; i < monster->eyeCount; ++i) {
        const Eye* eye = &monster->eyes[i];
        hash = HashSizeT(eye->bodyPartIndex, hash);
        hash = HashVector3(eye->offset, hash);
        hash = HashVector3(eye->rotation, hash);
        hash = HashVector3(eye->scale, hash);
        hash = HashFloat(eye->pupilScale, hash);
        hash = HashColor(eye->scleraColor, hash);
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
    cfg.interactiveMesherConfig.voxelSize = 0.22f;
    cfg.interactiveMesherConfig.maxCells = 100000;

    cfg.settledMesherConfig = SDFMesher_DefaultConfig();
    cfg.settledMesherConfig.voxelSize = 0.13f;
    cfg.settledMesherConfig.maxCells = 500000;

    cfg.sdfConfig = MonsterSDF_DefaultConfig();
    cfg.settledDelaySec = 0.15f;
    return cfg;
}

static void FreeEyeArray(MonsterVisualEyeAsync* eyes, size_t count) {
    if (!eyes) return;
    for (size_t i = 0; i < count; ++i) {
        Mesh_Free(&eyes[i].sclera);
        Mesh_Free(&eyes[i].pupil);
    }
    free(eyes);
}

static void* WorkerThreadRoutine(void* arg) {
    MonsterVisualAsync* asyncMgr = (MonsterVisualAsync*)arg;

    MonsterSDF workerSdf = MonsterSDF_Create();
    SDFMesher workerMesher = SDFMesher_Create(asyncMgr->config.interactiveMesherConfig);
    Mesh workBodyMesh = Mesh_Create();

    while (1) {
        pthread_mutex_lock(&asyncMgr->lock);
        while (!asyncMgr->hasPendingRequest && !asyncMgr->shouldQuit) {
            pthread_cond_wait(&asyncMgr->cond, &asyncMgr->lock);
        }

        if (asyncMgr->shouldQuit) {
            pthread_mutex_unlock(&asyncMgr->lock);
            break;
        }

        Monster workMonster = asyncMgr->pendingSnapshot;
        memset(&asyncMgr->pendingSnapshot, 0, sizeof(Monster));

        uint64_t workFingerprint = asyncMgr->pendingFingerprint;
        MonsterVisualQualityTier workTier = asyncMgr->pendingTier;
        asyncMgr->hasPendingRequest = false;
        asyncMgr->stats.isWorkerBusy = true;

        pthread_mutex_unlock(&asyncMgr->lock);

        /* --- TRABAJO PESADO FUERA DEL MUTEX --- */
        double tStart = GetTimeMs();

        SDFMesherConfig activeMesherCfg = (workTier == MONSTER_VISUAL_QUALITY_INTERACTIVE) ?
            asyncMgr->config.interactiveMesherConfig : asyncMgr->config.settledMesherConfig;

        workerMesher.config = activeMesherCfg;

        bool buildOk = MonsterSDF_Build(&workerSdf, &workMonster, asyncMgr->config.sdfConfig);
        bool meshOk = false;

        if (buildOk) {
            SDFField field = MonsterSDF_GetField(&workerSdf);
            Mesh_Clear(&workBodyMesh);
            meshOk = SDFMesher_GenerateMesh(&workerMesher, &field, &workBodyMesh);
        }

        /* Generar mallas primitivas de ojos */
        MonsterVisualEyeAsync* workEyes = NULL;
        size_t workEyeCount = workMonster.eyeCount;
        bool eyeOk = true;

        if (meshOk && workEyeCount > 0) {
            workEyes = (MonsterVisualEyeAsync*)calloc(workEyeCount, sizeof(MonsterVisualEyeAsync));
            if (workEyes) {
                const float EYE_VISIBILITY_EPS = 1e-4f;
                for (size_t i = 0; i < workEyeCount; ++i) {
                    const Eye* eye = &workMonster.eyes[i];
                    workEyes[i].sclera = Mesh_Create();
                    workEyes[i].pupil = Mesh_Create();

                    if (eye->scale.x <= EYE_VISIBILITY_EPS ||
                        eye->scale.y <= EYE_VISIBILITY_EPS ||
                        eye->scale.z <= EYE_VISIBILITY_EPS) {
                        continue;
                    }

                    Vector3 partPos = Vec3_Zero();
                    if (eye->bodyPartIndex < workMonster.bodyPartCount) {
                        partPos = workMonster.bodyParts[eye->bodyPartIndex].positionRender;
                    }

                    Vector3 eyePos = Vec3_Add(partPos, eye->offset);
                    Vector3 eyeRadii = Vec3_Scale(eye->scale, 0.5f);

                    Transform3D scleraTrans = Transform3D_Create(eyePos, eye->rotation, eyeRadii);
                    Vector3 pupilForward = Transform3D_RotateVector(eye->rotation, Vec3_Create(0.0f, 0.0f, 1.0f));
                    float zOffset = eyeRadii.z * 0.90f;
                    Vector3 pupilCenter = Vec3_Add(eyePos, Vec3_Scale(pupilForward, zOffset));

                    float pupilRadius = Math_Min(eyeRadii.x, eyeRadii.y) * Math_Clamp01(eye->pupilScale) * 0.5f;
                    pupilRadius = Math_Max(pupilRadius, 0.01f);
                    Transform3D pupilTrans = Transform3D_Create(pupilCenter, eye->rotation, Vec3_Create(pupilRadius, pupilRadius, pupilRadius * 0.2f));

                    if (!PrimitiveMesh_GenerateEllipsoid(&workEyes[i].sclera, scleraTrans, 16, 12, eye->scleraColor) ||
                        !PrimitiveMesh_GenerateEllipsoid(&workEyes[i].pupil, pupilTrans, 12, 8, eye->pupilColor)) {
                        eyeOk = false;
                        break;
                    }
                }
            } else {
                eyeOk = false;
            }
        }

        Monster_Free(&workMonster);
        double tEnd = GetTimeMs();
        float durationMs = (float)(tEnd - tStart);

        /* --- DEPOSITAR RESULTADO EN READY BUFFER DENTRO DEL MUTEX --- */
        pthread_mutex_lock(&asyncMgr->lock);

        if (buildOk && meshOk && eyeOk && !asyncMgr->shouldQuit) {
            /* Liberar readyEyes anterior */
            FreeEyeArray(asyncMgr->readyEyes, asyncMgr->readyEyeCount);

            /* Intercambiar mallas */
            Mesh tmp = asyncMgr->readyMesh;
            asyncMgr->readyMesh = workBodyMesh;
            workBodyMesh = tmp;

            asyncMgr->readyEyes = workEyes;
            asyncMgr->readyEyeCount = workEyeCount;
            asyncMgr->readyEyeCapacity = workEyeCount;
            asyncMgr->readyGeneration++;
            asyncMgr->readyFingerprint = workFingerprint;
            asyncMgr->readyTier = workTier;
            asyncMgr->hasReadyMesh = true;

            asyncMgr->stats.completedBuildCount++;
            asyncMgr->stats.lastBuildDurationMs = durationMs;
            asyncMgr->stats.activeQualityTier = workTier;
        } else {
            if (workEyes) FreeEyeArray(workEyes, workEyeCount);
        }

        asyncMgr->stats.isWorkerBusy = false;
        pthread_mutex_unlock(&asyncMgr->lock);
    }

    Mesh_Free(&workBodyMesh);
    SDFMesher_Free(&workerMesher);
    MonsterSDF_Free(&workerSdf);

    return NULL;
}

MonsterVisualAsync* MonsterVisualAsync_Create(MonsterVisualAsyncConfig config) {
    MonsterVisualAsync* asyncMgr = (MonsterVisualAsync*)calloc(1, sizeof(MonsterVisualAsync));
    if (!asyncMgr) return NULL;

    asyncMgr->config = config;
    asyncMgr->displayMesh = Mesh_Create();
    asyncMgr->readyMesh = Mesh_Create();

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

    if (asyncMgr->hasPendingRequest) {
        Monster_Free(&asyncMgr->pendingSnapshot);
    }

    Mesh_Free(&asyncMgr->displayMesh);
    FreeEyeArray(asyncMgr->displayEyes, asyncMgr->displayEyeCount);

    Mesh_Free(&asyncMgr->readyMesh);
    FreeEyeArray(asyncMgr->readyEyes, asyncMgr->readyEyeCount);

    pthread_mutex_unlock(&asyncMgr->lock);

    pthread_mutex_destroy(&asyncMgr->lock);
    pthread_cond_destroy(&asyncMgr->cond);

    free(asyncMgr);
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

    MonsterVisualQualityTier targetTier = (asyncMgr->timeSinceLastMotionSec < asyncMgr->config.settledDelaySec) ?
        MONSTER_VISUAL_QUALITY_INTERACTIVE : MONSTER_VISUAL_QUALITY_SETTLED;

    uint64_t targetFingerprint = ComputeMonsterFingerprint(monster, asyncMgr->config.sdfConfig, targetTier);

    bool updatedDisplay = false;

    pthread_mutex_lock(&asyncMgr->lock);

    /* 2. Si hay malla lista generada por el worker, swap hacia el front buffer (Display) */
    if (asyncMgr->hasReadyMesh) {
        FreeEyeArray(asyncMgr->displayEyes, asyncMgr->displayEyeCount);

        Mesh tmp = asyncMgr->displayMesh;
        asyncMgr->displayMesh = asyncMgr->readyMesh;
        asyncMgr->readyMesh = tmp;

        asyncMgr->displayEyes = asyncMgr->readyEyes;
        asyncMgr->displayEyeCount = asyncMgr->readyEyeCount;
        asyncMgr->displayEyeCapacity = asyncMgr->readyEyeCapacity;
        asyncMgr->readyEyes = NULL;
        asyncMgr->readyEyeCount = 0;
        asyncMgr->readyEyeCapacity = 0;

        asyncMgr->displayGeneration = asyncMgr->readyGeneration;
        asyncMgr->displayFingerprint = asyncMgr->readyFingerprint;
        asyncMgr->hasReadyMesh = false;
        updatedDisplay = true;
    }

    /* 3. Comprobar si se requiere solicitar una nueva reconstrucción */
    bool isPendingMatch = asyncMgr->hasPendingRequest && (asyncMgr->pendingFingerprint == targetFingerprint);
    bool isDisplayMatch = (asyncMgr->displayFingerprint == targetFingerprint);

    if (!isDisplayMatch && !isPendingMatch) {
        if (asyncMgr->hasPendingRequest) {
            Monster_Free(&asyncMgr->pendingSnapshot);
            asyncMgr->stats.coalescedCount++;
        }

        asyncMgr->pendingSnapshot = Monster_Clone(monster);
        asyncMgr->pendingFingerprint = targetFingerprint;
        asyncMgr->pendingTier = targetTier;
        asyncMgr->hasPendingRequest = true;
        asyncMgr->stats.requestCount++;

        pthread_cond_signal(&asyncMgr->cond);
    }

    pthread_mutex_unlock(&asyncMgr->lock);

    return updatedDisplay;
}

const Mesh* MonsterVisualAsync_GetDisplayMesh(const MonsterVisualAsync* asyncMgr) {
    return asyncMgr ? &asyncMgr->displayMesh : NULL;
}

size_t MonsterVisualAsync_GetDisplayEyeCount(const MonsterVisualAsync* asyncMgr) {
    return asyncMgr ? asyncMgr->displayEyeCount : 0;
}

const Mesh* MonsterVisualAsync_GetDisplayEyeSclera(const MonsterVisualAsync* asyncMgr, size_t index) {
    if (!asyncMgr || index >= asyncMgr->displayEyeCount || !asyncMgr->displayEyes) return NULL;
    return &asyncMgr->displayEyes[index].sclera;
}

const Mesh* MonsterVisualAsync_GetDisplayEyePupil(const MonsterVisualAsync* asyncMgr, size_t index) {
    if (!asyncMgr || index >= asyncMgr->displayEyeCount || !asyncMgr->displayEyes) return NULL;
    return &asyncMgr->displayEyes[index].pupil;
}

bool MonsterVisualAsync_Render(const MonsterVisualAsync* asyncMgr, Renderer3D* renderer) {
    if (!asyncMgr || !renderer || !renderer->renderMesh) return false;

    if (asyncMgr->displayMesh.vertexCount > 0) {
        renderer->renderMesh(renderer, &asyncMgr->displayMesh);
    }
    for (size_t i = 0; i < asyncMgr->displayEyeCount; ++i) {
        if (asyncMgr->displayEyes[i].sclera.vertexCount > 0) {
            renderer->renderMesh(renderer, &asyncMgr->displayEyes[i].sclera);
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

        asyncMgr->displayEyes = asyncMgr->readyEyes;
        asyncMgr->displayEyeCount = asyncMgr->readyEyeCount;
        asyncMgr->displayEyeCapacity = asyncMgr->readyEyeCapacity;
        asyncMgr->readyEyes = NULL;
        asyncMgr->readyEyeCount = 0;
        asyncMgr->readyEyeCapacity = 0;

        asyncMgr->displayGeneration = asyncMgr->readyGeneration;
        asyncMgr->displayFingerprint = asyncMgr->readyFingerprint;
        asyncMgr->hasReadyMesh = false;
    }
    pthread_mutex_unlock(&asyncMgr->lock);
}
