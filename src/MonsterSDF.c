#include "MonsterSDF.h"
#include "Monster.h"
#include "SDFPrimitives.h"
#include "MathUtils.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

MonsterSDFConfig MonsterSDF_DefaultConfig(void) {
    return (MonsterSDFConfig){
        .bodySmoothness = 0.5f,
        .connectionSmoothness = 0.4f,
        .mouthSmoothness = 0.25f,
        .connectionRadiusFactor = 0.85f,
        .boundsPadding = 0.7f
    };
}

MonsterSDF MonsterSDF_Create(void) {
    MonsterSDF sdf;
    memset(&sdf, 0, sizeof(MonsterSDF));
    sdf.config = MonsterSDF_DefaultConfig();
    sdf.bounds = AABB_Empty();
    return sdf;
}

void MonsterSDF_Free(MonsterSDF* sdf) {
    if (!sdf) return;

    if (sdf->bodyParts) {
        free(sdf->bodyParts);
        sdf->bodyParts = NULL;
    }
    if (sdf->connectors) {
        free(sdf->connectors);
        sdf->connectors = NULL;
    }
    if (sdf->mouths) {
        free(sdf->mouths);
        sdf->mouths = NULL;
    }

    sdf->bodyPartCount = 0;
    sdf->bodyPartCapacity = 0;
    sdf->connectorCount = 0;
    sdf->connectorCapacity = 0;
    sdf->mouthCount = 0;
    sdf->mouthCapacity = 0;
    sdf->bounds = AABB_Empty();
}

static bool MonsterSDF_EnsureCapacity(void** buffer, size_t elementSize, size_t* capacity, size_t needed) {
    if (needed <= *capacity) return true;

    size_t newCapacity = 0;
    if (!Math_GrowCapacity(*capacity, needed, elementSize, &newCapacity)) return false;

    void* grown = realloc(*buffer, newCapacity * elementSize);
    if (!grown) return false;

    *buffer = grown;
    *capacity = newCapacity;
    return true;
}

bool MonsterSDF_Build(MonsterSDF* sdf, const Monster* monster, MonsterSDFConfig config) {
    if (!sdf) return false;

    sdf->config = config;
    sdf->bounds = AABB_Empty();
    sdf->bodyPartCount = 0;
    sdf->connectorCount = 0;
    sdf->mouthCount = 0;

    if (!monster || monster->bodyPartCount == 0) {
        sdf->bounds = AABB_FromMinMax(Vec3_Create(-1.0f, -1.0f, -1.0f), Vec3_Create(1.0f, 1.0f, 1.0f));
        return true;
    }

    /* 1. Construir partes del cuerpo */
    sdf->bodyPartCount = monster->bodyPartCount;
    if (!MonsterSDF_EnsureCapacity((void**)&sdf->bodyParts, sizeof(MonsterSDFBodyPart), &sdf->bodyPartCapacity, sdf->bodyPartCount)) {
        MonsterSDF_Free(sdf);
        return false;
    }

    for (size_t i = 0; i < monster->bodyPartCount; ++i) {
        const BodyPart* part = &monster->bodyParts[i];
        sdf->bodyParts[i].center = part->positionRender;
        sdf->bodyParts[i].radii = Vec3_Create(
            part->widthRender * 0.5f,
            part->heightRender * 0.5f,
            part->lengthRender * 0.5f
        );
        sdf->bodyParts[i].color = Monster_GetColorFromIndexStruct(monster, part->color);

        AABB_ExpandRadius(&sdf->bounds, sdf->bodyParts[i].center, sdf->bodyParts[i].radii);
    }

    /* 2. Construir conectores */
    if (monster->bodyPartCount > 1) {
        sdf->connectorCount = monster->bodyPartCount - 1;
        if (!MonsterSDF_EnsureCapacity((void**)&sdf->connectors, sizeof(MonsterSDFConnector), &sdf->connectorCapacity, sdf->connectorCount)) {
            MonsterSDF_Free(sdf);
            return false;
        }

        for (size_t i = 0; i < monster->bodyPartCount - 1; ++i) {
            const BodyPart* p1 = &monster->bodyParts[i];
            const BodyPart* p2 = &monster->bodyParts[i + 1];

            /* Usar radio de sección transversal sin distorsionar por longitud */
            float r1 = sqrtf(Math_Max(p1->widthRender * 0.5f * p1->heightRender * 0.5f, 0.0001f)) * config.connectionRadiusFactor;
            float r2 = sqrtf(Math_Max(p2->widthRender * 0.5f * p2->heightRender * 0.5f, 0.0001f)) * config.connectionRadiusFactor;

            sdf->connectors[i].a = p1->positionRender;
            sdf->connectors[i].b = p2->positionRender;
            sdf->connectors[i].r1 = r1;
            sdf->connectors[i].r2 = r2;

            Color c1 = Monster_GetColorFromIndexStruct(monster, p1->color);
            Color c2 = Monster_GetColorFromIndexStruct(monster, p2->color);
            sdf->connectors[i].color = Color_Lerp(c1, c2, 0.5f);

            AABB_ExpandRadius(&sdf->bounds, sdf->connectors[i].a, Vec3_Create(r1, r1, r1));
            AABB_ExpandRadius(&sdf->bounds, sdf->connectors[i].b, Vec3_Create(r2, r2, r2));
        }
    }

    /* 3. Construir cavidades bucales */
    if (monster->mouthCount > 0) {
        sdf->mouthCount = monster->mouthCount;
        if (!MonsterSDF_EnsureCapacity((void**)&sdf->mouths, sizeof(MonsterSDFMouth), &sdf->mouthCapacity, sdf->mouthCount)) {
            MonsterSDF_Free(sdf);
            return false;
        }

        for (size_t m = 0; m < monster->mouthCount; ++m) {
            const Mouth* mouth = &monster->mouths[m];

            Vector3 partPos = Vec3_Zero();
            if (mouth->bodyPartIndex < monster->bodyPartCount) {
                partPos = monster->bodyParts[mouth->bodyPartIndex].positionRender;
            }

            Vector3 mouthWorldPos = Vec3_Add(partPos, mouth->offset);
            sdf->mouths[m].transform = Transform3D_Create(mouthWorldPos, mouth->rotation, Vec3_Create(1.0f, 1.0f, 1.0f));

            float openF = Math_Clamp01(mouth->openFactor);
            sdf->mouths[m].radii = Vec3_Create(
                mouth->scale.x * 0.5f,
                mouth->scale.y * 0.5f * (0.15f + 0.85f * openF),
                mouth->scale.z * (0.30f + 0.70f * openF)
            );

            sdf->mouths[m].insideColor = mouth->insideColor;
            sdf->mouths[m].lipColor = mouth->lipColor;
            sdf->mouths[m].openFactor = openF;

            /* Nota: Las bocas son cavidades sustractivas y NO expanden los límites exteriores del monstruo */
        }
    }

    /* Padding de los bounds */
    float pad = config.boundsPadding + config.bodySmoothness;
    AABB_Pad(&sdf->bounds, pad);

    return true;
}

SDFSample MonsterSDF_Evaluate(const MonsterSDF* sdf, Vector3 point) {
    if (!sdf || sdf->bodyPartCount == 0) {
        return SDFSample_Create(1e6f, COLOR_WHITE, SDF_MATERIAL_UNKNOWN);
    }

    SDFSample accumulated = SDFSample_Create(1e6f, COLOR_WHITE, SDF_MATERIAL_SKIN);
    bool hasInitialSample = false;

    /* 1. Evaluar elipsoides para cada BodyPart */
    for (size_t i = 0; i < sdf->bodyPartCount; ++i) {
        const MonsterSDFBodyPart* part = &sdf->bodyParts[i];
        if (part->radii.x < 0.0001f || part->radii.y < 0.0001f || part->radii.z < 0.0001f) {
            continue;
        }

        Vector3 pLocal = Vec3_Sub(point, part->center);
        float dist = SDF_Ellipsoid(pLocal, part->radii);
        SDFSample partSample = SDFSample_Create(dist, part->color, SDF_MATERIAL_SKIN);

        if (!hasInitialSample) {
            accumulated = partSample;
            hasInitialSample = true;
        } else {
            accumulated = SDFSample_SmoothUnion(accumulated, partSample, sdf->config.bodySmoothness);
        }
    }

    /* 2. Evaluar conectores cónicos entre partes */
    for (size_t i = 0; i < sdf->connectorCount; ++i) {
        const MonsterSDFConnector* conn = &sdf->connectors[i];
        if (conn->r1 < 0.0001f && conn->r2 < 0.0001f) continue;

        float dist = SDF_TaperedCapsuleApprox(point, conn->a, conn->b, conn->r1, conn->r2);
        SDFSample connSample = SDFSample_Create(dist, conn->color, SDF_MATERIAL_SKIN);

        if (!hasInitialSample) {
            accumulated = connSample;
            hasInitialSample = true;
        } else {
            accumulated = SDFSample_SmoothUnion(accumulated, connSample, sdf->config.connectionSmoothness);
        }
    }

    /* 3. Evaluar cavidades bucales mediante sustracción suave */
    for (size_t m = 0; m < sdf->mouthCount; ++m) {
        const MonsterSDFMouth* mouth = &sdf->mouths[m];
        if (mouth->radii.x < 0.0001f || mouth->radii.y < 0.0001f || mouth->radii.z < 0.0001f) {
            continue;
        }

        Vector3 localP = Transform3D_WorldToLocalPoint(mouth->transform, point);
        float cutterDist = SDF_Ellipsoid(localP, mouth->radii);

        float normX = localP.x / Math_Max(mouth->radii.x, 0.0001f);
        float normY = localP.y / Math_Max(mouth->radii.y, 0.0001f);
        float radial = sqrtf(normX * normX + normY * normY);

        Color mouthColor;
        SDFMaterial mat;
        if (radial >= 0.75f && mouth->lipColor.a > 0) {
            mouthColor = mouth->lipColor;
            mat = SDF_MATERIAL_LIP;
        } else {
            mouthColor = mouth->insideColor;
            mat = SDF_MATERIAL_MOUTH;
        }

        SDFSample cutterSample = SDFSample_Create(cutterDist, mouthColor, mat);
        accumulated = SDFSample_Subtract(accumulated, cutterSample, sdf->config.mouthSmoothness);
    }

    return accumulated;
}

SDFSample MonsterSDF_EvaluateWrapper(const void* context, Vector3 point) {
    return MonsterSDF_Evaluate((const MonsterSDF*)context, point);
}

AABB3D MonsterSDF_GetBounds(const MonsterSDF* sdf) {
    if (!sdf) return AABB_Empty();
    return sdf->bounds;
}

AABB3D MonsterSDF_GetBoundsWrapper(const void* context) {
    return MonsterSDF_GetBounds((const MonsterSDF*)context);
}

SDFField MonsterSDF_GetField(const MonsterSDF* sdf) {
    return (SDFField){
        .evaluate = MonsterSDF_EvaluateWrapper,
        .getBounds = MonsterSDF_GetBoundsWrapper,
        .context = (const void*)sdf
    };
}
