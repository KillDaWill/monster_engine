#include "AIWorld.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>

static inline AIWorldObject *objects(const AIWorld *w) {
    return w->storage ? w->storage : (AIWorldObject *)w->objects;
}
static inline bool geometry(const AIWorldObject *o) {
    return !o->walkable && o->type != AI_CREATURE;
}
static inline float segmentDistanceSqXZ(Vector3 a, Vector3 b, Vector3 c, float *t) {
    float x = b.x - a.x, z = b.z - a.z, d = x * x + z * z;
    *t = d > 1e-8f ? fmaxf(0.0f, fminf(1.0f, ((c.x - a.x) * x + (c.z - a.z) * z) / d)) : 0.0f;
    x = a.x + *t * x - c.x;
    z = a.z + *t * z - c.z;
    return x * x + z * z;
}

static inline int findSlot(const AIWorld *w, AIObjectId id) {
    if (!w || !id) return -1;
    AIWorldObject *objs = objects(w);
    if (w->spatial) {
        int slot = AISpatialIndex_Lookup(w->spatial, id);
        if (slot >= 0 && (unsigned)slot < w->capacity && objs[slot].active && objs[slot].id == id) {
            return slot;
        }
        return -1;
    }
    for (unsigned i = 0; i < w->capacity; ++i) {
        if (objs[i].active && objs[i].id == id) return (int)i;
    }
    return -1;
}

void AIWorld_Init(AIWorld *w, float half) {
    memset(w, 0, sizeof(*w));
    w->capacity = AI_WORLD_CAPACITY;
    w->nextId = 1;
    w->halfSize = half;
}

bool AIWorld_InitCapacity(AIWorld *w, float half, unsigned capacity) {
    if (!w || !capacity || capacity > 1000000u || !isfinite(half) || half <= 0.0f) return false;
    AIWorld_Init(w, half);
    w->storage = calloc(capacity, sizeof(*w->storage));
    if (!w->storage) return false;
    w->spatial = AISpatialIndex_Create(capacity, half, 4.0f);
    if (!w->spatial) {
        free(w->storage);
        w->storage = NULL;
        return false;
    }
    w->capacity = capacity;
    return true;
}

void AIWorld_Destroy(AIWorld *w) {
    if (!w) return;
    AISpatialIndex_Destroy(w->spatial);
    free(w->navCache);
    free(w->storage);
    memset(w, 0, sizeof(*w));
}

bool AIWorld_SetSpatialCellSize(AIWorld *w, float cellSize) {
    if (!w || !w->spatial) return false;
    return AISpatialIndex_SetCellSize(w->spatial, cellSize, objects(w));
}

const AIWorldObject *AIWorld_At(const AIWorld *w, unsigned slot) {
    return w && slot < w->capacity ? &objects(w)[slot] : NULL;
}

AIObjectId AIWorld_Add(AIWorld *w, AIWorldObject o) {
    if (!w || !w->nextId || !isfinite(o.position.x) || !isfinite(o.position.y) || !isfinite(o.position.z) ||
        !isfinite(o.radius) || o.radius < 0.0f || !isfinite(o.height) || o.height < 0.0f)
        return 0;
    AIWorldObject *objs = objects(w);
    uint32_t slot = 0;
    if (w->spatial) {
        if (!AISpatialIndex_AllocSlot(w->spatial, &slot)) return 0;
    } else {
        bool found = false;
        for (unsigned i = 0; i < w->capacity; ++i) {
            if (!objs[i].active) { slot = i; found = true; break; }
        }
        if (!found) return 0;
    }
    o.id = w->nextId++;
    o.active = true;
    o.revision = 1;
    objs[slot] = o;
    if (w->spatial) {
        AISpatialIndex_Insert(w->spatial, slot, o.id,
                              AISpatialIndex_LayerForObject(&objs[slot]),
                              o.position, o.radius);
    }
    if (geometry(&o)) ++w->revision;
    return o.id;
}

const AIWorldObject *AIWorld_Get(const AIWorld *w, AIObjectId id) {
    int slot = findSlot(w, id);
    return slot >= 0 ? &objects(w)[slot] : NULL;
}

bool AIWorld_Move(AIWorld *w, AIObjectId id, Vector3 p) {
    if (!w || !isfinite(p.x) || !isfinite(p.y) || !isfinite(p.z)) return false;
    int slot = findSlot(w, id);
    if (slot < 0) return false;
    AIWorldObject *o = &objects(w)[slot];
    if (geometry(o)) ++w->revision;
    o->moving = Vec3_Distance(o->position, p) > 1e-6f;
    if (o->moving) ++o->revision;
    o->position = p;
    if (w->spatial) {
        AISpatialIndex_Update(w->spatial, (uint32_t)slot,
                              AISpatialIndex_LayerForObject(o), o->position, o->radius);
    }
    return true;
}

bool AIWorld_SetBounds(AIWorld *w, AIObjectId id, float radius, float height) {
    if (!w || !isfinite(radius) || !isfinite(height) || radius < 0.0f || height < 0.0f)
        return false;
    int slot = findSlot(w, id);
    if (slot < 0) return false;
    AIWorldObject *o = &objects(w)[slot];
    if (geometry(o) && (fabsf(o->radius - radius) > 1e-5f || fabsf(o->height - height) > 1e-5f))
        ++w->revision;
    if (o->radius != radius || o->height != height) ++o->revision;
    o->radius = radius;
    o->height = height;
    if (w->spatial) {
        AISpatialIndex_Update(w->spatial, (uint32_t)slot,
                              AISpatialIndex_LayerForObject(o), o->position, o->radius);
    }
    return true;
}

void AIWorld_Remove(AIWorld *w, AIObjectId id) {
    if (!w || !id) return;
    int slot = findSlot(w, id);
    if (slot < 0) return;
    AIWorldObject *o = &objects(w)[slot];
    if (geometry(o)) ++w->revision;
    o->active = false;
    if (w->spatial) {
        AISpatialIndex_Remove(w->spatial, (uint32_t)slot, id);
    }
}

bool AIWorld_SetObservation(AIWorld *w, AIObjectId id, unsigned species, unsigned category,
                            unsigned food, bool available, bool ready) {
    int slot = findSlot(w, id);
    if (slot < 0) return false;
    AIWorldObject *o = &objects(w)[slot];
    bool layerMayChange = (o->foodCategory != food);
    o->species = species;
    o->category = category;
    o->foodCategory = food;
    if (o->available != available) ++o->revision;
    o->available = available;
    o->reproductiveReady = ready;
    if (o->type == AI_WATER && food) o->drinkable = available;
    if (w->spatial && layerMayChange) {
        AISpatialIndex_Update(w->spatial, (uint32_t)slot,
                              AISpatialIndex_LayerForObject(o), o->position, o->radius);
    }
    return true;
}

bool AIWorld_SetAgentState(AIWorld *w, AIObjectId id, unsigned role, float speed) {
    if (!w || !isfinite(speed) || speed < 0.0f) return false;
    int slot = findSlot(w, id);
    if (slot < 0) return false;
    AIWorldObject *o = &objects(w)[slot];
    o->group = role;
    o->maximumSpeed = speed;
    return true;
}

bool AIWorld_PublishAgent(AIWorld *w, AIObjectId id, unsigned role, float maximumSpeed,
                          Vector3 position, Vector3 velocity, float radius, float height,
                          unsigned species, unsigned category, unsigned foodCategory,
                          bool available, bool reproductiveReady) {
    if (!w || !isfinite(maximumSpeed) || maximumSpeed < 0.0f ||
        !isfinite(position.x) || !isfinite(position.y) || !isfinite(position.z) ||
        !isfinite(radius) || radius < 0.0f || !isfinite(height) || height < 0.0f)
        return false;
    int slot = findSlot(w, id);
    if (slot < 0) return false;
    AIWorldObject *o = &objects(w)[slot];
    o->group = role;
    o->maximumSpeed = maximumSpeed;
    bool moved = Vec3_LengthSq(Vec3_Sub(o->position, position)) > 1e-12f;
    o->moving = moved;
    bool boundsChanged = (o->radius != radius || o->height != height);
    if (moved || boundsChanged || o->available != available) ++o->revision;
    o->position = position;
    o->velocity = velocity;
    o->radius = radius;
    o->height = height;
    o->species = species;
    o->category = category;
    o->foodCategory = foodCategory;
    o->available = available;
    o->reproductiveReady = reproductiveReady;
    if (w->spatial && (moved || boundsChanged)) {
        AISpatialIndex_Update(w->spatial, (uint32_t)slot,
                              AI_SPATIAL_LAYER_CREATURES, position, radius);
    }
    return true;
}

bool AIWorld_PublishResource(AIWorld *w, AIObjectId id, unsigned species,
                             unsigned foodCategory, bool available) {
    int slot = findSlot(w, id);
    if (slot < 0) return false;
    AIWorldObject *o = &objects(w)[slot];
    bool layerMayChange = (o->foodCategory != foodCategory);
    o->species = species;
    o->foodCategory = foodCategory;
    if (o->available != available) ++o->revision;
    o->available = available;
    o->reproductiveReady = false;
    if (o->type == AI_WATER && foodCategory) o->drinkable = available;
    if (w->spatial && layerMayChange) {
        AISpatialIndex_Update(w->spatial, (uint32_t)slot,
                              AISpatialIndex_LayerForObject(o), o->position, o->radius);
    }
    return true;
}

bool AIWorld_PublishMolecularResource(AIWorld *w, AIObjectId id, unsigned species,
                                      unsigned foodCategory, bool available,
                                      uint8_t substancePhase, float hardness,
                                      float availabilityFraction,
                                      const SensorySignature *signature) {
    int slot = findSlot(w, id);
    if (slot < 0) return false;
    AIWorldObject *o = &objects(w)[slot];
    bool layerMayChange = (o->foodCategory != foodCategory);
    o->species = species;
    o->foodCategory = foodCategory;
    if (o->available != available) ++o->revision;
    o->available = available;
    o->reproductiveReady = false;
    o->molecularResource = true;
    o->substancePhase = substancePhase;
    o->hardness = hardness;
    o->availabilityFraction = availabilityFraction;
    if (signature) o->sensorySignature = *signature;
    if (o->type == AI_WATER) o->drinkable = available;
    if (w->spatial && layerMayChange) {
        AISpatialIndex_Update(w->spatial, (uint32_t)slot,
                              AISpatialIndex_LayerForObject(o), o->position, o->radius);
    }
    return true;
}

unsigned AIWorld_QuerySlotsRadius(const AIWorld *w, uint32_t layerMask, Vector3 center, float radius,
                                  uint32_t *outSlots, unsigned capacity, bool *overflowed) {
    if (overflowed) *overflowed = false;
    if (!w || !outSlots || !capacity) return 0u;
    const AIWorldObject *objs = objects(w);
    if (w->spatial) {
        unsigned n = AISpatialIndex_QueryRadius(w->spatial, layerMask, center, radius,
                                                outSlots, capacity, overflowed);
        if (n > 1u) AISpatialIndex_SortSlotsById(objs, outSlots, n);
        return n;
    }
    unsigned n = 0u;
    for (unsigned i = 0; i < w->capacity; ++i) {
        if (!objs[i].active) continue;
        if (!(layerMask & (1u << AISpatialIndex_LayerForObject(&objs[i])))) continue;
        if (n < capacity) {
            outSlots[n++] = i;
        } else {
            if (overflowed) *overflowed = true;
            break;
        }
    }
    return n;
}

unsigned AIWorld_QuerySlotsSegment(const AIWorld *w, uint32_t layerMask, Vector3 a, Vector3 b, float radius,
                                   uint32_t *outSlots, unsigned capacity, bool *overflowed) {
    if (overflowed) *overflowed = false;
    if (!w || !outSlots || !capacity) return 0u;
    const AIWorldObject *objs = objects(w);
    if (w->spatial) {
        return AISpatialIndex_QuerySegment(w->spatial, layerMask, a, b, radius,
                                           outSlots, capacity, overflowed);
    }
    unsigned n = 0u;
    for (unsigned i = 0; i < w->capacity; ++i) {
        if (!objs[i].active) continue;
        if (!(layerMask & (1u << AISpatialIndex_LayerForObject(&objs[i])))) continue;
        if (n < capacity) {
            outSlots[n++] = i;
        } else {
            if (overflowed) *overflowed = true;
            break;
        }
    }
    return n;
}

unsigned AIWorld_Candidates(const AIWorld *w, Vector3 p, float r, AIObjectId *out, unsigned cap) {
    if (!w || !out || !cap) return 0u;
    const AIWorldObject *objs = objects(w);
    if (w->spatial) {
        uint32_t stackSlots[512];
        bool overflow = false;
        unsigned cand = AISpatialIndex_QueryRadius(w->spatial, AI_SPATIAL_MASK_ALL, p, r,
                                                   stackSlots, 512u, &overflow);
        if (!overflow) {
            if (cand > 1u) AISpatialIndex_SortSlotsById(objs, stackSlots, cand);
            unsigned n = 0u;
            for (unsigned k = 0; k < cand && n < cap; ++k) {
                const AIWorldObject *o = &objs[stackSlots[k]];
                float lim = r + o->radius;
                if (o->active && Vec3_LengthSq(Vec3_Sub(p, o->position)) <= lim * lim)
                    out[n++] = o->id;
            }
            return n;
        }
    }
    unsigned n = 0u;
    for (unsigned i = 0; i < w->capacity && n < cap; ++i) {
        const AIWorldObject *o = &objs[i];
        float lim = r + o->radius;
        if (o->active && Vec3_LengthSq(Vec3_Sub(p, o->position)) <= lim * lim)
            out[n++] = o->id;
    }
    return n;
}

static bool testCylinderSegment(const AIWorldObject *o, Vector3 a, Vector3 b,
                                AIObjectId ignore, float radius, bool collision) {
    if (!o->active || (!o->occludes && !(collision && o->type == AI_OBSTACLE)) || o->id == ignore)
        return true;
    float dx = b.x - a.x, dz = b.z - a.z, x = a.x - o->position.x, z = a.z - o->position.z;
    float aa = dx * dx + dz * dz, bb = 2.0f * (x * dx + z * dz);
    float rad = o->radius + radius;
    float cc = x * x + z * z - rad * rad;
    float lo = 0.0f, hi = 1.0f;
    if (aa < 1e-12f) {
        if (cc > 0.0f) return true;
    } else {
        float discriminant = bb * bb - 4.0f * aa * cc;
        if (discriminant < 0.0f) return true;
        float root = sqrtf(discriminant);
        lo = fmaxf(lo, (-bb - root) / (2.0f * aa));
        hi = fminf(hi, (-bb + root) / (2.0f * aa));
    }
    float dy = b.y - a.y;
    if (fabsf(dy) < 1e-8f) {
        if (a.y < o->position.y - radius || a.y > o->position.y + o->height + radius) return true;
    } else {
        float t0 = (o->position.y - radius - a.y) / dy;
        float t1 = (o->position.y + o->height + radius - a.y) / dy;
        lo = fmaxf(lo, fminf(t0, t1));
        hi = fminf(hi, fmaxf(t0, t1));
    }
    return !(lo <= hi);
}

static bool clearSegment(const AIWorld *w, Vector3 a, Vector3 b, AIObjectId ignore,
                         float radius, bool collision) {
    if (!w || !isfinite(a.x) || !isfinite(a.y) || !isfinite(a.z) ||
        !isfinite(b.x) || !isfinite(b.y) || !isfinite(b.z))
        return false;
    const AIWorldObject *objs = objects(w);
    if (w->spatial) {
        if (w->spatial->layers[AI_SPATIAL_LAYER_STATIC].count == 0u) return true;
        uint32_t stackSlots[256];
        bool overflow = false;
        unsigned cand = AISpatialIndex_QuerySegment(w->spatial, AI_SPATIAL_MASK_STATIC,
                                                    a, b, radius, stackSlots, 256u, &overflow);
        if (!overflow) {
            for (unsigned k = 0; k < cand; ++k) {
                if (!testCylinderSegment(&objs[stackSlots[k]], a, b, ignore, radius, collision))
                    return false;
            }
            return true;
        }
    }
    for (unsigned i = 0; i < w->capacity; ++i) {
        if (!testCylinderSegment(&objs[i], a, b, ignore, radius, collision))
            return false;
    }
    return true;
}

bool AIWorld_LineOfSight(const AIWorld *w, Vector3 a, Vector3 b, AIObjectId ignore) {
    return clearSegment(w, a, b, ignore, 0.0f, false);
}

bool AIWorld_ClearSegment(const AIWorld *w, Vector3 a, Vector3 b, float radius, AIObjectId ignore) {
    return isfinite(radius) && radius >= 0.0f && clearSegment(w, a, b, ignore, radius, true);
}

bool AIWorld_Walkable(const AIWorld *w, Vector3 p, float r, bool swim) {
    if (!w || fabsf(p.x) + r > w->halfSize || fabsf(p.z) + r > w->halfSize)
        return false;
    const AIWorldObject *objs = objects(w);
    if (w->spatial) {
        if (w->spatial->layers[AI_SPATIAL_LAYER_STATIC].count == 0u) return true;
        uint32_t stackSlots[256];
        bool overflow = false;
        unsigned cand = AISpatialIndex_QueryRadius(w->spatial, AI_SPATIAL_MASK_STATIC,
                                                   p, r, stackSlots, 256u, &overflow);
        if (!overflow) {
            for (unsigned k = 0; k < cand; ++k) {
                const AIWorldObject *o = &objs[stackSlots[k]];
                if (!o->active || o->walkable || o->type == AI_CREATURE || (swim && o->type == AI_WATER))
                    continue;
                float x = p.x - o->position.x, z = p.z - o->position.z;
                float rad = r + o->radius;
                if (x * x + z * z < rad * rad) return false;
            }
            return true;
        }
    }
    for (unsigned i = 0; i < w->capacity; ++i) {
        const AIWorldObject *o = &objs[i];
        if (!o->active || o->walkable || o->type == AI_CREATURE || (swim && o->type == AI_WATER))
            continue;
        float x = p.x - o->position.x, z = p.z - o->position.z;
        float rad = r + o->radius;
        if (x * x + z * z < rad * rad)
            return false;
    }
    return true;
}

bool AIWorld_SegmentWalkable(const AIWorld *w, Vector3 a, Vector3 b, float r, bool swim) {
    if (!w || fabsf(a.x) + r > w->halfSize || fabsf(a.z) + r > w->halfSize ||
        fabsf(b.x) + r > w->halfSize || fabsf(b.z) + r > w->halfSize)
        return false;
    const AIWorldObject *objs = objects(w);
    if (w->spatial) {
        if (w->spatial->layers[AI_SPATIAL_LAYER_STATIC].count == 0u) return true;
        uint32_t stackSlots[256];
        bool overflow = false;
        unsigned cand = AISpatialIndex_QuerySegment(w->spatial, AI_SPATIAL_MASK_STATIC,
                                                    a, b, r, stackSlots, 256u, &overflow);
        if (!overflow) {
            for (unsigned k = 0; k < cand; ++k) {
                const AIWorldObject *o = &objs[stackSlots[k]];
                float t;
                float rad = r + o->radius;
                if (o->active && !o->walkable && o->type != AI_CREATURE &&
                    !(swim && o->type == AI_WATER) &&
                    segmentDistanceSqXZ(a, b, o->position, &t) < rad * rad)
                    return false;
            }
            return true;
        }
    }
    if (!AIWorld_Walkable(w, a, r, swim) || !AIWorld_Walkable(w, b, r, swim))
        return false;
    for (unsigned i = 0; i < w->capacity; ++i) {
        const AIWorldObject *o = &objs[i];
        float t;
        float rad = r + o->radius;
        if (o->active && !o->walkable && o->type != AI_CREATURE && !(swim && o->type == AI_WATER) &&
            segmentDistanceSqXZ(a, b, o->position, &t) < rad * rad)
            return false;
    }
    return true;
}

bool AIWorld_CanDrink(const AIWorld *w, AIObjectId id, Vector3 mouth, float reach) {
    const AIWorldObject *o = AIWorld_Get(w, id);
    if (!o || o->type != AI_WATER || !o->drinkable)
        return false;
    Vector3 d = Vec3_Sub(mouth, o->position);
    d.y = 0;
    float length = Vec3_Length(d);
    if (length < o->radius - reach)
        return false;
    Vector3 bank = Vec3_Add(o->position, Vec3_Scale(Vec3_Normalize(d), o->radius));
    return Vec3_Distance(mouth, bank) <= reach && AIWorld_LineOfSight(w, mouth, bank, id);
}

bool AIWorld_InteractionTarget(const AIWorld *w, AIObjectId id, Vector3 mouth, float tolerance,
                               InteractionTarget *t) {
    if (!t) return false;
    *t = (InteractionTarget){0};
    const AIWorldObject *o = AIWorld_Get(w, id);
    if (!o || (o->foodCategory && !o->available) ||
        (o->type != AI_WATER && o->type != AI_FOOD) ||
        (o->type == AI_WATER && !o->drinkable))
        return false;
    Vector3 normal = Vec3_LengthSq(o->surfaceNormal) > 0.5f ? Vec3_Normalize(o->surfaceNormal) : Vec3_Create(0, 1, 0);
    Vector3 origin = Vec3_Add(o->position, Vec3_Scale(normal, o->type == AI_FOOD ? o->height : 0));
    Vector3 d = Vec3_Sub(mouth, origin);
    d = Vec3_Sub(d, Vec3_Scale(normal, Vec3_Dot(d, normal)));
    float distance = Vec3_Length(d);
    Vector3 point = Vec3_Add(origin, d);
    if (distance > o->radius && distance > 1e-6f)
        point = Vec3_Add(origin, Vec3_Scale(d, o->radius / distance));
    Vector3 approach = Vec3_Sub(point, mouth);
    if (Vec3_LengthSq(o->approachDirection) > 0.5f) approach = o->approachDirection;
    if (Vec3_LengthSq(approach) < 1e-8f) approach = Vec3_Scale(normal, -1);
    *t = (InteractionTarget){
        .objectId = id, .revision = o->revision,
        .type = o->type == AI_WATER ? INTERACTION_DRINK : INTERACTION_EAT,
        .position = point, .normal = normal, .approach = Vec3_Normalize(approach),
        .velocity = o->velocity, .moving = o->moving, .positionTolerance = tolerance,
        .orientationTolerance = 1.4f, .valid = true,
        .blocked = !AIWorld_ClearSegment(w, mouth, point, 0, id)
    };
    return InteractionTarget_Validate(t);
}
