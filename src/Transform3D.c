#include "Transform3D.h"
#include "MathUtils.h"
#include <math.h>

Transform3D Transform3D_Identity(void) {
    return (Transform3D){
        .position = Vec3_Create(0.0f, 0.0f, 0.0f),
        .rotation = Vec3_Create(0.0f, 0.0f, 0.0f),
        .scale    = Vec3_Create(1.0f, 1.0f, 1.0f)
    };
}

Transform3D Transform3D_Create(Vector3 position, Vector3 rotationDegrees, Vector3 scale) {
    return (Transform3D){
        .position = position,
        .rotation = rotationDegrees,
        .scale    = scale
    };
}

RotationBasis3D Transform3D_BuildInverseRotationBasis(Vector3 rotationDegrees) {
    Vector3 c0 = Transform3D_InverseRotateVector(rotationDegrees, Vec3_Create(1.0f, 0.0f, 0.0f));
    Vector3 c1 = Transform3D_InverseRotateVector(rotationDegrees, Vec3_Create(0.0f, 1.0f, 0.0f));
    Vector3 c2 = Transform3D_InverseRotateVector(rotationDegrees, Vec3_Create(0.0f, 0.0f, 1.0f));

    return (RotationBasis3D){
        .row0 = Vec3_Create(c0.x, c1.x, c2.x),
        .row1 = Vec3_Create(c0.y, c1.y, c2.y),
        .row2 = Vec3_Create(c0.z, c1.z, c2.z)
    };
}

Vector3 Transform3D_ApplyRotationBasis(RotationBasis3D basis, Vector3 v) {
    return Vec3_Create(
        basis.row0.x * v.x + basis.row0.y * v.y + basis.row0.z * v.z,
        basis.row1.x * v.x + basis.row1.y * v.y + basis.row1.z * v.z,
        basis.row2.x * v.x + basis.row2.y * v.y + basis.row2.z * v.z
    );
}

Vector3 Transform3D_RotateVector(Vector3 rotDegrees, Vector3 v) {
    float radX = Math_DegToRad(rotDegrees.x);
    float radY = Math_DegToRad(rotDegrees.y);
    float radZ = Math_DegToRad(rotDegrees.z);

    float cx = cosf(radX), sx = sinf(radX);
    float cy = cosf(radY), sy = sinf(radY);
    float cz = cosf(radZ), sz = sinf(radZ);

    /* Rotación X */
    Vector3 v1 = Vec3_Create(
        v.x,
        v.y * cx - v.z * sx,
        v.y * sx + v.z * cx
    );

    /* Rotación Y */
    Vector3 v2 = Vec3_Create(
        v1.x * cy + v1.z * sy,
        v1.y,
        -v1.x * sy + v1.z * cy
    );

    /* Rotación Z */
    Vector3 v3 = Vec3_Create(
        v2.x * cz - v2.y * sz,
        v2.x * sz + v2.y * cz,
        v2.z
    );

    return v3;
}

Vector3 Transform3D_InverseRotateVector(Vector3 rotDegrees, Vector3 v) {
    float radX = Math_DegToRad(-rotDegrees.x);
    float radY = Math_DegToRad(-rotDegrees.y);
    float radZ = Math_DegToRad(-rotDegrees.z);

    float cx = cosf(radX), sx = sinf(radX);
    float cy = cosf(radY), sy = sinf(radY);
    float cz = cosf(radZ), sz = sinf(radZ);

    /* Inversa Z */
    Vector3 v1 = Vec3_Create(
        v.x * cz - v.y * sz,
        v.x * sz + v.y * cz,
        v.z
    );

    /* Inversa Y */
    Vector3 v2 = Vec3_Create(
        v1.x * cy + v1.z * sy,
        v1.y,
        -v1.x * sy + v1.z * cy
    );

    /* Inversa X */
    Vector3 v3 = Vec3_Create(
        v2.x,
        v2.y * cx - v2.z * sx,
        v2.y * sx + v2.z * cx
    );

    return v3;
}

Vector3 Transform3D_LocalToWorldPoint(Transform3D transform, Vector3 localPoint) {
    /* Escala */
    Vector3 scaled = Vec3_Create(
        localPoint.x * transform.scale.x,
        localPoint.y * transform.scale.y,
        localPoint.z * transform.scale.z
    );

    /* Rotación */
    Vector3 rotated = Transform3D_RotateVector(transform.rotation, scaled);

    /* Traslación */
    return Vec3_Add(rotated, transform.position);
}

Vector3 Transform3D_WorldToLocalPoint(Transform3D transform, Vector3 worldPoint) {
    /* Traslación Inversa */
    Vector3 translated = Vec3_Sub(worldPoint, transform.position);

    /* Rotación Inversa */
    Vector3 rotated = Transform3D_InverseRotateVector(transform.rotation, translated);

    /* Escala Inversa */
    float sx = (fabsf(transform.scale.x) > 1e-6f) ? (1.0f / transform.scale.x) : 1.0f;
    float sy = (fabsf(transform.scale.y) > 1e-6f) ? (1.0f / transform.scale.y) : 1.0f;
    float sz = (fabsf(transform.scale.z) > 1e-6f) ? (1.0f / transform.scale.z) : 1.0f;

    return Vec3_Create(rotated.x * sx, rotated.y * sy, rotated.z * sz);
}
