#include "MorphViewCamera.h"
#include "MathUtils.h"
#include <math.h>

void MorphViewCamera_Framing(const Monster* m, const MonsterSDF* sdf, Vector3* outCenter, float* outRadius,
    Vector3* outHeadCenter, Vector3* outTailCenter) {
    Vector3 minP = Vec3_Create(1e6f, 1e6f, 1e6f), maxP = Vec3_Create(-1e6f, -1e6f, -1e6f);
    bool valid = false;

    if (sdf && sdf->hasSurface && isfinite(sdf->bounds.start.x) && isfinite(sdf->bounds.end.x)) {
        minP = sdf->bounds.start;
        maxP = sdf->bounds.end;
        valid = true;
    } else if (m && m->hasAnatomyGraph && m->anatomyGraph.nodeCount > 0) {
        for (size_t i = 0; i < m->anatomyGraph.nodeCount; ++i) {
            const AnatomyNode* n = &m->anatomyGraph.nodes[i];
            if (n->development <= 1e-4f && n->widthRadius <= 2e-4f) continue;
            float rx = fmaxf(n->widthRadius, n->envelopeRadii.x);
            float ry = fmaxf(n->heightRadius, n->envelopeRadii.y);
            float rz = fmaxf(rx, n->envelopeRadii.z);
            minP.x = fminf(minP.x, n->center.x - rx); maxP.x = fmaxf(maxP.x, n->center.x + rx);
            minP.y = fminf(minP.y, n->center.y - ry); maxP.y = fmaxf(maxP.y, n->center.y + ry);
            minP.z = fminf(minP.z, n->center.z - rz); maxP.z = fmaxf(maxP.z, n->center.z + rz);
            valid = true;
        }
    }

    if (!valid) {
        *outCenter = Vec3_Create(0.0f, 1.8f, 0.0f);
        *outRadius = 3.5f;
    } else {
        *outCenter = Vec3_Scale(Vec3_Add(minP, maxP), 0.5f);
        Vector3 ext = Vec3_Scale(Vec3_Sub(maxP, minP), 0.5f);
        *outRadius = fmaxf(1.5f, sqrtf(ext.x * ext.x + ext.y * ext.y + ext.z * ext.z));
    }

    *outHeadCenter = *outCenter;
    *outTailCenter = *outCenter;
    if (m && m->hasAnatomyGraph) {
        const AnatomyNode* head = AnatomyGraph_FindFirstRegion(&m->anatomyGraph, ANATOMY_REGION_HEAD);
        if (head) *outHeadCenter = head->center;
        const AnatomyNode* tail = AnatomyGraph_FindFirstRegion(&m->anatomyGraph, ANATOMY_REGION_TAIL);
        if (!tail) tail = AnatomyGraph_FindFirstRegion(&m->anatomyGraph, ANATOMY_REGION_PELVIS);
        if (tail) *outTailCenter = tail->center;
    }
}

void MorphViewCamera_Setup(ICamera* cam, int viewMode, float orbitYaw, float orbitPitch, float zoomScale,
    const MonsterSDF* sdf, Vector3 bodyCenter, float bodyRadius, Vector3 headCenter, Vector3 tailCenter) {
    float clampedZoom = Math_Clamp(zoomScale, 0.55f, 1.85f);
    float dist = bodyRadius * 2.05f * clampedZoom;
    Vector3 focus = bodyCenter;
    float yaw = orbitYaw, pitch = orbitPitch;

    if (viewMode == 1) {
        /* Lateral */
        yaw = 1.570796f + orbitYaw * 0.15f;
        pitch = 0.08f;
    } else if (viewMode == 2) {
        /* Frontal */
        yaw = 0.05f + orbitYaw * 0.15f;
        pitch = 0.10f;
    } else if (viewMode == 4) {
        /* Primer plano cabeza */
        focus = headCenter;
        dist = fmaxf(1.6f, bodyRadius * 0.65f * clampedZoom);
        yaw = 0.65f + orbitYaw * 0.35f;
        pitch = 0.18f;
    } else if (viewMode == 5) {
        /* Vista caudal */
        focus = tailCenter;
        dist = fmaxf(2.0f, bodyRadius * 0.85f * clampedZoom);
        yaw = 2.65f + orbitYaw * 0.35f;
        pitch = 0.22f;
    }

    float cp = cosf(pitch);
    float yLift = (viewMode == 4 || viewMode == 5) ? 0.0f : bodyRadius * 0.06f * clampedZoom;
    Vector3 offset = Vec3_Create(
        sinf(yaw) * cp * dist,
        sinf(pitch) * dist + yLift,
        cosf(yaw) * cp * dist);
    float len = Vec3_Length(offset);
    Vector3 dir = len > 1e-5f ? Vec3_Scale(offset, 1.0f / len) : Vec3_Create(0.0f, 0.0f, 1.0f);
    float camDist = len > 1e-5f ? len : dist;
    if (sdf) {
        /* Mantener la cámara fuera del volumen SDF con margen seguro para nearPlane y malla. */
        const float minClearance = fmaxf(cam->nearPlane * 8.0f, 0.80f);
        for (int iter = 0; iter < 4; ++iter) {
            Vector3 candidate = Vec3_Add(focus, Vec3_Scale(dir, camDist));
            float d = MonsterSDF_EvaluateDistance(sdf, candidate);
            if (d >= minClearance) break;
            camDist += (minClearance - d) + 0.05f;
        }
    }

    cam->target = focus;
    cam->position = Vec3_Add(focus, Vec3_Scale(dir, camDist));
    cam->up = Vec3_Create(0.0f, 1.0f, 0.0f);
}
