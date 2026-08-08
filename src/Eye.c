#include "Eye.h"

Eye Eye_Create(size_t bodyPartIndex, Vector3 offset, Vector3 scale, Color scleraColor, Color pupilColor) {
    Eye eye;
    eye.bodyPartIndex = bodyPartIndex;
    eye.offset = offset;
    eye.rotation = Vec3_Zero();
    eye.scale = scale;
    eye.scleraColor = scleraColor;
    eye.pupilColor = pupilColor;
    eye.pupilScale = 0.5f;
    return eye;
}
