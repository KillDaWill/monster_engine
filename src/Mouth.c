#include "Mouth.h"
#include "MathUtils.h"

Mouth Mouth_Create(size_t bodyPartIndex, Vector3 offset, Vector3 scale, Color insideColor, Color lipColor) {
    Mouth mouth;
    mouth.bodyPartIndex = bodyPartIndex;
    mouth.offset = offset;
    mouth.rotation = Vec3_Zero();
    mouth.scale = scale;
    mouth.insideColor = insideColor;
    mouth.lipColor = lipColor;
    mouth.openFactor = 0.5f;
    mouth.shape = MOUTH_SHAPE_MANDIBLE;

    mouth.slitThickness = Math_Max(0.018f, Math_Min(scale.x, scale.y) * 0.055f);
    mouth.slitSoftness = 0.35f;
    mouth.cornerRadius = Math_Max(mouth.slitThickness * 1.5f, scale.x * 0.08f);
    mouth.jawPivot = Vec3_Create(0.0f, -scale.y * 0.42f, -scale.z * 0.28f);
    mouth.jawLength = Math_Max(scale.z * 0.75f, scale.x * 0.65f);
    mouth.jawWidth = Math_Max(scale.x * 0.78f, 0.001f);
    mouth.jawThickness = Math_Max(scale.y * 0.28f, 0.001f);
    mouth.jawRearMass = scale.y * 0.45f;
    mouth.jawMuscle = scale.y * 0.30f;
    mouth.maxJawAngle = 32.0f;
    mouth.hingeRadius = Math_Max(scale.y * 0.22f, 0.001f);
    mouth.throatRadius = Math_Max(Math_Min(scale.x, scale.y) * 0.30f, 0.001f);
    mouth.cranium = Vec3_Create(1.0f, 1.0f, 1.0f);
    mouth.snout = Vec3_Create(1.0f, 0.8f, 0.8f);
    mouth.cheeks = Vec3_Create(1.0f, 0.9f, 0.75f);
    mouth.brows = Vec3_Create(1.0f, 0.6f, 0.5f);
    Mouth_Normalize(&mouth);
    return mouth;
}

void Mouth_SetOpenFactor(Mouth* mouth, float factor) {
    if (!mouth) return;
    mouth->openFactor = Math_Clamp01(factor);
}

void Mouth_Normalize(Mouth* mouth) {
    if (!mouth) return;
    mouth->openFactor = Math_Clamp01(mouth->openFactor);
    if(mouth->shape!=MOUTH_SHAPE_MANDIBLE&&mouth->shape!=MOUTH_SHAPE_LOWER_BEAK) mouth->shape=MOUTH_SHAPE_MANDIBLE;
    mouth->scale.x = Math_Max(fabsf(mouth->scale.x), 0.001f);
    mouth->scale.y = Math_Max(fabsf(mouth->scale.y), 0.001f);
    mouth->scale.z = Math_Max(fabsf(mouth->scale.z), 0.001f);
    mouth->slitThickness = Math_Clamp(mouth->slitThickness, 0.002f, Math_Min(mouth->scale.x, mouth->scale.y) * 0.35f);
    mouth->slitSoftness = Math_Clamp01(mouth->slitSoftness);
    mouth->cornerRadius = Math_Clamp(Math_Max(fabsf(mouth->cornerRadius), mouth->slitThickness), mouth->slitThickness, mouth->scale.x * 0.45f);
    mouth->jawLength = Math_Max(mouth->jawLength, mouth->scale.z * 0.2f);
    mouth->jawWidth = Math_Max(mouth->jawWidth, mouth->scale.x * 0.2f);
    mouth->jawThickness = Math_Max(mouth->jawThickness, mouth->slitThickness * 2.0f);
    mouth->jawRearMass = Math_Max(mouth->jawRearMass, mouth->hingeRadius);
    mouth->jawMuscle = Math_Max(mouth->jawMuscle, mouth->hingeRadius * 0.5f);
    mouth->maxJawAngle = Math_Clamp(mouth->maxJawAngle, 1.0f, 85.0f);
    mouth->hingeRadius = Math_Max(mouth->hingeRadius, mouth->slitThickness);
    mouth->throatRadius = Math_Max(mouth->throatRadius, mouth->slitThickness);
    mouth->cranium.x = Math_Max(fabsf(mouth->cranium.x), 0.1f);
    mouth->cranium.y = Math_Max(fabsf(mouth->cranium.y), 0.1f);
    mouth->cranium.z = Math_Max(fabsf(mouth->cranium.z), 0.1f);
    mouth->snout.x = Math_Max(fabsf(mouth->snout.x), 0.1f);
    mouth->snout.y = Math_Max(fabsf(mouth->snout.y), 0.1f);
    mouth->snout.z = Math_Max(fabsf(mouth->snout.z), 0.1f);
    mouth->cheeks.x = Math_Max(fabsf(mouth->cheeks.x), 0.1f);
    mouth->cheeks.y = Math_Max(fabsf(mouth->cheeks.y), 0.1f);
    mouth->cheeks.z = Math_Max(fabsf(mouth->cheeks.z), 0.1f);
    mouth->brows.x = Math_Max(fabsf(mouth->brows.x), 0.1f);
    mouth->brows.y = Math_Max(fabsf(mouth->brows.y), 0.1f);
    mouth->brows.z = Math_Max(fabsf(mouth->brows.z), 0.1f);
}

float Mouth_GetJawAngle(const Mouth* mouth) {
    if (!mouth) return 0.0f;
    return Math_Clamp01(mouth->openFactor) * mouth->maxJawAngle;
}
