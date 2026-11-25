/**
 * @file WorldPlacement.c
 * @brief Implementación de colocación determinista de hábitat, relocalización y cadáveres.
 */
#include "WorldPlacement.h"
#include <math.h>

static float nextRng01(uint32_t *state) {
    uint32_t x = *state ? *state : 1u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return (float)(x & 0xFFFFFFu) / 16777216.0f;
}

WorldHabitatFilter WorldHabitatFilter_Vegetation(float clearanceRadius) {
    return (WorldHabitatFilter){
        .minAltitudeNorm = 0.0f,
        .maxAltitudeNorm = 0.82f,
        .maxSlopeRadians = 0.48f,
        .minMoisture = 0.18f,
        .maxMoisture = 1.0f,
        .minFertility = 0.22f,
        .maxWaterDepth = 0.04f,
        .minShoreDistance = 0.0f,
        .maxShoreDistance = 0.0f,
        .clearanceRadius = clearanceRadius > 0.1f ? clearanceRadius : 0.85f,
        .minDistFromCenter = 0.0f,
        .maxDistFromCenter = 0.0f
    };
}

WorldHabitatFilter WorldHabitatFilter_LandCreature(const TraversalProfile *profile) {
    float r = profile ? profile->footprintRadius : 0.9f;
    float sl = profile ? profile->maxSlopeRadians * 0.90f : 0.54f;
    float wd = (profile && profile->canWade) ? fminf(0.12f, profile->maxWadingDepth * 0.4f) : 0.03f;
    return (WorldHabitatFilter){
        .minAltitudeNorm = 0.0f,
        .maxAltitudeNorm = 0.95f,
        .maxSlopeRadians = sl,
        .minMoisture = 0.0f,
        .maxMoisture = 1.0f,
        .minFertility = 0.0f,
        .maxWaterDepth = wd,
        .minShoreDistance = 0.0f,
        .maxShoreDistance = 0.0f,
        .clearanceRadius = r,
        .minDistFromCenter = 0.0f,
        .maxDistFromCenter = 0.0f
    };
}

bool WorldPlacement_ValidatePoint(const WorldStaticData *w, float x, float z,
                                  const WorldHabitatFilter *filter,
                                  Vector3 *outPos, Vector3 *outNormal) {
    if (!w || !filter || !isfinite(x) || !isfinite(z)) return false;
    float margin = fmaxf(0.5f, filter->clearanceRadius + 0.5f);
    if (fabsf(x) > w->terrain.halfSize - margin || fabsf(z) > w->terrain.halfSize - margin) {
        return false;
    }

    float r2 = x * x + z * z;
    if (filter->minDistFromCenter > 0.0f && r2 < filter->minDistFromCenter * filter->minDistFromCenter) {
        return false;
    }
    if (filter->maxDistFromCenter > 0.0f && r2 > filter->maxDistFromCenter * filter->maxDistFromCenter) {
        return false;
    }

    SurfaceHit hit;
    if (!Terrain_SampleGround(&w->terrain, x, z, &hit)) return false;

    if (!w->isFlat) {
        float slope = acosf(fmaxf(0.0f, fminf(1.0f, hit.normal.y)));
        if (slope > filter->maxSlopeRadians) return false;

        WaterSample ws = {0};
        if (WorldHydrology_SampleWater(&w->hydrology, &w->terrain, x, z, &ws) && ws.wet) {
            if (ws.depth > filter->maxWaterDepth) return false;
        }
        if (WorldGeology_CollidesDisc(&w->geology, hit.position, filter->clearanceRadius)) {
            return false;
        }

        WorldBiomeSample bs = {0};
        if (WorldBiome_Sample(&w->biome, &w->terrain, x, z, &bs)) {
            if (bs.altitudeNorm < filter->minAltitudeNorm || bs.altitudeNorm > filter->maxAltitudeNorm) return false;
            if (bs.moisture < filter->minMoisture || bs.moisture > filter->maxMoisture) return false;
            if (bs.fertility < filter->minFertility) return false;
            if (filter->minShoreDistance > 0.0f && bs.waterDistance < filter->minShoreDistance) return false;
            if (filter->maxShoreDistance > 0.0f && bs.waterDistance > filter->maxShoreDistance) return false;
        }
    }

    if (outPos) *outPos = hit.position;
    if (outNormal) *outNormal = hit.normal;
    return true;
}

bool WorldPlacement_SampleHabitat(const WorldStaticData *w, uint32_t *rngState,
                                  const WorldHabitatFilter *filter,
                                  bool hasAvoidPos, Vector3 avoidPos, float minAvoidDist,
                                  unsigned maxAttempts, Vector3 *outPos, Vector3 *outNormal) {
    if (!w || !rngState || !filter || !outPos) return false;
    float bound = fmaxf(1.0f, w->terrain.halfSize - fmaxf(0.6f, filter->clearanceRadius + 0.6f));
    float maxR = (filter->maxDistFromCenter > 0.1f && filter->maxDistFromCenter < bound)
                     ? filter->maxDistFromCenter : bound;
    float minR = fminf(fmaxf(0.0f, filter->minDistFromCenter), maxR);
    unsigned attempts = maxAttempts > 0u ? maxAttempts : 64u;

    for (unsigned a = 0; a < attempts; ++a) {
        float x, z;
        if (filter->maxDistFromCenter > 0.1f || filter->minDistFromCenter > 0.1f) {
            float ang = nextRng01(rngState) * 6.2831853f;
            float u = nextRng01(rngState);
            float dist = sqrtf(minR * minR + u * (maxR * maxR - minR * minR));
            x = sinf(ang) * dist;
            z = cosf(ang) * dist;
        } else {
            x = (nextRng01(rngState) * 2.0f - 1.0f) * bound;
            z = (nextRng01(rngState) * 2.0f - 1.0f) * bound;
        }

        if (hasAvoidPos && minAvoidDist > 0.0f) {
            float dx = x - avoidPos.x, dz = z - avoidPos.z;
            float req = (a < attempts * 2u / 3u) ? minAvoidDist : (minAvoidDist * 0.5f);
            if (dx * dx + dz * dz < req * req) continue;
        }

        /* Relajar gradualmente la fertilidad mínima sólo en el último tercio si el terreno es árido,
         * preservando siempre las restricciones físicas de pendiente, agua y rocas. */
        WorldHabitatFilter f = *filter;
        if (a >= (attempts * 3u) / 4u) {
            f.minFertility *= 0.45f;
            f.minMoisture *= 0.5f;
        }
        Vector3 pos, nrm;
        if (WorldPlacement_ValidatePoint(w, x, z, &f, &pos, &nrm)) {
            *outPos = pos;
            if (outNormal) *outNormal = nrm;
            return true;
        }
    }
    return false;
}

bool WorldPlacement_ResolveCarcass(const WorldStaticData *w, Vector3 deathPos, float radius,
                                   Vector3 *outPos, Vector3 *outNormal) {
    if (!outPos) return false;
    if (!w || w->isFlat) {
        *outPos = Vec3_Create(deathPos.x, 0.0f, deathPos.z);
        if (outNormal) *outNormal = Vec3_Create(0.0f, 1.0f, 0.0f);
        return true;
    }

    SurfaceHit hit;
    if (Terrain_SampleGround(&w->terrain, deathPos.x, deathPos.z, &hit)) {
        WaterSample ws = {0};
        bool deepWater = WorldHydrology_SampleWater(&w->hydrology, &w->terrain,
                                                    deathPos.x, deathPos.z, &ws) &&
                         ws.wet && ws.depth > 0.45f;
        bool inRock = WorldGeology_CollidesDisc(&w->geology, hit.position, radius * 0.5f);
        if (!deepWater && !inRock) {
            *outPos = hit.position;
            if (outNormal) *outNormal = hit.normal;
            return true;
        }
    }

    /* Búsqueda local acotada (radio <= 3.5 u.m.) cerca del lugar real de la muerte. */
    for (int ring = 1; ring <= 4; ++ring) {
        float dist = (float)ring * 0.85f;
        for (int k = 0; k < 12; ++k) {
            float ang = (float)k * (6.2831853f / 12.0f);
            float cx = deathPos.x + cosf(ang) * dist;
            float cz = deathPos.z + sinf(ang) * dist;
            if (!Terrain_SampleGround(&w->terrain, cx, cz, &hit)) continue;
            WaterSample ws = {0};
            if (WorldHydrology_SampleWater(&w->hydrology, &w->terrain, cx, cz, &ws) &&
                ws.wet && ws.depth > 0.35f) continue;
            if (WorldGeology_CollidesDisc(&w->geology, hit.position, radius * 0.5f)) continue;
            *outPos = hit.position;
            if (outNormal) *outNormal = hit.normal;
            return true;
        }
    }
    return false;
}
