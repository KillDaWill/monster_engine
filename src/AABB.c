#include "AABB.h"
#include "MathUtils.h"
#include <math.h>

AABB3D AABB_Empty(void) {
    return (AABB3D){
        .start = Vec3_Create(1e9f, 1e9f, 1e9f),
        .end   = Vec3_Create(-1e9f, -1e9f, -1e9f)
    };
}

AABB3D AABB_FromMinMax(Vector3 min, Vector3 max) {
    return (AABB3D){
        .start = min,
        .end   = max
    };
}

void AABB_ExpandPoint(AABB3D* bounds, Vector3 point) {
    if (!bounds) return;
    bounds->start.x = Math_Min(bounds->start.x, point.x);
    bounds->start.y = Math_Min(bounds->start.y, point.y);
    bounds->start.z = Math_Min(bounds->start.z, point.z);

    bounds->end.x = Math_Max(bounds->end.x, point.x);
    bounds->end.y = Math_Max(bounds->end.y, point.y);
    bounds->end.z = Math_Max(bounds->end.z, point.z);
}

void AABB_ExpandRadius(AABB3D* bounds, Vector3 center, Vector3 radius) {
    if (!bounds) return;
    float rx = fabsf(radius.x);
    float ry = fabsf(radius.y);
    float rz = fabsf(radius.z);

    bounds->start.x = Math_Min(bounds->start.x, center.x - rx);
    bounds->start.y = Math_Min(bounds->start.y, center.y - ry);
    bounds->start.z = Math_Min(bounds->start.z, center.z - rz);

    bounds->end.x = Math_Max(bounds->end.x, center.x + rx);
    bounds->end.y = Math_Max(bounds->end.y, center.y + ry);
    bounds->end.z = Math_Max(bounds->end.z, center.z + rz);
}

void AABB_Pad(AABB3D* bounds, float padding) {
    if (!bounds) return;
    Vector3 padVec = Vec3_Create(padding, padding, padding);
    bounds->start = Vec3_Sub(bounds->start, padVec);
    bounds->end   = Vec3_Add(bounds->end, padVec);
}

Vector3 AABB_Size(AABB3D bounds) {
    Vector3 s = Vec3_Sub(bounds.end, bounds.start);
    s.x = Math_Max(s.x, 0.0f);
    s.y = Math_Max(s.y, 0.0f);
    s.z = Math_Max(s.z, 0.0f);
    return s;
}

bool AABB_ContainsPoint(AABB3D bounds, Vector3 point) {
    return (point.x >= bounds.start.x && point.x <= bounds.end.x &&
            point.y >= bounds.start.y && point.y <= bounds.end.y &&
            point.z >= bounds.start.z && point.z <= bounds.end.z);
}
