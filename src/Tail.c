/**
 * @file Tail.c
 * @brief Resolución de anatomía modular de colas con soporte para múltiples arquetipos.
 * @author Monster Engine Team
 * @date 2026
 */

#include "Tail.h"
#include "CreatureModuleInternal.h"
#include "MathUtils.h"
#include <math.h>

typedef struct TailProfileSample {
    float widthFactor;
    float heightFactor;
    Vector3 localOffset;
} TailProfileSample;

static TailProfileSample EvaluateTailProfile(TailArchetype archetype, float t, float curvature, float taperCurve) {
    TailProfileSample s;
    s.widthFactor = 1.0f;
    s.heightFactor = 1.0f;
    s.localOffset = Vec3_Zero();

    switch (archetype) {
    case TAIL_ARCHETYPE_WHIP: {
        float tc = taperCurve > 0.0f ? taperCurve * 1.5f : 2.2f;
        float taper = powf(fmaxf(0.0f, 1.0f - t), tc);
        s.widthFactor = fmaxf(taper, 0.03f);
        s.heightFactor = fmaxf(taper, 0.03f);
        s.localOffset.x = curvature * (t * t * 0.7f + 0.3f * sinf(t * 3.5f));
        s.localOffset.y = 0.08f * t;
        s.localOffset.z = 0.0f;
        break;
    }
    case TAIL_ARCHETYPE_HEAVY: {
        float bulb = 1.0f + 0.32f * sinf(t * 3.14159265f);
        float drop = (t > 0.65f) ? powf(fmaxf(0.0f, (1.0f - t) / 0.35f), 1.4f) : 1.0f;
        s.widthFactor = fmaxf(bulb * drop, 0.08f);
        s.heightFactor = fmaxf((0.88f + 0.24f * sinf(t * 3.14159265f)) * drop, 0.08f);
        s.localOffset.x = curvature * t * t * 0.4f;
        s.localOffset.y = 0.05f * t;
        s.localOffset.z = 0.0f;
        break;
    }
    case TAIL_ARCHETYPE_PREHENSILE: {
        float tc = taperCurve > 0.0f ? taperCurve : 1.25f;
        float taper = powf(fmaxf(0.0f, 1.0f - t), tc);
        s.widthFactor = fmaxf(taper, 0.04f);
        s.heightFactor = fmaxf(taper, 0.04f);
        if (t > 0.35f) {
            float u = (t - 0.35f) / 0.65f;
            float curlAngle = u * u * 3.14159265f * 1.35f;
            float curlRadius = 0.55f * (1.0f - u * 0.35f);
            float sign = (curvature < 0.0f) ? -1.0f : 1.0f;
            s.localOffset.x = sign * (1.0f - cosf(curlAngle)) * curlRadius + curvature * t * 0.3f;
            s.localOffset.y = sinf(curlAngle) * curlRadius + 0.12f * t;
            s.localOffset.z = -curlAngle * 0.20f;
        } else {
            s.localOffset.x = curvature * t * t * 0.5f;
            s.localOffset.y = 0.12f * t;
            s.localOffset.z = 0.0f;
        }
        break;
    }
    case TAIL_ARCHETYPE_FINNED: {
        float u = (t >= 0.20f && t <= 0.90f) ? (t - 0.20f) / 0.70f : 0.0f;
        float finBlade = (u > 0.0f) ? sinf(u * 3.14159265f) * 1.45f : 0.0f;
        float baseTaper = powf(fmaxf(0.0f, 1.0f - t), 0.85f);
        s.widthFactor = fmaxf(baseTaper * 0.50f, 0.04f);
        s.heightFactor = fmaxf(baseTaper * 1.15f + finBlade, 0.06f);
        s.localOffset.x = curvature * t * t * 0.35f;
        s.localOffset.y = 0.06f * t;
        s.localOffset.z = 0.0f;
        break;
    }
    case TAIL_ARCHETYPE_TAPERED:
    default: {
        float taper = powf(fmaxf(0.0f, 1.0f - t), taperCurve);
        s.widthFactor = taper;
        s.heightFactor = taper;
        s.localOffset.x = curvature * t * t;
        s.localOffset.y = 0.12f * t;
        s.localOffset.z = 0.0f;
        break;
    }
    }
    return s;
}

bool Tail_Resolve(const TailPhenotype* p, uint32_t module, const AttachmentSlot* slot, AnatomyGraph* g) {
    if (!p || !slot || !g || p->segmentCount < 2 ||
        p->segmentCount > CREATURE_MAX_TAIL_STATIONS || !Anatomy_MakeId(module, 1)) {
        return false;
    }

    AnatomyId previous = slot->hostNode;
    for (unsigned i = 0; i < p->segmentCount; ++i) {
        const float four[] = {0.0f, 0.28f, 0.62f, 1.0f};
        float t = (p->segmentCount == 4 && p->archetype == TAIL_ARCHETYPE_TAPERED) ?
            four[i] : (float)i / (float)(p->segmentCount - 1);

        TailProfileSample sProfile = EvaluateTailProfile(p->archetype, t, p->curvature, p->taperCurve);
        float s = slot->scale;
        float development = fmaxf(0.0005f, p->development);

        float wr = fmaxf(1e-5f, Math_Lerp(p->tipWidth, p->baseWidth, sProfile.widthFactor) * s * development);
        float hr = fmaxf(1e-5f, Math_Lerp(p->tipHeight, p->baseHeight, sProfile.heightFactor) * s * development);

        /* Acoplamiento anatómico opcional con la sección transversal pélvica anfitriona */
        if (p->rootMatchStrength > 0.0f && slot->hostRadii.x > 0.0f && slot->hostRadii.y > 0.0f) {
            float hostWeight = Math_Clamp01(p->rootMatchStrength) * powf(fmaxf(0.0f, 1.0f - t), 3.0f);
            float hostW = slot->hostRadii.x * 0.95f;
            float hostH = slot->hostRadii.y * 0.95f;
            wr = Math_Lerp(wr, hostW, hostWeight);
            hr = Math_Lerp(hr, hostH, hostWeight);
        }

        Vector3 c = Vec3_Add(slot->position,
            Vec3_Add(Vec3_Scale(slot->forward, (p->length * t + sProfile.localOffset.z) * s * development + 0.10f * fminf(slot->hostRadii.z, p->baseWidth * s)),
            Vec3_Add(Vec3_Scale(slot->up, sProfile.localOffset.y * s),
                     Vec3_Scale(slot->side, sProfile.localOffset.x * s))));

        if (p->archetype == TAIL_ARCHETYPE_PREHENSILE ||
            (p->archetype == TAIL_ARCHETYPE_TAPERED && fabsf(p->curvature) > 1e-6f)) {
            /* Integración de tangentes: longitud de arco estable y curvatura distal. */
            float x = 0, z = 0;
            const unsigned steps = 96;
            for (unsigned j = 0; j < steps; ++j) {
                float u = t * ((float)j + .5f) / steps;
                float v = Math_Clamp01((u - .25f) / .75f);
                float angle = p->curvature * 2.7f * v * v;
                x += sinf(angle) * p->length * t / steps;
                z += cosf(angle) * p->length * t / steps;
            }
            c = Vec3_Add(slot->position, Vec3_Add(Vec3_Scale(slot->forward, z*s*development),
                    Vec3_Scale(slot->side, x*s*development)));
        }

        AnatomyId id = Anatomy_MakeId(module, (uint16_t)(i + 1));
        if (!Module_Node(g, module, (uint16_t)(i + 1), c, wr, hr, i < 2 ? 2 : 1,
                         ANATOMY_ROLE_AXIAL, ANATOMY_REGION_TAIL, ANATOMY_SIDE_CENTER, p->development) ||
            !Module_Edge(g, module, (uint16_t)(i + 1), previous, id,
                         BODY_CONNECTION_AXIAL_LOFT, p->development)) {
            return false;
        }
        g->dormantConnections[g->connectionCount - 1] = (p->development <= 0.0f);
        previous = id;
    }
    return true;
}
