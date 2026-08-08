#include "Mouth.h"

Mouth Mouth_Create(size_t bodyPartIndex, Vector3 offset, Vector3 scale, Color insideColor, Color lipColor) {
    Mouth mouth;
    mouth.bodyPartIndex = bodyPartIndex;
    mouth.offset = offset;
    mouth.rotation = Vec3_Zero();
    mouth.scale = scale;
    mouth.insideColor = insideColor;
    mouth.lipColor = lipColor;
    mouth.openFactor = 0.5f;
    return mouth;
}
