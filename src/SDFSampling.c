#include "SDFSampling.h"
#include <math.h>

Vector3 SDF_EstimateNormal(SDFEvaluateFn evalFn, const void* context, Vector3 p, float eps) {
    if (!evalFn) return Vec3_Create(0.0f, 1.0f, 0.0f);
    if (eps <= 1e-6f) eps = 0.005f;

    Vector3 pX1 = Vec3_Create(p.x + eps, p.y, p.z);
    Vector3 pX0 = Vec3_Create(p.x - eps, p.y, p.z);
    Vector3 pY1 = Vec3_Create(p.x, p.y + eps, p.z);
    Vector3 pY0 = Vec3_Create(p.x, p.y - eps, p.z);
    Vector3 pZ1 = Vec3_Create(p.x, p.y, p.z + eps);
    Vector3 pZ0 = Vec3_Create(p.x, p.y, p.z - eps);

    float dx = evalFn(context, pX1).distance - evalFn(context, pX0).distance;
    float dy = evalFn(context, pY1).distance - evalFn(context, pY0).distance;
    float dz = evalFn(context, pZ1).distance - evalFn(context, pZ0).distance;

    Vector3 normal = Vec3_Create(dx, dy, dz);
    float len = Vec3_Length(normal);

    if (len < 1e-6f) return Vec3_Create(0.0f, 1.0f, 0.0f);
    return Vec3_Div(normal, len);
}
