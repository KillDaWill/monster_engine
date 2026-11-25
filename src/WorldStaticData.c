/**
 * @file WorldStaticData.c
 * @brief Propiedad refcounted atómica y caché global acotada single-flight para WorldStaticData.
 */
#include "WorldStaticData.h"
#include "WorldGenerator.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

static _Atomic uint64_t g_worldEpochCounter = 1u;

WorldStaticData *WorldStaticData_Alloc(void) {
    WorldStaticData *w = calloc(1, sizeof(*w));
    if (!w) return NULL;
    atomic_init(&w->refCount, 1u);
    w->epochId = atomic_fetch_add(&g_worldEpochCounter, 1u);
    return w;
}

const WorldStaticData *WorldStaticData_Retain(const WorldStaticData *world) {
    if (!world) return NULL;
    WorldStaticData *mut = (WorldStaticData *)world;
    atomic_fetch_add_explicit(&mut->refCount, 1u, memory_order_relaxed);
    return world;
}

void WorldStaticData_Release(const WorldStaticData *world) {
    if (!world) return;
    WorldStaticData *mut = (WorldStaticData *)world;
    uint32_t prev = atomic_fetch_sub_explicit(&mut->refCount, 1u, memory_order_acq_rel);
    if (prev == 1u) {
        TerrainHeightfield_Free(&mut->terrain);
        WorldHydrology_Free(&mut->hydrology);
        WorldBiome_Free(&mut->biome);
        WorldGeology_Free(&mut->geology);
        free(mut->navBase.cellHeight);
        free(mut->navBase.cellMaxSlope);
        free(mut->navBase.cellMaxWaterDepth);
        free(mut->navBase.cellRoughness);
        free(mut->navBase.cellRockClearance);
        free(mut);
    }
}

uint32_t WorldStaticData_RefCount(const WorldStaticData *world) {
    if (!world) return 0u;
    const WorldStaticData *w = world;
    return atomic_load_explicit(&((WorldStaticData *)w)->refCount, memory_order_acquire);
}

/* ========================================================================== */
/* Caché compartida thread-safe con generación single-flight (Lab + Arena)    */
/* ========================================================================== */

#define WORLD_STATIC_CACHE_CAP 16u

typedef struct WorldCacheSlot {
    bool used;
    bool building;
    uint64_t recipeHash;
    uint32_t seed;
    uint32_t genVersion;
    uint64_t lastUse;
    const WorldStaticData *world;
} WorldCacheSlot;

static pthread_mutex_t g_cacheMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cacheCond = PTHREAD_COND_INITIALIZER;
static WorldCacheSlot g_cache[WORLD_STATIC_CACHE_CAP];
static uint64_t g_cacheClock = 1u;
static uint64_t g_cacheHits = 0u;
static uint64_t g_cacheMisses = 0u;

const WorldStaticData *WorldStaticCache_Acquire(const WorldRecipe *recipe,
                                                char *error, size_t errorSize) {
    if (!recipe) return NULL;
    WorldRecipe validated = *recipe;
    if (!WorldRecipe_Validate(&validated, error, errorSize)) return NULL;
    validated.recipeHash = WorldRecipe_Fingerprint(&validated);

    uint64_t rHash = validated.recipeHash;
    uint32_t seed = validated.seed;
    uint32_t genV = validated.generatorVersion;

    pthread_mutex_lock(&g_cacheMutex);
    for (;;) {
        int found = -1;
        for (unsigned i = 0; i < WORLD_STATIC_CACHE_CAP; ++i) {
            if (g_cache[i].used && g_cache[i].recipeHash == rHash &&
                g_cache[i].seed == seed && g_cache[i].genVersion == genV) {
                found = (int)i;
                break;
            }
        }
        if (found >= 0) {
            if (g_cache[found].building) {
                /* Single-flight: otro trabajador está generando esta misma clave. */
                pthread_cond_wait(&g_cacheCond, &g_cacheMutex);
                continue;
            }
            const WorldStaticData *res = WorldStaticData_Retain(g_cache[found].world);
            g_cache[found].lastUse = ++g_cacheClock;
            ++g_cacheHits;
            pthread_mutex_unlock(&g_cacheMutex);
            return res;
        }

        /* Elegir ranura libre o LRU no en construcción. */
        int slot = -1;
        uint64_t oldest = UINT64_MAX;
        for (unsigned i = 0; i < WORLD_STATIC_CACHE_CAP; ++i) {
            if (!g_cache[i].used) { slot = (int)i; break; }
            if (!g_cache[i].building && g_cache[i].lastUse < oldest) {
                oldest = g_cache[i].lastUse;
                slot = (int)i;
            }
        }
        if (slot < 0) {
            pthread_cond_wait(&g_cacheCond, &g_cacheMutex);
            continue;
        }

        const WorldStaticData *evicted = g_cache[slot].world;
        g_cache[slot].used = true;
        g_cache[slot].building = true;
        g_cache[slot].recipeHash = rHash;
        g_cache[slot].seed = seed;
        g_cache[slot].genVersion = genV;
        g_cache[slot].lastUse = ++g_cacheClock;
        g_cache[slot].world = NULL;
        ++g_cacheMisses;
        pthread_mutex_unlock(&g_cacheMutex);

        if (evicted) WorldStaticData_Release(evicted);

        /* Generar fuera del mutex para no bloquear aciertos de otras claves. */
        WorldStaticData *built = NULL;
        bool ok = WorldGenerator_Generate(&validated, &built, error, errorSize);

        pthread_mutex_lock(&g_cacheMutex);
        if (ok && built) {
            g_cache[slot].world = built; /* La caché retiene 1 referencia. */
            g_cache[slot].building = false;
            g_cache[slot].lastUse = ++g_cacheClock;
            const WorldStaticData *ret = WorldStaticData_Retain(built);
            pthread_cond_broadcast(&g_cacheCond);
            pthread_mutex_unlock(&g_cacheMutex);
            return ret;
        } else {
            g_cache[slot].used = false;
            g_cache[slot].building = false;
            g_cache[slot].world = NULL;
            pthread_cond_broadcast(&g_cacheCond);
            pthread_mutex_unlock(&g_cacheMutex);
            return NULL;
        }
    }
}

void WorldStaticCache_Clear(void) {
    const WorldStaticData *toRelease[WORLD_STATIC_CACHE_CAP];
    unsigned count = 0u;
    pthread_mutex_lock(&g_cacheMutex);
    for (unsigned i = 0; i < WORLD_STATIC_CACHE_CAP; ++i) {
        if (g_cache[i].used && !g_cache[i].building) {
            if (g_cache[i].world) toRelease[count++] = g_cache[i].world;
            memset(&g_cache[i], 0, sizeof(g_cache[i]));
        }
    }
    pthread_cond_broadcast(&g_cacheCond);
    pthread_mutex_unlock(&g_cacheMutex);
    for (unsigned i = 0; i < count; ++i) {
        WorldStaticData_Release(toRelease[i]);
    }
}

void WorldStaticCache_Stats(uint64_t *outHits, uint64_t *outMisses, uint32_t *outEntries) {
    pthread_mutex_lock(&g_cacheMutex);
    if (outHits) *outHits = g_cacheHits;
    if (outMisses) *outMisses = g_cacheMisses;
    if (outEntries) {
        uint32_t n = 0u;
        for (unsigned i = 0; i < WORLD_STATIC_CACHE_CAP; ++i) {
            if (g_cache[i].used && !g_cache[i].building && g_cache[i].world) ++n;
        }
        *outEntries = n;
    }
    pthread_mutex_unlock(&g_cacheMutex);
}
