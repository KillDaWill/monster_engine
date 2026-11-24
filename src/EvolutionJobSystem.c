/**
 * @file EvolutionJobSystem.c
 * @brief Implementación del despachador paralelo determinista sobre SDFSamplingPool.
 */
#include "EvolutionJobSystem.h"
#include "SDFSamplingPool.h"
#include <stdlib.h>
#include <unistd.h>

struct EvolutionJobSystem {
    SDFSamplingPool *pool;
    unsigned workerCount;
};

typedef struct JobDispatchContext {
    EvolutionJobRangeFn fn;
    void *userContext;
} JobDispatchContext;

static void dispatchAdapter(void *ctx, int start, int end, int threadIdx) {
    JobDispatchContext *d = (JobDispatchContext *)ctx;
    if (d && d->fn && start < end) {
        d->fn(d->userContext, (unsigned)start, (unsigned)end, (unsigned)threadIdx);
    }
}

unsigned EvolutionJobSystem_RecommendedWorkers(void) {
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    if (n <= 1) return 1u;
    if (n <= 2) return 2u;
    if (n <= 4) return 3u;
    if (n <= 8) return 4u;
    /* En CPU de 12+ hilos (p. ej. Ryzen 5/7 portátil), 6 hilos para simulación dejan
     * suficiente margen para presentación, OpenGL y trabajador asíncrono de mallas. */
    return 6u;
}

EvolutionJobSystem *EvolutionJobSystem_Create(unsigned workerCount) {
    if (workerCount == 0u) workerCount = EvolutionJobSystem_RecommendedWorkers();
    if (workerCount > (unsigned)SDF_SAMPLING_POOL_MAX_THREADS) {
        workerCount = (unsigned)SDF_SAMPLING_POOL_MAX_THREADS;
    }
    EvolutionJobSystem *sys = calloc(1, sizeof(*sys));
    if (!sys) return NULL;
    sys->workerCount = 1u;
    if (workerCount > 1u) {
        sys->pool = SDFSamplingPool_Create((int)workerCount);
        if (sys->pool) {
            sys->workerCount = (unsigned)SDFSamplingPool_GetThreadCount(sys->pool);
        }
    }
    return sys;
}

void EvolutionJobSystem_Destroy(EvolutionJobSystem *sys) {
    if (!sys) return;
    if (sys->pool) {
        SDFSamplingPool_Free(sys->pool);
        sys->pool = NULL;
    }
    free(sys);
}

unsigned EvolutionJobSystem_WorkerCount(const EvolutionJobSystem *sys) {
    return sys ? sys->workerCount : 1u;
}

void EvolutionJobSystem_ParallelFor(EvolutionJobSystem *sys, unsigned itemCount,
                                    unsigned minParallelItems,
                                    EvolutionJobRangeFn fn, void *context) {
    if (!fn || itemCount == 0u) return;
    if (!sys || !sys->pool || sys->workerCount <= 1u || itemCount < minParallelItems) {
        fn(context, 0u, itemCount, 0u);
        return;
    }
    JobDispatchContext d = {.fn = fn, .userContext = context};
    SDFSamplingPool_ParallelFor(sys->pool, (int)itemCount, dispatchAdapter, &d);
}
