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

        float rx = Math_Max(part->widthRender * 0.5f, 0.0001f);
        float ry = Math_Max(part->heightRender * 0.5f, 0.0001f);
        float rz = Math_Max(part->lengthRender * 0.5f, 0.0001f);

        sdf->bodyParts[i].radii = Vec3_Create(rx, ry, rz);
        sdf->bodyParts[i].invRadii = Vec3_Create(1.0f / rx, 1.0f / ry, 1.0f / rz);
        sdf->bodyParts[i].invRadiiSquared = Vec3_Create(
            sdf->bodyParts[i].invRadii.x * sdf->bodyParts[i].invRadii.x,
            sdf->bodyParts[i].invRadii.y * sdf->bodyParts[i].invRadii.y,
            sdf->bodyParts[i].invRadii.z * sdf->bodyParts[i].invRadii.z
        );
        sdf->bodyParts[i].minRadius = Math_Min(rx, Math_Min(ry, rz));
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
            sdf->connectors[i].ba = Vec3_Sub(p2->positionRender, p1->positionRender);

            float baLenSq = Vec3_Dot(sdf->connectors[i].ba, sdf->connectors[i].ba);
            sdf->connectors[i].invBaLengthSquared = (baLenSq > 1e-8f) ? (1.0f / baLenSq) : 0.0f;
            sdf->connectors[i].r1 = r1;
            sdf->connectors[i].r2 = r2;
            sdf->connectors[i].radiusDelta = r2 - r1;

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
            sdf->mouths[m].center = mouthWorldPos;
            sdf->mouths[m].inverseRotation = Transform3D_BuildInverseRotationBasis(mouth->rotation);

            float width = Math_Max(mouth->scale.x, 0.0001f);
            float maxOpening = Math_Max(mouth->scale.y, 0.0001f);
            float depth = Math_Max(mouth->scale.z, 0.0001f);

            float halfWidth = width * 0.5f;
            float halfHeight = maxOpening * 0.5f;
            float halfDepth = depth * 0.35f;

            sdf->mouths[m].entranceCenterLocal = Vec3_Create(0.0f, 0.0f, -depth * 0.10f);
            sdf->mouths[m].entranceHalfExtents = Vec3_Create(halfWidth, halfHeight, halfDepth);

            sdf->mouths[m].cavityCenterLocal = Vec3_Create(0.0f, 0.0f, -depth * 0.55f);
            sdf->mouths[m].cavityRadii = Vec3_Create(
                width * 0.45f,
                Math_Max(maxOpening * 0.70f, width * 0.20f),
                depth * 0.65f
            );

            sdf->mouths[m].insideColor = mouth->insideColor;
            sdf->mouths[m].entranceToCavitySmoothness = Math_Min(maxOpening, depth) * 0.15f;

            /* AABB conservador para descarte de boca */
            RotationBasis3D invRot = sdf->mouths[m].inverseRotation;
            float rx = width * 0.8f;
            float ry = maxOpening * 0.8f;
            float rz = depth * 1.2f;

            float extX = fabsf(invRot.row0.x) * rx + fabsf(invRot.row1.x) * ry + fabsf(invRot.row2.x) * rz;
            float extY = fabsf(invRot.row0.y) * rx + fabsf(invRot.row1.y) * ry + fabsf(invRot.row2.y) * rz;
            float extZ = fabsf(invRot.row0.z) * rx + fabsf(invRot.row1.z) * ry + fabsf(invRot.row2.z) * rz;

            float mouthPad = 0.10f;
            extX += mouthPad;
            extY += mouthPad;
            extZ += mouthPad;

            sdf->mouths[m].influenceBounds = (AABB3D){
                .start = Vec3_Sub(mouthWorldPos, Vec3_Create(extX, extY, extZ)),
                .end   = Vec3_Add(mouthWorldPos, Vec3_Create(extX, extY, extZ))
            };
        }
    }

    /* Padding de los bounds globales */
    float pad = config.boundsPadding + config.bodySmoothness;
    AABB_Pad(&sdf->bounds, pad);

    return true;
}

static inline float MonsterSDF_EvalBodyPartDistance(const MonsterSDFBodyPart* part, Vector3 point) {
    Vector3 pLocal = Vec3_Sub(point, part->center);
    Vector3 scaledP = Vec3_Create(pLocal.x * part->invRadii.x, pLocal.y * part->invRadii.y, pLocal.z * part->invRadii.z);
    float k0 = Vec3_Length(scaledP);

    Vector3 scaledP2 = Vec3_Create(pLocal.x * part->invRadiiSquared.x, pLocal.y * part->invRadiiSquared.y, pLocal.z * part->invRadiiSquared.z);
    float k1 = Vec3_Length(scaledP2);

    if (k0 < 1e-6f || k1 < 1e-6f) {
        return -part->minRadius;
    }
    return k0 * (k0 - 1.0f) / k1;
}

static inline float MonsterSDF_EvalConnectorDistance(const MonsterSDFConnector* conn, Vector3 point) {
    Vector3 pa = Vec3_Sub(point, conn->a);
    if (conn->invBaLengthSquared <= 0.0f) {
        return Vec3_Length(pa) - conn->r1;
    }

    float h = Math_Clamp01(Vec3_Dot(pa, conn->ba) * conn->invBaLengthSquared);
    float radius = conn->r1 + conn->radiusDelta * h;
    Vector3 projection = Vec3_Sub(pa, Vec3_Scale(conn->ba, h));
    return Vec3_Length(projection) - radius;
}

static inline float MonsterSDF_EvalMouthDistance(const MonsterSDFMouth* mouth, Vector3 point, Vector3* outLocalP) {
    Vector3 translated = Vec3_Sub(point, mouth->center);
    Vector3 localP = Transform3D_ApplyRotationBasis(mouth->inverseRotation, translated);

    if (outLocalP) *outLocalP = localP;

    Vector3 pEntrance = Vec3_Sub(localP, mouth->entranceCenterLocal);
    float entranceDist = SDF_RoundedSlotExtruded(
        pEntrance,
        mouth->entranceHalfExtents.x,
        mouth->entranceHalfExtents.y,
        mouth->entranceHalfExtents.z
    );

    Vector3 pCavity = Vec3_Sub(localP, mouth->cavityCenterLocal);
    float cavityDist = SDF_Ellipsoid(pCavity, mouth->cavityRadii);

    return SDF_SmoothUnion(entranceDist, cavityDist, mouth->entranceToCavitySmoothness);
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
        float dist = MonsterSDF_EvalBodyPartDistance(part, point);
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
        float dist = MonsterSDF_EvalConnectorDistance(conn, point);
        SDFSample connSample = SDFSample_Create(dist, conn->color, SDF_MATERIAL_SKIN);

        if (!hasInitialSample) {
            accumulated = connSample;
            hasInitialSample = true;
        } else {
            accumulated = SDFSample_SmoothUnion(accumulated, connSample, sdf->config.connectionSmoothness);
        }
    }

    /* 3. Evaluar cavidades bucales mediante sustracción limpia */
    for (size_t m = 0; m < sdf->mouthCount; ++m) {
        const MonsterSDFMouth* mouth = &sdf->mouths[m];
        if (!AABB_ContainsPoint(mouth->influenceBounds, point)) {
            continue;
        }

        Vector3 localP;
        float cutterDist = MonsterSDF_EvalMouthDistance(mouth, point, &localP);

        SDFSample cutterSample = SDFSample_Create(cutterDist, mouth->insideColor, SDF_MATERIAL_MOUTH);
        accumulated = SDFSample_Subtract(accumulated, cutterSample, 0.01f);
    }

    return accumulated;
}

float MonsterSDF_EvaluateDistance(const MonsterSDF* sdf, Vector3 point) {
    if (!sdf || sdf->bodyPartCount == 0) {
        return 1e6f;
    }

    float accumulated = 1e6f;
    bool hasInitial = false;

    for (size_t i = 0; i < sdf->bodyPartCount; ++i) {
        float dist = MonsterSDF_EvalBodyPartDistance(&sdf->bodyParts[i], point);
        if (!hasInitial) {
            accumulated = dist;
            hasInitial = true;
        } else {
            accumulated = SDF_SmoothUnion(accumulated, dist, sdf->config.bodySmoothness);
        }
    }

    for (size_t i = 0; i < sdf->connectorCount; ++i) {
        float dist = MonsterSDF_EvalConnectorDistance(&sdf->connectors[i], point);
        if (!hasInitial) {
            accumulated = dist;
            hasInitial = true;
        } else {
            accumulated = SDF_SmoothUnion(accumulated, dist, sdf->config.connectionSmoothness);
        }
    }

    for (size_t m = 0; m < sdf->mouthCount; ++m) {
        const MonsterSDFMouth* mouth = &sdf->mouths[m];
        if (!AABB_ContainsPoint(mouth->influenceBounds, point)) {
            continue;
        }

        float cutterDist = MonsterSDF_EvalMouthDistance(mouth, point, NULL);
        accumulated = SDF_SmoothSubtract(accumulated, cutterDist, 0.01f);
    }

    return accumulated;
}

SDFSample MonsterSDF_EvaluateWrapper(const void* context, Vector3 point) {
    return MonsterSDF_Evaluate((const MonsterSDF*)context, point);
}

float MonsterSDF_EvaluateDistanceWrapper(const void* context, Vector3 point) {
    return MonsterSDF_EvaluateDistance((const MonsterSDF*)context, point);
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
        .evaluateDistance = MonsterSDF_EvaluateDistanceWrapper,
        .getBounds = MonsterSDF_GetBoundsWrapper,
        .context = (const void*)sdf
    };
}
