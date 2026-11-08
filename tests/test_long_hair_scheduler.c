/**
 * @file test_long_hair_scheduler.c
 * @brief Pruebas de estrés, determinismo y concurrencia del LongHairScheduler.
 *
 * El archivo se puede compilar de forma aislada con LONG_HAIR_SCHEDULER_STANDALONE
 * (ver tools/agent_d_build_test.sh), sin tocar el Makefile compartido.
 */

#include "LongHairScheduler.h"
#include "test_utils.h"

#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifdef __linux__
#include <stdio.h>
#endif

#define TEST_MAX_ITEMS 1024

void run_long_hair_scheduler_tests(void);

/* ------------------------------------------------------------------------- */
/* Utilidades                                                                */
/* ------------------------------------------------------------------------- */

static void SleepMillis(long milliseconds) {
    struct timespec delay;
    delay.tv_sec = milliseconds / 1000;
    delay.tv_nsec = (milliseconds % 1000) * 1000000L;
    nanosleep(&delay, NULL);
}

static double NowSeconds(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)now.tv_sec + (double)now.tv_nsec * 1e-9;
}

static LongHairScheduler* MakeScheduler(int threads) {
    LongHairSchedulerConfig config = LongHairScheduler_DefaultConfig();
    config.threadCount = threads;
    if (threads > 0) config.maxThreads = threads;
    LongHairScheduler* scheduler = LongHairScheduler_Create(&config);
    TEST_ASSERT(scheduler != NULL, "no se pudo crear el scheduler");
    return scheduler;
}

static int ExpectedPartition(int item, int total, int partitions) {
    for (int partition = 0; partition < partitions; ++partition) {
        int start = (int)(((int64_t)partition * total) / partitions);
        int end = (int)(((int64_t)(partition + 1) * total) / partitions);
        if (item >= start && item < end) return partition;
    }
    return -1;
}

#ifdef __linux__
static int CountProcessThreads(void) {
    FILE* status = fopen("/proc/self/status", "r");
    if (!status) return -1;
    char line[256];
    int count = -1;
    while (fgets(line, sizeof(line), status)) {
        if (sscanf(line, "Threads: %d", &count) == 1) break;
    }
    fclose(status);
    return count;
}
#endif

/* ------------------------------------------------------------------------- */
/* Cobertura de rangos y determinismo                                        */
/* ------------------------------------------------------------------------- */

typedef struct CoverageContext {
    int total;
    atomic_int calls;
    atomic_int badRange;
    int visits[TEST_MAX_ITEMS];
    int threadOf[TEST_MAX_ITEMS];
} CoverageContext;

static bool CoverageWork(void* opaque, int start, int end, int threadIndex) {
    CoverageContext* context = (CoverageContext*)opaque;
    if (start < 0 || end < start || end > context->total ||
        threadIndex < 0 || threadIndex >= LONG_HAIR_SCHEDULER_MAX_THREADS) {
        atomic_store(&context->badRange, 1);
        return false;
    }
    atomic_fetch_add(&context->calls, 1);
    for (int i = start; i < end; ++i) {
        context->visits[i]++;
        context->threadOf[i] = threadIndex;
    }
    return true;
}

typedef struct DigestContext {
    int total;
    atomic_int badRange;
    float output[TEST_MAX_ITEMS];
} DigestContext;

static bool DigestWork(void* opaque, int start, int end, int threadIndex) {
    DigestContext* context = (DigestContext*)opaque;
    (void)threadIndex;
    if (start < 0 || end < start || end > context->total) {
        atomic_store(&context->badRange, 1);
        return false;
    }
    for (int i = start; i < end; ++i) {
        float x = sinf((float)i * 0.017f) + cosf((float)i * 0.0031f);
        context->output[i] = x * x - 0.25f * x;
    }
    return true;
}

static void TestDefaultAndSerial(void) {
    LongHairScheduler* automatic = LongHairScheduler_Create(NULL);
    TEST_ASSERT(automatic != NULL, "creación con configuración NULL");
    int autoThreads = LongHairScheduler_GetThreadCount(automatic);
    TEST_ASSERT(autoThreads >= 1 && autoThreads <= LONG_HAIR_SCHEDULER_DEFAULT_MAX_THREADS,
        "el modo automático respeta el tope por defecto");
    TEST_ASSERT(LongHairScheduler_IsRunning(automatic), "el scheduler automático está activo");
    LongHairScheduler_Destroy(automatic);

    /* El tope explícito se aplica incluso si se piden más participantes. */
    LongHairSchedulerConfig clamped = LongHairScheduler_DefaultConfig();
    clamped.threadCount = 8;
    clamped.maxThreads = 2;
    LongHairScheduler* clampedScheduler = LongHairScheduler_Create(&clamped);
    TEST_ASSERT(clampedScheduler != NULL, "creación con tope explícito");
    TEST_ASSERT(LongHairScheduler_GetThreadCount(clampedScheduler) <= 2,
        "el tope explícito limita los participantes");
    LongHairScheduler_Destroy(clampedScheduler);

    /* Pedir serial explícitamente es compatible con requireParallel. */
    LongHairSchedulerConfig serialConfig = LongHairScheduler_DefaultConfig();
    serialConfig.threadCount = 1;
    serialConfig.requireParallel = true;
    LongHairScheduler* serial = LongHairScheduler_Create(&serialConfig);
    TEST_ASSERT(serial != NULL, "serial explícito con requireParallel");
    TEST_ASSERT(LongHairScheduler_GetThreadCount(serial) == 1, "un hilo efectivo");

    CoverageContext coverage;
    memset(&coverage, 0, sizeof(coverage));
    coverage.total = 37;
    atomic_init(&coverage.calls, 0);
    atomic_init(&coverage.badRange, 0);

    LongHairSchedulerStatus status =
        LongHairScheduler_ParallelFor(serial, coverage.total, CoverageWork, &coverage);
    TEST_ASSERT(status == LONG_HAIR_SCHEDULER_OK, "trabajo serial correcto");
    TEST_ASSERT(atomic_load(&coverage.calls) == 1, "una sola llamada serial");
    TEST_ASSERT(atomic_load(&coverage.badRange) == 0, "rangos seriales válidos");
    for (int i = 0; i < coverage.total; ++i) {
        TEST_ASSERT(coverage.visits[i] == 1, "cada elemento serial se visita una vez");
        TEST_ASSERT(coverage.threadOf[i] == 0, "el índice serial es cero");
    }

    /* La entrada vacía es un no-op válido y no invoca el callback. */
    memset(&coverage, 0, sizeof(coverage));
    coverage.total = 0;
    atomic_init(&coverage.calls, 0);
    atomic_init(&coverage.badRange, 0);
    status = LongHairScheduler_ParallelFor(serial, 0, CoverageWork, &coverage);
    TEST_ASSERT(status == LONG_HAIR_SCHEDULER_OK, "total cero es válido");
    TEST_ASSERT(atomic_load(&coverage.calls) == 0, "total cero no invoca el callback");

    LongHairScheduler_Destroy(serial);
}

static void TestPartitionCoverageAndDeterminism(void) {
    const int threadsToTest[] = {1, 2, 4, 8};
    const int total = 288;

    DigestContext serialOutput;
    memset(&serialOutput, 0, sizeof(serialOutput));
    serialOutput.total = total;
    atomic_init(&serialOutput.badRange, 0);

    LongHairScheduler* serial = MakeScheduler(1);
    TEST_ASSERT(LongHairScheduler_ParallelFor(serial, total, DigestWork, &serialOutput) ==
        LONG_HAIR_SCHEDULER_OK, "referencia serial");
    TEST_ASSERT(atomic_load(&serialOutput.badRange) == 0, "rangos seriales de digest");
    LongHairScheduler_Destroy(serial);

    for (size_t index = 0; index < sizeof(threadsToTest) / sizeof(threadsToTest[0]); ++index) {
        int threads = threadsToTest[index];
        LongHairScheduler* scheduler = MakeScheduler(threads);
        int effective = LongHairScheduler_GetThreadCount(scheduler);
        TEST_ASSERT(effective >= 1 && effective <= threads, "participantes efectivos");

        CoverageContext coverage;
        memset(&coverage, 0, sizeof(coverage));
        coverage.total = total;
        atomic_init(&coverage.calls, 0);
        atomic_init(&coverage.badRange, 0);

        LongHairSchedulerStatus status =
            LongHairScheduler_ParallelFor(scheduler, total, CoverageWork, &coverage);
        TEST_ASSERT(status == LONG_HAIR_SCHEDULER_OK, "cobertura paralela");
        TEST_ASSERT(atomic_load(&coverage.badRange) == 0, "rangos paralelos válidos");

        int nonEmptyPartitions = 0;
        for (int partition = 0; partition < effective; ++partition) {
            int start = (int)(((int64_t)partition * total) / effective);
            int end = (int)(((int64_t)(partition + 1) * total) / effective);
            if (start < end) nonEmptyPartitions++;
        }
        TEST_ASSERT(atomic_load(&coverage.calls) == nonEmptyPartitions,
            "una llamada por partición no vacía");

        for (int item = 0; item < total; ++item) {
            TEST_ASSERT(coverage.visits[item] == 1,
                "cada guía se visita exactamente una vez");
            TEST_ASSERT(coverage.threadOf[item] == ExpectedPartition(item, total, effective),
                "el índice de partición es estable y contiguo");
        }

        /* La segunda ejecución debe usar exactamente las mismas particiones. */
        int firstThreadOf[TEST_MAX_ITEMS];
        memcpy(firstThreadOf, coverage.threadOf, sizeof(firstThreadOf));
        memset(&coverage, 0, sizeof(coverage));
        coverage.total = total;
        atomic_init(&coverage.calls, 0);
        atomic_init(&coverage.badRange, 0);
        TEST_ASSERT(LongHairScheduler_ParallelFor(scheduler, total, CoverageWork, &coverage) ==
            LONG_HAIR_SCHEDULER_OK, "segunda cobertura");
        for (int item = 0; item < total; ++item) {
            TEST_ASSERT(coverage.threadOf[item] == firstThreadOf[item],
                "las particiones no cambian entre trabajos");
        }

        DigestContext parallelOutput;
        memset(&parallelOutput, 0, sizeof(parallelOutput));
        parallelOutput.total = total;
        atomic_init(&parallelOutput.badRange, 0);
        TEST_ASSERT(LongHairScheduler_ParallelFor(scheduler, total, DigestWork, &parallelOutput) ==
            LONG_HAIR_SCHEDULER_OK, "digest paralelo");
        TEST_ASSERT(memcmp(serialOutput.output, parallelOutput.output,
            (size_t)total * sizeof(float)) == 0,
            "la salida es idéntica con cualquier número de hilos");

        LongHairScheduler_Destroy(scheduler);
    }
}

/* ------------------------------------------------------------------------- */
/* Barreras de tres fases                                                    */
/* ------------------------------------------------------------------------- */

typedef struct BarrierContext {
    int total;
    int* phaseA;
    int* phaseB;
    int* phaseC;
    atomic_int badRange;
} BarrierContext;

static bool BarrierPhaseA(void* opaque, int start, int end, int threadIndex) {
    BarrierContext* context = (BarrierContext*)opaque;
    (void)threadIndex;
    if (start < 0 || end < start || end > context->total) return false;
    for (int i = start; i < end; ++i) context->phaseA[i] = i * 3 + 1;
    return true;
}

static bool BarrierPhaseB(void* opaque, int start, int end, int threadIndex) {
    BarrierContext* context = (BarrierContext*)opaque;
    (void)threadIndex;
    if (start < 0 || end < start || end > context->total) return false;
    /* Lee una ranura de otra partición: sólo es válido tras la barrera. */
    for (int i = start; i < end; ++i) {
        int source = (i + context->total / 2) % context->total;
        context->phaseB[i] = context->phaseA[source] + 7;
    }
    return true;
}

static bool BarrierPhaseC(void* opaque, int start, int end, int threadIndex) {
    BarrierContext* context = (BarrierContext*)opaque;
    (void)threadIndex;
    if (start < 0 || end < start || end > context->total) return false;
    /* Lee una ranura de la fase 2 escrita por otra partición. */
    for (int i = start; i < end; ++i) {
        int source = (i + 1) % context->total;
        context->phaseC[i] = context->phaseB[source] + 11;
    }
    return true;
}

static void TestThreePhaseBarriers(void) {
    const int total = 288;
    LongHairScheduler* scheduler = MakeScheduler(4);

    int* phaseA = (int*)calloc((size_t)total, sizeof(int));
    int* phaseB = (int*)calloc((size_t)total, sizeof(int));
    int* phaseC = (int*)calloc((size_t)total, sizeof(int));
    TEST_ASSERT(phaseA && phaseB && phaseC, "memoria de fases");

    BarrierContext context;
    context.total = total;
    context.phaseA = phaseA;
    context.phaseB = phaseB;
    context.phaseC = phaseC;
    atomic_init(&context.badRange, 0);

    LongHairSchedulerPhase phases[3];
    phases[0] = (LongHairSchedulerPhase){total, BarrierPhaseA, &context};
    phases[1] = (LongHairSchedulerPhase){total, BarrierPhaseB, &context};
    phases[2] = (LongHairSchedulerPhase){total, BarrierPhaseC, &context};

    const int iterations = 100;
    LongHairScheduler_ResetStats(scheduler);
    for (int iteration = 0; iteration < iterations; ++iteration) {
        memset(phaseA, 0, (size_t)total * sizeof(int));
        memset(phaseB, 0, (size_t)total * sizeof(int));
        memset(phaseC, 0, (size_t)total * sizeof(int));
        LongHairSchedulerStatus status =
            LongHairScheduler_RunPhases(scheduler, phases, 3);
        TEST_ASSERT(status == LONG_HAIR_SCHEDULER_OK, "las tres fases terminan");
        TEST_ASSERT(atomic_load(&context.badRange) == 0, "rangos de fases válidos");
        for (int i = 0; i < total; ++i) {
            int sourceB = (i + 1) % total;
            int sourceA = (sourceB + total / 2) % total;
            int expected = phaseA[sourceA] + 7 + 11;
            TEST_ASSERT(phaseC[i] == expected,
                "la fase 3 observa la fase 2 completa y la fase 2 la fase 1");
        }
    }

    LongHairSchedulerStats stats;
    LongHairScheduler_GetStats(scheduler, &stats);
    TEST_ASSERT(stats.jobsCompleted == (uint64_t)iterations, "contador de trabajos completados");
    TEST_ASSERT(stats.jobsFailed == 0, "sin fallos en las barreras");
    TEST_ASSERT(stats.phasesExecuted == (uint64_t)iterations * 3, "tres fases por trabajo");
    TEST_ASSERT(stats.lastPhaseCount == 3, "última fase contabilizada");

    free(phaseA);
    free(phaseB);
    free(phaseC);
    LongHairScheduler_Destroy(scheduler);
}

/* ------------------------------------------------------------------------- */
/* Fallos, argumentos inválidos y anidamiento                                */
/* ------------------------------------------------------------------------- */

typedef struct FailureContext {
    int total;
    int failItem;
    atomic_int calls;
    int visits[TEST_MAX_ITEMS];
} FailureContext;

static bool FailureWork(void* opaque, int start, int end, int threadIndex) {
    FailureContext* context = (FailureContext*)opaque;
    (void)threadIndex;
    if (start < 0 || end < start || end > context->total) return false;
    atomic_fetch_add(&context->calls, 1);
    for (int i = start; i < end; ++i) {
        if (i == context->failItem) return false;
        context->visits[i]++;
    }
    return true;
}

static void TestFailureHandling(void) {
    LongHairScheduler* serial = MakeScheduler(1);
    FailureContext serialFailure;
    memset(&serialFailure, 0, sizeof(serialFailure));
    serialFailure.total = 64;
    serialFailure.failItem = 5;
    atomic_init(&serialFailure.calls, 0);

    LongHairSchedulerStatus status =
        LongHairScheduler_ParallelFor(serial, serialFailure.total, FailureWork, &serialFailure);
    TEST_ASSERT(status == LONG_HAIR_SCHEDULER_CALLBACK_FAILED, "fallo serial propagado");
    for (int i = 0; i < 5; ++i) {
        TEST_ASSERT(serialFailure.visits[i] == 1, "el trabajo previo al fallo se ejecutó");
    }
    for (int i = 5; i < serialFailure.total; ++i) {
        TEST_ASSERT(serialFailure.visits[i] == 0, "el trabajo posterior al fallo se omite");
    }

    /* Un fallo no debe envenenar el scheduler. */
    memset(&serialFailure, 0, sizeof(serialFailure));
    serialFailure.total = 64;
    serialFailure.failItem = -1;
    atomic_init(&serialFailure.calls, 0);
    TEST_ASSERT(LongHairScheduler_ParallelFor(serial, serialFailure.total,
        FailureWork, &serialFailure) == LONG_HAIR_SCHEDULER_OK, "el scheduler se recupera");
    for (int i = 0; i < serialFailure.total; ++i) {
        TEST_ASSERT(serialFailure.visits[i] == 1, "trabajo posterior completo");
    }

    LongHairSchedulerStats stats;
    LongHairScheduler_GetStats(serial, &stats);
    TEST_ASSERT(stats.jobsFailed == 1, "un trabajo fallido contabilizado");
    TEST_ASSERT(stats.jobsCompleted == 1, "un trabajo completado contabilizado");
    LongHairScheduler_Destroy(serial);

    /* En paralelo el aborto es cooperativo: algunas particiones pueden empezar. */
    LongHairScheduler* parallel = MakeScheduler(4);
    FailureContext parallelFailure;
    memset(&parallelFailure, 0, sizeof(parallelFailure));
    parallelFailure.total = 256;
    parallelFailure.failItem = 0;
    atomic_init(&parallelFailure.calls, 0);
    status = LongHairScheduler_ParallelFor(parallel, parallelFailure.total,
        FailureWork, &parallelFailure);
    TEST_ASSERT(status == LONG_HAIR_SCHEDULER_CALLBACK_FAILED, "fallo paralelo propagado");
    TEST_ASSERT(atomic_load(&parallelFailure.calls) <= 4,
        "el aborto cooperativo limita las particiones que arrancan");
    LongHairScheduler_Destroy(parallel);
}

static bool NoopWork(void* opaque, int start, int end, int threadIndex) {
    (void)opaque;
    (void)start;
    (void)end;
    (void)threadIndex;
    return true;
}

typedef struct DestroyFromCallbackContext {
    LongHairScheduler* scheduler;
    atomic_int calls;
} DestroyFromCallbackContext;

static bool DestroyFromCallbackWork(void* opaque, int start, int end, int threadIndex) {
    DestroyFromCallbackContext* context = (DestroyFromCallbackContext*)opaque;
    (void)start;
    (void)end;
    (void)threadIndex;
    LongHairScheduler_Destroy(context->scheduler);
    atomic_fetch_add(&context->calls, 1);
    return true;
}

typedef struct NestedContext {
    LongHairScheduler* scheduler;
    atomic_int nestedStatus;
    atomic_int nestedCalls;
} NestedContext;

static bool NestedWork(void* opaque, int start, int end, int threadIndex) {
    NestedContext* context = (NestedContext*)opaque;
    (void)start;
    (void)end;
    (void)threadIndex;
    LongHairSchedulerStatus status =
        LongHairScheduler_ParallelFor(context->scheduler, 4, NoopWork, NULL);
    atomic_store(&context->nestedStatus, (int)status);
    atomic_fetch_add(&context->nestedCalls, 1);
    return true;
}

typedef struct NestedChainContext {
    LongHairScheduler* schedulerA;
    LongHairScheduler* schedulerB;
    atomic_int returnToAStatus;
} NestedChainContext;

static bool ReturnToAWork(void* opaque, int start, int end, int threadIndex) {
    NestedChainContext* context = (NestedChainContext*)opaque;
    (void)start;
    (void)end;
    (void)threadIndex;
    LongHairSchedulerStatus status = LongHairScheduler_ParallelFor(
        context->schedulerA, 1, NoopWork, NULL);
    atomic_store(&context->returnToAStatus, (int)status);
    return true;
}

static bool EnterBWork(void* opaque, int start, int end, int threadIndex) {
    NestedChainContext* context = (NestedChainContext*)opaque;
    (void)start;
    (void)end;
    (void)threadIndex;
    return LongHairScheduler_ParallelFor(context->schedulerB, 1,
        ReturnToAWork, context) == LONG_HAIR_SCHEDULER_OK;
}

static void TestInvalidArgumentsAndNesting(void) {
    LongHairScheduler* scheduler = MakeScheduler(1);

    TEST_ASSERT(LongHairScheduler_ParallelFor(NULL, 4, NoopWork, NULL) ==
        LONG_HAIR_SCHEDULER_INVALID_ARGUMENT, "scheduler NULL inválido");
    TEST_ASSERT(LongHairScheduler_ParallelFor(scheduler, -1, NoopWork, NULL) ==
        LONG_HAIR_SCHEDULER_INVALID_ARGUMENT, "total negativo inválido");
    TEST_ASSERT(LongHairScheduler_ParallelFor(scheduler, 4, NULL, NULL) ==
        LONG_HAIR_SCHEDULER_INVALID_ARGUMENT, "callback NULL inválido");
    TEST_ASSERT(LongHairScheduler_ParallelFor(scheduler, 0, NULL, NULL) ==
        LONG_HAIR_SCHEDULER_OK, "total cero con callback NULL es no-op");
    TEST_ASSERT(LongHairScheduler_RunPhases(scheduler, NULL, 1) ==
        LONG_HAIR_SCHEDULER_INVALID_ARGUMENT, "fases NULL inválidas");
    TEST_ASSERT(LongHairScheduler_RunPhases(scheduler, NULL, 0) ==
        LONG_HAIR_SCHEDULER_INVALID_ARGUMENT, "cero fases inválido");

    LongHairSchedulerPhase invalidPhase = {-1, NoopWork, NULL};
    TEST_ASSERT(LongHairScheduler_RunPhases(scheduler, &invalidPhase, 1) ==
        LONG_HAIR_SCHEDULER_INVALID_ARGUMENT, "fase con total negativo inválida");
    invalidPhase = (LongHairSchedulerPhase){4, NULL, NULL};
    TEST_ASSERT(LongHairScheduler_RunPhases(scheduler, &invalidPhase, 1) ==
        LONG_HAIR_SCHEDULER_INVALID_ARGUMENT, "fase sin callback inválida");

    NestedContext nested;
    nested.scheduler = scheduler;
    atomic_init(&nested.nestedStatus, 0);
    atomic_init(&nested.nestedCalls, 0);
    TEST_ASSERT(LongHairScheduler_ParallelFor(scheduler, 8, NestedWork, &nested) ==
        LONG_HAIR_SCHEDULER_OK, "el callback externo termina");
    TEST_ASSERT(atomic_load(&nested.nestedCalls) == 1, "un callback serial");
    TEST_ASSERT(atomic_load(&nested.nestedStatus) == LONG_HAIR_SCHEDULER_NESTED,
        "la llamada anidada se rechaza");

    /* Sólo el scheduler TLS más interno cuenta como NESTED: A->B->A es BUSY. */
    LongHairScheduler* schedulerB = MakeScheduler(1);
    NestedChainContext chain;
    chain.schedulerA = scheduler;
    chain.schedulerB = schedulerB;
    atomic_init(&chain.returnToAStatus, LONG_HAIR_SCHEDULER_INVALID_ARGUMENT);
    TEST_ASSERT(LongHairScheduler_ParallelFor(scheduler, 1, EnterBWork, &chain) ==
        LONG_HAIR_SCHEDULER_OK, "la cadena A->B finaliza");
    TEST_ASSERT(atomic_load(&chain.returnToAStatus) == LONG_HAIR_SCHEDULER_BUSY,
        "la cadena A->B->A rechaza A como BUSY");
    LongHairScheduler_Destroy(schedulerB);

    LongHairSchedulerStats stats;
    LongHairScheduler_GetStats(scheduler, &stats);
    TEST_ASSERT(stats.nestedRejections == 1, "rechazo anidado contabilizado");
    TEST_ASSERT(stats.busyRejections == 1, "retorno A->B->A contabilizado como BUSY");
    TEST_ASSERT(stats.jobsCompleted == 2, "los trabajos externos se completan");

    /* Destroy desde un callback se rechaza y no libera el scheduler. */
    DestroyFromCallbackContext destroyContext;
    destroyContext.scheduler = scheduler;
    atomic_init(&destroyContext.calls, 0);
    TEST_ASSERT(LongHairScheduler_ParallelFor(scheduler, 8, DestroyFromCallbackWork,
        &destroyContext) == LONG_HAIR_SCHEDULER_OK, "el callback de destrucción retorna");
    TEST_ASSERT(atomic_load(&destroyContext.calls) == 1, "el callback de destrucción se ejecutó");
    TEST_ASSERT(LongHairScheduler_IsRunning(scheduler),
        "Destroy desde un callback no libera el scheduler");

    LongHairScheduler* otherScheduler = MakeScheduler(1);
    destroyContext.scheduler = otherScheduler;
    atomic_store(&destroyContext.calls, 0);
    TEST_ASSERT(LongHairScheduler_ParallelFor(scheduler, 8, DestroyFromCallbackWork,
        &destroyContext) == LONG_HAIR_SCHEDULER_OK,
        "callback en A intenta destruir otro scheduler B");
    TEST_ASSERT(atomic_load(&destroyContext.calls) == 1 &&
        LongHairScheduler_IsRunning(otherScheduler),
        "Destroy desde callback de A también se rechaza para B");
    LongHairScheduler_Destroy(otherScheduler);

    /* Los argumentos inválidos no consumen ni bloquean el scheduler. */
    TEST_ASSERT(LongHairScheduler_ParallelFor(scheduler, 8, NoopWork, NULL) ==
        LONG_HAIR_SCHEDULER_OK, "el scheduler sigue operativo");
    LongHairScheduler_Destroy(scheduler);
}

/* ------------------------------------------------------------------------- */
/* Dos llamadores, destrucción activa y ciclo de vida                        */
/* ------------------------------------------------------------------------- */

typedef struct BlockingContext {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int entered;
    int release;
    atomic_int calls;
    atomic_int badRange;
} BlockingContext;

static void BlockingContextInit(BlockingContext* context, int releaseImmediately) {
    memset(context, 0, sizeof(*context));
    pthread_mutex_init(&context->mutex, NULL);
    pthread_cond_init(&context->cond, NULL);
    context->release = releaseImmediately;
    atomic_init(&context->calls, 0);
    atomic_init(&context->badRange, 0);
}

static void BlockingContextDestroy(BlockingContext* context) {
    pthread_cond_destroy(&context->cond);
    pthread_mutex_destroy(&context->mutex);
}

static bool BlockingWork(void* opaque, int start, int end, int threadIndex) {
    BlockingContext* context = (BlockingContext*)opaque;
    (void)threadIndex;
    if (start < 0 || end < start) {
        atomic_store(&context->badRange, 1);
        return false;
    }
    atomic_fetch_add(&context->calls, 1);
    pthread_mutex_lock(&context->mutex);
    context->entered++;
    pthread_cond_broadcast(&context->cond);
    while (!context->release) pthread_cond_wait(&context->cond, &context->mutex);
    pthread_mutex_unlock(&context->mutex);
    return true;
}

typedef struct CallArguments {
    LongHairScheduler* scheduler;
    BlockingContext* context;
    int total;
    LongHairSchedulerStatus status;
} CallArguments;

static void* CallThread(void* opaque) {
    CallArguments* arguments = (CallArguments*)opaque;
    arguments->status = LongHairScheduler_ParallelFor(arguments->scheduler,
        arguments->total, BlockingWork, arguments->context);
    return NULL;
}

static void WaitForEntries(BlockingContext* context, int minimum) {
    pthread_mutex_lock(&context->mutex);
    while (context->entered < minimum) pthread_cond_wait(&context->cond, &context->mutex);
    pthread_mutex_unlock(&context->mutex);
}

static void ReleaseBlocked(BlockingContext* context) {
    pthread_mutex_lock(&context->mutex);
    context->release = 1;
    pthread_cond_broadcast(&context->cond);
    pthread_mutex_unlock(&context->mutex);
}

static void TestTwoCallers(void) {
    LongHairScheduler* scheduler = MakeScheduler(4);
    BlockingContext first;
    BlockingContext second;
    BlockingContextInit(&first, 0);
    BlockingContextInit(&second, 1);

    CallArguments firstArguments = {scheduler, &first, 64, LONG_HAIR_SCHEDULER_INVALID_ARGUMENT};
    CallArguments secondArguments = {scheduler, &second, 64, LONG_HAIR_SCHEDULER_INVALID_ARGUMENT};

    pthread_t firstThread;
    pthread_t secondThread;
    TEST_ASSERT(pthread_create(&firstThread, NULL, CallThread, &firstArguments) == 0,
        "hilo del primer llamador");
    WaitForEntries(&first, 1);
    TEST_ASSERT(pthread_create(&secondThread, NULL, CallThread, &secondArguments) == 0,
        "hilo del segundo llamador");
    pthread_join(secondThread, NULL);

    TEST_ASSERT(secondArguments.status == LONG_HAIR_SCHEDULER_BUSY,
        "el segundo llamador recibe BUSY sin bloquearse");
    TEST_ASSERT(atomic_load(&second.calls) == 0,
        "el segundo llamador no ejecuta el callback");

    ReleaseBlocked(&first);
    pthread_join(firstThread, NULL);
    TEST_ASSERT(firstArguments.status == LONG_HAIR_SCHEDULER_OK,
        "el primer llamador termina correctamente");

    LongHairSchedulerStats stats;
    LongHairScheduler_GetStats(scheduler, &stats);
    TEST_ASSERT(stats.busyRejections >= 1, "rechazo BUSY contabilizado");
    TEST_ASSERT(stats.jobsCompleted == 1, "sólo un trabajo completado");

    BlockingContextDestroy(&first);
    BlockingContextDestroy(&second);
    LongHairScheduler_Destroy(scheduler);
}

typedef struct DestroyArguments {
    LongHairScheduler* scheduler;
    atomic_int started;
    atomic_int done;
} DestroyArguments;

static void* DestroyThread(void* opaque) {
    DestroyArguments* arguments = (DestroyArguments*)opaque;
    atomic_store(&arguments->started, 1);
    LongHairScheduler_Destroy(arguments->scheduler);
    atomic_store(&arguments->done, 1);
    return NULL;
}

static void TestDestroyWhileActive(void) {
    LongHairScheduler* scheduler = MakeScheduler(4);
    BlockingContext blocking;
    BlockingContextInit(&blocking, 0);

    CallArguments call = {scheduler, &blocking, 64, LONG_HAIR_SCHEDULER_INVALID_ARGUMENT};
    pthread_t jobThread;
    TEST_ASSERT(pthread_create(&jobThread, NULL, CallThread, &call) == 0, "hilo del trabajo");
    WaitForEntries(&blocking, 1);

    /* Quiescencia del propietario: este hilo no tendrá más llamadas API tras Destroy. */
    LongHairSchedulerStats beforeDestroy;
    LongHairScheduler_GetStats(scheduler, &beforeDestroy);
    TEST_ASSERT(beforeDestroy.jobsStarted == 1, "job activo observado antes de destruir");

    DestroyArguments destroy;
    destroy.scheduler = scheduler;
    atomic_init(&destroy.started, 0);
    atomic_init(&destroy.done, 0);
    pthread_t destroyThread;
    TEST_ASSERT(pthread_create(&destroyThread, NULL, DestroyThread, &destroy) == 0,
        "hilo de destrucción");
    while (!atomic_load(&destroy.started)) SleepMillis(1);

    /*
     * No se hacen probes mientras Destroy corre: lectores/submitters deben estar
     * quiescentes. El job activo es la única llamada permitida en vuelo y Destroy
     * debe esperar hasta que el propietario libere su callback.
     */
    SleepMillis(10);
    TEST_ASSERT(atomic_load(&destroy.done) == 0,
        "Destroy espera al trabajo activo en lugar de liberar el pool en uso");

    ReleaseBlocked(&blocking);
    pthread_join(jobThread, NULL);
    pthread_join(destroyThread, NULL);

    TEST_ASSERT(call.status == LONG_HAIR_SCHEDULER_OK, "el trabajo activo termina antes de liberar");
    TEST_ASSERT(atomic_load(&destroy.done) == 1, "Destroy completa tras el trabajo");

    BlockingContextDestroy(&blocking);
}

static void TestRepeatedLifecycle(void) {
    const int threadOptions[] = {1, 2, 4};
    for (int iteration = 0; iteration < 30; ++iteration) {
        int threads = threadOptions[iteration % 3];
        LongHairScheduler* scheduler = MakeScheduler(threads);
        TEST_ASSERT(LongHairScheduler_IsRunning(scheduler), "scheduler activo");
        CoverageContext coverage;
        memset(&coverage, 0, sizeof(coverage));
        coverage.total = 32;
        atomic_init(&coverage.calls, 0);
        atomic_init(&coverage.badRange, 0);
        TEST_ASSERT(LongHairScheduler_ParallelFor(scheduler, 32, CoverageWork, &coverage) ==
            LONG_HAIR_SCHEDULER_OK, "trabajo corto antes de destruir");
        LongHairScheduler_Destroy(scheduler);
    }
}

static void TestPersistentWorkers(void) {
#ifdef __linux__
    LongHairScheduler* scheduler = MakeScheduler(4);
    CoverageContext coverage;
    memset(&coverage, 0, sizeof(coverage));
    coverage.total = 16;
    atomic_init(&coverage.calls, 0);
    atomic_init(&coverage.badRange, 0);
    TEST_ASSERT(LongHairScheduler_ParallelFor(scheduler, 16, CoverageWork, &coverage) ==
        LONG_HAIR_SCHEDULER_OK, "trabajo inicial");

    int before = CountProcessThreads();
    LongHairScheduler_ResetStats(scheduler);
    for (int iteration = 0; iteration < 100; ++iteration) {
        TEST_ASSERT(LongHairScheduler_ParallelFor(scheduler, 16, CoverageWork, &coverage) ==
            LONG_HAIR_SCHEDULER_OK, "trabajo repetido");
    }
    int after = CountProcessThreads();
    TEST_ASSERT(before > 0 && after == before,
        "no se crean ni destruyen hilos entre trabajos");

    LongHairSchedulerStats stats;
    LongHairScheduler_GetStats(scheduler, &stats);
    TEST_ASSERT(stats.jobsCompleted == 100, "cien trabajos completados");
    TEST_ASSERT(stats.jobsStarted == 100, "cien trabajos iniciados");
    TEST_ASSERT(stats.threadCount == LongHairScheduler_GetThreadCount(scheduler),
        "los contadores reflejan los participantes efectivos");
    LongHairScheduler_Destroy(scheduler);
#endif
}

static void TestStatsReset(void) {
    LongHairScheduler* scheduler = MakeScheduler(2);
    TEST_ASSERT(LongHairScheduler_ParallelFor(scheduler, 8, NoopWork, NULL) ==
        LONG_HAIR_SCHEDULER_OK, "trabajo previo al reset");
    LongHairSchedulerStats stats;
    LongHairScheduler_GetStats(scheduler, &stats);
    TEST_ASSERT(stats.jobsStarted == 1, "trabajo contado");
    LongHairScheduler_ResetStats(scheduler);
    LongHairScheduler_GetStats(scheduler, &stats);
    TEST_ASSERT(stats.jobsStarted == 0 && stats.jobsCompleted == 0, "contadores a cero");
    TEST_ASSERT(stats.threadCount == LongHairScheduler_GetThreadCount(scheduler),
        "el reset conserva los participantes");
    LongHairScheduler_Destroy(scheduler);
}

/* Schedulers distintos no comparten estado y pueden trabajar en paralelo. */
typedef struct IndependentArguments {
    LongHairScheduler* scheduler;
    DigestContext* context;
    LongHairSchedulerStatus status;
} IndependentArguments;

static void* IndependentThread(void* opaque) {
    IndependentArguments* arguments = (IndependentArguments*)opaque;
    arguments->status = LongHairScheduler_ParallelFor(arguments->scheduler,
        arguments->context->total, DigestWork, arguments->context);
    return NULL;
}

static void TestIndependentSchedulers(void) {
    LongHairScheduler* first = MakeScheduler(2);
    LongHairScheduler* second = MakeScheduler(2);

    DigestContext firstContext;
    DigestContext secondContext;
    memset(&firstContext, 0, sizeof(firstContext));
    memset(&secondContext, 0, sizeof(secondContext));
    firstContext.total = 128;
    secondContext.total = 128;
    atomic_init(&firstContext.badRange, 0);
    atomic_init(&secondContext.badRange, 0);

    IndependentArguments firstArguments = {first, &firstContext, LONG_HAIR_SCHEDULER_INVALID_ARGUMENT};
    IndependentArguments secondArguments = {second, &secondContext, LONG_HAIR_SCHEDULER_INVALID_ARGUMENT};
    pthread_t firstThread;
    pthread_t secondThread;
    TEST_ASSERT(pthread_create(&firstThread, NULL, IndependentThread, &firstArguments) == 0,
        "hilo del scheduler independiente 1");
    TEST_ASSERT(pthread_create(&secondThread, NULL, IndependentThread, &secondArguments) == 0,
        "hilo del scheduler independiente 2");
    pthread_join(firstThread, NULL);
    pthread_join(secondThread, NULL);

    TEST_ASSERT(firstArguments.status == LONG_HAIR_SCHEDULER_OK, "scheduler 1 correcto");
    TEST_ASSERT(secondArguments.status == LONG_HAIR_SCHEDULER_OK, "scheduler 2 correcto");
    TEST_ASSERT(memcmp(firstContext.output, secondContext.output,
        (size_t)firstContext.total * sizeof(float)) == 0, "salidas independientes idénticas");

    LongHairScheduler_Destroy(first);
    LongHairScheduler_Destroy(second);
}

typedef struct ImbalancedContext {
    int total;
    long work[TEST_MAX_ITEMS];
    int output[TEST_MAX_ITEMS];
} ImbalancedContext;

static bool ImbalancedWork(void* opaque, int start, int end, int threadIndex) {
    (void)threadIndex;
    ImbalancedContext* context = (ImbalancedContext*)opaque;
    if (start < 0 || end < start || end > context->total) return false;
    for (int i = start; i < end; ++i) {
        long accumulator = i;
        for (long k = 0; k < context->work[i] * 1000; ++k) {
            accumulator = (accumulator * 5 + 3) % 1000003;
        }
        context->output[i] = (int)accumulator;
    }
    return true;
}

static void TestImbalancedWorkload(void) {
    /* El trabajo por elemento varía 17x; la corrección no depende del reparto. */
    ImbalancedContext context;
    memset(&context, 0, sizeof(context));
    context.total = 96;
    for (int i = 0; i < context.total; ++i) context.work[i] = 1 + (i * 37) % 17;

    LongHairScheduler* serial = MakeScheduler(1);
    ImbalancedContext reference = context;
    TEST_ASSERT(LongHairScheduler_ParallelFor(serial, context.total, ImbalancedWork, &reference) ==
        LONG_HAIR_SCHEDULER_OK, "referencia desbalanceada serial");
    LongHairScheduler_Destroy(serial);

    const int threadsToTest[] = {2, 4, 8};
    for (size_t index = 0; index < sizeof(threadsToTest) / sizeof(threadsToTest[0]); ++index) {
        LongHairScheduler* scheduler = MakeScheduler(threadsToTest[index]);
        ImbalancedContext result = context;
        TEST_ASSERT(LongHairScheduler_ParallelFor(scheduler, context.total,
            ImbalancedWork, &result) == LONG_HAIR_SCHEDULER_OK, "trabajo desbalanceado");
        TEST_ASSERT(memcmp(reference.output, result.output,
            (size_t)context.total * sizeof(int)) == 0,
            "el reparto fijo no altera el resultado desbalanceado");
        LongHairScheduler_Destroy(scheduler);
    }
}

/* ------------------------------------------------------------------------- */
/* Benchmark sintético (sólo en el binario standalone)                       */
/* ------------------------------------------------------------------------- */

#ifdef LONG_HAIR_SCHEDULER_STANDALONE

typedef struct BenchContext {
    int total;
    int inner;
    int imbalanced;
    const float* input;
    float* output;
} BenchContext;

static bool BenchWork(void* opaque, int start, int end, int threadIndex) {
    BenchContext* context = (BenchContext*)opaque;
    (void)threadIndex;
    for (int i = start; i < end; ++i) {
        float x = context->input[i];
        int repeats = context->inner;
        if (context->imbalanced) repeats *= 1 + (i * 37) % 17;
        for (int k = 0; k < repeats; ++k) {
            x = sinf(x * 0.0013f + 0.17f) * 0.998f +
                cosf(x * 0.0007f + 0.03f) * 0.002f;
        }
        context->output[i] = x;
    }
    return true;
}

static int CompareDouble(const void* first, const void* second) {
    double a = *(const double*)first;
    double b = *(const double*)second;
    return (a > b) - (a < b);
}

typedef struct BackgroundLoad {
    atomic_bool stop;
} BackgroundLoad;

static void* BackgroundLoadThread(void* opaque) {
    BackgroundLoad* load = (BackgroundLoad*)opaque;
    volatile uint64_t state = 0x9e3779b97f4a7c15ULL;
    while (!atomic_load(&load->stop)) {
        for (int i = 0; i < 20000; ++i) state = state * 6364136223846793005ULL + 1ULL;
    }
    return NULL;
}

static double SortAndMedian(double* values, int count) {
    qsort(values, (size_t)count, sizeof(double), CompareDouble);
    return values[count / 2];
}

static double Checksum(const float* values, int count) {
    double sum = 0.0;
    for (int i = 0; i < count; ++i) sum += (double)values[i] * (double)(i + 1);
    return sum;
}

static int RunBenchmark(int backgroundThreads) {
    const int total = 288;
    const int balancedInner = 24000;
    const int imbalancedInner = 4000;
    const int repetitions = 5;
    const int threadOptions[] = {1, 2, 4, 8};

    float* input = (float*)malloc((size_t)total * sizeof(float));
    float* output = (float*)malloc((size_t)total * sizeof(float));
    float* reference = (float*)malloc((size_t)total * sizeof(float));
    if (!input || !output || !reference) {
        free(input);
        free(output);
        free(reference);
        fprintf(stderr, "benchmark: sin memoria\n");
        return 1;
    }
    for (int i = 0; i < total; ++i) input[i] = 0.25f + (float)(i % 31) * 0.125f;

    BackgroundLoad background;
    atomic_init(&background.stop, false);
    pthread_t backgroundThreadsStarted[LONG_HAIR_SCHEDULER_MAX_THREADS];
    int backgroundStarted = 0;
    for (int i = 0; i < backgroundThreads && i < LONG_HAIR_SCHEDULER_MAX_THREADS; ++i) {
        if (pthread_create(&backgroundThreadsStarted[backgroundStarted], NULL,
            BackgroundLoadThread, &background) == 0) {
            backgroundStarted++;
        }
    }

    printf("BENCH_HEADER nproc=%ld items=%d balanced_inner=%d imbalanced_inner=%d "
           "reps=%d background=%d\n",
        sysconf(_SC_NPROCESSORS_ONLN), total, balancedInner, imbalancedInner,
        repetitions, backgroundStarted);

    for (int imbalanced = 0; imbalanced <= 1; ++imbalanced) {
        int inner = imbalanced ? imbalancedInner : balancedInner;
        double referenceSeconds = 0.0;
        for (size_t option = 0; option < sizeof(threadOptions) / sizeof(threadOptions[0]); ++option) {
            int threads = threadOptions[option];
            LongHairSchedulerConfig config = LongHairScheduler_DefaultConfig();
            config.threadCount = threads;
            config.maxThreads = threads;
            LongHairScheduler* scheduler = LongHairScheduler_Create(&config);
            if (!scheduler) {
                printf("BENCH threads=%d imbalance=%d unavailable=1\n", threads, imbalanced);
                continue;
            }
            BenchContext context;
            context.total = total;
            context.inner = inner;
            context.imbalanced = imbalanced;
            context.input = input;
            context.output = output;

            /* Calentamiento. */
            if (LongHairScheduler_ParallelFor(scheduler, total, BenchWork, &context) !=
                LONG_HAIR_SCHEDULER_OK) {
                printf("BENCH threads=%d imbalance=%d failed=1\n", threads, imbalanced);
                LongHairScheduler_Destroy(scheduler);
                continue;
            }
            if (threads == 1) memcpy(reference, output, (size_t)total * sizeof(float));

            double samples[16];
            for (int repetition = 0; repetition < repetitions; ++repetition) {
                double start = NowSeconds();
                if (LongHairScheduler_ParallelFor(scheduler, total, BenchWork, &context) !=
                    LONG_HAIR_SCHEDULER_OK) {
                    printf("BENCH threads=%d imbalance=%d failed=1\n", threads, imbalanced);
                }
                samples[repetition] = NowSeconds() - start;
            }
            double medianSeconds = SortAndMedian(samples, repetitions);
            int p95Index = (int)(0.95 * (double)repetitions + 0.999999);
            if (p95Index >= repetitions) p95Index = repetitions - 1;
            double p95Seconds = samples[p95Index];
            double maxSeconds = samples[repetitions - 1];
            if (threads == 1) referenceSeconds = medianSeconds;
            int verified = memcmp(reference, output, (size_t)total * sizeof(float)) == 0;
            double speedup = medianSeconds > 0.0 ? referenceSeconds / medianSeconds : 0.0;
            printf("BENCH threads=%d imbalance=%d p50_ms=%.3f p95_ms=%.3f max_ms=%.3f "
                   "speedup=%.2f checksum=%.6f verified=%d\n",
                threads, imbalanced, medianSeconds * 1000.0, p95Seconds * 1000.0,
                maxSeconds * 1000.0, speedup, Checksum(output, total), verified);
            fflush(stdout);
            LongHairScheduler_Destroy(scheduler);
        }
    }

    if (backgroundStarted > 0) {
        atomic_store(&background.stop, true);
        for (int i = 0; i < backgroundStarted; ++i) {
            pthread_join(backgroundThreadsStarted[i], NULL);
        }
    }

    free(input);
    free(output);
    free(reference);
    return 0;
}

int main(int argc, char** argv) {
    if (argc > 1 && strcmp(argv[1], "--bench") == 0) return RunBenchmark(0);
    if (argc > 1 && strcmp(argv[1], "--bench-contention") == 0) return RunBenchmark(3);
    if (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        printf("uso: %s [--bench|--bench-contention|--help]\n", argv[0]);
        return 0;
    }
    run_long_hair_scheduler_tests();
    return 0;
}

#endif /* LONG_HAIR_SCHEDULER_STANDALONE */

/* ------------------------------------------------------------------------- */
/* Runner                                                                    */
/* ------------------------------------------------------------------------- */

void run_long_hair_scheduler_tests(void) {
    printf("[ RUN  ] long_hair_scheduler.DefaultAndSerial\n");
    TestDefaultAndSerial();
    printf("[ PASS ] long_hair_scheduler.DefaultAndSerial\n");

    printf("[ RUN  ] long_hair_scheduler.PartitionCoverageAndDeterminism\n");
    TestPartitionCoverageAndDeterminism();
    printf("[ PASS ] long_hair_scheduler.PartitionCoverageAndDeterminism\n");

    printf("[ RUN  ] long_hair_scheduler.ThreePhaseBarriers\n");
    TestThreePhaseBarriers();
    printf("[ PASS ] long_hair_scheduler.ThreePhaseBarriers\n");

    printf("[ RUN  ] long_hair_scheduler.FailureHandling\n");
    TestFailureHandling();
    printf("[ PASS ] long_hair_scheduler.FailureHandling\n");

    printf("[ RUN  ] long_hair_scheduler.InvalidArgumentsAndNesting\n");
    TestInvalidArgumentsAndNesting();
    printf("[ PASS ] long_hair_scheduler.InvalidArgumentsAndNesting\n");

    printf("[ RUN  ] long_hair_scheduler.TwoCallers\n");
    TestTwoCallers();
    printf("[ PASS ] long_hair_scheduler.TwoCallers\n");

    printf("[ RUN  ] long_hair_scheduler.DestroyWhileActive\n");
    TestDestroyWhileActive();
    printf("[ PASS ] long_hair_scheduler.DestroyWhileActive\n");

    printf("[ RUN  ] long_hair_scheduler.RepeatedLifecycle\n");
    TestRepeatedLifecycle();
    printf("[ PASS ] long_hair_scheduler.RepeatedLifecycle\n");

    printf("[ RUN  ] long_hair_scheduler.PersistentWorkers\n");
    TestPersistentWorkers();
    printf("[ PASS ] long_hair_scheduler.PersistentWorkers\n");

    printf("[ RUN  ] long_hair_scheduler.StatsReset\n");
    TestStatsReset();
    printf("[ PASS ] long_hair_scheduler.StatsReset\n");

    printf("[ RUN  ] long_hair_scheduler.IndependentSchedulers\n");
    TestIndependentSchedulers();
    printf("[ PASS ] long_hair_scheduler.IndependentSchedulers\n");

    printf("[ RUN  ] long_hair_scheduler.ImbalancedWorkload\n");
    TestImbalancedWorkload();
    printf("[ PASS ] long_hair_scheduler.ImbalancedWorkload\n");

    printf("[ PASS ] long_hair_scheduler: todas las pruebas\n");
}
