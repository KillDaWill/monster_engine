#include "SDFPrimitives.h"
#include "MathUtils.h"
#include <math.h>

float SDF_Sphere(Vector3 point, float radius) {
    if (radius <= 0.0001f) return Vec3_Length(point);
    return Vec3_Length(point) - radius;
}

float SDF_Ellipsoid(Vector3 p, Vector3 r) {
    /* Protección contra radios nulos o negativos */
    float rx = Math_Max(r.x, 0.0001f);
    float ry = Math_Max(r.y, 0.0001f);
    float rz = Math_Max(r.z, 0.0001f);

    Vector3 invR = Vec3_Create(1.0f / rx, 1.0f / ry, 1.0f / rz);
    Vector3 invR2 = Vec3_Create(invR.x * invR.x, invR.y * invR.y, invR.z * invR.z);

    Vector3 scaledP = Vec3_Create(p.x * invR.x, p.y * invR.y, p.z * invR.z);
    float k0 = Vec3_Length(scaledP);

    Vector3 scaledP2 = Vec3_Create(p.x * invR2.x, p.y * invR2.y, p.z * invR2.z);
    float k1 = Vec3_Length(scaledP2);

    if (k1 < 1e-6f) return 0.0f;
    return k0 * (k0 - 1.0f) / k1;
}

float SDF_Capsule(Vector3 p, Vector3 a, Vector3 b, float radius) {
    float r = Math_Max(radius, 0.0001f);
    Vector3 pa = Vec3_Sub(p, a);
    Vector3 ba = Vec3_Sub(b, a);

    float baLengthSq = Vec3_Dot(ba, ba);
    if (baLengthSq < 1e-8f) {
        return Vec3_Length(pa) - r;
    }

    float h = Math_Clamp01(Vec3_Dot(pa, ba) / baLengthSq);
    Vector3 projection = Vec3_Sub(pa, Vec3_Scale(ba, h));
    return Vec3_Length(projection) - r;
}

float SDF_RoundCone(Vector3 p, Vector3 a, Vector3 b, float r1, float r2) {
    r1 = Math_Max(r1, 0.0001f);
    r2 = Math_Max(r2, 0.0001f);

    Vector3 pa = Vec3_Sub(p, a);
    Vector3 ba = Vec3_Sub(b, a);

    float baLengthSq = Vec3_Dot(ba, ba);
    if (baLengthSq < 1e-8f) {
        return Vec3_Length(pa) - r1;
    }

    float h = Math_Clamp01(Vec3_Dot(pa, ba) / baLengthSq);
    float r = Math_Lerp(r1, r2, h);
    Vector3 projection = Vec3_Sub(pa, Vec3_Scale(ba, h));
    return Vec3_Length(projection) - r;
}

float SDF_Box(Vector3 p, Vector3 b) {
    Vector3 d = Vec3_Create(
        fabsf(p.x) - b.x,
        fabsf(p.y) - b.y,
        fabsf(p.z) - b.z
    );

    Vector3 maxD = Vec3_Create(
        Math_Max(d.x, 0.0f),
        Math_Max(d.y, 0.0f),
        Math_Max(d.z, 0.0f)
    );

    float outsideDistance = Vec3_Length(maxD);
    float insideDistance = Math_Min(Math_Max(d.x, Math_Max(d.y, d.z)), 0.0f);

    return outsideDistance + insideDistance;
}
