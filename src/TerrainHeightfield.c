/**
 * @file TerrainHeightfield.c
 * @brief Implementación del campo de alturas, muestreo baricéntrico exacto y raycast DDA.
 */
#include "TerrainHeightfield.h"
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static void fnv1aUpdate(uint64_t *h, const void *data, size_t n) {
    const unsigned char *p = (const unsigned char *)data;
    for (size_t i = 0; i < n; ++i) {
        *h ^= (uint64_t)p[i];
        *h *= UINT64_C(1099511628211);
    }
}

bool TerrainHeightfield_Init(TerrainHeightfield *tf, uint32_t resolution, float size) {
    if (!tf) return false;
    memset(tf, 0, sizeof(*tf));
    if (resolution < TERRAIN_MIN_RESOLUTION || resolution > TERRAIN_MAX_RESOLUTION ||
        (resolution & 1u) == 0u) {
        return false;
    }
    if (!isfinite(size) || size < 8.0f || size > 4096.0f) return false;

    uint64_t total64 = (uint64_t)resolution * (uint64_t)resolution;
    if (total64 > (uint64_t)TERRAIN_MAX_RESOLUTION * (uint64_t)TERRAIN_MAX_RESOLUTION) return false;
    size_t count = (size_t)total64;

    uint32_t cellsPerAxis = resolution - 1u;
    uint32_t blockDim = (cellsPerAxis + TERRAIN_COARSE_BLOCK_SIZE - 1u) / TERRAIN_COARSE_BLOCK_SIZE;
    size_t blockCount = (size_t)blockDim * (size_t)blockDim;

    tf->heights = calloc(count, sizeof(float));
    tf->smoothNormals = calloc(count, sizeof(Vector3));
    tf->slopeTan = calloc(count, sizeof(float));
    tf->blockMinY = calloc(blockCount, sizeof(float));
    tf->blockMaxY = calloc(blockCount, sizeof(float));
    if (!tf->heights || !tf->smoothNormals || !tf->slopeTan || !tf->blockMinY || !tf->blockMaxY) {
        TerrainHeightfield_Free(tf);
        return false;
    }

    tf->resolution = resolution;
    tf->span = size;
    tf->halfSize = size * 0.5f;
    tf->originX = -tf->halfSize;
    tf->originZ = -tf->halfSize;
    tf->spacing = size / (float)cellsPerAxis;
    tf->invSpacing = 1.0f / tf->spacing;
    tf->blockDim = blockDim;
    tf->isFlat = true;
    tf->memoryBytes = count * (sizeof(float) * 2u + sizeof(Vector3)) + blockCount * sizeof(float) * 2u;

    for (size_t i = 0; i < count; ++i) {
        tf->smoothNormals[i] = Vec3_Create(0.0f, 1.0f, 0.0f);
    }
    return true;
}

void TerrainHeightfield_Free(TerrainHeightfield *tf) {
    if (!tf) return;
    free(tf->heights);
    free(tf->smoothNormals);
    free(tf->slopeTan);
    free(tf->blockMinY);
    free(tf->blockMaxY);
    memset(tf, 0, sizeof(*tf));
}

bool TerrainHeightfield_RebuildDerived(TerrainHeightfield *tf) {
    if (!tf || !tf->heights || !tf->smoothNormals || !tf->slopeTan || tf->resolution < TERRAIN_MIN_RESOLUTION) {
        return false;
    }
    uint32_t res = tf->resolution;
    size_t count = (size_t)res * (size_t)res;
    float minH = FLT_MAX;
    float maxH = -FLT_MAX;
    bool allZero = true;

    for (size_t i = 0; i < count; ++i) {
        float h = tf->heights[i];
        if (!isfinite(h)) return false;
        if (h < minH) minH = h;
        if (h > maxH) maxH = h;
        if (fabsf(h) > 1e-6f) allZero = false;
        tf->smoothNormals[i] = Vec3_Zero();
    }
    tf->minHeight = minH;
    tf->maxHeight = maxH;
    tf->isFlat = allZero;

    /* Acumular normales ponderadas por área de los triángulos autoritativos (T0 y T1). */
    float s = tf->spacing;
    uint32_t cells = res - 1u;
    for (uint32_t iz = 0; iz < cells; ++iz) {
        size_t row0 = (size_t)iz * res;
        size_t row1 = row0 + res;
        for (uint32_t ix = 0; ix < cells; ++ix) {
            size_t i00 = row0 + ix;
            size_t i10 = i00 + 1u;
            size_t i01 = row1 + ix;
            size_t i11 = i01 + 1u;
            float h00 = tf->heights[i00];
            float h10 = tf->heights[i10];
            float h01 = tf->heights[i01];
            float h11 = tf->heights[i11];

            /* T0 = (v00, v11, v10): cross(v11-v00, v10-v00) = (-s*(h10-h00), s*s, s*(h10-h11)) */
            Vector3 n0 = Vec3_Create(-s * (h10 - h00), s * s, s * (h10 - h11));
            tf->smoothNormals[i00] = Vec3_Add(tf->smoothNormals[i00], n0);
            tf->smoothNormals[i11] = Vec3_Add(tf->smoothNormals[i11], n0);
            tf->smoothNormals[i10] = Vec3_Add(tf->smoothNormals[i10], n0);

            /* T1 = (v00, v01, v11): cross(v01-v00, v11-v00) = (s*(h01-h11), s*s, -s*(h01-h00)) */
            Vector3 n1 = Vec3_Create(s * (h01 - h11), s * s, -s * (h01 - h00));
            tf->smoothNormals[i00] = Vec3_Add(tf->smoothNormals[i00], n1);
            tf->smoothNormals[i01] = Vec3_Add(tf->smoothNormals[i01], n1);
            tf->smoothNormals[i11] = Vec3_Add(tf->smoothNormals[i11], n1);
        }
    }

    for (size_t i = 0; i < count; ++i) {
        Vector3 n = tf->smoothNormals[i];
        float lenSq = Vec3_LengthSq(n);
        if (lenSq > 1e-12f) {
            n = Vec3_Scale(n, 1.0f / sqrtf(lenSq));
        } else {
            n = Vec3_Create(0.0f, 1.0f, 0.0f);
        }
        tf->smoothNormals[i] = n;
        float horiz = sqrtf(n.x * n.x + n.z * n.z);
        tf->slopeTan[i] = (n.y > 1e-4f) ? (horiz / n.y) : 100.0f;
    }

    /* Reconstruir bloques 8x8 min/max. */
    uint32_t bdim = tf->blockDim;
    for (uint32_t bz = 0; bz < bdim; ++bz) {
        uint32_t z0 = bz * TERRAIN_COARSE_BLOCK_SIZE;
        uint32_t z1 = z0 + TERRAIN_COARSE_BLOCK_SIZE;
        if (z1 > cells) z1 = cells;
        for (uint32_t bx = 0; bx < bdim; ++bx) {
            uint32_t x0 = bx * TERRAIN_COARSE_BLOCK_SIZE;
            uint32_t x1 = x0 + TERRAIN_COARSE_BLOCK_SIZE;
            if (x1 > cells) x1 = cells;
            float bMin = FLT_MAX;
            float bMax = -FLT_MAX;
            for (uint32_t iz = z0; iz <= z1; ++iz) {
                const float *row = &tf->heights[(size_t)iz * res];
                for (uint32_t ix = x0; ix <= x1; ++ix) {
                    float h = row[ix];
                    if (h < bMin) bMin = h;
                    if (h > bMax) bMax = h;
                }
            }
            size_t bi = (size_t)bz * bdim + bx;
            tf->blockMinY[bi] = bMin;
            tf->blockMaxY[bi] = bMax;
        }
    }
    return true;
}

bool Terrain_InBounds(const TerrainHeightfield *tf, float x, float z) {
    if (!tf || !tf->heights || !isfinite(x) || !isfinite(z)) return false;
    return x >= tf->originX && x <= tf->originX + tf->span &&
           z >= tf->originZ && z <= tf->originZ + tf->span;
}

static inline bool locateCell(const TerrainHeightfield *tf, float x, float z,
                              uint32_t *outIx, uint32_t *outIz, float *outU, float *outV) {
    if (!Terrain_InBounds(tf, x, z)) return false;
    float fx = (x - tf->originX) * tf->invSpacing;
    float fz = (z - tf->originZ) * tf->invSpacing;
    uint32_t maxCell = tf->resolution - 2u;
    int ix = (int)floorf(fx);
    int iz = (int)floorf(fz);
    if (ix < 0) ix = 0;
    if (iz < 0) iz = 0;
    if ((uint32_t)ix > maxCell) ix = (int)maxCell;
    if ((uint32_t)iz > maxCell) iz = (int)maxCell;
    float u = fx - (float)ix;
    float v = fz - (float)iz;
    if (u < 0.0f) u = 0.0f; else if (u > 1.0f) u = 1.0f;
    if (v < 0.0f) v = 0.0f; else if (v > 1.0f) v = 1.0f;
    *outIx = (uint32_t)ix;
    *outIz = (uint32_t)iz;
    *outU = u;
    *outV = v;
    return true;
}

bool Terrain_SampleHeight(const TerrainHeightfield *tf, float x, float z, float *outHeight) {
    if (!outHeight) return false;
    uint32_t ix, iz;
    float u, v;
    if (!locateCell(tf, x, z, &ix, &iz, &u, &v)) return false;
    if (tf->isFlat) {
        *outHeight = 0.0f;
        return true;
    }
    uint32_t res = tf->resolution;
    size_t i00 = (size_t)iz * res + ix;
    float h00 = tf->heights[i00];
    float h10 = tf->heights[i00 + 1u];
    float h01 = tf->heights[i00 + res];
    float h11 = tf->heights[i00 + res + 1u];
    *outHeight = (u >= v)
                     ? (h00 + u * (h10 - h00) + v * (h11 - h10))
                     : (h00 + v * (h01 - h00) + u * (h11 - h01));
    return isfinite(*outHeight);
}

bool Terrain_SampleGround(const TerrainHeightfield *tf, float x, float z, SurfaceHit *out) {
    if (!out) return false;
    uint32_t ix, iz;
    float u, v;
    if (!locateCell(tf, x, z, &ix, &iz, &u, &v)) return false;
    if (tf->isFlat) {
        out->position = Vec3_Create(x, 0.0f, z);
        out->normal = Vec3_Create(0.0f, 1.0f, 0.0f);
        return true;
    }
    uint32_t res = tf->resolution;
    size_t i00 = (size_t)iz * res + ix;
    float h00 = tf->heights[i00];
    float h10 = tf->heights[i00 + 1u];
    float h01 = tf->heights[i00 + res];
    float h11 = tf->heights[i00 + res + 1u];
    float s = tf->spacing;
    float h;
    Vector3 n;
    if (u >= v) {
        /* Triángulo T0 = (v00, v11, v10): normal exacta = normalize(-(h10-h00)/s, 1, -(h11-h10)/s) */
        h = h00 + u * (h10 - h00) + v * (h11 - h10);
        n = Vec3_Create(-(h10 - h00), s, -(h11 - h10));
    } else {
        /* Triángulo T1 = (v00, v01, v11): normal exacta = normalize(-(h11-h01)/s, 1, -(h01-h00)/s) */
        h = h00 + v * (h01 - h00) + u * (h11 - h01);
        n = Vec3_Create(-(h11 - h01), s, -(h01 - h00));
    }
    if (!isfinite(h)) return false;
    out->position = Vec3_Create(x, h, z);
    out->normal = Vec3_Normalize(n);
    return true;
}

bool Terrain_SampleSmoothNormal(const TerrainHeightfield *tf, float x, float z, Vector3 *outNormal) {
    if (!outNormal) return false;
    uint32_t ix, iz;
    float u, v;
    if (!locateCell(tf, x, z, &ix, &iz, &u, &v)) return false;
    if (tf->isFlat) {
        *outNormal = Vec3_Create(0.0f, 1.0f, 0.0f);
        return true;
    }
    uint32_t res = tf->resolution;
    size_t i00 = (size_t)iz * res + ix;
    Vector3 n00 = tf->smoothNormals[i00];
    Vector3 n10 = tf->smoothNormals[i00 + 1u];
    Vector3 n01 = tf->smoothNormals[i00 + res];
    Vector3 n11 = tf->smoothNormals[i00 + res + 1u];
    Vector3 n;
    if (u >= v) {
        float w00 = 1.0f - u;
        float w10 = u - v;
        float w11 = v;
        n = Vec3_Add(Vec3_Add(Vec3_Scale(n00, w00), Vec3_Scale(n10, w10)), Vec3_Scale(n11, w11));
    } else {
        float w00 = 1.0f - v;
        float w01 = v - u;
        float w11 = u;
        n = Vec3_Add(Vec3_Add(Vec3_Scale(n00, w00), Vec3_Scale(n01, w01)), Vec3_Scale(n11, w11));
    }
    *outNormal = Vec3_LengthSq(n) > 1e-10f ? Vec3_Normalize(n) : Vec3_Create(0.0f, 1.0f, 0.0f);
    return true;
}

bool Terrain_SampleSlopeAngle(const TerrainHeightfield *tf, float x, float z, float *outRadians) {
    if (!outRadians) return false;
    SurfaceHit hit;
    if (!Terrain_SampleGround(tf, x, z, &hit)) return false;
    float ny = fmaxf(-1.0f, fminf(1.0f, hit.normal.y));
    *outRadians = acosf(fmaxf(0.0f, ny));
    return true;
}

bool Terrain_QueryRegionBounds(const TerrainHeightfield *tf,
                               float minX, float minZ, float maxX, float maxZ,
                               float *outMinY, float *outMaxY) {
    if (!tf || !tf->heights || !outMinY || !outMaxY ||
        !isfinite(minX) || !isfinite(minZ) || !isfinite(maxX) || !isfinite(maxZ)) {
        return false;
    }
    if (maxX < minX) { float t = minX; minX = maxX; maxX = t; }
    if (maxZ < minZ) { float t = minZ; minZ = maxZ; maxZ = t; }
    if (maxX < tf->originX || minX > tf->originX + tf->span ||
        maxZ < tf->originZ || minZ > tf->originZ + tf->span) {
        return false;
    }
    if (tf->isFlat) {
        *outMinY = 0.0f;
        *outMaxY = 0.0f;
        return true;
    }
    int x0 = (int)floorf((minX - tf->originX) * tf->invSpacing);
    int z0 = (int)floorf((minZ - tf->originZ) * tf->invSpacing);
    int x1 = (int)ceilf((maxX - tf->originX) * tf->invSpacing);
    int z1 = (int)ceilf((maxZ - tf->originZ) * tf->invSpacing);
    int maxIdx = (int)tf->resolution - 1;
    if (x0 < 0) x0 = 0;
    if (z0 < 0) z0 = 0;
    if (x1 > maxIdx) x1 = maxIdx;
    if (z1 > maxIdx) z1 = maxIdx;

    float lo = FLT_MAX, hi = -FLT_MAX;
    uint32_t res = tf->resolution;
    for (int iz = z0; iz <= z1; ++iz) {
        const float *row = &tf->heights[(size_t)iz * res];
        for (int ix = x0; ix <= x1; ++ix) {
            float h = row[ix];
            if (h < lo) lo = h;
            if (h > hi) hi = h;
        }
    }
    *outMinY = lo;
    *outMaxY = hi;
    return lo <= hi;
}

static bool rayTriangleSegment(Vector3 orig, Vector3 dir, Vector3 v0, Vector3 v1, Vector3 v2,
                               float tMin, float tMax, float *outT) {
    Vector3 e1 = Vec3_Sub(v1, v0);
    Vector3 e2 = Vec3_Sub(v2, v0);
    Vector3 pvec = Vec3_Cross(dir, e2);
    float det = Vec3_Dot(e1, pvec);
    if (fabsf(det) < 1e-9f) return false;
    float invDet = 1.0f / det;
    Vector3 tvec = Vec3_Sub(orig, v0);
    float u = Vec3_Dot(tvec, pvec) * invDet;
    if (u < -1e-5f || u > 1.0f + 1e-5f) return false;
    Vector3 qvec = Vec3_Cross(tvec, e1);
    float v = Vec3_Dot(dir, qvec) * invDet;
    if (v < -1e-5f || u + v > 1.0f + 1e-5f) return false;
    float t = Vec3_Dot(e2, qvec) * invDet;
    if (t >= tMin && t <= tMax) {
        *outT = t;
        return true;
    }
    return false;
}

static bool testCellTriangles(const TerrainHeightfield *tf, uint32_t ix, uint32_t iz,
                              Vector3 a, Vector3 dir, float tMin, float tMax,
                              float *bestT) {
    uint32_t res = tf->resolution;
    size_t i00 = (size_t)iz * res + ix;
    float x0 = tf->originX + (float)ix * tf->spacing;
    float x1 = x0 + tf->spacing;
    float z0 = tf->originZ + (float)iz * tf->spacing;
    float z1 = z0 + tf->spacing;
    float h00 = tf->heights[i00];
    float h10 = tf->heights[i00 + 1u];
    float h01 = tf->heights[i00 + res];
    float h11 = tf->heights[i00 + res + 1u];

    float cellMax = fmaxf(fmaxf(h00, h10), fmaxf(h01, h11));
    float segMinY = fminf(a.y, a.y + dir.y);
    if (segMinY > cellMax + 1e-4f) return false;

    Vector3 v00 = Vec3_Create(x0, h00, z0);
    Vector3 v10 = Vec3_Create(x1, h10, z0);
    Vector3 v01 = Vec3_Create(x0, h01, z1);
    Vector3 v11 = Vec3_Create(x1, h11, z1);

    bool hit = false;
    float t = 0.0f;
    if (rayTriangleSegment(a, dir, v00, v11, v10, tMin, tMax, &t)) {
        if (t < *bestT) { *bestT = t; hit = true; }
    }
    if (rayTriangleSegment(a, dir, v00, v01, v11, tMin, tMax, &t)) {
        if (t < *bestT) { *bestT = t; hit = true; }
    }
    return hit;
}

bool Terrain_RaycastSegment(const TerrainHeightfield *tf, Vector3 a, Vector3 b,
                            float endpointTolerance, float *outT, Vector3 *outHit) {
    if (!tf || !tf->heights ||
        !isfinite(a.x) || !isfinite(a.y) || !isfinite(a.z) ||
        !isfinite(b.x) || !isfinite(b.y) || !isfinite(b.z)) {
        return false;
    }
    float segMinY = fminf(a.y, b.y);
    if (segMinY > tf->maxHeight + 1e-4f) return false;

    Vector3 dir = Vec3_Sub(b, a);
    float segLen = Vec3_Length(dir);
    if (segLen < 1e-7f) return false;

    float tol = (isfinite(endpointTolerance) && endpointTolerance > 0.0f) ? endpointTolerance : 0.0f;
    float tPad = fminf(0.45f, tol / segLen);
    float tMin = tPad;
    float tMax = 1.0f - tPad;
    if (tMax <= tMin) return false;

    /* Recorte paramétrico contra la caja XZ del terreno. */
    float xMin = tf->originX, xMax = tf->originX + tf->span;
    float zMin = tf->originZ, zMax = tf->originZ + tf->span;
    float lo = tMin, hi = tMax;
    if (fabsf(dir.x) < 1e-9f) {
        if (a.x < xMin || a.x > xMax) return false;
    } else {
        float tx0 = (xMin - a.x) / dir.x;
        float tx1 = (xMax - a.x) / dir.x;
        if (tx0 > tx1) { float tmp = tx0; tx0 = tx1; tx1 = tmp; }
        lo = fmaxf(lo, tx0);
        hi = fminf(hi, tx1);
    }
    if (fabsf(dir.z) < 1e-9f) {
        if (a.z < zMin || a.z > zMax) return false;
    } else {
        float tz0 = (zMin - a.z) / dir.z;
        float tz1 = (zMax - a.z) / dir.z;
        if (tz0 > tz1) { float tmp = tz0; tz0 = tz1; tz1 = tmp; }
        lo = fmaxf(lo, tz0);
        hi = fminf(hi, tz1);
    }
    if (lo > hi) return false;

    if (tf->isFlat) {
        /* Plano Y=0 exacto: intersección analítica. */
        if (fabsf(dir.y) < 1e-9f) {
            if (fabsf(a.y) < 1e-6f && tol <= 0.0f) {
                if (outT) *outT = lo;
                if (outHit) *outHit = Vec3_Add(a, Vec3_Scale(dir, lo));
                return true;
            }
            return false;
        }
        float t = -a.y / dir.y;
        if (t >= lo && t <= hi) {
            if (outT) *outT = t;
            if (outHit) *outHit = Vec3_Add(a, Vec3_Scale(dir, t));
            return true;
        }
        return false;
    }

    /* Si el segmento comienza o termina claramente enterrado bajo el suelo (más allá de tolerancia),
     * comprobar también punto medio/extremos y recorrido DDA de celdas. */
    uint32_t maxCell = tf->resolution - 2u;
    Vector3 pStart = Vec3_Add(a, Vec3_Scale(dir, lo));
    Vector3 pEnd = Vec3_Add(a, Vec3_Scale(dir, hi));

    float gx0 = (pStart.x - tf->originX) * tf->invSpacing;
    float gz0 = (pStart.z - tf->originZ) * tf->invSpacing;
    float gx1 = (pEnd.x - tf->originX) * tf->invSpacing;
    float gz1 = (pEnd.z - tf->originZ) * tf->invSpacing;

    int cx = (int)floorf(gx0);
    int cz = (int)floorf(gz0);
    int endX = (int)floorf(gx1);
    int endZ = (int)floorf(gz1);
    if (cx < 0) cx = 0; else if ((uint32_t)cx > maxCell) cx = (int)maxCell;
    if (cz < 0) cz = 0; else if ((uint32_t)cz > maxCell) cz = (int)maxCell;
    if (endX < 0) endX = 0; else if ((uint32_t)endX > maxCell) endX = (int)maxCell;
    if (endZ < 0) endZ = 0; else if ((uint32_t)endZ > maxCell) endZ = (int)maxCell;

    float bestT = FLT_MAX;
    bool found = false;

    int stepX = (dir.x > 1e-9f) ? 1 : (dir.x < -1e-9f ? -1 : 0);
    int stepZ = (dir.z > 1e-9f) ? 1 : (dir.z < -1e-9f ? -1 : 0);

    float tDeltaX = stepX != 0 ? fabsf(tf->spacing / dir.x) : FLT_MAX;
    float tDeltaZ = stepZ != 0 ? fabsf(tf->spacing / dir.z) : FLT_MAX;

    float nextBoundX = tf->originX + (float)(stepX > 0 ? (cx + 1) : cx) * tf->spacing;
    float nextBoundZ = tf->originZ + (float)(stepZ > 0 ? (cz + 1) : cz) * tf->spacing;
    float tMaxX = stepX != 0 ? (nextBoundX - a.x) / dir.x : FLT_MAX;
    float tMaxZ = stepZ != 0 ? (nextBoundZ - a.z) / dir.z : FLT_MAX;

    float tCur = lo;
    uint32_t maxSteps = (tf->resolution * 4u) + 8u;
    for (uint32_t iter = 0; iter < maxSteps; ++iter) {
        float tNext = fminf(hi, fminf(tMaxX, tMaxZ));
        float yEnter = a.y + dir.y * tCur;
        float yExit = a.y + dir.y * tNext;
        float segCellMinY = fminf(yEnter, yExit);

        uint32_t bx = (uint32_t)cx / TERRAIN_COARSE_BLOCK_SIZE;
        uint32_t bz = (uint32_t)cz / TERRAIN_COARSE_BLOCK_SIZE;
        float bMaxY = tf->blockMaxY[(size_t)bz * tf->blockDim + bx];

        if (segCellMinY <= bMaxY + 1e-4f) {
            if (testCellTriangles(tf, (uint32_t)cx, (uint32_t)cz, a, dir, lo, hi, &bestT)) {
                found = true;
                if (bestT <= tNext + 1e-5f) break;
            }
        }

        if (cx == endX && cz == endZ) break;
        if (tMaxX < tMaxZ) {
            tCur = tMaxX;
            tMaxX += tDeltaX;
            cx += stepX;
        } else if (tMaxZ < tMaxX) {
            tCur = tMaxZ;
            tMaxZ += tDeltaZ;
            cz += stepZ;
        } else {
            /* Cruce exacto de esquina: probar también celdas adyacentes por seguridad. */
            if (cx + stepX >= 0 && (uint32_t)(cx + stepX) <= maxCell) {
                if (testCellTriangles(tf, (uint32_t)(cx + stepX), (uint32_t)cz, a, dir, lo, hi, &bestT)) found = true;
            }
            if (cz + stepZ >= 0 && (uint32_t)(cz + stepZ) <= maxCell) {
                if (testCellTriangles(tf, (uint32_t)cx, (uint32_t)(cz + stepZ), a, dir, lo, hi, &bestT)) found = true;
            }
            tCur = tMaxX;
            tMaxX += tDeltaX;
            tMaxZ += tDeltaZ;
            cx += stepX;
            cz += stepZ;
        }
        if (cx < 0 || cz < 0 || (uint32_t)cx > maxCell || (uint32_t)cz > maxCell) break;
        if (tCur > hi + 1e-6f) break;
    }

    if (found && bestT <= hi) {
        if (outT) *outT = bestT;
        if (outHit) *outHit = Vec3_Add(a, Vec3_Scale(dir, bestT));
        return true;
    }
    return false;
}

uint64_t TerrainHeightfield_Fingerprint(const TerrainHeightfield *tf) {
    if (!tf || !tf->heights) return 0u;
    uint64_t h = UINT64_C(14695981039346656037);
    fnv1aUpdate(&h, &tf->resolution, sizeof(tf->resolution));
    int32_t qSpan = (int32_t)llround((double)tf->span * 1000.0);
    fnv1aUpdate(&h, &qSpan, sizeof(qSpan));
    size_t count = (size_t)tf->resolution * (size_t)tf->resolution;
    for (size_t i = 0; i < count; ++i) {
        /* Cuantización canónica a 0.1 mm para portabilidad y ausencia de -0.0f. */
        int32_t qh = (int32_t)llround((double)tf->heights[i] * 10000.0);
        fnv1aUpdate(&h, &qh, sizeof(qh));
    }
    return h;
}
