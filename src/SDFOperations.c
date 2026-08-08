#include "SDFOperations.h"
#include "MathUtils.h"
#include <math.h>

float SDF_Union(float a, float b) {
    return Math_Min(a, b);
}

float SDF_Intersection(float a, float b) {
    return Math_Max(a, b);
}

float SDF_Subtract(float body, float cutter) {
    return Math_Max(body, -cutter);
}

float SDF_SmoothUnion(float a, float b, float k) {
    if (k <= 0.0001f) return Math_Min(a, b);
    float h = Math_Clamp01(0.5f + 0.5f * (b - a) / k);
    return Math_Lerp(b, a, h) - k * h * (1.0f - h);
}

float SDF_SmoothSubtract(float body, float cutter, float k) {
    if (k <= 0.0001f) return Math_Max(body, -cutter);
    float h = Math_Clamp01(0.5f - 0.5f * (body + cutter) / k);
    return Math_Lerp(body, -cutter, h) + k * h * (1.0f - h);
}

float SDF_SmoothIntersection(float a, float b, float k) {
    if (k <= 0.0001f) return Math_Max(a, b);
    float h = Math_Clamp01(0.5f - 0.5f * (b - a) / k);
    return Math_Lerp(b, a, h) + k * h * (1.0f - h);
}

SDFSample SDFSample_Create(float distance, Color color, SDFMaterial material) {
    return (SDFSample){
        .distance = distance,
        .color = color,
        .material = material
    };
}

SDFSample SDFSample_Union(SDFSample a, SDFSample b) {
    if (a.distance < b.distance) return a;
    return b;
}

SDFSample SDFSample_SmoothUnion(SDFSample a, SDFSample b, float k) {
    if (k <= 0.0001f) return SDFSample_Union(a, b);

    float d = SDF_SmoothUnion(a.distance, b.distance, k);
    float w = Math_Clamp01(0.5f + 0.5f * (b.distance - a.distance) / k);

    Color blendedColor = Color_Lerp(b.color, a.color, w);
    SDFMaterial blendedMat = (w >= 0.5f) ? a.material : b.material;

    return (SDFSample){
        .distance = d,
        .color = blendedColor,
        .material = blendedMat
    };
}

SDFSample SDFSample_Subtract(SDFSample body, SDFSample cutter, float k) {
    if (k <= 0.0001f) {
        float d = SDF_Subtract(body.distance, cutter.distance);
        if (-cutter.distance > body.distance) {
            return (SDFSample){ .distance = d, .color = cutter.color, .material = cutter.material };
        }
        return (SDFSample){ .distance = d, .color = body.color, .material = body.material };
    }

    float d = SDF_SmoothSubtract(body.distance, cutter.distance, k);
    float w = Math_Clamp01(0.5f - 0.5f * (body.distance + cutter.distance) / k);

    Color blendedColor = Color_Lerp(body.color, cutter.color, w);
    SDFMaterial blendedMat = (w >= 0.5f) ? cutter.material : body.material;

    return (SDFSample){
        .distance = d,
        .color = blendedColor,
        .material = blendedMat
    };
}
