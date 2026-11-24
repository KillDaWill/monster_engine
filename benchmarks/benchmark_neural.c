/**
 * @file benchmark_neural.c
 * @brief Medición aislada de inferencia RNN (100 y 1000 redes) y arena completa a varias poblaciones.
 */
#include "EvolutionArena.h"
#include "CreatureRecipeRegistry.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>

static double nowSeconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static int cmpDouble(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static void benchmarkInference(unsigned netCount, unsigned stepsPerNet) {
    NeuralBrain *brains = calloc(netCount, sizeof(*brains));
    if (!brains) return;
    uint32_t rng = 42u + netCount;
    for (unsigned i = 0; i < netCount; ++i) {
        NeuralGenome g;
        NeuralGenome_InitRandom(&g, &rng, 0.8f, 5.0f, i + 1u);
        NeuralBrain_Init(&brains[i], &g);
        NeuralGenome_Destroy(&g);
    }

    float inputs[NEURAL_RNN_INPUTS];
    for (unsigned i = 0; i < NEURAL_RNN_INPUTS; ++i) inputs[i] = sinf((float)(i + 1) * 0.25f);
    float outputs[NEURAL_RNN_OUTPUTS];
    volatile float sink = 0.0f;

    /* Calentamiento */
    for (unsigned i = 0; i < netCount; ++i) NeuralBrain_Step(&brains[i], inputs, outputs);

    double t0 = nowSeconds();
    for (unsigned s = 0; s < stepsPerNet; ++s) {
        inputs[0] = (float)(s % 100) * 0.01f;
        for (unsigned i = 0; i < netCount; ++i) {
            NeuralBrain_Step(&brains[i], inputs, outputs);
            sink += outputs[0];
        }
    }
    double elapsed = nowSeconds() - t0;
    double totalEvals = (double)netCount * (double)stepsPerNet;
    double usPerEval = elapsed * 1e6 / totalEvals;
    double msPerBatch = elapsed * 1e3 / (double)stepsPerNet;
    printf("{\"benchmark\":\"rnn_inference\",\"networks\":%u,\"steps\":%u,\"total_evals\":%.0f,"
           "\"us_per_eval\":%.4f,\"ms_per_batch\":%.4f,\"evals_per_sec\":%.0f,\"sink\":%.3f}\n",
           netCount, stepsPerNet, totalEvals, usPerEval, msPerBatch, totalEvals / elapsed, (double)sink * 0.0);

    for (unsigned i = 0; i < netCount; ++i) NeuralBrain_Destroy(&brains[i]);
    free(brains);
}

static void benchmarkArena(unsigned brum, unsigned velk, unsigned ticks) {
    EvolutionArenaConfig cfg = EvolutionArenaConfig_Default();
    cfg.seed = 42u;
    cfg.brumacernos = brum;
    cfg.velkares = velk;
    cfg.capacity = (brum + velk) * 2u;
    if (cfg.capacity < 128u) cfg.capacity = 128u;
    cfg.plants = 36u;
    cfg.obstacles = 5u;
    cfg.controller = CREATURE_CONTROLLER_NEURAL;
    cfg.founderMode = EVOLUTION_FOUNDERS_SEEDED;

    char err[256] = {0};
    EcosystemWorld w;
    if (!EvolutionArena_Create(&w, &cfg, err, sizeof(err))) {
        fprintf(stderr, "Error creando arena (%u+%u): %s\n", brum, velk, err);
        return;
    }

    double *samples = calloc(ticks, sizeof(double));
    if (!samples) { EcosystemWorld_Free(&w); return; }

    double sumMs = 0.0;
    for (unsigned t = 0; t < ticks; ++t) {
        double s0 = nowSeconds();
        EcosystemWorld_Step(&w);
        double dtMs = (nowSeconds() - s0) * 1000.0;
        samples[t] = dtMs;
        sumMs += dtMs;
    }
    qsort(samples, ticks, sizeof(double), cmpDouble);
    double meanMs = sumMs / (double)ticks;
    double maxRealSpeed = (1000.0 / 60.0) / fmax(meanMs, 1e-6);

    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    printf("{\"benchmark\":\"evolution_arena\",\"initial_population\":%u,\"final_population\":%u,"
           "\"ticks\":%u,\"mean_tick_ms\":%.4f,\"p50_ms\":%.4f,\"p95_ms\":%.4f,\"p99_ms\":%.4f,"
           "\"max_ms\":%.4f,\"effective_max_speed_x\":%.1f,\"peak_rss_kib\":%ld}\n",
           brum + velk, w.count, ticks, meanMs,
           samples[ticks / 2], samples[(ticks - 1) * 95 / 100],
           samples[(ticks - 1) * 99 / 100], samples[ticks - 1],
           maxRealSpeed, ru.ru_maxrss);

    free(samples);
    EcosystemWorld_Free(&w);
}

int main(void) {
    char err[256] = {0};
    CreatureRecipeRegistry_LoadDirectory("recipes", err, sizeof(err));
    benchmarkInference(100u, 2000u);
    benchmarkInference(1000u, 500u);
    benchmarkArena(16u, 6u, 3600u);
    benchmarkArena(38u, 12u, 2400u);
    benchmarkArena(76u, 24u, 1200u);
    return 0;
}
