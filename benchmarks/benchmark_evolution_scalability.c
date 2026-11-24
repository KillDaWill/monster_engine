/**
 * @file benchmark_evolution_scalability.c
 * @brief Suite de benchmarks de escalabilidad para Evolution Arena (22..1000 criaturas).
 *
 * Separa inicialización, calentamiento y estado estacionario; distingue cargas
 * diagnósticas con población sostenida de experimentos evolutivos naturales.
 */
#include "EvolutionArena.h"
#include "EvolutionProfiler.h"
#include "EcosystemAnatomy.h"
#include "CreatureRecipeRegistry.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>

static double nowMs(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec * 1e-6;
}

static int cmpDouble(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static long currentPeakRssKib(void) {
    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru) == 0) return ru.ru_maxrss;
    return 0;
}

static EvolutionArenaConfig configForPopulation(unsigned pop, bool baselineCompat) {
    EvolutionArenaConfig cfg = EvolutionArenaConfig_Default();
    cfg.seed = 42u;
    cfg.controller = CREATURE_CONTROLLER_NEURAL;
    cfg.founderMode = EVOLUTION_FOUNDERS_SEEDED;
    if (pop <= 22u) {
        cfg.brumacernos = 16u;
        cfg.velkares = 6u;
        cfg.arenaSize = 40.0f;
        cfg.capacity = 128u;
        cfg.plants = 36u;
        cfg.ponds = 2u;
        cfg.obstacles = 5u;
    } else if (pop <= 50u) {
        cfg.brumacernos = 38u;
        cfg.velkares = 12u;
        cfg.arenaSize = 65.0f;
        cfg.capacity = 128u;
        cfg.plants = 80u;
        cfg.ponds = 4u;
        cfg.obstacles = 8u;
    } else if (pop <= 100u) {
        cfg.brumacernos = 76u;
        cfg.velkares = 24u;
        cfg.arenaSize = 90.0f;
        cfg.capacity = 256u;
        cfg.plants = 160u;
        cfg.ponds = 8u;
        cfg.obstacles = 12u;
    } else if (pop <= 250u) {
        cfg.brumacernos = 200u;
        cfg.velkares = 50u;
        cfg.arenaSize = 140.0f;
        cfg.capacity = 512u;
        cfg.plants = 400u;
        cfg.ponds = baselineCompat ? 16u : 20u;
        cfg.obstacles = 18u;
    } else if (pop <= 500u) {
        cfg.brumacernos = 400u;
        cfg.velkares = 100u;
        cfg.arenaSize = 200.0f;
        cfg.capacity = 1024u;
        cfg.plants = 800u;
        cfg.ponds = baselineCompat ? 16u : 40u;
        cfg.obstacles = 24u;
    } else {
        cfg.brumacernos = 800u;
        cfg.velkares = 200u;
        cfg.arenaSize = 280.0f;
        cfg.capacity = 2048u;
        cfg.plants = 1600u;
        cfg.ponds = baselineCompat ? 16u : 80u;
        cfg.obstacles = 32u;
    }
    return cfg;
}

/* Mantiene viva la población en cargas diagnósticas para medir N sostenido sin decaimiento. */
static void sustainDiagnosticPopulation(EcosystemWorld *w) {
    for (unsigned i = 0; i < w->capacity; ++i) {
        EcosystemEntity *e = &w->entities[i];
        if (!e->id) continue;
        if (e->physiology.health < 0.7f) e->physiology.health = 0.9f;
        if (e->physiology.energy < 0.45f) e->physiology.energy = 0.75f;
        if (e->physiology.hydration < 0.45f) e->physiology.hydration = 0.75f;
        e->physiology.cause = DEATH_NONE;
        if (e->physiology.stage == LIFE_DEAD) e->physiology.stage = LIFE_ADULT;
    }
}

static void runInferenceWorkload(unsigned netCount, unsigned stepsPerNet) {
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
    for (unsigned i = 0; i < netCount; ++i) NeuralBrain_Step(&brains[i], inputs, outputs);
    double t0 = nowMs();
    for (unsigned s = 0; s < stepsPerNet; ++s) {
        inputs[0] = (float)(s % 100) * 0.01f;
        for (unsigned i = 0; i < netCount; ++i) {
            NeuralBrain_Step(&brains[i], inputs, outputs);
            sink += outputs[0];
        }
    }
    double elapsedMs = nowMs() - t0;
    double totalEvals = (double)netCount * (double)stepsPerNet;
    double usPerEval = (elapsedMs * 1000.0) / totalEvals;
    printf("{\"workload\":\"inference\",\"networks\":%u,\"steps\":%u,\"us_per_eval\":%.4f,"
           "\"ms_per_batch\":%.4f,\"evals_per_sec\":%.0f,\"sink\":%.1f}\n",
           netCount, stepsPerNet, usPerEval, elapsedMs / (double)stepsPerNet,
           totalEvals / (elapsedMs * 1e-3), (double)sink * 0.0);
    for (unsigned i = 0; i < netCount; ++i) NeuralBrain_Destroy(&brains[i]);
    free(brains);
}

static void runSustainedWorkload(unsigned pop, unsigned warmupTicks, unsigned measureTicks,
                                 bool baselineCompat, unsigned workers) {
    EvolutionArenaConfig cfg = configForPopulation(pop, baselineCompat);
    cfg.workers = workers;
    EcosystemWorld w;
    char err[256] = {0};
    double tInit0 = nowMs();
    if (!EvolutionArena_Create(&w, &cfg, err, sizeof(err))) {
        printf("{\"workload\":\"sustained\",\"target_pop\":%u,\"supported\":false,\"error\":\"%s\"}\n",
               pop, err);
        return;
    }
    double initMs = nowMs() - tInit0;

    double tWarm0 = nowMs();
    for (unsigned t = 0; t < warmupTicks; ++t) {
        sustainDiagnosticPopulation(&w);
        EcosystemWorld_Step(&w);
    }
    double warmupMs = nowMs() - tWarm0;

    double *samples = calloc(measureTicks, sizeof(double));
    if (!samples) { EcosystemWorld_Free(&w); return; }
    double sumMs = 0.0;
    unsigned minPop = w.count, maxPop = w.count;
    double popSum = 0.0;
    for (unsigned t = 0; t < measureTicks; ++t) {
        sustainDiagnosticPopulation(&w);
        double t0 = nowMs();
        EcosystemWorld_Step(&w);
        double dt = nowMs() - t0;
        samples[t] = dt;
        sumMs += dt;
        if (w.count < minPop) minPop = w.count;
        if (w.count > maxPop) maxPop = w.count;
        popSum += (double)w.count;
    }
    qsort(samples, measureTicks, sizeof(double), cmpDouble);
    double meanMs = sumMs / (double)measureTicks;
    double p50 = samples[measureTicks / 2u];
    double p95 = samples[((measureTicks - 1u) * 95u) / 100u];
    double p99 = samples[((measureTicks - 1u) * 99u) / 100u];
    double maxMs = samples[measureTicks - 1u];
    double achievedSpeed = (1000.0 / 60.0) / fmax(meanMs, 1e-6);

    printf("{\"workload\":\"sustained\",\"target_pop\":%u,\"supported\":true,"
           "\"min_pop\":%u,\"mean_pop\":%.1f,\"max_pop\":%u,"
           "\"init_ms\":%.2f,\"warmup_ms\":%.2f,\"ticks\":%u,"
           "\"mean_tick_ms\":%.4f,\"p50_ms\":%.4f,\"p95_ms\":%.4f,\"p99_ms\":%.4f,\"max_ms\":%.4f,"
           "\"achieved_speed_x\":%.2f,\"peak_rss_kib\":%ld}\n",
           pop, minPop, popSum / (double)measureTicks, maxPop,
           initMs, warmupMs, measureTicks, meanMs, p50, p95, p99, maxMs,
           achievedSpeed, currentPeakRssKib());
    fflush(stdout);
    free(samples);
    EcosystemWorld_Free(&w);
}

static void runInteractionStressWorkload(unsigned pop, unsigned ticks, bool baselineCompat) {
    EvolutionArenaConfig cfg = configForPopulation(pop, baselineCompat);
    EcosystemWorld w;
    char err[256] = {0};
    if (!EvolutionArena_Create(&w, &cfg, err, sizeof(err))) {
        printf("{\"workload\":\"interaction_stress\",\"target_pop\":%u,\"supported\":false,\"error\":\"%s\"}\n",
               pop, err);
        return;
    }
    /* Forzar objetivos orales alcanzables activos para el 60% de la población y neutros para el 40%. */
    unsigned activeCount = 0;
    for (unsigned i = 0; i < w.capacity; ++i) {
        EcosystemEntity *e = &w.entities[i];
        if (!e->id) continue;
        e->physiology.energy = 0.35f;
        e->physiology.hydration = 0.35f;
        if ((i % 5u) < 3u) {
            Vector3 goal = e->body.mouth;
            goal.y = 0.04f;
            Vector3 dir = Vec3_Normalize(Vec3_Create(sinf(e->body.yaw), -1.0f, cosf(e->body.yaw)));
            e->mind.action = (i % 3u == 0u) ? ACTION_DRINK : ACTION_EAT;
            e->mind.intent.interact = true;
            e->mind.intent.interaction = (InteractionTarget){
                .valid = true, .objectId = 1u + (i % 4u),
                .type = (i % 3u == 0u) ? INTERACTION_DRINK : INTERACTION_EAT,
                .position = goal, .normal = {0, 1, 0}, .approach = dir,
                .positionTolerance = 0.09f, .orientationTolerance = 1.4f
            };
            ++activeCount;
        }
    }
    double *samples = calloc(ticks, sizeof(double));
    if (!samples) { EcosystemWorld_Free(&w); return; }
    double sumMs = 0.0;
    for (unsigned t = 0; t < ticks; ++t) {
        double t0 = nowMs();
        for (unsigned i = 0; i < w.capacity; ++i) {
            EcosystemEntity *e = &w.entities[i];
            if (!e->id || !e->anatomicalBody) continue;
            EcosystemAnatomy_Update(e, 1.0f / 60.0f);
        }
        double dt = nowMs() - t0;
        samples[t] = dt;
        sumMs += dt;
    }
    qsort(samples, ticks, sizeof(double), cmpDouble);
    printf("{\"workload\":\"interaction_stress\",\"target_pop\":%u,\"active_oral\":%u,\"ticks\":%u,"
           "\"mean_ms\":%.4f,\"p50_ms\":%.4f,\"p95_ms\":%.4f,\"p99_ms\":%.4f,\"max_ms\":%.4f,"
           "\"peak_rss_kib\":%ld}\n",
           pop, activeCount, ticks, sumMs / (double)ticks,
           samples[ticks / 2u], samples[((ticks - 1u) * 95u) / 100u],
           samples[((ticks - 1u) * 99u) / 100u], samples[ticks - 1u], currentPeakRssKib());
    fflush(stdout);
    free(samples);
    EcosystemWorld_Free(&w);
}

static void runJuvenileGrowthWorkload(unsigned pop, unsigned ticks, bool baselineCompat, unsigned workers) {
    EvolutionArenaConfig cfg = configForPopulation(pop, baselineCompat);
    cfg.workers = workers;
    EcosystemWorld w;
    char err[256] = {0};
    if (!EvolutionArena_Create(&w, &cfg, err, sizeof(err))) {
        printf("{\"workload\":\"juvenile_growth\",\"target_pop\":%u,\"supported\":false,\"error\":\"%s\"}\n",
               pop, err);
        return;
    }
    /* Convertir toda la población en juveniles con edades escalonadas para disparar crecimiento continuo. */
    for (unsigned i = 0; i < w.capacity; ++i) {
        EcosystemEntity *e = &w.entities[i];
        if (!e->id) continue;
        e->physiology.age = (double)(i % 60u) * 0.45;
        e->physiology.stage = LIFE_JUVENILE;
        EcosystemAnatomy_Configure(e, &w.species[e->speciesIndex]);
    }
    double *samples = calloc(ticks, sizeof(double));
    if (!samples) { EcosystemWorld_Free(&w); return; }
    double sumMs = 0.0;
    for (unsigned t = 0; t < ticks; ++t) {
        sustainDiagnosticPopulation(&w);
        double t0 = nowMs();
        EcosystemWorld_Step(&w);
        double dt = nowMs() - t0;
        samples[t] = dt;
        sumMs += dt;
    }
    qsort(samples, ticks, sizeof(double), cmpDouble);
    printf("{\"workload\":\"juvenile_growth\",\"target_pop\":%u,\"final_pop\":%u,\"ticks\":%u,"
           "\"mean_tick_ms\":%.4f,\"p50_ms\":%.4f,\"p95_ms\":%.4f,\"p99_ms\":%.4f,\"max_ms\":%.4f,"
           "\"peak_rss_kib\":%ld}\n",
           pop, w.count, ticks, sumMs / (double)ticks,
           samples[ticks / 2u], samples[((ticks - 1u) * 95u) / 100u],
           samples[((ticks - 1u) * 99u) / 100u], samples[ticks - 1u], currentPeakRssKib());
    fflush(stdout);
    free(samples);
    EcosystemWorld_Free(&w);
}

static void runNaturalWorkload(unsigned pop, unsigned ticks, bool baselineCompat, unsigned workers) {
    EvolutionArenaConfig cfg = configForPopulation(pop, baselineCompat);
    cfg.workers = workers;
    EcosystemWorld w;
    char err[256] = {0};
    if (!EvolutionArena_Create(&w, &cfg, err, sizeof(err))) {
        printf("{\"workload\":\"natural\",\"target_pop\":%u,\"supported\":false,\"error\":\"%s\"}\n",
               pop, err);
        return;
    }
    long rssStart = currentPeakRssKib();
    double *samples = calloc(ticks, sizeof(double));
    if (!samples) { EcosystemWorld_Free(&w); return; }
    double sumMs = 0.0, popSum = 0.0;
    unsigned minPop = w.count, maxPop = w.count;
    for (unsigned t = 0; t < ticks; ++t) {
        double t0 = nowMs();
        EcosystemWorld_Step(&w);
        double dt = nowMs() - t0;
        samples[t] = dt;
        sumMs += dt;
        if (w.count < minPop) minPop = w.count;
        if (w.count > maxPop) maxPop = w.count;
        popSum += (double)w.count;
    }
    qsort(samples, ticks, sizeof(double), cmpDouble);
    printf("{\"workload\":\"natural\",\"initial_pop\":%u,\"min_pop\":%u,\"mean_pop\":%.1f,"
           "\"max_pop\":%u,\"final_pop\":%u,\"births\":%llu,\"deaths\":%llu,\"ticks\":%u,"
           "\"mean_tick_ms\":%.4f,\"p50_ms\":%.4f,\"p95_ms\":%.4f,\"p99_ms\":%.4f,\"max_ms\":%.4f,"
           "\"rss_start_kib\":%ld,\"peak_rss_kib\":%ld}\n",
           pop, minPop, popSum / (double)ticks, maxPop, w.count,
           (unsigned long long)w.statistics.births, (unsigned long long)w.statistics.deaths,
           ticks, sumMs / (double)ticks,
           samples[ticks / 2u], samples[((ticks - 1u) * 95u) / 100u],
           samples[((ticks - 1u) * 99u) / 100u], samples[ticks - 1u], rssStart, currentPeakRssKib());
    fflush(stdout);
    free(samples);
    EcosystemWorld_Free(&w);
}

static void runCellSizeSweep(unsigned pop, unsigned ticks, unsigned workers) {
    const float cells[3] = {2.0f, 4.0f, 8.0f};
    for (unsigned c = 0; c < 3u; ++c) {
        EvolutionArenaConfig cfg = configForPopulation(pop, false);
        cfg.workers = workers;
        EcosystemWorld w;
        char err[256] = {0};
        if (!EvolutionArena_Create(&w, &cfg, err, sizeof(err))) continue;
        AIWorld_SetSpatialCellSize(&w.observations, cells[c]);
        for (unsigned t = 0; t < 30u; ++t) { sustainDiagnosticPopulation(&w); EcosystemWorld_Step(&w); }
        double *samples = calloc(ticks, sizeof(double));
        double sumMs = 0.0;
        for (unsigned t = 0; t < ticks; ++t) {
            sustainDiagnosticPopulation(&w);
            double t0 = nowMs();
            EcosystemWorld_Step(&w);
            double dt = nowMs() - t0;
            samples[t] = dt; sumMs += dt;
        }
        qsort(samples, ticks, sizeof(double), cmpDouble);
        printf("{\"workload\":\"cell_sweep\",\"pop\":%u,\"cell_size\":%.1f,\"workers\":%u,\"ticks\":%u,"
               "\"mean_tick_ms\":%.4f,\"p95_ms\":%.4f,\"p99_ms\":%.4f}\n",
               pop, cells[c], EcosystemWorld_WorkerCount(&w), ticks,
               sumMs / (double)ticks, samples[((ticks - 1u) * 95u) / 100u], samples[((ticks - 1u) * 99u) / 100u]);
        fflush(stdout);
        free(samples);
        EcosystemWorld_Free(&w);
    }
}

static void runWorkerScaling(unsigned pop, unsigned ticks) {
    const unsigned ws[5] = {1u, 2u, 4u, 8u, 0u};
    for (unsigned i = 0; i < 5u; ++i) {
        EvolutionArenaConfig cfg = configForPopulation(pop, false);
        cfg.workers = ws[i];
        EcosystemWorld w;
        char err[256] = {0};
        if (!EvolutionArena_Create(&w, &cfg, err, sizeof(err))) continue;
        for (unsigned t = 0; t < 30u; ++t) { sustainDiagnosticPopulation(&w); EcosystemWorld_Step(&w); }
        double *samples = calloc(ticks, sizeof(double));
        double sumMs = 0.0;
        for (unsigned t = 0; t < ticks; ++t) {
            sustainDiagnosticPopulation(&w);
            double t0 = nowMs();
            EcosystemWorld_Step(&w);
            double dt = nowMs() - t0;
            samples[t] = dt; sumMs += dt;
        }
        qsort(samples, ticks, sizeof(double), cmpDouble);
        double meanMs = sumMs / (double)ticks;
        printf("{\"workload\":\"worker_scaling\",\"pop\":%u,\"requested_workers\":%u,\"actual_workers\":%u,"
               "\"ticks\":%u,\"mean_tick_ms\":%.4f,\"p95_ms\":%.4f,\"p99_ms\":%.4f,\"achieved_speed_x\":%.2f}\n",
               pop, ws[i], EcosystemWorld_WorkerCount(&w), ticks, meanMs,
               samples[((ticks - 1u) * 95u) / 100u], samples[((ticks - 1u) * 99u) / 100u],
               (1000.0 / 60.0) / fmax(meanMs, 1e-6));
        fflush(stdout);
        free(samples);
        EcosystemWorld_Free(&w);
    }
}

static void runPondCongestionWorkload(unsigned ticks) {
    EvolutionArenaConfig cfg = EvolutionArenaConfig_Default();
    cfg.seed = 42u;
    cfg.arenaSize = 48.0f;
    cfg.brumacernos = 40u;
    cfg.velkares = 0u;
    cfg.ponds = 1u;
    cfg.plants = 20u;
    cfg.obstacles = 2u;
    cfg.capacity = 64u;
    cfg.controller = CREATURE_CONTROLLER_UTILITY; /* Forzar búsqueda directa de agua para medir congestión física. */
    EcosystemWorld w;
    char err[256] = {0};
    if (!EvolutionArena_Create(&w, &cfg, err, sizeof(err))) return;
    /* Poner sed inicial alta a los 40 individuos para generar congestión simultánea en el estanque único. */
    bool drank[64] = {false};
    double waitSum = 0.0;
    unsigned drankCount = 0u, reachedTicks = 0u;
    for (unsigned k = 0; k < w.count; ++k) {
        EcosystemEntity *e = &w.entities[w.activeEntitySlots[k]];
        e->physiology.hydration = 0.22f;
        e->physiology.energy = 0.90f;
    }
    double t0 = nowMs();
    for (unsigned t = 0; t < ticks; ++t) {
        EcosystemWorld_Step(&w);
        for (unsigned k = 0; k < w.count; ++k) {
            unsigned s = w.activeEntitySlots[k];
            const EcosystemEntity *e = &w.entities[s];
            if (e->mind.action == ACTION_DRINK && e->body.reach.status == REACH_REACHED) {
                ++reachedTicks;
                if (s < 64u && !drank[s]) {
                    drank[s] = true;
                    ++drankCount;
                    waitSum += w.clock.seconds;
                }
            }
        }
    }
    double elapsedMs = nowMs() - t0;
    printf("{\"workload\":\"pond_congestion\",\"creatures\":40,\"ponds\":1,\"ticks\":%u,"
           "\"unique_drinkers\":%u,\"drink_contact_ticks\":%u,\"mean_wait_seconds\":%.2f,"
           "\"dehydration_deaths\":%llu,\"mean_tick_ms\":%.4f}\n",
           ticks, drankCount, reachedTicks, drankCount ? waitSum / (double)drankCount : 0.0,
           (unsigned long long)w.statistics.deathCauses[DEATH_DEHYDRATION],
           elapsedMs / (double)ticks);
    fflush(stdout);
    EcosystemWorld_Free(&w);
}

int main(int argc, char **argv) {
    bool baselineCompat = false;
    bool quick = false;
    unsigned workers = 0u;
    const char *onlyWorkload = NULL;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--baseline-compat") == 0) baselineCompat = true;
        else if (strcmp(argv[i], "--quick") == 0) quick = true;
        else if (strncmp(argv[i], "--workers=", 10) == 0) workers = (unsigned)strtoul(argv[i] + 10, NULL, 10);
        else if (strncmp(argv[i], "--workload=", 11) == 0) onlyWorkload = argv[i] + 11;
    }

    char err[256] = {0};
    CreatureRecipeRegistry_LoadDirectory("recipes", err, sizeof(err));

    if (!onlyWorkload || strcmp(onlyWorkload, "inference") == 0) {
        runInferenceWorkload(100u, 2000u);
        runInferenceWorkload(1000u, 500u);
    }

    const unsigned pops[] = {22u, 50u, 100u, 250u, 500u, 1000u};
    const unsigned nPops = (unsigned)(sizeof(pops) / sizeof(pops[0]));

    if (!onlyWorkload || strcmp(onlyWorkload, "sustained") == 0) {
        for (unsigned i = 0; i < nPops; ++i) {
            unsigned p = pops[i];
            unsigned warmup = quick ? 20u : 60u;
            unsigned ticks = quick ? (p >= 500u ? 60u : 120u) : (p >= 500u ? 300u : 600u);
            runSustainedWorkload(p, warmup, ticks, baselineCompat, workers);
        }
    }

    if (!onlyWorkload || strcmp(onlyWorkload, "interaction") == 0) {
        unsigned stressPops[] = {100u, 500u};
        for (unsigned i = 0; i < 2u; ++i) {
            runInteractionStressWorkload(stressPops[i], quick ? 60u : 180u, baselineCompat);
        }
    }

    if (!onlyWorkload || strcmp(onlyWorkload, "juvenile") == 0) {
        unsigned juvPops[] = {100u, 500u};
        for (unsigned i = 0; i < 2u; ++i) {
            runJuvenileGrowthWorkload(juvPops[i], quick ? 60u : 180u, baselineCompat, workers);
        }
    }

    if (!onlyWorkload || strcmp(onlyWorkload, "natural") == 0) {
        runNaturalWorkload(500u, quick ? 120u : 1200u, baselineCompat, workers);
    }

    if (!onlyWorkload || strcmp(onlyWorkload, "cells") == 0) {
        runCellSizeSweep(500u, quick ? 90u : 240u, workers);
    }

    if (!onlyWorkload || strcmp(onlyWorkload, "workers") == 0) {
        runWorkerScaling(500u, quick ? 90u : 240u);
    }

    if (!onlyWorkload || strcmp(onlyWorkload, "congestion") == 0) {
        runPondCongestionWorkload(quick ? 600u : 1200u);
    }

    return 0;
}
