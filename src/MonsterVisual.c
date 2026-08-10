#include "MonsterVisual.h"
#include "PrimitiveMesh.h"
#include "MathUtils.h"
#include <stdlib.h>
#include <string.h>

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

static inline uint64_t HashInt(int v, uint64_t hash) {
    return Fnv1a64Bytes(&v, sizeof(int), hash);
}

static uint64_t MonsterVisual_ComputeFingerprint(const MonsterVisual* visual, const Monster* monster, MonsterSDFConfig sdfConfig) {
    uint64_t hash = 0xcbf29ce484222325ULL;

    /* Configuración SDF */
    hash = HashFloat(sdfConfig.bodySmoothness, hash);
    hash = HashFloat(sdfConfig.connectionSmoothness, hash);
    hash = HashFloat(sdfConfig.mouthSmoothness, hash);
    hash = HashFloat(sdfConfig.connectionRadiusFactor, hash);
    hash = HashFloat(sdfConfig.boundsPadding, hash);

    /* Configuración Mesher */
    if (visual) {
        SDFMesherConfig cfg = visual->mesher.config;
        hash = HashInt(cfg.resolutionX, hash);
        hash = HashInt(cfg.resolutionY, hash);
        hash = HashInt(cfg.resolutionZ, hash);
        hash = HashFloat(cfg.voxelSize, hash);
        hash = HashInt(cfg.maxResolution, hash);
        hash = HashSizeT(cfg.maxCells, hash);
        hash = HashFloat(cfg.isolevel, hash);
        hash = HashFloat(cfg.normalEps, hash);
        hash = HashBool(cfg.useAutoBounds, hash);
        if (!cfg.useAutoBounds) {
            hash = HashVector3(cfg.bounds.start, hash);
            hash = HashVector3(cfg.bounds.end, hash);
        }
    }

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

MonsterVisual MonsterVisual_Create(SDFMesherConfig mesherConfig) {
    MonsterVisual visual;
    memset(&visual, 0, sizeof(MonsterVisual));
    visual.sdf = MonsterSDF_Create();
    visual.stagingSdf = MonsterSDF_Create();
    visual.mesher = SDFMesher_Create(mesherConfig);
    visual.mesh = Mesh_Create();
    visual.stagingMesh = Mesh_Create();
    visual.isDirty = true;
    visual.updateTimer = 0.0f;
    visual.rebuildGeneration = 0;
    return visual;
}

void MonsterVisual_Free(MonsterVisual* visual) {
    if (!visual) return;
    MonsterSDF_Free(&visual->sdf);
    MonsterSDF_Free(&visual->stagingSdf);
    Mesh_Free(&visual->mesh);
    Mesh_Free(&visual->stagingMesh);
    for (size_t i = 0; i < visual->eyeCount; ++i) {
        Mesh_Free(&visual->eyes[i].sclera);
        Mesh_Free(&visual->eyes[i].pupil);
    }
    free(visual->eyes);
    visual->eyes = NULL;
    visual->eyeCount = 0;
    visual->eyeCapacity = 0;
    visual->hasFingerprint = false;
    visual->isDirty = false;
    visual->updateTimer = 0.0f;
    visual->rebuildGeneration = 0;
}

void MonsterVisual_MarkDirty(MonsterVisual* visual) {
    if (visual) visual->isDirty = true;
}

uint64_t MonsterVisual_GetGeneration(const MonsterVisual* visual) {
    return visual ? visual->rebuildGeneration : 0;
}

bool MonsterVisual_RebuildNow(
    MonsterVisual* visual,
    const Monster* monster,
    MonsterSDFConfig sdfConfig
) {
    if (!visual || !monster) return false;

    /* Reconstrucción transaccional reutilizando buffers staging */
    if (!MonsterSDF_Build(&visual->stagingSdf, monster, sdfConfig)) {
        return false;
    }

    SDFField field = MonsterSDF_GetField(&visual->stagingSdf);
    Mesh_Clear(&visual->stagingMesh);
    if (!SDFMesher_GenerateMesh(&visual->mesher, &field, &visual->stagingMesh)) {
        return false;
    }

    MonsterVisualEye* tempEyes = NULL;
    size_t tempEyeCount = monster->eyeCount;

    if (tempEyeCount > 0) {
        tempEyes = (MonsterVisualEye*)calloc(tempEyeCount, sizeof(MonsterVisualEye));
        if (!tempEyes) {
            return false;
        }

        const float EYE_VISIBILITY_EPS = 1e-4f;
        bool eyeSuccess = true;
        for (size_t i = 0; i < tempEyeCount; ++i) {
            const Eye* eye = &monster->eyes[i];

            tempEyes[i].sclera = Mesh_Create();
            tempEyes[i].pupil = Mesh_Create();

            /* Ojo con escala cero o casi cero: mantener la ranura lógica con mallas vacías */
            if (eye->scale.x <= EYE_VISIBILITY_EPS ||
                eye->scale.y <= EYE_VISIBILITY_EPS ||
                eye->scale.z <= EYE_VISIBILITY_EPS) {
                continue;
            }

            Vector3 partPos = Vec3_Zero();
            if (eye->bodyPartIndex < monster->bodyPartCount) {
                partPos = monster->bodyParts[eye->bodyPartIndex].positionRender;
            }

            Vector3 eyePos = Vec3_Add(partPos, eye->offset);
            Vector3 eyeRadii = Vec3_Scale(eye->scale, 0.5f);

            Transform3D scleraTrans = Transform3D_Create(eyePos, eye->rotation, eyeRadii);

            /* Dirección frontal local rotada para ubicar la pupila sobre la superficie de la esclerótica */
            Vector3 pupilForward = Transform3D_RotateVector(eye->rotation, Vec3_Create(0.0f, 0.0f, 1.0f));
            float zOffset = eyeRadii.z * 0.90f;
            Vector3 pupilCenter = Vec3_Add(eyePos, Vec3_Scale(pupilForward, zOffset));

            float pupilRadius = Math_Min(eyeRadii.x, eyeRadii.y) * Math_Clamp01(eye->pupilScale) * 0.5f;
            pupilRadius = Math_Max(pupilRadius, 0.01f);
            Transform3D pupilTrans = Transform3D_Create(pupilCenter, eye->rotation, Vec3_Create(pupilRadius, pupilRadius, pupilRadius * 0.2f));

            if (!PrimitiveMesh_GenerateEllipsoid(&tempEyes[i].sclera, scleraTrans, 16, 12, eye->scleraColor) ||
                !PrimitiveMesh_GenerateEllipsoid(&tempEyes[i].pupil, pupilTrans, 12, 8, eye->pupilColor)) {
                eyeSuccess = false;
                break;
            }
        }

        if (!eyeSuccess) {
            for (size_t i = 0; i < tempEyeCount; ++i) {
                Mesh_Free(&tempEyes[i].sclera);
                Mesh_Free(&tempEyes[i].pupil);
            }
            free(tempEyes);
            return false;
        }
    }

    /* ÉXITO TOTAL: Intercambiar atómicamente buffers activos y staging */
    MonsterSDF tmpSDF = visual->sdf;
    visual->sdf = visual->stagingSdf;
    visual->stagingSdf = tmpSDF;

    Mesh tmpMesh = visual->mesh;
    visual->mesh = visual->stagingMesh;
    visual->stagingMesh = tmpMesh;

    for (size_t i = 0; i < visual->eyeCount; ++i) {
        Mesh_Free(&visual->eyes[i].sclera);
        Mesh_Free(&visual->eyes[i].pupil);
    }
    if (visual->eyes) free(visual->eyes);

    visual->eyes = tempEyes;
    visual->eyeCount = tempEyeCount;
    visual->eyeCapacity = tempEyeCount;

    visual->geometryFingerprint = MonsterVisual_ComputeFingerprint(visual, monster, sdfConfig);
    visual->hasFingerprint = true;
    visual->isDirty = false;
    visual->updateTimer = 0.0f;
    visual->rebuildGeneration++;

    return true;
}

bool MonsterVisual_Update(
    MonsterVisual* visual,
    const Monster* monster,
    float deltaTime,
    float rebuildInterval,
    MonsterSDFConfig sdfConfig
) {
    if (!visual || !monster) return false;

    visual->updateTimer += deltaTime;

    bool timeTriggered = (rebuildInterval > 0.0f) && (visual->updateTimer >= rebuildInterval);
    bool geometryChanged = !visual->hasFingerprint ||
                           (visual->geometryFingerprint != MonsterVisual_ComputeFingerprint(visual, monster, sdfConfig));

    if (visual->isDirty || timeTriggered || geometryChanged || visual->mesh.vertexCount == 0) {
        return MonsterVisual_RebuildNow(visual, monster, sdfConfig);
    }

    return false;
}

const Mesh* MonsterVisual_GetMesh(const MonsterVisual* visual) {
    return visual ? &visual->mesh : NULL;
}

size_t MonsterVisual_GetEyeCount(const MonsterVisual* visual) {
    return visual ? visual->eyeCount : 0;
}

const Mesh* MonsterVisual_GetEyeSclera(const MonsterVisual* visual, size_t index) {
    if (!visual || index >= visual->eyeCount) return NULL;
    return &visual->eyes[index].sclera;
}

const Mesh* MonsterVisual_GetEyePupil(const MonsterVisual* visual, size_t index) {
    if (!visual || index >= visual->eyeCount) return NULL;
    return &visual->eyes[index].pupil;
}

bool MonsterVisual_Render(const MonsterVisual* visual, Renderer3D* renderer) {
    if (!visual || !renderer || !renderer->renderMesh) return false;

    renderer->renderMesh(renderer, &visual->mesh);
    for (size_t i = 0; i < visual->eyeCount; ++i) {
        renderer->renderMesh(renderer, &visual->eyes[i].sclera);
        renderer->renderMesh(renderer, &visual->eyes[i].pupil);
    }
    return true;
}
