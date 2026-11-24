/**
 * @file EcosystemAnatomyCache.c
 * @brief Caché compartida e inmutable de plantillas anatómicas con conteo de referencias.
 */
#include "EcosystemAnatomyCache.h"
#include "Creature.h"
#include "CreatureRecipes.h"
#include "EcosystemAnatomy.h"
#include "Monster.h"
#include "VisualVariation.h"
#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

typedef struct CacheSlot {
    EcosystemAnatomyTemplate *tmpl;
    bool invalidated;
} CacheSlot;

static pthread_mutex_t g_cacheMutex = PTHREAD_MUTEX_INITIALIZER;
static CacheSlot g_cacheSlots[ECOSYSTEM_ANATOMY_CACHE_CAPACITY];
static uint64_t g_stamp = 0;
static uint64_t g_hits = 0;
static uint64_t g_misses = 0;
static uint64_t g_evictions = 0;

static void fnvBytes(uint64_t *h, const void *ptr, size_t n) {
    const unsigned char *p = (const unsigned char *)ptr;
    for (size_t i = 0; i < n; ++i) {
        *h ^= (uint64_t)p[i];
        *h *= UINT64_C(1099511628211);
    }
}

static uint64_t hashRecipe(const CreatureRecipe *r) {
    uint64_t h = UINT64_C(14695981039346656037);
    fnvBytes(&h, &r->id, sizeof(r->id));
    fnvBytes(&h, &r->bodyPlan, sizeof(r->bodyPlan));
    fnvBytes(&h, &r->gait, sizeof(r->gait));
    fnvBytes(&h, &r->juvenile.axial, sizeof(r->juvenile.axial));
    fnvBytes(&h, &r->juvenile.headEnvelope, sizeof(r->juvenile.headEnvelope));
    fnvBytes(&h, &r->adult.axial, sizeof(r->adult.axial));
    fnvBytes(&h, &r->adult.headEnvelope, sizeof(r->adult.headEnvelope));
    fnvBytes(&h, &r->adult.limbCount, sizeof(r->adult.limbCount));
    fnvBytes(&h, &r->adult.limbs, sizeof(r->adult.limbs));
    return h;
}

static uint64_t hashRigTopology(const Rig *rig) {
    uint64_t h = UINT64_C(14695981039346656037);
    uint32_t jc = (uint32_t)rig->skeleton.jointCount;
    fnvBytes(&h, &jc, sizeof(jc));
    fnvBytes(&h, &rig->headJoint, sizeof(rig->headJoint));
    fnvBytes(&h, &rig->neckJoint, sizeof(rig->neckJoint));
    fnvBytes(&h, &rig->cervicalBaseJoint, sizeof(rig->cervicalBaseJoint));
    for (size_t i = 0; i < rig->skeleton.jointCount; ++i) {
        fnvBytes(&h, &rig->skeleton.joints[i].id, sizeof(rig->skeleton.joints[i].id));
        fnvBytes(&h, &rig->skeleton.joints[i].parentIndex, sizeof(rig->skeleton.joints[i].parentIndex));
    }
    return h;
}

static bool keyEqual(EcosystemAnatomyTemplateKey a, EcosystemAnatomyTemplateKey b) {
    return a.recipeId == b.recipeId && a.recipeHash == b.recipeHash &&
           a.morphologyBucket == b.morphologyBucket && a.growthStep == b.growthStep;
}

uint32_t EcosystemAnatomyCache_GrowthStep(double normalizedAge) {
    if (!isfinite(normalizedAge) || normalizedAge <= 0.0) return 0u;
    if (normalizedAge >= 1.0) return ECOSYSTEM_ANATOMY_GROWTH_STEPS;
    double scaled = floor(normalizedAge * (double)ECOSYSTEM_ANATOMY_GROWTH_STEPS + 1e-9);
    if (scaled < 0.0) scaled = 0.0;
    if (scaled >= (double)ECOSYSTEM_ANATOMY_GROWTH_STEPS) return ECOSYSTEM_ANATOMY_GROWTH_STEPS;
    return (uint32_t)scaled;
}

static EcosystemAnatomyTemplate *compileTemplate(const CreatureRecipe *base,
                                                 EcosystemAnatomyTemplateKey key,
                                                 EvolutionProfiler *profiler) {
    float dev = (float)key.growthStep / (float)ECOSYSTEM_ANATOMY_GROWTH_STEPS;
    if (dev > 1.0f) dev = 1.0f;
    CreatureRecipe recipe;
    CreaturePhenotype p;
    CreaturePhenotype development = CreatureRecipe_EvaluateDevelopment(base, dev);
    if (!VisualVariation_ApplyBucket(base, &development, key.morphologyBucket, 0u, &recipe, &p)) {
        return NULL;
    }

    Monster m = Monster_Create();
    if (profiler) EvolutionProfiler_AddAtomic(&profiler->counters.monsterConstructions, 1u);
    if (!Creature_BuildMonster(&m, &recipe, &p) || !m.animation) {
        Monster_Free(&m);
        return NULL;
    }

    EcosystemAnatomyTemplate *tmpl = calloc(1, sizeof(*tmpl));
    if (!tmpl) {
        Monster_Free(&m);
        return NULL;
    }
    if (profiler) EvolutionProfiler_AddAtomic(&profiler->counters.allocations, 1u);

    tmpl->key = key;
    tmpl->normalizedDevelopment = dev;
    tmpl->rig = m.animation->rig;
    tmpl->standingHeight = m.animation->standingHeight - m.animation->animator.gait.supportCompression;
    tmpl->modelLength = EcosystemAnatomy_ModelLength(&p);
    tmpl->center = m.animation->animator.locomotion.supportCenter;
    tmpl->phenotype = p;
    tmpl->graph = m.anatomyGraph;
    SkeletonPose_Init(&tmpl->rest, &tmpl->rig.skeleton);

    bool feline = (recipe.gait == CREATURE_GAIT_FELINE_STALK);
    ReachPreset preset = AnatomicalReach_DefaultPreset();
    preset.torsoJoints = feline ? 1u : 0u;
    preset.crouchFraction = feline ? 0.38f : 0.25f;
    bool ok = AnatomicalReach_Compile(&tmpl->profile, &tmpl->rig, preset);
    Monster_Free(&m);
    if (!ok) {
        free(tmpl);
        return NULL;
    }

    tmpl->topologyHash = hashRigTopology(&tmpl->rig);

    /* Pose de pie con standingHeight (crouch=0) y landmarks neutrales precomputados. */
    tmpl->standingRest = tmpl->rest;
    for (size_t j = 0; j < tmpl->rig.skeleton.jointCount; ++j) {
        if (tmpl->rig.skeleton.joints[j].parentIndex < 0) {
            tmpl->standingRest.joints[j].localPosition.y += tmpl->standingHeight;
        }
    }
    SkeletonPose_UpdateWorldTransforms(&tmpl->standingRest, &tmpl->rig.skeleton);

    int baseParent = tmpl->profile.baseParentIndex;
    if (baseParent >= 0 && (size_t)baseParent < tmpl->standingRest.jointCount) {
        tmpl->cervicalParentStandingPos = tmpl->standingRest.joints[baseParent].worldPosition;
        tmpl->cervicalParentStandingRot = tmpl->standingRest.joints[baseParent].worldRotation;
    } else {
        tmpl->cervicalParentStandingPos = Vec3_Create(0.0f, tmpl->standingHeight, 0.0f);
        tmpl->cervicalParentStandingRot = Quat_Identity();
    }

    const JointPose *head = &tmpl->standingRest.joints[tmpl->rig.headJoint];
    tmpl->neutralMouthLocal = Vec3_Add(head->worldPosition,
                                       Quat_RotateVector(head->worldRotation, tmpl->rig.oralPoint));
    tmpl->neutralMouthDir = Quat_RotateVector(head->worldRotation, tmpl->rig.oralDirection);
    int pivotJ = tmpl->rig.cervicalBaseJoint >= 0 ? tmpl->rig.cervicalBaseJoint : tmpl->rig.neckJoint;
    int elbowJ = tmpl->rig.neckJoint;
    tmpl->neutralPivotLocal = (pivotJ >= 0 && (size_t)pivotJ < tmpl->standingRest.jointCount)
                                  ? tmpl->standingRest.joints[pivotJ].worldPosition
                                  : head->worldPosition;
    tmpl->neutralElbowLocal = (elbowJ >= 0 && (size_t)elbowJ < tmpl->standingRest.jointCount)
                                  ? tmpl->standingRest.joints[elbowJ].worldPosition
                                  : head->worldPosition;
    tmpl->restOralPointWorld = Vec3_Add(tmpl->rest.joints[tmpl->rig.headJoint].worldPosition,
                                        tmpl->rig.oralPoint);

    tmpl->pivotChainIndex = -1;
    tmpl->elbowChainIndex = -1;
    for (unsigned k = 0; k < tmpl->profile.count; ++k) {
        if (tmpl->profile.joints[k].index == pivotJ) tmpl->pivotChainIndex = (int)k;
        if (tmpl->profile.joints[k].index == elbowJ) tmpl->elbowChainIndex = (int)k;
    }
    tmpl->valid = true;
    tmpl->refCount = 1u;
    return tmpl;
}

const EcosystemAnatomyTemplate *EcosystemAnatomyCache_Acquire(uint32_t recipeId,
                                                              uint32_t morphologyBucket,
                                                              uint32_t growthStep,
                                                              EvolutionProfiler *profiler) {
    const CreatureRecipe *base = CreatureRecipes_Find(recipeId);
    if (!base) return NULL;
    if (growthStep > ECOSYSTEM_ANATOMY_GROWTH_STEPS) growthStep = ECOSYSTEM_ANATOMY_GROWTH_STEPS;
    EcosystemAnatomyTemplateKey key = {
        .recipeId = recipeId,
        .recipeHash = hashRecipe(base),
        .morphologyBucket = morphologyBucket,
        .growthStep = growthStep
    };

    pthread_mutex_lock(&g_cacheMutex);
    for (unsigned i = 0; i < ECOSYSTEM_ANATOMY_CACHE_CAPACITY; ++i) {
        if (g_cacheSlots[i].tmpl && !g_cacheSlots[i].invalidated &&
            keyEqual(g_cacheSlots[i].tmpl->key, key)) {
            EcosystemAnatomyTemplate *hit = g_cacheSlots[i].tmpl;
            __atomic_fetch_add(&hit->refCount, 1u, __ATOMIC_RELAXED);
            hit->lastUsedStamp = ++g_stamp;
            ++g_hits;
            pthread_mutex_unlock(&g_cacheMutex);
            if (profiler) EvolutionProfiler_AddAtomic(&profiler->counters.anatomicalTemplateHits, 1u);
            return hit;
        }
    }
    ++g_misses;
    pthread_mutex_unlock(&g_cacheMutex);
    if (profiler) EvolutionProfiler_AddAtomic(&profiler->counters.anatomicalTemplateMisses, 1u);

    /* Compilar fuera del mutex para no bloquear otros hilos mientras se construye la plantilla. */
    EcosystemAnatomyTemplate *built = compileTemplate(base, key, profiler);
    if (!built) return NULL;

    pthread_mutex_lock(&g_cacheMutex);
    /* Re-comprobar si otro hilo ya insertó la misma clave mientras compilábamos. */
    for (unsigned i = 0; i < ECOSYSTEM_ANATOMY_CACHE_CAPACITY; ++i) {
        if (g_cacheSlots[i].tmpl && !g_cacheSlots[i].invalidated &&
            keyEqual(g_cacheSlots[i].tmpl->key, key)) {
            EcosystemAnatomyTemplate *existing = g_cacheSlots[i].tmpl;
            __atomic_fetch_add(&existing->refCount, 1u, __ATOMIC_RELAXED);
            existing->lastUsedStamp = ++g_stamp;
            pthread_mutex_unlock(&g_cacheMutex);
            free(built);
            return existing;
        }
    }

    int freeSlot = -1;
    int lruSlot = -1;
    uint64_t lruStamp = UINT64_MAX;
    for (unsigned i = 0; i < ECOSYSTEM_ANATOMY_CACHE_CAPACITY; ++i) {
        if (!g_cacheSlots[i].tmpl) {
            freeSlot = (int)i;
            break;
        }
        uint32_t rc = __atomic_load_n(&g_cacheSlots[i].tmpl->refCount, __ATOMIC_ACQUIRE);
        if (rc == 0u && g_cacheSlots[i].tmpl->lastUsedStamp < lruStamp) {
            lruStamp = g_cacheSlots[i].tmpl->lastUsedStamp;
            lruSlot = (int)i;
        }
    }
    int targetSlot = (freeSlot >= 0) ? freeSlot : lruSlot;
    if (targetSlot >= 0) {
        if (g_cacheSlots[targetSlot].tmpl) {
            free(g_cacheSlots[targetSlot].tmpl);
            ++g_evictions;
        }
        built->lastUsedStamp = ++g_stamp;
        /* La caché mantiene propiedad mientras reside; el llamador posee refCount=1. */
        g_cacheSlots[targetSlot].tmpl = built;
        g_cacheSlots[targetSlot].invalidated = false;
    } else {
        /* Caché llena con todas las plantillas activas: la entidad adquiere plantilla efímera. */
        built->valid = false; /* Marca de no residente en caché: se libera al llegar refCount a 0. */
    }
    pthread_mutex_unlock(&g_cacheMutex);
    return built;
}

const EcosystemAnatomyTemplate *EcosystemAnatomyCache_Retain(const EcosystemAnatomyTemplate *tmpl) {
    if (!tmpl) return NULL;
    EcosystemAnatomyTemplate *mut = (EcosystemAnatomyTemplate *)tmpl;
    __atomic_fetch_add(&mut->refCount, 1u, __ATOMIC_RELAXED);
    return tmpl;
}

void EcosystemAnatomyCache_Release(const EcosystemAnatomyTemplate *tmpl) {
    if (!tmpl) return;
    EcosystemAnatomyTemplate *mut = (EcosystemAnatomyTemplate *)tmpl;
    uint32_t prev = __atomic_fetch_sub(&mut->refCount, 1u, __ATOMIC_ACQ_REL);
    if (prev == 1u) {
        /* Si la plantilla fue marcada como invalidada o no residente, liberarla de forma segura. */
        bool shouldFree = false;
        pthread_mutex_lock(&g_cacheMutex);
        if (!mut->valid) {
            shouldFree = true;
        } else {
            for (unsigned i = 0; i < ECOSYSTEM_ANATOMY_CACHE_CAPACITY; ++i) {
                if (g_cacheSlots[i].tmpl == mut && g_cacheSlots[i].invalidated &&
                    __atomic_load_n(&mut->refCount, __ATOMIC_ACQUIRE) == 0u) {
                    g_cacheSlots[i].tmpl = NULL;
                    g_cacheSlots[i].invalidated = false;
                    shouldFree = true;
                    break;
                }
            }
        }
        pthread_mutex_unlock(&g_cacheMutex);
        if (shouldFree) free(mut);
    }
}

void EcosystemAnatomyCache_InvalidateRecipe(uint32_t recipeId) {
    pthread_mutex_lock(&g_cacheMutex);
    for (unsigned i = 0; i < ECOSYSTEM_ANATOMY_CACHE_CAPACITY; ++i) {
        EcosystemAnatomyTemplate *t = g_cacheSlots[i].tmpl;
        if (!t || t->key.recipeId != recipeId) continue;
        uint32_t rc = __atomic_load_n(&t->refCount, __ATOMIC_ACQUIRE);
        if (rc == 0u) {
            free(t);
            g_cacheSlots[i].tmpl = NULL;
            g_cacheSlots[i].invalidated = false;
        } else {
            g_cacheSlots[i].invalidated = true;
        }
    }
    pthread_mutex_unlock(&g_cacheMutex);
}

void EcosystemAnatomyCache_ClearUnused(void) {
    pthread_mutex_lock(&g_cacheMutex);
    for (unsigned i = 0; i < ECOSYSTEM_ANATOMY_CACHE_CAPACITY; ++i) {
        EcosystemAnatomyTemplate *t = g_cacheSlots[i].tmpl;
        if (!t) continue;
        if (__atomic_load_n(&t->refCount, __ATOMIC_ACQUIRE) == 0u) {
            free(t);
            g_cacheSlots[i].tmpl = NULL;
            g_cacheSlots[i].invalidated = false;
        }
    }
    pthread_mutex_unlock(&g_cacheMutex);
}

EcosystemAnatomyCacheStats EcosystemAnatomyCache_GetStats(void) {
    EcosystemAnatomyCacheStats st = {0};
    pthread_mutex_lock(&g_cacheMutex);
    st.hits = g_hits;
    st.misses = g_misses;
    st.evictions = g_evictions;
    for (unsigned i = 0; i < ECOSYSTEM_ANATOMY_CACHE_CAPACITY; ++i) {
        if (!g_cacheSlots[i].tmpl) continue;
        ++st.activeEntries;
        if (__atomic_load_n(&g_cacheSlots[i].tmpl->refCount, __ATOMIC_ACQUIRE) > 0u) {
            ++st.referencedEntries;
        }
    }
    pthread_mutex_unlock(&g_cacheMutex);
    return st;
}
