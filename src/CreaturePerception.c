#include "CreaturePerception.h"
#include <math.h>
const PerceptionEntry *CreaturePerception_Find(const CreaturePerception *p, AIObjectId id) {
    for (unsigned i = 0; i < p->count; ++i)
        if (p->entries[i].id == id)
            return &p->entries[i];
    return NULL;
}
void CreaturePerception_Update(CreaturePerception *p, const PerceptionProfile *cfg,
                               const AIWorld *w, AIObjectId self, Vector3 eye, float yaw,
                               double time) {
    for (unsigned i = 0; i < p->count;)
        if (!AIWorld_Get(w, p->entries[i].id) ||
            time - p->entries[i].observed > cfg->memoryDuration)
            p->entries[i] = p->entries[--p->count];
        else
            ++i;
    if (time < p->nextRefresh)
        return;
    p->nextRefresh = time + fmaxf(.01f, cfg->interval);
    for (unsigned i = 0; i < p->count; ++i)
        p->entries[i].visible = false;
    for (unsigned i = 0; i < w->capacity; ++i) {
        const AIWorldObject *o = AIWorld_At(w, i);
        if(!o->active)continue;
        if (o->id == self || !o->visible)
            continue;
        Vector3 d = Vec3_Sub(o->position, eye);
        float planar = sqrtf(d.x * d.x + d.z * d.z);
        if (Vec3_LengthSq(d) > cfg->range * cfg->range)
            continue;
        float dot = planar > 1e-6f ? (sinf(yaw) * d.x + cosf(yaw) * d.z) / planar : 1;
        dot=fmaxf(-1,fminf(1,dot));
        if (dot < cosf(cfg->horizontalFOV * .5f))
            continue;
        if (cfg->verticalFOV > 0 && fabsf(atan2f(d.y, planar)) > cfg->verticalFOV * .5f)
            continue;
        if (!AIWorld_LineOfSight(w, eye, o->position, o->id))
            continue;
        unsigned slot = p->count;
        for (unsigned j = 0; j < p->count; ++j)
            if (p->entries[j].id == o->id) {
                slot = j;
                break;
            }
        if (slot == CREATURE_MEMORY_CAPACITY) {
            slot = 0;
            for (unsigned j = 1; j < p->count; ++j)
                if (p->entries[j].observed < p->entries[slot].observed)
                    slot = j;
        } else if (slot == p->count)
            ++p->count;
        double unreachable = p->entries[slot].id == o->id ? p->entries[slot].unreachableUntil : 0;
        p->entries[slot] = (PerceptionEntry){.id = o->id,
                                             .type = o->type,
                                             .group = o->group,
                                             .species=o->species,.category=o->category,
                                             .foodCategory=o->foodCategory,.available=o->available,
                                             .reproductiveReady=o->reproductiveReady,
                                             .position = o->position,
                                             .radius = o->radius,.maximumSpeed=o->maximumSpeed,
                                             .observed = time,
                                             .visible = true,
                                             .drinkable = o->drinkable,
                                             .unreachableUntil = unreachable};
    }
}
