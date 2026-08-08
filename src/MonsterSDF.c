#include "MonsterSDF.h"
#include "SDFPrimitives.h"
#include "Transform3D.h"
#include "MathUtils.h"
#include <math.h>

MonsterSDFConfig MonsterSDF_DefaultConfig(void) {
    return (MonsterSDFConfig){
        .bodySmoothness = 0.5f,
        .connectionSmoothness = 0.4f,
        .mouthSmoothness = 0.25f,
        .connectionRadiusFactor = 0.85f,
        .boundsPadding = 1.5f
    };
}

MonsterSDF MonsterSDF_Create(const Monster* monster) {
    return (MonsterSDF){
        .monster = monster,
        .config = MonsterSDF_DefaultConfig()
    };
}

void MonsterSDF_SetConfig(MonsterSDF* sdf, MonsterSDFConfig config) {
    if (sdf) {
        sdf->config = config;
    }
}

SDFSample MonsterSDF_Evaluate(const MonsterSDF* sdf, Vector3 point) {
    if (!sdf || !sdf->monster || sdf->monster->bodyPartCount == 0) {
        return SDFSample_Create(1e6f, COLOR_WHITE, SDF_MATERIAL_UNKNOWN);
    }

    const Monster* monster = sdf->monster;
    SDFSample accumulated = SDFSample_Create(1e6f, COLOR_WHITE, SDF_MATERIAL_SKIN);
    bool hasInitialSample = false;

    /* 1. Evaluar elipsoides para cada BodyPart */
    for (size_t i = 0; i < monster->bodyPartCount; ++i) {
        const BodyPart* part = &monster->bodyParts[i];
        Vector3 pos = part->positionRender;
        Vector3 radii = Vec3_Create(
            part->widthRender * 0.5f,
            part->heightRender * 0.5f,
            part->lengthRender * 0.5f
        );

        if (radii.x < 0.001f || radii.y < 0.001f || radii.z < 0.001f) {
            continue;
        }

        Vector3 pLocal = Vec3_Sub(point, pos);
        float dist = SDF_Ellipsoid(pLocal, radii);
        Color col = Monster_GetColorFromIndexStruct(monster, part->color);

        SDFSample partSample = SDFSample_Create(dist, col, SDF_MATERIAL_SKIN);

        if (!hasInitialSample) {
            accumulated = partSample;
            hasInitialSample = true;
        } else {
            accumulated = SDFSample_SmoothUnion(accumulated, partSample, sdf->config.bodySmoothness);
        }
    }

    /* 2. Evaluar conectores cónicos (RoundCone) entre partes consecutivas */
    for (size_t i = 0; i < monster->bodyPartCount - 1; ++i) {
        const BodyPart* p1 = &monster->bodyParts[i];
        const BodyPart* p2 = &monster->bodyParts[i + 1];

        float r1 = (p1->widthRender + p1->heightRender + p1->lengthRender) / 6.0f * sdf->config.connectionRadiusFactor;
        float r2 = (p2->widthRender + p2->heightRender + p2->lengthRender) / 6.0f * sdf->config.connectionRadiusFactor;

        if (r1 < 0.001f && r2 < 0.001f) continue;

        float dist = SDF_RoundCone(point, p1->positionRender, p2->positionRender, r1, r2);

        Color c1 = Monster_GetColorFromIndexStruct(monster, p1->color);
        Color c2 = Monster_GetColorFromIndexStruct(monster, p2->color);
        Color connColor = Color_Lerp(c1, c2, 0.5f);

        SDFSample connSample = SDFSample_Create(dist, connColor, SDF_MATERIAL_SKIN);

        if (!hasInitialSample) {
            accumulated = connSample;
            hasInitialSample = true;
        } else {
            accumulated = SDFSample_SmoothUnion(accumulated, connSample, sdf->config.connectionSmoothness);
        }
    }

    /* 3. Evaluar cavidades bucales (Mouth) mediante sustracción suave */
    for (size_t m = 0; m < monster->mouthCount; ++m) {
        const Mouth* mouth = &monster->mouths[m];

        Vector3 partPos = Vec3_Zero();
        if (mouth->bodyPartIndex < monster->bodyPartCount) {
            partPos = monster->bodyParts[mouth->bodyPartIndex].positionRender;
        }

        Vector3 mouthWorldPos = Vec3_Add(partPos, mouth->offset);
        Transform3D mouthXform = Transform3D_Create(mouthWorldPos, mouth->rotation, Vec3_Create(1.0f, 1.0f, 1.0f));
        Vector3 localP = Transform3D_WorldToLocalPoint(mouthXform, point);

        float openF = Math_Clamp01(mouth->openFactor);
        Vector3 cutterRadii = Vec3_Create(
            mouth->scale.x * 0.5f,
            mouth->scale.y * 0.5f * (0.15f + 0.85f * openF),
            mouth->scale.z * (0.30f + 0.70f * openF)
        );

        if (cutterRadii.x < 0.001f || cutterRadii.y < 0.001f || cutterRadii.z < 0.001f) {
            continue;
        }

        float cutterDist = SDF_Ellipsoid(localP, cutterRadii);

        float normX = localP.x / Math_Max(cutterRadii.x, 0.001f);
        float normY = localP.y / Math_Max(cutterRadii.y, 0.001f);
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
    AABB3D bounds = {
        .start = Vec3_Create(1e6f, 1e6f, 1e6f),
        .end   = Vec3_Create(-1e6f, -1e6f, -1e6f)
    };

    if (!sdf || !sdf->monster || sdf->monster->bodyPartCount == 0) {
        bounds.start = Vec3_Create(-1.0f, -1.0f, -1.0f);
        bounds.end   = Vec3_Create(1.0f, 1.0f, 1.0f);
        return bounds;
    }

    const Monster* monster = sdf->monster;

    for (size_t i = 0; i < monster->bodyPartCount; ++i) {
        const BodyPart* part = &monster->bodyParts[i];
        Vector3 pos = part->positionRender;
        float rx = part->widthRender * 0.5f;
        float ry = part->heightRender * 0.5f;
        float rz = part->lengthRender * 0.5f;

        bounds.start.x = Math_Min(bounds.start.x, pos.x - rx);
        bounds.start.y = Math_Min(bounds.start.y, pos.y - ry);
        bounds.start.z = Math_Min(bounds.start.z, pos.z - rz);

        bounds.end.x = Math_Max(bounds.end.x, pos.x + rx);
        bounds.end.y = Math_Max(bounds.end.y, pos.y + ry);
        bounds.end.z = Math_Max(bounds.end.z, pos.z + rz);
    }

    /* Extender el margen por el padding configurado y suavizado */
    float margin = sdf->config.boundsPadding + sdf->config.bodySmoothness;
    bounds.start = Vec3_Sub(bounds.start, Vec3_Create(margin, margin, margin));
    bounds.end   = Vec3_Add(bounds.end, Vec3_Create(margin, margin, margin));

    return bounds;
}
