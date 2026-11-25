/**
 * @file WorldCollision.c
 * @brief Implementación de consultas de terreno, validación de huella barrida, oclusión 3D
 *        y transformaciones compartidas de marco corporal.
 */
#include "WorldCollision.h"
#include <math.h>
#include <string.h>

TraversalProfile TraversalProfile_DefaultLand(float footprintRadius) {
    return (TraversalProfile){
        .maxSlopeRadians = 0.62f,     /* ~35.5 grados */
        .maxStepHeight = 0.55f,
        .maxWadingDepth = 0.42f,
        .footprintRadius = footprintRadius > 0.05f ? footprintRadius : 0.85f,
        .uphillSpeedFactor = 0.52f,
        .downhillSpeedFactor = 0.88f,
        .wadingSpeedFactor = 0.55f,
        .slopeEnergyCost = 0.85f,
        .wadingEnergyCost = 0.65f,
        .maxBodyTiltRadians = 0.48f,  /* ~27.5 grados de inclinación visual/corporal máxima */
        .canWade = true,
        .canSwim = false
    };
}

bool World_SampleSurface(const WorldStaticData *w, Vector3 query, SurfaceHit *out) {
    if (!w || !out) return false;
    return Terrain_SampleGround(&w->terrain, query.x, query.z, out);
}

bool World_SampleWater(const WorldStaticData *w, float x, float z, WaterSample *out) {
    if (!w || !out) return false;
    return WorldHydrology_SampleWater(&w->hydrology, &w->terrain, x, z, out);
}

bool World_CanTraverse(const WorldStaticData *w, Vector3 groundPos, const TraversalProfile *profile,
                       TraversalBlockReason *outReason) {
    if (outReason) *outReason = TRAVERSAL_OK;
    if (!w || !profile || !isfinite(groundPos.x) || !isfinite(groundPos.z)) {
        if (outReason) *outReason = TRAVERSAL_INVALID_INPUT;
        return false;
    }
    float r = fmaxf(0.05f, profile->footprintRadius);
    if (fabsf(groundPos.x) + r > w->terrain.halfSize ||
        fabsf(groundPos.z) + r > w->terrain.halfSize) {
        if (outReason) *outReason = TRAVERSAL_OUT_OF_BOUNDS;
        return false;
    }

    if (w->isFlat) {
        return true;
    }

    /* Comprobar rocas colisionables en la huella. */
    if (WorldGeology_CollidesDisc(&w->geology, groundPos, r)) {
        if (outReason) *outReason = TRAVERSAL_ROCK_BLOCKED;
        return false;
    }

    /* Comprobar centro y 4 puntos cardinales de la huella (pendiente, escalón y profundidad de agua). */
    SurfaceHit centerHit;
    if (!Terrain_SampleGround(&w->terrain, groundPos.x, groundPos.z, &centerHit)) {
        if (outReason) *outReason = TRAVERSAL_OUT_OF_BOUNDS;
        return false;
    }
    float maxSlope = acosf(fmaxf(0.0f, fminf(1.0f, centerHit.normal.y)));
    if (maxSlope > profile->maxSlopeRadians) {
        if (outReason) *outReason = TRAVERSAL_TOO_STEEP;
        return false;
    }

    float maxAllowedDepth = profile->canWade ? fmaxf(0.0f, profile->maxWadingDepth) : 0.05f;
    WaterSample ws;
    if (WorldHydrology_SampleWater(&w->hydrology, &w->terrain, groundPos.x, groundPos.z, &ws) &&
        ws.wet && ws.depth > maxAllowedDepth) {
        if (outReason) *outReason = TRAVERSAL_WATER_TOO_DEEP;
        return false;
    }

    static const float dx4[4] = {1.0f, -1.0f, 0.0f, 0.0f};
    static const float dz4[4] = {0.0f, 0.0f, 1.0f, -1.0f};
    float probeR = r * 0.85f;
    float maxFootSlope = profile->maxSlopeRadians + 0.06f;
    for (int k = 0; k < 4; ++k) {
        float px = groundPos.x + dx4[k] * probeR;
        float pz = groundPos.z + dz4[k] * probeR;
        SurfaceHit ph;
        if (!Terrain_SampleGround(&w->terrain, px, pz, &ph)) {
            if (outReason) *outReason = TRAVERSAL_OUT_OF_BOUNDS;
            return false;
        }
        float sl = acosf(fmaxf(0.0f, fminf(1.0f, ph.normal.y)));
        if (sl > maxFootSlope) {
            if (outReason) *outReason = TRAVERSAL_TOO_STEEP;
            return false;
        }
        float stepDiff = fabsf(ph.position.y - centerHit.position.y);
        float maxFootDelta = fmaxf(profile->maxStepHeight, probeR * tanf(profile->maxSlopeRadians) + 0.12f);
        if (stepDiff > maxFootDelta) {
            if (outReason) *outReason = TRAVERSAL_STEP_TOO_HIGH;
            return false;
        }
        if (WorldHydrology_SampleWater(&w->hydrology, &w->terrain, px, pz, &ws) &&
            ws.wet && ws.depth > maxAllowedDepth + 0.08f) {
            if (outReason) *outReason = TRAVERSAL_WATER_TOO_DEEP;
            return false;
        }
    }
    return true;
}

bool World_CheckTraversalSegment(const WorldStaticData *w, Vector3 from, Vector3 to,
                                 const TraversalProfile *profile, TraversalResult *out) {
    TraversalResult localRes;
    memset(&localRes, 0, sizeof(localRes));
    localRes.speedMultiplier = 1.0f;
    localRes.energyMultiplier = 1.0f;
    localRes.endNormal = Vec3_Create(0.0f, 1.0f, 0.0f);

    if (!w || !profile || !isfinite(from.x) || !isfinite(from.z) || !isfinite(to.x) || !isfinite(to.z)) {
        localRes.reason = TRAVERSAL_INVALID_INPUT;
        if (out) *out = localRes;
        return false;
    }

    float r = fmaxf(0.05f, profile->footprintRadius);
    if (fabsf(from.x) + r > w->terrain.halfSize || fabsf(from.z) + r > w->terrain.halfSize ||
        fabsf(to.x) + r > w->terrain.halfSize || fabsf(to.z) + r > w->terrain.halfSize) {
        localRes.reason = TRAVERSAL_OUT_OF_BOUNDS;
        if (out) *out = localRes;
        return false;
    }

    if (w->isFlat) {
        localRes.traversable = true;
        localRes.resolvedEndPos = Vec3_Create(to.x, 0.0f, to.z);
        if (out) *out = localRes;
        return true;
    }

    /* Barrido conservador de segmento contra rocas estáticas. */
    if (WorldGeology_CollidesSegmentXZ(&w->geology, from, to, r)) {
        localRes.reason = TRAVERSAL_ROCK_BLOCKED;
        if (out) *out = localRes;
        return false;
    }

    float dx = to.x - from.x;
    float dz = to.z - from.z;
    float distXZ = sqrtf(dx * dx + dz * dz);
    float sampleStep = fmaxf(0.25f, fminf(w->terrain.spacing * 0.9f, r * 0.65f));
    unsigned steps = (distXZ > 1e-5f) ? (unsigned)ceilf(distXZ / sampleStep) : 1u;
    if (steps < 1u) steps = 1u;
    if (steps > 512u) steps = 512u;

    SurfaceHit prevHit;
    if (!Terrain_SampleGround(&w->terrain, from.x, from.z, &prevHit)) {
        localRes.reason = TRAVERSAL_OUT_OF_BOUNDS;
        if (out) *out = localRes;
        return false;
    }

    float maxSlopeSeen = 0.0f;
    float maxDepthSeen = 0.0f;
    float maxAllowedDepth = profile->canWade ? fmaxf(0.0f, profile->maxWadingDepth) : 0.05f;
    float maxSlopeTan = tanf(profile->maxSlopeRadians);

    for (unsigned s = 1u; s <= steps; ++s) {
        float t = (float)s / (float)steps;
        float px = from.x + dx * t;
        float pz = from.z + dz * t;
        SurfaceHit curHit;
        if (!Terrain_SampleGround(&w->terrain, px, pz, &curHit)) {
            localRes.reason = TRAVERSAL_OUT_OF_BOUNDS;
            if (out) *out = localRes;
            return false;
        }
        float slope = acosf(fmaxf(0.0f, fminf(1.0f, curHit.normal.y)));
        if (slope > maxSlopeSeen) maxSlopeSeen = slope;
        if (slope > profile->maxSlopeRadians) {
            localRes.reason = TRAVERSAL_TOO_STEEP;
            localRes.maxSlopeSeen = maxSlopeSeen;
            if (out) *out = localRes;
            return false;
        }

        float segStepDist = distXZ / (float)steps;
        float dh = fabsf(curHit.position.y - prevHit.position.y);
        if (dh > profile->maxStepHeight && segStepDist > 1e-4f && (dh / segStepDist) > maxSlopeTan * 1.15f) {
            localRes.reason = TRAVERSAL_STEP_TOO_HIGH;
            localRes.maxSlopeSeen = maxSlopeSeen;
            if (out) *out = localRes;
            return false;
        }

        WaterSample ws;
        if (WorldHydrology_SampleWater(&w->hydrology, &w->terrain, px, pz, &ws) && ws.wet) {
            if (ws.depth > maxDepthSeen) maxDepthSeen = ws.depth;
            if (ws.depth > maxAllowedDepth) {
                localRes.reason = TRAVERSAL_WATER_TOO_DEEP;
                localRes.maxWaterDepthSeen = maxDepthSeen;
                if (out) *out = localRes;
                return false;
            }
        }
        prevHit = curHit;
    }

    /* Validar huella completa en el punto destino final. */
    TraversalBlockReason endReason = TRAVERSAL_OK;
    if (!World_CanTraverse(w, prevHit.position, profile, &endReason)) {
        localRes.reason = endReason;
        localRes.maxSlopeSeen = maxSlopeSeen;
        localRes.maxWaterDepthSeen = maxDepthSeen;
        if (out) *out = localRes;
        return false;
    }

    /* Calcular factores acotados de velocidad y gasto energético por pendiente y vadeo. */
    float startY = 0.0f;
    Terrain_SampleHeight(&w->terrain, from.x, from.z, &startY);
    float totalDh = prevHit.position.y - startY;
    float grade = (distXZ > 1e-4f) ? (totalDh / distXZ) : 0.0f;
    float normSlope = fminf(1.0f, maxSlopeSeen / fmaxf(0.1f, profile->maxSlopeRadians));

    float speedMult = 1.0f;
    float energyMult = 1.0f;
    if (grade > 0.02f) {
        float upFrac = fminf(1.0f, grade / fmaxf(0.1f, maxSlopeTan));
        speedMult *= 1.0f - upFrac * (1.0f - fmaxf(0.25f, fminf(1.0f, profile->uphillSpeedFactor)));
        energyMult += upFrac * fmaxf(0.0f, profile->slopeEnergyCost);
    } else if (grade < -0.04f) {
        float downFrac = fminf(1.0f, -grade / fmaxf(0.1f, maxSlopeTan));
        speedMult *= 1.0f - downFrac * (1.0f - fmaxf(0.50f, fminf(1.15f, profile->downhillSpeedFactor)));
    } else if (normSlope > 0.35f) {
        /* Pendiente transversal moderada */
        speedMult *= 1.0f - 0.12f * normSlope;
        energyMult += 0.15f * normSlope * fmaxf(0.0f, profile->slopeEnergyCost);
    }

    if (maxDepthSeen > 0.04f && maxAllowedDepth > 0.05f) {
        float wadeFrac = fminf(1.0f, maxDepthSeen / maxAllowedDepth);
        speedMult *= 1.0f - wadeFrac * (1.0f - fmaxf(0.25f, fminf(1.0f, profile->wadingSpeedFactor)));
        energyMult += wadeFrac * fmaxf(0.0f, profile->wadingEnergyCost);
    }

    localRes.traversable = true;
    localRes.reason = TRAVERSAL_OK;
    localRes.maxSlopeSeen = maxSlopeSeen;
    localRes.maxWaterDepthSeen = maxDepthSeen;
    localRes.speedMultiplier = fmaxf(0.20f, fminf(1.15f, speedMult));
    localRes.energyMultiplier = fmaxf(1.0f, fminf(3.0f, energyMult));
    localRes.resolvedEndPos = prevHit.position;
    localRes.endNormal = prevHit.normal;
    if (out) *out = localRes;
    return true;
}

bool World_ClearSightSegment(const WorldStaticData *w, Vector3 a, Vector3 b,
                             float radius, float endpointTolerance) {
    if (!w) return true;
    if (!isfinite(a.x) || !isfinite(a.y) || !isfinite(a.z) ||
        !isfinite(b.x) || !isfinite(b.y) || !isfinite(b.z)) {
        return false;
    }
    if (w->isFlat) return true;

    /* 1) Oclusión por rocas geológicas (cilindros/lóbulos conservadores 3D). */
    if (WorldGeology_OccludesSegment3D(&w->geology, a, b, fmaxf(0.0f, radius))) {
        return false;
    }

    /* 2) Intersección exacta del rayo contra triángulos del terreno.
     * Para segmentos con radio > 0 desplazamos el segmento hacia abajo por radio como cota conservadora. */
    Vector3 qa = a, qb = b;
    if (radius > 1e-4f) {
        qa.y -= radius;
        qb.y -= radius;
    }
    if (Terrain_RaycastSegment(&w->terrain, qa, qb, endpointTolerance, NULL, NULL)) {
        return false;
    }
    return true;
}

Quaternion World_BuildBodyFrame(float yaw, Vector3 groundNormal, float maxTiltRadians) {
    if (!isfinite(yaw)) yaw = 0.0f;
    Quaternion qYaw = Quat_FromAxisAngle(Vec3_Create(0.0f, 1.0f, 0.0f), yaw);
    if (!isfinite(groundNormal.x) || !isfinite(groundNormal.y) || !isfinite(groundNormal.z) ||
        Vec3_LengthSq(groundNormal) < 1e-8f) {
        return qYaw;
    }
    Vector3 n = Vec3_Normalize(groundNormal);
    if (n.y < 0.0f) n = Vec3_Scale(n, -1.0f);
    if (n.y >= 0.99995f || maxTiltRadians <= 1e-4f) {
        return qYaw;
    }

    /* Acotar el ángulo de inclinación desde +Y para evitar posturas antinaturales en taludes. */
    float angle = acosf(fmaxf(-1.0f, fminf(1.0f, n.y)));
    float clampedAngle = fminf(angle, maxTiltRadians);
    Vector3 axis = Vec3_Cross(Vec3_Create(0.0f, 1.0f, 0.0f), n);
    float axisLenSq = Vec3_LengthSq(axis);
    if (axisLenSq < 1e-10f) return qYaw;
    axis = Vec3_Scale(axis, 1.0f / sqrtf(axisLenSq));
    Quaternion qTilt = Quat_FromAxisAngle(axis, clampedAngle);
    return Quat_Normalize(Quat_Multiply(qTilt, qYaw));
}

Vector3 World_LocalToWorld(Vector3 localPos, Vector3 modelCenter, float scale,
                           Quaternion bodyOrientation, Vector3 worldOrigin) {
    Vector3 rel = Vec3_Scale(Vec3_Sub(localPos, modelCenter), scale);
    return Vec3_Add(worldOrigin, Quat_RotateVector(bodyOrientation, rel));
}

Vector3 World_WorldToLocal(Vector3 worldPos, Vector3 modelCenter, float scale,
                           Quaternion bodyOrientation, Vector3 worldOrigin) {
    float invScale = fabsf(scale) > 1e-7f ? (1.0f / scale) : 1.0f;
    Quaternion invQ = Quat_Conjugate(bodyOrientation);
    Vector3 rel = Quat_RotateVector(invQ, Vec3_Sub(worldPos, worldOrigin));
    return Vec3_Add(modelCenter, Vec3_Scale(rel, invScale));
}

Vector3 World_LocalDirToWorld(Vector3 localDir, Quaternion bodyOrientation) {
    return Quat_RotateVector(bodyOrientation, localDir);
}

Vector3 World_WorldDirToLocal(Vector3 worldDir, Quaternion bodyOrientation) {
    return Quat_RotateVector(Quat_Conjugate(bodyOrientation), worldDir);
}
