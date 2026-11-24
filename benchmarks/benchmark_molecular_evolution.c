/**
 * @file benchmark_molecular_evolution.c
 * @brief Benchmark comparativo de rendimiento (Secciones 21 y 41):
 *        1) Legado v1 vs Molecular v2 sostenido a 22, 100, 500 y 1000 criaturas.
 *        2) Desglose de cargas metabólicas (tracto vacío, digestión activa, sustancias mixtas + descomposición).
 *        3) Escalado de trabajadores de Brain Evolution Lab (1, 2, 4, 8 y auto) + verificación de determinismo.
 */
#include "BrainEvolutionLab.h"
#include "CreatureRecipeRegistry.h"
#include "EvolutionArena.h"
#include "EvolutionEvaluator.h"
#include "EvolutionProfiler.h"
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

static double peakRssMb(void) {
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    return (double)ru.ru_maxrss / 1024.0;
}

static void runWorldScaleCase(const char *label, EcosystemMetabolismMode mode,
                              unsigned brum, unsigned velk, float arenaSize,
                              unsigned plants, unsigned ponds, unsigned ticks,
                              bool mixed, bool prefillStomach) {
    EvolutionArenaConfig cfg = (mode == ECOSYSTEM_METABOLISM_MOLECULAR)
        ? EvolutionArenaConfig_DefaultMolecular()
        : EvolutionArenaConfig_Default();
    cfg.seed = 42u;
    cfg.brumacernos = brum;
    cfg.velkares = velk;
    cfg.capacity = (brum + velk) * 2u;
    cfg.arenaSize = arenaSize;
    cfg.plants = plants;
    cfg.ponds = ponds;
    cfg.obstacles = (brum + velk >= 500u) ? 24u : 8u;
    cfg.workers = 0u; /* auto */
    cfg.includeMixedSubstances = mixed;

    EcosystemWorld w;
    char err[256] = {0};
    if (!EvolutionArena_Create(&w, &cfg, err, sizeof(err))) {
        fprintf(stderr, "Error creando %s: %s\n", label, err);
        return;
    }

    EvolutionProfiler prof;
    EvolutionProfiler_Init(&prof, true);
    EcosystemWorld_SetProfiler(&w, &prof);

    if (prefillStomach && mode == ECOSYSTEM_METABOLISM_MOLECULAR && w.metabolismDb) {
        SubstanceHandle hGrass = SubstanceRegistry_Find(&w.metabolismDb->substances, "grass");
        SubstanceHandle hRotten = SubstanceRegistry_Find(&w.metabolismDb->substances, "rotten_meat");
        SubstanceHandle hWater = SubstanceRegistry_Find(&w.metabolismDb->substances, "contaminated_water");
        const SubstanceDef *grass = SubstanceRegistry_Get(&w.metabolismDb->substances, hGrass);
        const SubstanceDef *rotten = SubstanceRegistry_Get(&w.metabolismDb->substances, hRotten);
        const SubstanceDef *water = SubstanceRegistry_Get(&w.metabolismDb->substances, hWater);
        for (unsigned i = 0; i < w.count; ++i) {
            EcosystemEntity *e = &w.entities[w.activeEntitySlots[i]];
            const MetabolicProfile *mp = MetabolicProfileRegistry_Get(&w.metabolismDb->profiles,
                                                                      w.species[e->speciesIndex].metabolicProfile);
            if (!mp) continue;
            if (grass) CreatureMetabolism_Ingest(&e->metabolism, mp, grass->handle, grass->components, grass->componentCount, 0.35f, w.clock.seconds);
            if (rotten && (i % 3u == 0u)) CreatureMetabolism_Ingest(&e->metabolism, mp, rotten->handle, rotten->components, rotten->componentCount, 0.20f, w.clock.seconds);
            if (water && (i % 4u == 0u)) CreatureMetabolism_Ingest(&e->metabolism, mp, water->handle, water->components, water->componentCount, 0.15f, w.clock.seconds);
        }
        /* Añadir cadáveres descomponiéndose para ejercitar descomposición en masa. */
        SubstanceHandle hTissue = SubstanceRegistry_Find(&w.metabolismDb->substances, "brumacerno_tissue");
        const SubstanceDef *tissue = SubstanceRegistry_Get(&w.metabolismDb->substances, hTissue);
        if (tissue) {
            for (unsigned c = 0; c < 20u; ++c) {
                float ang = (float)c * 0.314f;
                Vector3 pos = Vec3_Create(cosf(ang) * arenaSize * 0.25f, 0.0f, sinf(ang) * arenaSize * 0.25f);
                EcosystemResource res = EcosystemResource_FromSubstance(
                    tissue, 18.0f, 18.0f, RESOURCE_REGEN_NONE, 0.0f,
                    RESOURCE_DECAY_DECOMPOSE, 0.008f, w.decayTargetMolecule, 400.0);
                EcosystemWorld_AddMolecularResource(&w, res, pos, 0.7f);
            }
        }
    }

    /* Warmup */
    for (unsigned t = 0; t < 20u; ++t) EcosystemWorld_Step(&w);
    EvolutionProfiler_Reset(&prof);

    double *times = calloc(ticks, sizeof(double));
    double tStart = nowMs();
    for (unsigned t = 0; t < ticks; ++t) {
        double a = nowMs();
        EcosystemWorld_Step(&w);
        times[t] = nowMs() - a;
    }
    double totalMs = nowMs() - tStart;

    qsort(times, ticks, sizeof(double), cmpDouble);
    double meanMs = totalMs / (double)ticks;
    double p95Ms = times[(ticks - 1u) * 95u / 100u];
    double p99Ms = times[(ticks - 1u) * 99u / 100u];
    double tps = 1000.0 / fmax(1e-6, meanMs);

    double percMs = prof.sections[EVO_PROF_PERCEPTION].totalMs / (double)ticks;
    double neurMs = prof.sections[EVO_PROF_NEURAL_INFERENCE].totalMs / (double)ticks;
    double metaMs = prof.sections[EVO_PROF_METABOLISM].totalMs / (double)ticks;
    double physMs = prof.sections[EVO_PROF_PHYSIOLOGY].totalMs / (double)ticks;

    printf("%-28s | pop=%4u | w=%u | mean=%6.3fms p95=%6.3fms p99=%6.3fms | tps=%7.1f | perc=%5.3f neur=%5.3f meta=%5.3f phys=%5.3f | rss=%.1fMB\n",
           label, w.count, EcosystemWorld_WorkerCount(&w),
           meanMs, p95Ms, p99Ms, tps, percMs, neurMs, metaMs, physMs, peakRssMb());

    free(times);
    EcosystemWorld_Free(&w);
}

static void runLabWorkerScaling(void) {
    printf("\n=== BRAIN EVOLUTION LAB PARALLEL WORKER SCALING (pop=48, 3 seeds = 144 episodes) ===\n");
    MetabolismDatabase db;
    EvolutionSpeciesConfig species;
    EvolutionEnvironmentConfig env;
    char err[256] = {0};
    if (!MetabolismIO_LoadDatabase("metabolism", &db, err, sizeof(err)) ||
        !EvolutionSpeciesConfig_LoadFile("species/brumacerno.species.json", &species, err, sizeof(err)) ||
        !EvolutionEnvironment_LoadFile("environments/brumacerno_mixed.environment.json", &env, err, sizeof(err)) ||
        !EvolutionEnvironment_ValidateAgainstDatabase(&env, &db, err, sizeof(err))) {
        fprintf(stderr, "Error cargando datos del Lab: %s\n", err);
        return;
    }

    const unsigned popSize = 48u;
    const unsigned seedCount = 3u;
    uint32_t seeds[3] = {1001u, 1002u, 1003u};
    NeuralGenome *genomes = calloc(popSize, sizeof(*genomes));
    EvolutionAggregateMetrics *metrics = calloc(popSize, sizeof(*metrics));
    uint32_t rng = 424242u;
    for (unsigned i = 0; i < popSize; ++i) {
        NeuralGenome_InitRandomV2(&genomes[i], &rng, 0.35f, 4.0f, (uint64_t)(i + 1u));
    }

    EvolutionFitnessConfig fcfg = EvolutionFitnessConfig_Default();
    unsigned workerConfigs[] = {1u, 2u, 4u, 8u, 0u}; /* 0 = auto */
    double baselineWall = 0.0;
    float baselineFit0 = 0.0f;
    float baselineFitSum = 0.0f;

    for (unsigned wi = 0; wi < sizeof(workerConfigs) / sizeof(workerConfigs[0]); ++wi) {
        unsigned reqW = workerConfigs[wi];
        EvolutionJobSystem *jobs = EvolutionJobSystem_Create(reqW);
        unsigned effW = jobs ? EvolutionJobSystem_WorkerCount(jobs) : 1u;
        EvolutionEvalThroughput tp = {0};

        if (!EvolutionEvaluator_EvaluatePopulation(jobs, &env, &species, &db,
                                                   genomes, popSize, seeds, seedCount,
                                                   &fcfg, false, metrics, &tp, err, sizeof(err))) {
            fprintf(stderr, "Error en evaluador (w=%u): %s\n", reqW, err);
            EvolutionJobSystem_Destroy(jobs);
            continue;
        }
        float fitSum = 0.0f;
        for (unsigned i = 0; i < popSize; ++i) fitSum += metrics[i].meanFitness;
        if (wi == 0u) {
            baselineWall = tp.wallSeconds;
            baselineFit0 = metrics[0].meanFitness;
            baselineFitSum = fitSum;
        }
        double speedup = baselineWall > 1e-6 ? (baselineWall / fmax(1e-6, tp.wallSeconds)) : 1.0;
        double genomesPerMin = (double)popSize / fmax(1e-6, tp.wallSeconds) * 60.0;
        bool detOk = fabsf(metrics[0].meanFitness - baselineFit0) < 1e-6f &&
                     fabsf(fitSum - baselineFitSum) < 1e-4f;

        printf("workers=%2u (%s) | wall=%6.3fs | speedup=%4.2fx | eps/s=%6.1f | ticks/s=%9.0f | csec/s=%8.0f | gen/min=%6.1f | det=%s | rss=%.1fMB\n",
               effW, reqW == 0u ? "auto" : "fix ", tp.wallSeconds, speedup,
               tp.episodesPerSecond, tp.ticksPerSecond, tp.creatureSecondsPerSecond,
               genomesPerMin, detOk ? "EXACT" : "DIFF!", peakRssMb());

        EvolutionJobSystem_Destroy(jobs);
    }

    free(metrics);
    free(genomes);
}

int main(void) {
    char err[256] = {0};
    CreatureRecipeRegistry_LoadDirectory("recipes", err, sizeof(err));

    printf("=== ECOSYSTEM SCALABILITY: LEGACY V1 vs MOLECULAR V2 (22..1000 CREATURES) ===\n");
    runWorldScaleCase("legacy_v1_22",       ECOSYSTEM_METABOLISM_LEGACY,    16,   6,  48.0f,  45,  3, 300, false, false);
    runWorldScaleCase("molecular_v2_22",    ECOSYSTEM_METABOLISM_MOLECULAR, 16,   6,  48.0f,  45,  3, 300, true,  false);
    runWorldScaleCase("legacy_v1_100",      ECOSYSTEM_METABOLISM_LEGACY,    75,  25, 110.0f, 160,  8, 240, false, false);
    runWorldScaleCase("molecular_v2_100",   ECOSYSTEM_METABOLISM_MOLECULAR, 75,  25, 110.0f, 160,  8, 240, true,  false);
    runWorldScaleCase("legacy_v1_500",      ECOSYSTEM_METABOLISM_LEGACY,   380, 120, 220.0f, 420, 18, 160, false, false);
    runWorldScaleCase("molecular_v2_500",   ECOSYSTEM_METABOLISM_MOLECULAR,380, 120, 220.0f, 420, 18, 160, true,  false);
    runWorldScaleCase("legacy_v1_1000",     ECOSYSTEM_METABOLISM_LEGACY,   760, 240, 320.0f, 700, 28, 120, false, false);
    runWorldScaleCase("molecular_v2_1000",  ECOSYSTEM_METABOLISM_MOLECULAR,760, 240, 320.0f, 700, 28, 120, true,  false);

    printf("\n=== METABOLIC WORKLOAD BREAKDOWN (500 CREATURES, MOLECULAR V2) ===\n");
    runWorldScaleCase("mol500_empty_stomach",   ECOSYSTEM_METABOLISM_MOLECULAR, 380, 120, 220.0f, 420, 18, 160, false, false);
    runWorldScaleCase("mol500_active_mixed",    ECOSYSTEM_METABOLISM_MOLECULAR, 380, 120, 220.0f, 420, 18, 160, true,  true);

    runLabWorkerScaling();
    return 0;
}
