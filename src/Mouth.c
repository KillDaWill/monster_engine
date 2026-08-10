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

    float lipThickness = Math_Max(0.04f, scale.y * 0.12f);
    mouth.lipThickness = lipThickness;
    mouth.lipCurvature = 0.35f;
    mouth.lipProtrusion = lipThickness * 0.35f;
    return mouth;
}

void Mouth_SetOpenFactor(Mouth* mouth, float factor) {
    if (!mouth) return;
    mouth->openFactor = Math_Clamp01(factor);
}
