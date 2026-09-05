#include "Eye.h"

Eye Eye_Create(size_t bodyPartIndex, Vector3 offset, Vector3 scale, Color scleraColor, Color pupilColor) {
    Eye eye;
    eye.bodyPartIndex = bodyPartIndex;
    eye.offset = offset;
    eye.rotation = Vec3_Zero();
    eye.forward = Vec3_Create(0.0f, 0.0f, 1.0f);
    eye.scale = scale;
    eye.scleraColor = scleraColor;
    eye.pupilColor = pupilColor;
    eye.irisColor = pupilColor;
    eye.irisScale = 0.78f;
    eye.pupilScale = 0.5f;
    eye.pupilAspect = 1.0f;
    return eye;
}
