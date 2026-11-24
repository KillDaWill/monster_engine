#include "CreaturePerception.h"
#include <math.h>
#include <stdlib.h>

const PerceptionEntry *CreaturePerception_Find(const CreaturePerception *p, AIObjectId id) {
    for (unsigned i = 0; i < p->count; ++i)
        if (p->entries[i].id == id)
            return &p->entries[i];
    return NULL;
}

static void processCandidate(CreaturePerception *p, const PerceptionProfile *cfg,
                             const AIWorld *w, AIObjectId self, Vector3 eye,
                             float rangeSq, float sinYaw, float cosYaw,
                             float cosHalfH, bool useTanV, float tanHalfV, float halfV,
                             double time, const AIWorldObject *o) {
    if (!o || !o->active || o->id == self || !o->visible) return;
    float dx = o->position.x - eye.x;
    float dy = o->position.y - eye.y;
    float dz = o->position.z - eye.z;
    float distSq = dx * dx + dy * dy + dz * dz;
    if (distSq > rangeSq) return;

    float planarSq = dx * dx + dz * dz;
    float planar = planarSq > 1e-12f ? sqrtf(planarSq) : 0.0f;
    float dot = planar > 1e-6f ? (sinYaw * dx + cosYaw * dz) / planar : 1.0f;
    if (dot < -1.0f) dot = -1.0f;
    else if (dot > 1.0f) dot = 1.0f;
    if (dot < cosHalfH) return;

    if (cfg->verticalFOV > 0.0f) {
        if (useTanV) {
            if (fabsf(dy) > planar * tanHalfV) return;
        } else {
            if (fabsf(atan2f(dy, planar)) > halfV) return;
        }
    }

    if (!AIWorld_LineOfSight(w, eye, o->position, o->id)) return;

    unsigned slot = p->count;
    for (unsigned j = 0; j < p->count; ++j) {
        if (p->entries[j].id == o->id) {
            slot = j;
            break;
        }
    }
    if (slot == CREATURE_MEMORY_CAPACITY) {
        slot = 0;
        for (unsigned j = 1; j < p->count; ++j) {
            if (p->entries[j].observed < p->entries[slot].observed) slot = j;
        }
    } else if (slot == p->count) {
        ++p->count;
    }
    double unreachable = p->entries[slot].id == o->id ? p->entries[slot].unreachableUntil : 0.0;
    p->entries[slot] = (PerceptionEntry){
        .id = o->id,
        .type = o->type,
        .group = o->group,
        .species = o->species,
        .category = o->category,
        .foodCategory = o->foodCategory,
        .available = o->available,
        .reproductiveReady = o->reproductiveReady,
        .position = o->position,
        .radius = o->radius,
        .maximumSpeed = o->maximumSpeed,
        .observed = time,
        .visible = true,
        .drinkable = o->drinkable,
        .unreachableUntil = unreachable
    };
}

void CreaturePerception_Update(CreaturePerception *p, const PerceptionProfile *cfg,
                               const AIWorld *w, AIObjectId self, Vector3 eye, float yaw,
                               double time) {
    for (unsigned i = 0; i < p->count;) {
        if (!AIWorld_Get(w, p->entries[i].id) ||
            time - p->entries[i].observed > cfg->memoryDuration)
            p->entries[i] = p->entries[--p->count];
        else
            ++i;
    }
    if (time < p->nextRefresh) return;
    p->nextRefresh = time + fmaxf(0.01f, cfg->interval);
    for (unsigned i = 0; i < p->count; ++i) p->entries[i].visible = false;

    /* Precalcular constantes trigonométricas una sola vez por refresco perceptivo. */
    float rangeSq = cfg->range * cfg->range;
    float sinYaw = sinf(yaw);
    float cosYaw = cosf(yaw);
    float cosHalfH = cosf(cfg->horizontalFOV * 0.5f);
    float halfV = cfg->verticalFOV * 0.5f;
    bool useTanV = (cfg->verticalFOV > 0.0f && halfV < 1.55f);
    float tanHalfV = useTanV ? tanf(halfV) : 0.0f;

    if (w->spatial) {
        uint32_t stackSlots[512];
        bool overflow = false;
        unsigned cand = AIWorld_QuerySlotsRadius(w, AI_SPATIAL_MASK_ALL, eye, cfg->range,
                                                 stackSlots, 512u, &overflow);
        if (!overflow) {
            for (unsigned k = 0; k < cand; ++k) {
                processCandidate(p, cfg, w, self, eye, rangeSq, sinYaw, cosYaw,
                                 cosHalfH, useTanV, tanHalfV, halfV, time,
                                 AIWorld_At(w, stackSlots[k]));
            }
            return;
        }
        /* Manejo explícito de desbordamiento sin truncamiento silencioso. */
        uint32_t *dynSlots = malloc(w->capacity * sizeof(uint32_t));
        if (dynSlots) {
            cand = AIWorld_QuerySlotsRadius(w, AI_SPATIAL_MASK_ALL, eye, cfg->range,
                                            dynSlots, w->capacity, &overflow);
            for (unsigned k = 0; k < cand; ++k) {
                processCandidate(p, cfg, w, self, eye, rangeSq, sinYaw, cosYaw,
                                 cosHalfH, useTanV, tanHalfV, halfV, time,
                                 AIWorld_At(w, dynSlots[k]));
            }
            free(dynSlots);
            return;
        }
    }

    for (unsigned i = 0; i < w->capacity; ++i) {
        processCandidate(p, cfg, w, self, eye, rangeSq, sinYaw, cosYaw,
                         cosHalfH, useTanV, tanHalfV, halfV, time, AIWorld_At(w, i));
    }
}
