/**
 * @file LongHairScheduler.c
 * @brief Implementación del scheduler determinista sobre un SDFSamplingPool privado.
 */

#include "LongHairScheduler.h"
#include "SDFSamplingPool.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * Marca el scheduler cuyo callback está ejecutándose en este hilo. Sólo el más
 * interno queda marcado: A->B->A se detecta como BUSY en A (A ya tiene un job),
 * no como NESTED. Se guarda/restaura para permitir schedulers distintos.
 */
static _Thread_local LongHairScheduler* g_activeScheduler = NULL;
/* Profundidad de callbacks de cualquier scheduler en este hilo. Destroy se
 * rechaza desde cualquier callback, incluso para otro scheduler, porque la
 * destrucción puede esperar por un trabajo que depende del callback actual. */
static _Thread_local int g_callbackDepth = 0;

typedef struct LongHairSchedulerJob {
    LongHairScheduler* scheduler;
    LongHairSchedulerWorkFn workFn;
    void* context;
    int totalItems;
    int partitionCount;
    int starts[LONG_HAIR_SCHEDULER_MAX_THREADS];
    int ends[LONG_HAIR_SCHEDULER_MAX_THREADS];
    atomic_bool stop;
} LongHairSchedulerJob;

struct LongHairScheduler {
    pthread_mutex_t lock;
    pthread_cond_t idleCond;
    SDFSamplingPool* pool;   /**< Pool privado; NULL en modo serial. */
    int threadCount;         /**< Participantes efectivos, incluido el llamador. */
    atomic_bool running;
    atomic_bool shuttingDown;
    int activeCalls;         /**< 0 o 1; protege el pool no reentrante. */
    LongHairSchedulerStats stats;
};

static int DetectAutoThreadCount(void) {
    long processors = sysconf(_SC_NPROCESSORS_ONLN);
    if (processors <= 1) return 1;
    if (processors <= 2) return 2;
    return 4;
}

LongHairSchedulerConfig LongHairScheduler_DefaultConfig(void) {
    LongHairSchedulerConfig config;
    config.threadCount = 0;
    config.maxThreads = LONG_HAIR_SCHEDULER_DEFAULT_MAX_THREADS;
    config.requireParallel = false;
    return config;
}

static int ResolveThreadCount(const LongHairSchedulerConfig* config) {
    int maxThreads = config->maxThreads;
    if (maxThreads <= 0) maxThreads = LONG_HAIR_SCHEDULER_DEFAULT_MAX_THREADS;
    if (maxThreads > LONG_HAIR_SCHEDULER_MAX_THREADS) maxThreads = LONG_HAIR_SCHEDULER_MAX_THREADS;

    int requested = config->threadCount;
    if (requested <= 0) requested = DetectAutoThreadCount();
    if (requested > maxThreads) requested = maxThreads;
    if (requested < 1) requested = 1;
    return requested;
}

LongHairScheduler* LongHairScheduler_Create(const LongHairSchedulerConfig* config) {
    LongHairSchedulerConfig resolved =
        config ? *config : LongHairScheduler_DefaultConfig();
    int requested = ResolveThreadCount(&resolved);

    LongHairScheduler* scheduler = (LongHairScheduler*)calloc(1, sizeof(*scheduler));
    if (!scheduler) return NULL;

    if (pthread_mutex_init(&scheduler->lock, NULL) != 0) {
        free(scheduler);
        return NULL;
    }
    if (pthread_cond_init(&scheduler->idleCond, NULL) != 0) {
        pthread_mutex_destroy(&scheduler->lock);
        free(scheduler);
        return NULL;
    }

    scheduler->threadCount = 1;
    atomic_init(&scheduler->running, true);
    atomic_init(&scheduler->shuttingDown, false);

    if (requested > 1) {
        SDFSamplingPool* pool = SDFSamplingPool_Create(requested);
        if (pool) {
            int actual = SDFSamplingPool_GetThreadCount(pool);
            if (actual > 1) {
                scheduler->pool = pool;
                scheduler->threadCount = actual;
            } else {
                /* El pool no pudo crear ningún worker; no aporta paralelismo. */
                SDFSamplingPool_Free(pool);
            }
        }
        if (!scheduler->pool && resolved.requireParallel) {
            pthread_cond_destroy(&scheduler->idleCond);
            pthread_mutex_destroy(&scheduler->lock);
            free(scheduler);
            return NULL;
        }
    }

    scheduler->stats.threadCount = scheduler->threadCount;
    return scheduler;
}

void LongHairScheduler_Destroy(LongHairScheduler* scheduler) {
    if (!scheduler) return;

    /* Destroy no se permite desde ningún callback de scheduler. */
    if (g_callbackDepth > 0) return;

    pthread_mutex_lock(&scheduler->lock);
    atomic_store(&scheduler->shuttingDown, true);
    while (scheduler->activeCalls != 0) {
        pthread_cond_wait(&scheduler->idleCond, &scheduler->lock);
    }
    atomic_store(&scheduler->running, false);
    pthread_mutex_unlock(&scheduler->lock);

    if (scheduler->pool) {
        SDFSamplingPool_Free(scheduler->pool);
        scheduler->pool = NULL;
    }
    pthread_cond_destroy(&scheduler->idleCond);
    pthread_mutex_destroy(&scheduler->lock);
    free(scheduler);
}

bool LongHairScheduler_IsRunning(const LongHairScheduler* scheduler) {
    if (!scheduler) return false;
    return atomic_load(&scheduler->running) && !atomic_load(&scheduler->shuttingDown);
}

int LongHairScheduler_GetThreadCount(const LongHairScheduler* scheduler) {
    return scheduler ? scheduler->threadCount : 1;
}

const char* LongHairScheduler_StatusName(LongHairSchedulerStatus status) {
    switch (status) {
        case LONG_HAIR_SCHEDULER_OK: return "ok";
        case LONG_HAIR_SCHEDULER_INVALID_ARGUMENT: return "invalid_argument";
        case LONG_HAIR_SCHEDULER_BUSY: return "busy";
        case LONG_HAIR_SCHEDULER_NESTED: return "nested";
        case LONG_HAIR_SCHEDULER_SHUTDOWN: return "shutdown";
        case LONG_HAIR_SCHEDULER_CALLBACK_FAILED: return "callback_failed";
        default: return "unknown";
    }
}

static void PrepareJob(LongHairSchedulerJob* job, LongHairScheduler* scheduler,
    const LongHairSchedulerPhase* phase, int partitionCount) {
    memset(job, 0, sizeof(*job));
    job->scheduler = scheduler;
    job->workFn = phase->workFn;
    job->context = phase->context;
    job->totalItems = phase->totalItems;
    job->partitionCount = partitionCount;
    atomic_init(&job->stop, false);

    for (int partition = 0; partition < partitionCount; ++partition) {
        /* int64 evita desbordar p*totalItems con totales grandes. */
        job->starts[partition] = (int)(((int64_t)partition * (int64_t)phase->totalItems) /
            (int64_t)partitionCount);
        job->ends[partition] = (int)(((int64_t)(partition + 1) * (int64_t)phase->totalItems) /
            (int64_t)partitionCount);
    }
}

static void RunPartitionRange(LongHairSchedulerJob* job, int firstPartition, int lastPartition) {
    for (int partition = firstPartition; partition < lastPartition; ++partition) {
        if (atomic_load_explicit(&job->stop, memory_order_acquire)) return;

        int start = job->starts[partition];
        int end = job->ends[partition];
        if (start >= end) continue;

        LongHairScheduler* scheduler = job->scheduler;
        LongHairScheduler* previous = g_activeScheduler;
        g_activeScheduler = scheduler;
        g_callbackDepth++;
        bool keepGoing = job->workFn(job->context, start, end, partition);
        g_callbackDepth--;
        g_activeScheduler = previous;

        if (!keepGoing) {
            atomic_store_explicit(&job->stop, true, memory_order_release);
            return;
        }
    }
}

static void PartitionDispatch(void* context, int firstPartition, int lastPartition,
    int poolThreadIndex) {
    (void)poolThreadIndex;
    RunPartitionRange((LongHairSchedulerJob*)context, firstPartition, lastPartition);
}

static LongHairSchedulerStatus RunAdmittedJob(LongHairScheduler* scheduler,
    const LongHairSchedulerPhase* phases, int phaseCount, bool* usedParallel) {
    LongHairSchedulerStatus status = LONG_HAIR_SCHEDULER_OK;
    int executedPhases = 0;
    uint64_t executedItems = 0;

    for (int phaseIndex = 0; phaseIndex < phaseCount; ++phaseIndex) {
        const LongHairSchedulerPhase* phase = &phases[phaseIndex];

        int partitionCount = 1;
        if (phase->totalItems > 0) {
            partitionCount = scheduler->threadCount;
            if (partitionCount > phase->totalItems) partitionCount = phase->totalItems;
        }

        LongHairSchedulerJob job;
        PrepareJob(&job, scheduler, phase, partitionCount);

        if (partitionCount > 1 && scheduler->pool) {
            *usedParallel = true;
            /*
             * El pool reparte las particiones entre sus participantes y retorna
             * sólo después de que todos hayan terminado. Esta es la barrera.
             */
            SDFSamplingPool_ParallelFor(scheduler->pool, partitionCount,
                PartitionDispatch, &job);
        } else {
            /* Fallback serial válido: una sola partición o ejecución secuencial. */
            RunPartitionRange(&job, 0, partitionCount);
        }

        executedPhases++;
        if (atomic_load_explicit(&job.stop, memory_order_acquire)) {
            status = LONG_HAIR_SCHEDULER_CALLBACK_FAILED;
            break;
        }
        executedItems += (uint64_t)phase->totalItems;
    }

    pthread_mutex_lock(&scheduler->lock);
    scheduler->stats.phasesExecuted += (uint64_t)executedPhases;
    scheduler->stats.itemsExecuted += executedItems;
    scheduler->stats.lastPhaseCount = executedPhases;
    if (status == LONG_HAIR_SCHEDULER_OK) {
        scheduler->stats.jobsCompleted++;
    } else {
        scheduler->stats.jobsFailed++;
    }
    pthread_mutex_unlock(&scheduler->lock);

    return status;
}

LongHairSchedulerStatus LongHairScheduler_RunPhases(LongHairScheduler* scheduler,
    const LongHairSchedulerPhase* phases, int phaseCount) {
    if (!scheduler || !phases || phaseCount < 1) {
        return LONG_HAIR_SCHEDULER_INVALID_ARGUMENT;
    }
    for (int i = 0; i < phaseCount; ++i) {
        if (phases[i].totalItems < 0) return LONG_HAIR_SCHEDULER_INVALID_ARGUMENT;
        if (phases[i].totalItems > 0 && !phases[i].workFn) {
            return LONG_HAIR_SCHEDULER_INVALID_ARGUMENT;
        }
    }

    if (g_activeScheduler == scheduler) {
        pthread_mutex_lock(&scheduler->lock);
        scheduler->stats.nestedRejections++;
        pthread_mutex_unlock(&scheduler->lock);
        return LONG_HAIR_SCHEDULER_NESTED;
    }

    pthread_mutex_lock(&scheduler->lock);
    if (!atomic_load(&scheduler->running) || atomic_load(&scheduler->shuttingDown)) {
        scheduler->stats.shutdownRejections++;
        pthread_mutex_unlock(&scheduler->lock);
        return LONG_HAIR_SCHEDULER_SHUTDOWN;
    }
    if (scheduler->activeCalls != 0) {
        scheduler->stats.busyRejections++;
        pthread_mutex_unlock(&scheduler->lock);
        return LONG_HAIR_SCHEDULER_BUSY;
    }
    scheduler->activeCalls = 1;
    scheduler->stats.jobsStarted++;
    pthread_mutex_unlock(&scheduler->lock);

    bool usedParallel = false;
    LongHairSchedulerStatus status =
        RunAdmittedJob(scheduler, phases, phaseCount, &usedParallel);

    pthread_mutex_lock(&scheduler->lock);
    if (usedParallel) {
        scheduler->stats.parallelJobs++;
    } else {
        scheduler->stats.serialJobs++;
    }
    scheduler->activeCalls = 0;
    pthread_cond_broadcast(&scheduler->idleCond);
    pthread_mutex_unlock(&scheduler->lock);

    return status;
}

LongHairSchedulerStatus LongHairScheduler_ParallelFor(LongHairScheduler* scheduler,
    int totalItems, LongHairSchedulerWorkFn workFn, void* context) {
    if (!scheduler || totalItems < 0) return LONG_HAIR_SCHEDULER_INVALID_ARGUMENT;
    if (totalItems == 0) return LONG_HAIR_SCHEDULER_OK;
    if (!workFn) return LONG_HAIR_SCHEDULER_INVALID_ARGUMENT;

    LongHairSchedulerPhase phase;
    phase.totalItems = totalItems;
    phase.workFn = workFn;
    phase.context = context;
    return LongHairScheduler_RunPhases(scheduler, &phase, 1);
}

void LongHairScheduler_GetStats(LongHairScheduler* scheduler, LongHairSchedulerStats* outStats) {
    if (!outStats) return;
    memset(outStats, 0, sizeof(*outStats));
    if (!scheduler) return;

    pthread_mutex_lock(&scheduler->lock);
    *outStats = scheduler->stats;
    pthread_mutex_unlock(&scheduler->lock);
}

void LongHairScheduler_ResetStats(LongHairScheduler* scheduler) {
    if (!scheduler) return;
    pthread_mutex_lock(&scheduler->lock);
    memset(&scheduler->stats, 0, sizeof(scheduler->stats));
    scheduler->stats.threadCount = scheduler->threadCount;
    pthread_mutex_unlock(&scheduler->lock);
}
