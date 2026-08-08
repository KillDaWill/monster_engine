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

static uint64_t MonsterVisual_ComputeFingerprint(const Monster* monster, MonsterSDFConfig sdfConfig) {
    uint64_t hash = 0xcbf29ce484222325ULL;

    hash = Fnv1a64Bytes(&sdfConfig, sizeof(sdfConfig), hash);
    hash = Fnv1a64Bytes(&monster->bodyPartCount, sizeof(monster->bodyPartCount), hash);

    for (size_t i = 0; i < monster->bodyPartCount; ++i) {
        const BodyPart* part = &monster->bodyParts[i];
        hash = Fnv1a64Bytes(&part->positionRender, sizeof(part->positionRender), hash);
        hash = Fnv1a64Bytes(&part->widthRender, sizeof(part->widthRender), hash);
        hash = Fnv1a64Bytes(&part->heightRender, sizeof(part->heightRender), hash);
        hash = Fnv1a64Bytes(&part->lengthRender, sizeof(part->lengthRender), hash);
        hash = Fnv1a64Bytes(&part->color.index, sizeof(part->color.index), hash);
    }

    hash = Fnv1a64Bytes(&monster->mouthCount, sizeof(monster->mouthCount), hash);
    for (size_t i = 0; i < monster->mouthCount; ++i) {
        const Mouth* mouth = &monster->mouths[i];
        hash = Fnv1a64Bytes(&mouth->bodyPartIndex, sizeof(mouth->bodyPartIndex), hash);
        hash = Fnv1a64Bytes(&mouth->offset, sizeof(mouth->offset), hash);
        hash = Fnv1a64Bytes(&mouth->scale, sizeof(mouth->scale), hash);
        hash = Fnv1a64Bytes(&mouth->openFactor, sizeof(mouth->openFactor), hash);
        hash = Fnv1a64Bytes(&mouth->insideColor, sizeof(mouth->insideColor), hash);
        hash = Fnv1a64Bytes(&mouth->lipColor, sizeof(mouth->lipColor), hash);
    }

    hash = Fnv1a64Bytes(&monster->eyeCount, sizeof(monster->eyeCount), hash);
    for (size_t i = 0; i < monster->eyeCount; ++i) {
        const Eye* eye = &monster->eyes[i];
        hash = Fnv1a64Bytes(&eye->bodyPartIndex, sizeof(eye->bodyPartIndex), hash);
        hash = Fnv1a64Bytes(&eye->offset, sizeof(eye->offset), hash);
        hash = Fnv1a64Bytes(&eye->scale, sizeof(eye->scale), hash);
        hash = Fnv1a64Bytes(&eye->pupilScale, sizeof(eye->pupilScale), hash);
        hash = Fnv1a64Bytes(&eye->scleraColor, sizeof(eye->scleraColor), hash);
        hash = Fnv1a64Bytes(&eye->pupilColor, sizeof(eye->pupilColor), hash);
    }

    return hash;
}

static bool MonsterVisual_RebuildEyes(MonsterVisual* visual, const Monster* monster) {
    for (size_t i = 0; i < visual->eyeCount; ++i) {
        Mesh_Free(&visual->eyes[i].sclera);
        Mesh_Free(&visual->eyes[i].pupil);
    }
    visual->eyeCount = 0;

    if (!monster || monster->eyeCount == 0) {
        return true;
    }

    if (monster->eyeCount > visual->eyeCapacity) {
        size_t newCapacity = (visual->eyeCapacity > 0) ? visual->eyeCapacity : 1;
        while (newCapacity < monster->eyeCount) {
            newCapacity *= 2;
        }
        MonsterVisualEye* grown = (MonsterVisualEye*)realloc(visual->eyes, newCapacity * sizeof(MonsterVisualEye));
        if (!grown) {
            return false;
        }
        visual->eyes = grown;
        visual->eyeCapacity = newCapacity;
    }

    for (size_t i = 0; i < monster->eyeCount; ++i) {
        const Eye* eye = &monster->eyes[i];

        Vector3 partPos = Vec3_Zero();
        if (eye->bodyPartIndex < monster->bodyPartCount) {
            partPos = monster->bodyParts[eye->bodyPartIndex].positionRender;
        }
        Vector3 eyeCenter = Vec3_Add(partPos, eye->offset);

        float scleraRadius = 0.5f * Math_Max(eye->scale.x, Math_Max(eye->scale.y, 0.001f));
        float pupilFactor = Math_Clamp01(eye->pupilScale);
        float pupilRadius = scleraRadius * Math_Max(pupilFactor, 0.05f);
        Vector3 pupilCenter = Vec3_Add(eyeCenter, Vec3_Create(0.0f, 0.0f, scleraRadius * (1.0f - 0.3f * pupilFactor)));

        MonsterVisualEye* entry = &visual->eyes[i];
        entry->sclera = Mesh_Create();
        entry->pupil = Mesh_Create();

        if (!PrimitiveMesh_GenerateUVSphere(&entry->sclera, eyeCenter, scleraRadius, 12, 8, eye->scleraColor)) {
            return false;
        }
        if (!PrimitiveMesh_GenerateUVSphere(&entry->pupil, pupilCenter, pupilRadius, 12, 8, eye->pupilColor)) {
            return false;
        }
        visual->eyeCount = i + 1;
    }

    return true;
}

MonsterVisual MonsterVisual_Create(SDFMesherConfig mesherConfig) {
    MonsterVisual visual;
    memset(&visual, 0, sizeof(MonsterVisual));
    visual.sdf = MonsterSDF_Create();
    visual.mesher = SDFMesher_Create(mesherConfig);
    visual.mesh = Mesh_Create();
    visual.isDirty = true;
    visual.updateTimer = 0.0f;
    return visual;
}

void MonsterVisual_Free(MonsterVisual* visual) {
    if (!visual) return;
    MonsterSDF_Free(&visual->sdf);
    Mesh_Free(&visual->mesh);
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
}

void MonsterVisual_MarkDirty(MonsterVisual* visual) {
    if (visual) visual->isDirty = true;
}

bool MonsterVisual_RebuildNow(
    MonsterVisual* visual,
    const Monster* monster,
    MonsterSDFConfig sdfConfig
) {
    if (!visual || !monster) return false;

    if (!MonsterSDF_Build(&visual->sdf, monster, sdfConfig)) {
        return false;
    }

    SDFField field = MonsterSDF_GetField(&visual->sdf);
    if (!SDFMesher_GenerateMesh(&visual->mesher, &field, &visual->mesh)) {
        return false;
    }

    if (!MonsterVisual_RebuildEyes(visual, monster)) {
        return false;
    }

    visual->geometryFingerprint = MonsterVisual_ComputeFingerprint(monster, sdfConfig);
    visual->hasFingerprint = true;
    visual->isDirty = false;
    visual->updateTimer = 0.0f;
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
                           (visual->geometryFingerprint != MonsterVisual_ComputeFingerprint(monster, sdfConfig));

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

bool MonsterVisual_Render(const MonsterVisual* visual, MonsterRenderer* renderer) {
    if (!visual || !renderer || !renderer->renderMesh) return false;

    renderer->renderMesh(renderer, &visual->mesh);
    for (size_t i = 0; i < visual->eyeCount; ++i) {
        renderer->renderMesh(renderer, &visual->eyes[i].sclera);
        renderer->renderMesh(renderer, &visual->eyes[i].pupil);
    }
    return true;
}
