/**
 * @file benchmark_long_hair_scheduler.c
 * @brief Benchmark independiente del dispatch y de un compute sintético del scheduler.
 *
 * Compilar con LongHairScheduler.c y SDFSamplingPool.c. No enlaza el runtime de pelo,
 * no lee/escribe un SDF y no cambia el Makefile. Las mediciones son sólo del scheduler
 * con estas cargas sintéticas; no predicen el speedup de la construcción real de pelo.
 */

#define _GNU_SOURCE

#include "LongHairScheduler.h"
#include "SDFSamplingPool.h"

#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

#define ITEM_COUNT 288
#define COMPUTE_ITERATIONS 6000
#define WARMUP_SAMPLES 3
#define MEASURED_SAMPLES 21
#define THREAD_OPTION_COUNT 4

typedef enum WorkloadKind {
    WORKLOAD_DISPATCH = 0,
    WORKLOAD_COMPUTE = 1
} WorkloadKind;

typedef struct WorkContext {
    const float* input;
    float* output;
    int count;
    int iterations;
    WorkloadKind kind;
} WorkContext;

/* Layout mirror used only for a source-derived estimate of SDFSamplingPool metadata. */
typedef struct WorkerStateEstimate {
    SDFSamplingPool* pool;
    int threadIndex;
    pthread_t thread;
    int startIndex;
    int endIndex;
} WorkerStateEstimate;

typedef struct PoolMetadataEstimate {
    int totalThreadCount;
    int workerThreadCount;
    WorkerStateEstimate workers[SDF_SAMPLING_POOL_MAX_THREADS];
    pthread_mutex_t lock;
    pthread_cond_t startCond;
    pthread_cond_t doneCond;
    bool shouldQuit;
    uint64_t jobGeneration;
    int completedWorkers;
    SDFSamplingWorkFn currentWorkFn;
    void* currentContext;
} PoolMetadataEstimate;

static bool RunWork(void* opaque, int start, int end, int threadIndex) {
    WorkContext* context = (WorkContext*)opaque;
    (void)threadIndex;
    if (start < 0 || end < start || end > context->count) return false;

    if (context->kind == WORKLOAD_DISPATCH) {
        for (int i = start; i < end; ++i) {
            context->output[i] = context->input[i];
        }
        return true;
    }

    for (int i = start; i < end; ++i) {
        float value = context->input[i];
        for (int iteration = 0; iteration < context->iterations; ++iteration) {
            value = sinf(value * 0.0013f + 0.17f) * 0.998f +
                cosf(value * 0.0007f + 0.03f) * 0.002f;
        }
        context->output[i] = value;
    }
    return true;
}

static double NowSeconds(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC_RAW, &now);
    return (double)now.tv_sec + (double)now.tv_nsec * 1e-9;
}

static int CompareDouble(const void* first, const void* second) {
    double a = *(const double*)first;
    double b = *(const double*)second;
    return (a > b) - (a < b);
}

static double PercentileNearestRank(const double* sorted, int count, double percentile) {
    int rank = (int)ceil(percentile * (double)count);
    if (rank < 1) rank = 1;
    if (rank > count) rank = count;
    return sorted[rank - 1];
}

static uint64_t OutputHash(const float* values, int count) {
    uint64_t hash = UINT64_C(14695981039346656037);
    for (int i = 0; i < count; ++i) {
        uint32_t bits;
        memcpy(&bits, &values[i], sizeof(bits));
        for (int byte = 0; byte < 4; ++byte) {
            hash ^= (uint8_t)(bits >> (byte * 8));
            hash *= UINT64_C(1099511628211);
        }
    }
    return hash;
}

static double OutputChecksum(const float* values, int count) {
    double checksum = 0.0;
    for (int i = 0; i < count; ++i) {
        checksum += (double)values[i] * (double)(i + 1);
    }
    return checksum;
}

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

static void PrintCpuModel(void) {
    FILE* cpuinfo = fopen("/proc/cpuinfo", "r");
    if (!cpuinfo) {
        printf("ENV,cpu_model,unavailable\n");
        return;
    }
    char line[512];
    while (fgets(line, sizeof(line), cpuinfo)) {
        if (strncmp(line, "model name", 10) == 0) {
            char* colon = strchr(line, ':');
            if (colon) {
                char* value = colon + 1;
                while (*value == ' ' || *value == '\t') value++;
                value[strcspn(value, "\r\n")] = '\0';
                printf("ENV,cpu_model,%s\n", value);
                fclose(cpuinfo);
                return;
            }
        }
    }
    fclose(cpuinfo);
    printf("ENV,cpu_model,unavailable\n");
}

static void PrintEnvironment(void) {
    struct utsname systemInfo;
    struct timespec realtime;
    char timestamp[64] = "unavailable";
    double loads[3] = {-1.0, -1.0, -1.0};

    clock_gettime(CLOCK_REALTIME, &realtime);
    struct tm utc;
    if (gmtime_r(&realtime.tv_sec, &utc)) {
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &utc);
    }
    (void)getloadavg(loads, 3);

    printf("ENV,utc,%s\n", timestamp);
    printf("ENV,git_sha,%s\n", LONG_HAIR_BENCH_GIT_SHA);
    printf("ENV,compiler,%s\n", __VERSION__);
    printf("ENV,flags,%s\n", LONG_HAIR_BENCH_CFLAGS);
    if (uname(&systemInfo) == 0) {
        printf("ENV,kernel,%s %s %s\n", systemInfo.sysname, systemInfo.release,
            systemInfo.machine);
    }
    printf("ENV,logical_processors,%ld\n", sysconf(_SC_NPROCESSORS_ONLN));
    printf("ENV,loadavg_1_5_15,%.2f/%.2f/%.2f\n", loads[0], loads[1], loads[2]);
    PrintCpuModel();
    printf("ENV,measurement_clock,CLOCK_MONOTONIC_RAW\n");
    printf("ENV,work_items,%d\n", ITEM_COUNT);
    printf("ENV,warmup_samples,%d\n", WARMUP_SAMPLES);
    printf("ENV,measured_samples,%d\n", MEASURED_SAMPLES);
    printf("ENV,percentile_method,nearest_rank\n");
    printf("ENV,compute_iterations_per_item,%d\n", COMPUTE_ITERATIONS);
    printf("ENV,thread_model,participants_include_calling_thread\n");
    printf("ENV,contention,none_intentionally_injected; ambient_load_recorded\n");
    printf("SAMPLE_HEADER,workload,requested,effective,index,elapsed_us,exact_parity\n");
    printf("RESULT_HEADER,workload,requested,effective,extra_workers,"
        "process_threads_before,process_threads_after_create,default_stack_bytes,"
        "estimated_pool_metadata_bytes,estimated_total_extra_bytes,p50_us,p95_us,max_us,"
        "checksum,fnv1a64_output_hash,exact_parity\n");
}

static void InitializeInput(float* input) {
    for (int i = 0; i < ITEM_COUNT; ++i) {
        input[i] = 0.25f + (float)(i % 31) * 0.125f;
    }
}

static int RunWorkload(WorkloadKind kind, int requested, const float* input,
    const float* reference, uint64_t referenceHash, double referenceChecksum) {
    int beforeThreads = CountProcessThreads();
    LongHairSchedulerConfig config = LongHairScheduler_DefaultConfig();
    config.threadCount = requested;
    config.maxThreads = requested;
    LongHairScheduler* scheduler = LongHairScheduler_Create(&config);
    if (!scheduler) {
        fprintf(stderr, "ERROR,scheduler_create,requested=%d\n", requested);
        return 1;
    }

    int effective = LongHairScheduler_GetThreadCount(scheduler);
    int afterThreads = CountProcessThreads();
    pthread_attr_t attributes;
    size_t stackBytes = 0;
    if (pthread_attr_init(&attributes) == 0) {
        (void)pthread_attr_getstacksize(&attributes, &stackBytes);
        pthread_attr_destroy(&attributes);
    }

    float output[ITEM_COUNT];
    memset(output, 0, sizeof(output));
    WorkContext context = {
        input, output, ITEM_COUNT,
        kind == WORKLOAD_COMPUTE ? COMPUTE_ITERATIONS : 0,
        kind
    };

    for (int warmup = 0; warmup < WARMUP_SAMPLES; ++warmup) {
        LongHairSchedulerStatus status = LongHairScheduler_ParallelFor(scheduler,
            ITEM_COUNT, RunWork, &context);
        if (status != LONG_HAIR_SCHEDULER_OK) {
            fprintf(stderr, "ERROR,warmup,status=%s\n",
                LongHairScheduler_StatusName(status));
            LongHairScheduler_Destroy(scheduler);
            return 1;
        }
    }

    double samples[MEASURED_SAMPLES];
    bool sampleParity[MEASURED_SAMPLES];
    bool allSamplesMatch = true;
    const char* workloadName = kind == WORKLOAD_DISPATCH ? "dispatch_copy" : "readonly_compute";
    for (int sample = 0; sample < MEASURED_SAMPLES; ++sample) {
        double start = NowSeconds();
        LongHairSchedulerStatus status = LongHairScheduler_ParallelFor(scheduler,
            ITEM_COUNT, RunWork, &context);
        double elapsed = NowSeconds() - start;
        if (status != LONG_HAIR_SCHEDULER_OK) {
            fprintf(stderr, "ERROR,measured,status=%s\n",
                LongHairScheduler_StatusName(status));
            LongHairScheduler_Destroy(scheduler);
            return 1;
        }
        samples[sample] = elapsed * 1e6;
        /* Validar fuera del intervalo cronometrado y conservar cada muestra. */
        sampleParity[sample] = memcmp(output, reference, sizeof(output)) == 0;
        allSamplesMatch = allSamplesMatch && sampleParity[sample];
    }
    for (int sample = 0; sample < MEASURED_SAMPLES; ++sample) {
        printf("SAMPLE,%s,%d,%d,%d,%.6f,%d\n", workloadName, requested,
            effective, sample, samples[sample], sampleParity[sample] ? 1 : 0);
    }
    qsort(samples, MEASURED_SAMPLES, sizeof(samples[0]), CompareDouble);

    uint64_t hash = OutputHash(output, ITEM_COUNT);
    double checksum = OutputChecksum(output, ITEM_COUNT);
    bool parity = allSamplesMatch && memcmp(output, reference, sizeof(output)) == 0 &&
        hash == referenceHash && checksum == referenceChecksum;
    /* El fallback serial no crea SDFSamplingPool. No incluye el scheduler ni RSS. */
    size_t metadataBytes = effective > 1 ? sizeof(PoolMetadataEstimate) : 0;
    size_t estimatedExtraBytes = metadataBytes +
        (size_t)(effective > 0 ? effective - 1 : 0) * stackBytes;

    printf("RESULT,%s,%d,%d,%d,%d,%d,%zu,%zu,%zu,%.3f,%.3f,%.3f,"
        "%.9f,%016llx,%d\n",
        workloadName, requested, effective, effective - 1, beforeThreads, afterThreads,
        stackBytes, metadataBytes, estimatedExtraBytes,
        PercentileNearestRank(samples, MEASURED_SAMPLES, 0.50),
        PercentileNearestRank(samples, MEASURED_SAMPLES, 0.95),
        samples[MEASURED_SAMPLES - 1], checksum,
        (unsigned long long)hash, parity ? 1 : 0);
    fflush(stdout);

    LongHairScheduler_Destroy(scheduler);
    return parity ? 0 : 2;
}

int main(void) {
    static const int threadOptions[THREAD_OPTION_COUNT] = {1, 2, 4, 8};
    float input[ITEM_COUNT];
    float reference[ITEM_COUNT];
    InitializeInput(input);

    PrintEnvironment();
    int result = 0;
    uint64_t hashes[2] = {0, 0};
    double checksums[2] = {0.0, 0.0};

    for (int workload = 0; workload < 2; ++workload) {
        WorkloadKind kind = (WorkloadKind)workload;
        WorkContext referenceContext = {
            input, reference, ITEM_COUNT,
            kind == WORKLOAD_COMPUTE ? COMPUTE_ITERATIONS : 0,
            kind
        };
        LongHairSchedulerConfig serialConfig = LongHairScheduler_DefaultConfig();
        serialConfig.threadCount = 1;
        serialConfig.maxThreads = 1;
        LongHairScheduler* serial = LongHairScheduler_Create(&serialConfig);
        if (!serial) {
            fprintf(stderr, "ERROR,serial_scheduler_create\n");
            return 1;
        }
        if (LongHairScheduler_ParallelFor(serial, ITEM_COUNT, RunWork,
            &referenceContext) != LONG_HAIR_SCHEDULER_OK) {
            fprintf(stderr, "ERROR,serial_reference_job\n");
            LongHairScheduler_Destroy(serial);
            return 1;
        }
        hashes[workload] = OutputHash(reference, ITEM_COUNT);
        checksums[workload] = OutputChecksum(reference, ITEM_COUNT);
        LongHairScheduler_Destroy(serial);

        for (int option = 0; option < THREAD_OPTION_COUNT; ++option) {
            result |= RunWorkload(kind, threadOptions[option], input, reference,
                hashes[workload], checksums[workload]);
        }
    }

    printf("SUMMARY,exact_parity_all=%d\n", result == 0 ? 1 : 0);
    return result == 0 ? 0 : 1;
}
