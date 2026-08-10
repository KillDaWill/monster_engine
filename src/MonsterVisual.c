#include "MonsterVisual.h"
#include "PrimitiveMesh.h"
#include "MathUtils.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

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

static uint64_t MonsterVisual_ComputeBodyFingerprint(const MonsterVisual* visual, const Monster* monster, MonsterSDFConfig sdfConfig) {
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

    /* Bocas (sólo parámetros del corte estático del cuerpo) */
    hash = HashSizeT(monster->mouthCount, hash);
    for (size_t i = 0; i < monster->mouthCount; ++i) {
        const Mouth* mouth = &monster->mouths[i];
        hash = HashSizeT(mouth->bodyPartIndex, hash);
        hash = HashVector3(mouth->offset, hash);
        hash = HashVector3(mouth->rotation, hash);
        hash = HashVector3(mouth->scale, hash);
        hash = HashColor(mouth->insideColor, hash);
    }

    /* Ojos (sólo parámetros del cuerpo) */
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

static uint64_t MonsterVisual_ComputeMouthVisualFingerprint(const Monster* monster) {
    uint64_t hash = 0xcbf29ce484222325ULL;
    if (!monster) return hash;

    hash = HashSizeT(monster->mouthCount, hash);
    for (size_t i = 0; i < monster->mouthCount; ++i) {
        const Mouth* mouth = &monster->mouths[i];
        hash = HashSizeT(mouth->bodyPartIndex, hash);
        if (mouth->bodyPartIndex < monster->bodyPartCount) {
            hash = HashVector3(monster->bodyParts[mouth->bodyPartIndex].positionRender, hash);
        }
        hash = HashVector3(mouth->offset, hash);
        hash = HashVector3(mouth->rotation, hash);
        hash = HashVector3(mouth->scale, hash);
        hash = HashFloat(mouth->openFactor, hash);
        hash = HashFloat(mouth->lipThickness, hash);
        hash = HashFloat(mouth->lipCurvature, hash);
        hash = HashFloat(mouth->lipProtrusion, hash);
        hash = HashColor(mouth->insideColor, hash);
        hash = HashColor(mouth->lipColor, hash);
    }
    return hash;
}

static void TransformMeshToWorld(Mesh* mesh, Vector3 worldPos, Vector3 rotation) {
    for (size_t i = 0; i < mesh->vertexCount; ++i) {
        Vector3 rotPos = Transform3D_RotateVector(rotation, mesh->vertices[i].position);
        mesh->vertices[i].position = Vec3_Add(worldPos, rotPos);
        mesh->vertices[i].normal = Transform3D_RotateVector(rotation, mesh->vertices[i].normal);
    }
}

static bool MonsterVisual_BuildMouthMeshes(MonsterVisualMouth* visualMouth, const Mouth* mouth, const Monster* monster) {
    if (!visualMouth || !mouth || !monster) return false;

    visualMouth->upperLip = Mesh_Create();
    visualMouth->lowerLip = Mesh_Create();
    visualMouth->innerCavity = Mesh_Create();

    Vector3 partPos = Vec3_Zero();
    if (mouth->bodyPartIndex < monster->bodyPartCount) {
        partPos = monster->bodyParts[mouth->bodyPartIndex].positionRender;
    }
    Vector3 mouthWorldPos = Vec3_Add(partPos, mouth->offset);

    float width = Math_Max(mouth->scale.x, 0.0001f);
    float maxOpening = Math_Max(mouth->scale.y, 0.0001f);
    float depth = Math_Max(mouth->scale.z, 0.0001f);
    float openF = Math_Clamp01(mouth->openFactor);

    float lipThickness = mouth->lipThickness > 0.0f ? mouth->lipThickness : (width * 0.08f);
    float lipCurvature = mouth->lipCurvature > 0.0f ? mouth->lipCurvature : 0.20f;
    float lipProtrusion = mouth->lipProtrusion > 0.0f ? mouth->lipProtrusion : (depth * 0.15f);

    Color lipCol = (mouth->lipColor.a > 0) ? mouth->lipColor : Color_FromRGB(220, 90, 100);
    Color insideCol = mouth->insideColor;

    float currentOpening = maxOpening * openF;

    Vector3 p0 = Vec3_Create(-width * 0.5f, 0.0f, 0.0f);
    Vector3 p2 = Vec3_Create(width * 0.5f, 0.0f, 0.0f);

    Vector3 p1_upper = Vec3_Create(0.0f, currentOpening * 0.5f + width * lipCurvature * 0.25f, lipProtrusion);
    Vector3 p1_lower = Vec3_Create(0.0f, -currentOpening * 0.5f - width * lipCurvature * 0.25f, lipProtrusion);

    Mesh upperLocal = Mesh_Create();
    Mesh lowerLocal = Mesh_Create();
    Mesh cavityLocal = Mesh_Create();

    if (!PrimitiveMesh_GenerateQuadraticBezierTube(&upperLocal, p0, p1_upper, p2, lipThickness, 16, 8, lipCol) ||
        !PrimitiveMesh_GenerateQuadraticBezierTube(&lowerLocal, p0, p1_lower, p2, lipThickness, 16, 8, lipCol)) {
        Mesh_Free(&upperLocal);
        Mesh_Free(&lowerLocal);
        Mesh_Free(&cavityLocal);
        return false;
    }

    Vector3 cavityCenterLocal = Vec3_Create(0.0f, 0.0f, -depth * 0.50f);
    Vector3 cavityRadii = Vec3_Create(
        width * 0.42f,
        Math_Max(currentOpening * 0.65f + maxOpening * 0.10f, width * 0.15f),
        depth * 0.55f
    );
    Transform3D cavityTransform = Transform3D_Create(cavityCenterLocal, Vec3_Zero(), cavityRadii);

    if (!PrimitiveMesh_GenerateEllipsoidEx(&cavityLocal, cavityTransform, 16, 12, insideCol, true)) {
        Mesh_Free(&upperLocal);
        Mesh_Free(&lowerLocal);
        Mesh_Free(&cavityLocal);
        return false;
    }

    TransformMeshToWorld(&upperLocal, mouthWorldPos, mouth->rotation);
    TransformMeshToWorld(&lowerLocal, mouthWorldPos, mouth->rotation);
    TransformMeshToWorld(&cavityLocal, mouthWorldPos, mouth->rotation);

    visualMouth->upperLip = upperLocal;
    visualMouth->lowerLip = lowerLocal;
    visualMouth->innerCavity = cavityLocal;

    return true;
}

static bool MonsterVisual_RebuildMouthsOnly(MonsterVisual* visual, const Monster* monster) {
    if (!visual || !monster) return false;

    size_t newMouthCount = monster->mouthCount;
    MonsterVisualMouth* newMouths = NULL;

    if (newMouthCount > 0) {
        newMouths = (MonsterVisualMouth*)calloc(newMouthCount, sizeof(MonsterVisualMouth));
        if (!newMouths) return false;

        for (size_t m = 0; m < newMouthCount; ++m) {
            if (!MonsterVisual_BuildMouthMeshes(&newMouths[m], &monster->mouths[m], monster)) {
                for (size_t k = 0; k <= m; ++k) {
                    Mesh_Free(&newMouths[k].upperLip);
                    Mesh_Free(&newMouths[k].lowerLip);
                    Mesh_Free(&newMouths[k].innerCavity);
                }
                free(newMouths);
                return false;
            }
        }
    }

    for (size_t m = 0; m < visual->mouthCount; ++m) {
        Mesh_Free(&visual->mouths[m].upperLip);
        Mesh_Free(&visual->mouths[m].lowerLip);
        Mesh_Free(&visual->mouths[m].innerCavity);
    }
    if (visual->mouths) free(visual->mouths);

    visual->mouths = newMouths;
    visual->mouthCount = newMouthCount;
    visual->mouthCapacity = newMouthCount;
    visual->mouthVisualFingerprint = MonsterVisual_ComputeMouthVisualFingerprint(monster);
    return true;
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
    SDFMesher_Free(&visual->mesher);
    Mesh_Free(&visual->mesh);
    Mesh_Free(&visual->stagingMesh);
    for (size_t i = 0; i < visual->eyeCount; ++i) {
        Mesh_Free(&visual->eyes[i].sclera);
        Mesh_Free(&visual->eyes[i].pupil);
    }
    if (visual->eyes) free(visual->eyes);
    visual->eyes = NULL;
    visual->eyeCount = 0;
    visual->eyeCapacity = 0;

    for (size_t m = 0; m < visual->mouthCount; ++m) {
        Mesh_Free(&visual->mouths[m].upperLip);
        Mesh_Free(&visual->mouths[m].lowerLip);
        Mesh_Free(&visual->mouths[m].innerCavity);
    }
    if (visual->mouths) free(visual->mouths);
    visual->mouths = NULL;
    visual->mouthCount = 0;
    visual->mouthCapacity = 0;

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

    MonsterVisual_RebuildMouthsOnly(visual, monster);

    visual->geometryFingerprint = MonsterVisual_ComputeBodyFingerprint(visual, monster, sdfConfig);
    visual->mouthVisualFingerprint = MonsterVisual_ComputeMouthVisualFingerprint(monster);
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
    float minRebuildInterval,
    MonsterSDFConfig sdfConfig
) {
    if (!visual || !monster) return false;

    visual->updateTimer += deltaTime;

    uint64_t bodyFp = MonsterVisual_ComputeBodyFingerprint(visual, monster, sdfConfig);
    bool geometryChanged = !visual->hasFingerprint || (visual->geometryFingerprint != bodyFp);

    bool needsRebuild = visual->isDirty || geometryChanged || visual->mesh.vertexCount == 0;

    if (needsRebuild) {
        if (minRebuildInterval > 0.0f && visual->mesh.vertexCount > 0 && visual->updateTimer < minRebuildInterval) {
            uint64_t mouthFp = MonsterVisual_ComputeMouthVisualFingerprint(monster);
            if (mouthFp != visual->mouthVisualFingerprint) {
                MonsterVisual_RebuildMouthsOnly(visual, monster);
            }
            return false;
        }

        return MonsterVisual_RebuildNow(visual, monster, sdfConfig);
    }

    uint64_t mouthFp = MonsterVisual_ComputeMouthVisualFingerprint(monster);
    if (mouthFp != visual->mouthVisualFingerprint || visual->mouthCount != monster->mouthCount) {
        MonsterVisual_RebuildMouthsOnly(visual, monster);
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

size_t MonsterVisual_GetMouthCount(const MonsterVisual* visual) {
    return visual ? visual->mouthCount : 0;
}

const Mesh* MonsterVisual_GetUpperLip(const MonsterVisual* visual, size_t index) {
    if (!visual || index >= visual->mouthCount) return NULL;
    return &visual->mouths[index].upperLip;
}

const Mesh* MonsterVisual_GetLowerLip(const MonsterVisual* visual, size_t index) {
    if (!visual || index >= visual->mouthCount) return NULL;
    return &visual->mouths[index].lowerLip;
}

const Mesh* MonsterVisual_GetInnerCavity(const MonsterVisual* visual, size_t index) {
    if (!visual || index >= visual->mouthCount) return NULL;
    return &visual->mouths[index].innerCavity;
}

bool MonsterVisual_Render(const MonsterVisual* visual, Renderer3D* renderer) {
    if (!visual || !renderer || !renderer->renderMesh) return false;

    renderer->renderMesh(renderer, &visual->mesh);
    for (size_t i = 0; i < visual->eyeCount; ++i) {
        renderer->renderMesh(renderer, &visual->eyes[i].sclera);
        renderer->renderMesh(renderer, &visual->eyes[i].pupil);
    }
    for (size_t m = 0; m < visual->mouthCount; ++m) {
        renderer->renderMesh(renderer, &visual->mouths[m].innerCavity);
        renderer->renderMesh(renderer, &visual->mouths[m].upperLip);
        renderer->renderMesh(renderer, &visual->mouths[m].lowerLip);
    }
    return true;
}
