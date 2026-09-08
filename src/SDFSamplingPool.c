/**
 * @file SDFSamplingPool.c
 * @brief Implementación del pool persistente de hilos POSIX para muestreo paralelo en rejilla SDF.
 * @author Monster Engine Team
 * @date 2026
 */

#include "SDFSamplingPool.h"
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>

typedef struct WorkerState {
    struct SDFSamplingPool* pool;
    int threadIndex;
    pthread_t thread;
    int startIndex;
    int endIndex;
} WorkerState;

struct SDFSamplingPool {
    int totalThreadCount;       /* Hilos totales incluyendo el hilo llamador */
    int workerThreadCount;      /* Hilos secundarios persistentes (totalThreadCount - 1) */
    WorkerState workers[SDF_SAMPLING_POOL_MAX_THREADS];

    pthread_mutex_t lock;
    pthread_cond_t startCond;
    pthread_cond_t doneCond;

    bool shouldQuit;
    uint64_t jobGeneration;
    int completedWorkers;

    SDFSamplingWorkFn currentWorkFn;
    void* currentContext;
};

static void* WorkerThreadEntry(void* arg) {
    WorkerState* ws = (WorkerState*)arg;
    SDFSamplingPool* pool = ws->pool;
    uint64_t lastSeenGeneration = 0;

    while (1) {
        pthread_mutex_lock(&pool->lock);
        while (!pool->shouldQuit && pool->jobGeneration == lastSeenGeneration) {
            pthread_cond_wait(&pool->startCond, &pool->lock);
        }

        if (pool->shouldQuit) {
            pthread_mutex_unlock(&pool->lock);
            break;
        }

        lastSeenGeneration = pool->jobGeneration;
        SDFSamplingWorkFn fn = pool->currentWorkFn;
        void* ctx = pool->currentContext;
        int start = ws->startIndex;
        int end = ws->endIndex;
        int idx = ws->threadIndex;
        pthread_mutex_unlock(&pool->lock);

        /* Ejecutar el rango asignado fuera del mutex */
        if (fn && start < end) {
            fn(ctx, start, end, idx);
        }

        pthread_mutex_lock(&pool->lock);
        pool->completedWorkers++;
        if (pool->completedWorkers == pool->workerThreadCount) {
            pthread_cond_signal(&pool->doneCond);
        }
        pthread_mutex_unlock(&pool->lock);
    }

    return NULL;
}

static int DetectSensibleThreadCount(void) {
    long nprocs = sysconf(_SC_NPROCESSORS_ONLN);
    if (nprocs <= 1) return 1;
    if (nprocs <= 2) return 2;
    if (nprocs <= 4) return 3;
    if (nprocs <= 8) return 4;
    /* En máquinas con 12 hilos lógicos (como Ryzen 5 5600H), usar 4 o 6 hilos
     * deja suficiente margen para el render principal y evita sobre-suscripción. */
    return 4;
}

SDFSamplingPool* SDFSamplingPool_Create(int threadCount) {
    if (threadCount <= 0) {
        threadCount = DetectSensibleThreadCount();
    }
    if (threadCount > SDF_SAMPLING_POOL_MAX_THREADS) {
        threadCount = SDF_SAMPLING_POOL_MAX_THREADS;
    }

    SDFSamplingPool* pool = (SDFSamplingPool*)calloc(1, sizeof(SDFSamplingPool));
    if (!pool) return NULL;

    pool->totalThreadCount = threadCount;
    pool->workerThreadCount = threadCount - 1;

    if (pool->workerThreadCount > 0) {
        pthread_mutex_init(&pool->lock, NULL);
        pthread_cond_init(&pool->startCond, NULL);
        pthread_cond_init(&pool->doneCond, NULL);

        for (int i = 0; i < pool->workerThreadCount; ++i) {
            WorkerState* ws = &pool->workers[i];
            ws->pool = pool;
            ws->threadIndex = i + 1; /* El llamador es 0; secundarios son 1..N-1 */
            if (pthread_create(&ws->thread, NULL, WorkerThreadEntry, ws) != 0) {
                /* Error creando hilos: reducir cuenta a los creados */
                pool->workerThreadCount = i;
                pool->totalThreadCount = i + 1;
                break;
            }
        }
    }

    return pool;
}

int SDFSamplingPool_GetThreadCount(const SDFSamplingPool* pool) {
    return pool ? pool->totalThreadCount : 1;
}

void SDFSamplingPool_ParallelFor(SDFSamplingPool* pool, int totalItems, SDFSamplingWorkFn workFn, void* context) {
    if (!workFn || totalItems <= 0) return;

    if (!pool || pool->totalThreadCount <= 1 || pool->workerThreadCount <= 0 || totalItems <= 1) {
        /* Camino mono-hilo forzado o serial */
        workFn(context, 0, totalItems, 0);
        return;
    }

    int threads = pool->totalThreadCount;
    if (threads > totalItems) threads = totalItems;
    int secondaryThreads = threads - 1;

    pthread_mutex_lock(&pool->lock);
    pool->currentWorkFn = workFn;
    pool->currentContext = context;
    pool->completedWorkers = 0;
    pool->jobGeneration++;

    /* Calcular rangos disjuntos y asignarlos a los hilos secundarios */
    for (int i = 0; i < pool->workerThreadCount; ++i) {
        if (i < secondaryThreads) {
            int threadIdx = i + 1;
            int start = (threadIdx * totalItems) / threads;
            int end = ((threadIdx + 1) * totalItems) / threads;
            pool->workers[i].startIndex = start;
            pool->workers[i].endIndex = end;
        } else {
            pool->workers[i].startIndex = 0;
            pool->workers[i].endIndex = 0;
        }
    }

    /* Calcular rango del hilo principal (threadIndex 0) */
    int mainStart = 0;
    int mainEnd = (1 * totalItems) / threads;

    /* Despertar hilos secundarios activos */
    pthread_cond_broadcast(&pool->startCond);
    pthread_mutex_unlock(&pool->lock);

    /* Ejecutar el trozo del hilo llamador */
    workFn(context, mainStart, mainEnd, 0);

    /* Esperar a que los hilos secundarios terminen */
    pthread_mutex_lock(&pool->lock);
    while (pool->completedWorkers < pool->workerThreadCount) {
        pthread_cond_wait(&pool->doneCond, &pool->lock);
    }
    pthread_mutex_unlock(&pool->lock);
}

void SDFSamplingPool_Free(SDFSamplingPool* pool) {
    if (!pool) return;

    if (pool->workerThreadCount > 0) {
        pthread_mutex_lock(&pool->lock);
        pool->shouldQuit = true;
        pthread_cond_broadcast(&pool->startCond);
        pthread_mutex_unlock(&pool->lock);

        for (int i = 0; i < pool->workerThreadCount; ++i) {
            pthread_join(pool->workers[i].thread, NULL);
        }

        pthread_mutex_destroy(&pool->lock);
        pthread_cond_destroy(&pool->startCond);
        pthread_cond_destroy(&pool->doneCond);
    }

    free(pool);
}
