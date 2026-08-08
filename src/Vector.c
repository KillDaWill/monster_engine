#include "Vector.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>

/* =========================================================================
 * Vector2
 * ========================================================================= */

Vector2 Vec2_Create(float x, float y) {
    Vector2 v = {x, y};
    return v;
}

Vector2 Vec2_Zero(void) {
    return Vec2_Create(0.0f, 0.0f);
}

Vector2 Vec2_One(void) {
    return Vec2_Create(1.0f, 1.0f);
}

Vector2 Vec2_Add(Vector2 a, Vector2 b) {
    return Vec2_Create(a.x + b.x, a.y + b.y);
}

Vector2 Vec2_Sub(Vector2 a, Vector2 b) {
    return Vec2_Create(a.x - b.x, a.y - b.y);
}

Vector2 Vec2_Scale(Vector2 v, float scalar) {
    return Vec2_Create(v.x * scalar, v.y * scalar);
}

Vector2 Vec2_Div(Vector2 v, float scalar) {
    if (scalar == 0.0f) return Vec2_Zero();
    return Vec2_Create(v.x / scalar, v.y / scalar);
}

float Vec2_Dot(Vector2 a, Vector2 b) {
    return a.x * b.x + a.y * b.y;
}

float Vec2_Cross(Vector2 a, Vector2 b) {
    return a.x * b.y - a.y * b.x;
}

float Vec2_LengthSq(Vector2 v) {
    return v.x * v.x + v.y * v.y;
}

float Vec2_Length(Vector2 v) {
    return sqrtf(Vec2_LengthSq(v));
}

Vector2 Vec2_Normalize(Vector2 v) {
    float len = Vec2_Length(v);
    if (len <= 0.00001f) return Vec2_Zero();
    return Vec2_Div(v, len);
}

float Vec2_Distance(Vector2 a, Vector2 b) {
    return Vec2_Length(Vec2_Sub(a, b));
}

Vector2 Vec2_Lerp(Vector2 a, Vector2 b, float t) {
    return Vec2_Create(a.x + t * (b.x - a.x), a.y + t * (b.y - a.y));
}

/* =========================================================================
 * Vector3
 * ========================================================================= */

Vector3 Vec3_Create(float x, float y, float z) {
    Vector3 v = {x, y, z};
    return v;
}

Vector3 Vec3_Zero(void) {
    return Vec3_Create(0.0f, 0.0f, 0.0f);
}

Vector3 Vec3_One(void) {
    return Vec3_Create(1.0f, 1.0f, 1.0f);
}

Vector3 Vec3_Add(Vector3 a, Vector3 b) {
    return Vec3_Create(a.x + b.x, a.y + b.y, a.z + b.z);
}

Vector3 Vec3_Sub(Vector3 a, Vector3 b) {
    return Vec3_Create(a.x - b.x, a.y - b.y, a.z - b.z);
}

Vector3 Vec3_Scale(Vector3 v, float scalar) {
    return Vec3_Create(v.x * scalar, v.y * scalar, v.z * scalar);
}

Vector3 Vec3_Div(Vector3 v, float scalar) {
    if (scalar == 0.0f) return Vec3_Zero();
    return Vec3_Create(v.x / scalar, v.y / scalar, v.z / scalar);
}

float Vec3_Dot(Vector3 a, Vector3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vector3 Vec3_Cross(Vector3 a, Vector3 b) {
    return Vec3_Create(
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    );
}

float Vec3_LengthSq(Vector3 v) {
    return v.x * v.x + v.y * v.y + v.z * v.z;
}

float Vec3_Length(Vector3 v) {
    return sqrtf(Vec3_LengthSq(v));
}

Vector3 Vec3_Normalize(Vector3 v) {
    float len = Vec3_Length(v);
    if (len <= 0.00001f) return Vec3_Zero();
    return Vec3_Div(v, len);
}

float Vec3_Distance(Vector3 a, Vector3 b) {
    return Vec3_Length(Vec3_Sub(a, b));
}

Vector3 Vec3_Lerp(Vector3 a, Vector3 b, float t) {
    return Vec3_Create(
        a.x + t * (b.x - a.x),
        a.y + t * (b.y - a.y),
        a.z + t * (b.z - a.z)
    );
}

/* =========================================================================
 * Vector4
 * ========================================================================= */

Vector4 Vec4_Create(float x, float y, float z, float w) {
    Vector4 v = {x, y, z, w};
    return v;
}

Vector4 Vec4_Zero(void) {
    return Vec4_Create(0.0f, 0.0f, 0.0f, 0.0f);
}

Vector4 Vec4_One(void) {
    return Vec4_Create(1.0f, 1.0f, 1.0f, 1.0f);
}

Vector4 Vec4_Add(Vector4 a, Vector4 b) {
    return Vec4_Create(a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w);
}

Vector4 Vec4_Sub(Vector4 a, Vector4 b) {
    return Vec4_Create(a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w);
}

Vector4 Vec4_Scale(Vector4 v, float scalar) {
    return Vec4_Create(v.x * scalar, v.y * scalar, v.z * scalar, v.w * scalar);
}

Vector4 Vec4_Div(Vector4 v, float scalar) {
    if (scalar == 0.0f) return Vec4_Zero();
    return Vec4_Create(v.x / scalar, v.y / scalar, v.z / scalar, v.w / scalar);
}

float Vec4_Dot(Vector4 a, Vector4 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

float Vec4_LengthSq(Vector4 v) {
    return v.x * v.x + v.y * v.y + v.z * v.z + v.w * v.w;
}

float Vec4_Length(Vector4 v) {
    return sqrtf(Vec4_LengthSq(v));
}

Vector4 Vec4_Normalize(Vector4 v) {
    float len = Vec4_Length(v);
    if (len <= 0.00001f) return Vec4_Zero();
    return Vec4_Div(v, len);
}

Vector4 Vec4_Lerp(Vector4 a, Vector4 b, float t) {
    return Vec4_Create(
        a.x + t * (b.x - a.x),
        a.y + t * (b.y - a.y),
        a.z + t * (b.z - a.z),
        a.w + t * (b.w - a.w)
    );
}

/* =========================================================================
 * VectorND (N-Dimensional)
 * ========================================================================= */

VectorND VecND_Create(size_t dim) {
    VectorND v;
    v.dim = dim;
    if (dim > 0) {
        v.data = (float*)calloc(dim, sizeof(float));
    } else {
        v.data = NULL;
    }
    return v;
}

VectorND VecND_FromData(size_t dim, const float* data) {
    VectorND v = VecND_Create(dim);
    if (v.data && data) {
        memcpy(v.data, data, dim * sizeof(float));
    }
    return v;
}

void VecND_Free(VectorND* v) {
    if (v && v->data) {
        free(v->data);
        v->data = NULL;
        v->dim = 0;
    }
}

VectorND VecND_Copy(const VectorND* v) {
    if (!v) return VecND_Create(0);
    return VecND_FromData(v->dim, v->data);
}

float VecND_Get(const VectorND* v, size_t index) {
    if (!v || !v->data || index >= v->dim) return 0.0f;
    return v->data[index];
}

void VecND_Set(VectorND* v, size_t index, float value) {
    if (v && v->data && index < v->dim) {
        v->data[index] = value;
    }
}

bool VecND_Add(const VectorND* a, const VectorND* b, VectorND* out) {
    if (!a || !b || !out || a->dim != b->dim || out->dim != a->dim) return false;
    for (size_t i = 0; i < a->dim; ++i) {
        out->data[i] = a->data[i] + b->data[i];
    }
    return true;
}

bool VecND_Sub(const VectorND* a, const VectorND* b, VectorND* out) {
    if (!a || !b || !out || a->dim != b->dim || out->dim != a->dim) return false;
    for (size_t i = 0; i < a->dim; ++i) {
        out->data[i] = a->data[i] - b->data[i];
    }
    return true;
}

bool VecND_Scale(const VectorND* v, float scalar, VectorND* out) {
    if (!v || !out || out->dim != v->dim) return false;
    for (size_t i = 0; i < v->dim; ++i) {
        out->data[i] = v->data[i] * scalar;
    }
    return true;
}

float VecND_Dot(const VectorND* a, const VectorND* b) {
    if (!a || !b || a->dim != b->dim || !a->data || !b->data) return 0.0f;
    float dot = 0.0f;
    for (size_t i = 0; i < a->dim; ++i) {
        dot += a->data[i] * b->data[i];
    }
    return dot;
}

float VecND_LengthSq(const VectorND* v) {
    if (!v || !v->data) return 0.0f;
    return VecND_Dot(v, v);
}

float VecND_Length(const VectorND* v) {
    return sqrtf(VecND_LengthSq(v));
}

bool VecND_Normalize(const VectorND* v, VectorND* out) {
    if (!v || !out || v->dim != out->dim) return false;
    float len = VecND_Length(v);
    if (len <= 0.00001f) {
        for (size_t i = 0; i < out->dim; ++i) out->data[i] = 0.0f;
        return true;
    }
    return VecND_Scale(v, 1.0f / len, out);
}

float VecND_Distance(const VectorND* a, const VectorND* b) {
    if (!a || !b || a->dim != b->dim) return 0.0f;
    VectorND diff = VecND_Create(a->dim);
    VecND_Sub(a, b, &diff);
    float dist = VecND_Length(&diff);
    VecND_Free(&diff);
    return dist;
}

bool VecND_Lerp(const VectorND* a, const VectorND* b, float t, VectorND* out) {
    if (!a || !b || !out || a->dim != b->dim || out->dim != a->dim) return false;
    for (size_t i = 0; i < a->dim; ++i) {
        out->data[i] = a->data[i] + t * (b->data[i] - a->data[i]);
    }
    return true;
}
