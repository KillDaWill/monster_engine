/**
 * @file WorldWaterSurface.c
 * @brief Plano y contorno lineal compartidos entre consultas y malla de hidrología v2.
 */
#include "WorldHydrology.h"
#include <math.h>

unsigned WorldHydrology_GetWaterPatches(const WorldHydrology *hydro, const TerrainHeightfield *tf,
                                        uint32_t x, uint32_t z, unsigned triangle,
                                        WorldWaterPatch out[3]) {
    if (!hydro->surfaceBodyId || x >= tf->resolution - 1u ||
        z >= tf->resolution - 1u || triangle > 1u) return 0;
    uint32_t a = z * tf->resolution + x;
    uint32_t d = a + tf->resolution + 1u;
    const uint32_t grid[3] = {a, triangle ? d - 1u : d, triangle ? d : a + 1u};
    uint16_t owners[3];
    for (unsigned i = 0; i < 3; ++i) owners[i] = hydro->surfaceBodyId[grid[i]];
    unsigned count = 0;
    for (unsigned i = 0; i < 3; ++i) {
        uint16_t owner = owners[i];
        if (!owner || (i > 0 && owner == owners[0]) || (i == 2 && owner == owners[1])) continue;
        const WorldWaterBody *body = WorldHydrology_GetBody(hydro, owner);
        if (!body) continue;
        WorldWaterPatch patch = {.bodyId = owner};
        bool canBeWet = false;
        for (unsigned k = 0; k < 3; ++k) {
            uint32_t id = grid[k];
            bool owned = owners[k] == owner;
            patch.grid[k] = id;
            if (body->type == WATER_BODY_LAKE) {
                patch.surface[k] = body->surfaceY;
            } else if (owned) {
                patch.surface[k] = hydro->waterSurfaceY[id];
                patch.flowX[k] = hydro->flowX[id];
                patch.flowZ[k] = hydro->flowZ[id];
            } else {
                /* Fuera del corredor no hay cota fluvial. Prolongar con margen seco
                 * fijo, idéntico en ambos triángulos que comparten la arista. */
                patch.surface[k] = tf->heights[id] - 0.08f;
            }
            patch.depth[k] = patch.surface[k] - tf->heights[id];
            bool wet = patch.depth[k] > WORLD_WATER_DEPTH_EPSILON;
            /* Una ribera seca prolonga el plano hasta su arista, sin conceder
             * dominio húmedo sobre un hueco bajo de otra cuenca. */
            patch.domain[k] = owned ? (wet ? 1.0f : 0.0f) : -1.0f;
            canBeWet |= owned && wet;
        }
        if (canBeWet) out[count++] = patch;
    }
    return count;
}

typedef struct ClipPoint {
    float w[3];
} ClipPoint;

static float interpolate(const ClipPoint *p, const float values[3]) {
    return p->w[0] * values[0] + p->w[1] * values[1] + p->w[2] * values[2];
}

static ClipPoint intersectEdge(ClipPoint a, ClipPoint b, float fa, float fb,
                               const uint32_t grid[3]) {
    /* Usar la misma orientación global para la intersección en aristas compartidas. */
    unsigned first = 0;
    for (unsigned k = 1; k < 3; ++k) {
        if (a.w[k] != b.w[k] && (a.w[first] == b.w[first] || grid[k] < grid[first])) first = k;
    }
    if (a.w[first] < b.w[first]) {
        ClipPoint tmp = a; a = b; b = tmp;
        float f = fa; fa = fb; fb = f;
    }
    float t = fa / (fa - fb);
    ClipPoint p;
    for (unsigned k = 0; k < 3; ++k) p.w[k] = a.w[k] + (b.w[k] - a.w[k]) * t;
    return p;
}

static unsigned clipPlane(const ClipPoint *src, unsigned count, ClipPoint *dst,
                           const uint32_t grid[3], const float field[3], float cutoff) {
    if (!count) return 0;
    unsigned n = 0;
    ClipPoint a = src[count - 1u];
    float fa = interpolate(&a, field) - cutoff;
    for (unsigned i = 0; i < count; ++i) {
        ClipPoint b = src[i];
        float fb = interpolate(&b, field) - cutoff;
        if ((fa < 0 && fb > 0) || (fa > 0 && fb < 0))
            dst[n++] = intersectEdge(a, b, fa, fb, grid);
        if (fb >= 0) dst[n++] = b;
        a = b;
        fa = fb;
    }
    return n;
}

unsigned WorldHydrology_ClipWaterPatch(const TerrainHeightfield *tf, const WorldWaterPatch *patch,
                                       WorldWaterVertex out[WORLD_WATER_PATCH_VERTICES]) {
    const ClipPoint base[3] = {{{1,0,0}}, {{0,1,0}}, {{0,0,1}}};
    ClipPoint owned[4], wet[WORLD_WATER_PATCH_VERTICES];
    unsigned count = clipPlane(base, 3, owned, patch->grid, patch->domain, 0);
    count = clipPlane(owned, count, wet, patch->grid, patch->depth, WORLD_WATER_DEPTH_EPSILON);
    if (count < 3) return 0;
    float x[3], z[3];
    for (unsigned k = 0; k < 3; ++k) {
        x[k] = (float)(patch->grid[k] % tf->resolution);
        z[k] = (float)(patch->grid[k] / tf->resolution);
    }
    for (unsigned i = 0; i < count; ++i) {
        const ClipPoint *p = &wet[i];
        float depth = interpolate(p, patch->depth);
        float domain = interpolate(p, patch->domain);
        out[i] = (WorldWaterVertex){
            .position = {
                tf->originX + tf->spacing * interpolate(p, x),
                patch->surface[0] == patch->surface[1] && patch->surface[1] == patch->surface[2]
                    ? patch->surface[0] : interpolate(p, patch->surface),
                tf->originZ + tf->spacing * interpolate(p, z),
            },
            .depth = fmaxf(depth, WORLD_WATER_DEPTH_EPSILON),
            .flowX = interpolate(p, patch->flowX),
            .flowZ = interpolate(p, patch->flowZ),
            .boundary = (fabsf(domain) < 1e-6f ? 1u : 0u) |
                        (fabsf(depth - WORLD_WATER_DEPTH_EPSILON) < 1e-6f ? 2u : 0u),
        };
    }
    return count;
}

static bool clipRayAxis(float origin, float delta, float lo, float hi, float *enter, float *exit) {
    if (fabsf(delta) < 1e-7f) return origin >= lo && origin <= hi;
    float a = (lo - origin) / delta, b = (hi - origin) / delta;
    *enter = fmaxf(*enter, fminf(a, b));
    *exit = fminf(*exit, fmaxf(a, b));
    return *exit >= *enter;
}

bool WorldHydrology_RaycastWater(const WorldHydrology *hy, const TerrainHeightfield *tf,
                                 Vector3 origin, Vector3 end, Vector3 *hit, uint16_t *bodyId) {
    if (!hy || !tf || !hy->surfaceBodyId || !hy->totalWetCells) return false;
    Vector3 delta = Vec3_Sub(end, origin);
    float enter = 0.0f, exit = 1.0f;
    if (!clipRayAxis(origin.x, delta.x, tf->originX, tf->originX + tf->span, &enter, &exit) ||
        !clipRayAxis(origin.z, delta.z, tf->originZ, tf->originZ + tf->span, &enter, &exit)) return false;
    int last = (int)tf->resolution - 2;
    float gx = (origin.x - tf->originX) * tf->invSpacing;
    float gz = (origin.z - tf->originZ) * tf->invSpacing;
    float dx = delta.x * tf->invSpacing, dz = delta.z * tf->invSpacing;
    int x = (int)fmaxf(0, fminf((float)last, floorf(gx + dx * enter)));
    int z = (int)fmaxf(0, fminf((float)last, floorf(gz + dz * enter)));
    int sx = dx >= 0.0f ? 1 : -1, sz = dz >= 0.0f ? 1 : -1;
    float stepX = dx == 0.0f ? INFINITY : 1.0f / fabsf(dx);
    float stepZ = dz == 0.0f ? INFINITY : 1.0f / fabsf(dz);
    float nextX = dx == 0.0f ? INFINITY : ((float)(x + (sx > 0)) - gx) / dx;
    float nextZ = dz == 0.0f ? INFINITY : ((float)(z + (sz > 0)) - gz) / dz;
    for (uint32_t step = 0; step < 2u * tf->resolution && x >= 0 && x <= last && z >= 0 && z <= last; ++step) {
        float edge = fminf(exit, fminf(nextX, nextZ));
        float best = edge + 1e-7f;
        uint16_t found = 0;
        for (unsigned tri = 0; tri < 2; ++tri) {
            WorldWaterPatch patches[3];
            unsigned n = WorldHydrology_GetWaterPatches(hy, tf, (uint32_t)x, (uint32_t)z, tri, patches);
            for (unsigned k = 0; k < n; ++k) {
                const WorldWaterPatch *p = &patches[k];
                float hX = tri ? p->surface[2] - p->surface[1] : p->surface[2] - p->surface[0];
                float hZ = tri ? p->surface[1] - p->surface[0] : p->surface[1] - p->surface[2];
                float slope = delta.y - hX * dx - hZ * dz;
                if (fabsf(slope) < 1e-7f) continue;
                float t = (p->surface[0] + hX * (gx - x) + hZ * (gz - z) - origin.y) / slope;
                if (t < enter - 1e-7f || t > exit || t > best) continue;
                float u = gx + dx * t - x, v = gz + dz * t - z;
                if (u < -1e-5f || v < -1e-5f || u > 1.00001f || v > 1.00001f ||
                    (tri ? v + 1e-5f < u : u + 1e-5f < v)) continue;
                Vector3 point = Vec3_Add(origin, Vec3_Scale(delta, t));
                WaterSample water;
                if (WorldHydrology_SampleWater(hy, tf, point.x, point.z, &water) &&
                    water.waterBodyId == p->bodyId && fabsf(point.y - water.surfaceY) < 2e-4f) {
                    best = t;
                    found = p->bodyId;
                }
            }
        }
        if (found) {
            if (hit) *hit = Vec3_Add(origin, Vec3_Scale(delta, best));
            if (bodyId) *bodyId = found;
            return true;
        }
        if (edge >= exit) break;
        bool crossX = nextX <= nextZ, crossZ = nextZ <= nextX;
        if (crossX) { x += sx; nextX += stepX; }
        if (crossZ) { z += sz; nextZ += stepZ; }
        enter = edge;
    }
    return false;
}
