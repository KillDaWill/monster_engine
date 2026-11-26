/**
 * @file benchmark_world.c
 * @brief Benchmark reproducible del Sistema de Mundos Procedurales:
 *        1) Generación, memoria estática e hidrología/geología por receta (64..512 u.m., 129..1025).
 *        2) Escalabilidad de simulación (22, 100, 500, 1000 criaturas) en plano vs procedural 256/512.
 *        3) Escalado de trabajadores y caché fría vs caliente en Brain Evolution Lab sobre relieve 3D.
 */
#include "WorldGenerator.h"
#include "WorldStaticData.h"
#include "EvolutionArena.h"
#include "BrainEvolutionLab.h"
#include "EvolutionProfiler.h"
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

static long peakRssKiB(void) {
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    return ru.ru_maxrss;
}

static void benchWorldGeneration(const char *label, const WorldRecipe *rec) {
    char err[256] = {0};
    WorldStaticData *wd = NULL;
    double t0 = nowMs();
    if (!WorldGenerator_Generate(rec, &wd, err, sizeof(err)) || !wd) {
        printf("  [ERR] %-20s : %s\n", label, err);
        return;
    }
    double dt = nowMs() - t0;
    const WorldGenerationStats *st = &wd->stats;
    printf("  %-20s res=%4u size=%5.0f gen=%7.2fms (tf=%.1f hy=%.1f bm=%.1f geo=%.1f nav=%.1f) "
           "ram=%5.2fMB lakes=%2u riv=%2u sh=%3u rk=%3u trav=%4.1f%% wet=%4.1f%% v=%u fp=%016llx\n",
           label, wd->terrain.resolution, rec->size, dt,
           st->terrainMs, st->hydrologyMs, st->biomeMs, st->geologyMs, st->navBaseMs,
           (double)st->totalStaticBytes / (1024.0 * 1024.0),
           st->lakeCount, st->riverCount, st->shoreSiteCount, st->rockCount,
           st->traversableLandFraction * 100.0f, st->wetAreaFraction * 100.0f,
           wd->generatorVersion, (unsigned long long)wd->dataFingerprint);
    WorldStaticData_Release(wd);
}

static void benchArenaScalability(const char *label, const char *worldPath,
                                  float overrideSize, uint32_t overrideRes,
                                  unsigned brum, unsigned velk, unsigned plants,
                                  unsigned workers, unsigned ticks) {
    EvolutionArenaConfig cfg = EvolutionArenaConfig_Default();
    cfg.seed = 42u;
    cfg.brumacernos = brum;
    cfg.velkares = velk;
    cfg.plants = plants;
    cfg.capacity = (brum + velk) + 128u;
    cfg.workers = workers;
    cfg.metabolismMode = ECOSYSTEM_METABOLISM_MOLECULAR;
    cfg.includeMixedSubstances = true;

    const WorldStaticData *customStatic = NULL;
    char err[256] = {0};
    if (worldPath && overrideRes == 0u) {
        snprintf(cfg.worldRecipePath, sizeof(cfg.worldRecipePath), "%s", worldPath);
    } else if (!worldPath) {
        cfg.arenaSize = overrideSize > 0.0f ? overrideSize : 256.0f;
    }

    EcosystemWorld w = {0};
    if (overrideRes > 0u) {
        /* Territorio grande 512 u.m., 1025x1025 generado directamente desde receta base. */
        WorldRecipe rec;
        if (!WorldRecipe_LoadFile(worldPath, &rec, err, sizeof(err))) {
            printf("  [ERR] %s: %s\n", label, err);
            return;
        }
        rec.size = overrideSize;
        rec.terrain.resolution = overrideRes;
        rec.recipeHash = WorldRecipe_Fingerprint(&rec);
        WorldStaticData *wd = NULL;
        if (!WorldGenerator_Generate(&rec, &wd, err, sizeof(err)) || !wd) {
            printf("  [ERR] %s: %s\n", label, err);
            return;
        }
        customStatic = wd;
        /* Construir arena plana de tamaño 512 y adjuntar mundo 1025x1025 (o usar preset). */
        cfg.arenaSize = overrideSize;
        snprintf(cfg.worldRecipePath, sizeof(cfg.worldRecipePath), "/tmp/monster_bench_512.world.json");
        WorldRecipe_SaveFile(&rec, cfg.worldRecipePath, err, sizeof(err));
        WorldStaticData_Release(customStatic);
        customStatic = NULL;
    }

    double tInit0 = nowMs();
    if (!EvolutionArena_Create(&w, &cfg, err, sizeof(err))) {
        printf("  [ERR] %-28s : %s\n", label, err);
        return;
    }
    double initMs = nowMs() - tInit0;

    EvolutionProfiler prof = {0};
    EvolutionProfiler_Init(&prof, true);
    EcosystemWorld_SetProfiler(&w, &prof);

    /* Warmup 20 ticks */
    for (unsigned t = 0; t < 20u; ++t) EcosystemWorld_Step(&w);
    EvolutionProfiler_Reset(&prof);

    double *samples = (double *)malloc(ticks * sizeof(double));
    double tAll0 = nowMs();
    for (unsigned t = 0; t < ticks; ++t) {
        double ts = nowMs();
        EcosystemWorld_Step(&w);
        samples[t] = nowMs() - ts;
    }
    double totalMs = nowMs() - tAll0;
    qsort(samples, ticks, sizeof(double), cmpDouble);

    double meanMs = totalMs / (double)ticks;
    double p95Ms = samples[(ticks - 1u) * 95u / 100u];
    double p99Ms = samples[(ticks - 1u) * 99u / 100u];
    double simSec = (double)ticks * w.clock.fixedStep;
    double speedX = simSec / fmax(1e-6, totalMs * 0.001);

    printf("  %-28s pop=%4u->%-4u init=%6.1fms mean=%6.3fms p95=%6.3fms p99=%6.3fms "
           "speed=%5.2fx nav=%5llu cand=%7llu rss=%6ldKiB\n",
           label, brum + velk, w.count, initMs, meanMs, p95Ms, p99Ms, speedX,
           (unsigned long long)prof.counters.navigationRequests,
           (unsigned long long)prof.counters.spatialQueryCandidates,
           peakRssKiB());

    free(samples);
    EcosystemWorld_Free(&w);
    if (overrideRes > 0u) remove("/tmp/monster_bench_512.world.json");
}

static void benchBrainLabScaling(void) {
    printf("\n=== 3. Brain Evolution Lab en Terreno Procedural (Cache Fria vs Caliente y Workers) ===\n");
    const unsigned workerCounts[3] = {1u, 4u, 6u};
    double serialMs = 0.0;
    float refFit = 0.0f;

    for (int i = 0; i < 3; ++i) {
        unsigned nw = workerCounts[i];
        for (int warm = 0; warm < (i == 0 ? 2 : 1); ++warm) {
            if (warm == 0 && i == 0) WorldStaticCache_Clear();
            BrainLabConfig cfg = BrainLabConfig_Default();
            snprintf(cfg.speciesPath, sizeof(cfg.speciesPath), "species/brumacerno.species.json");
            snprintf(cfg.environmentPath, sizeof(cfg.environmentPath), "environments/brumacerno_river_valley.environment.json");
            snprintf(cfg.outputDir, sizeof(cfg.outputDir), "/tmp/monster_bench_lab_world");
            cfg.seed = 42u;
            cfg.populationSize = 16u;
            cfg.eliteCount = 6u;
            cfg.maxGenerations = 1u;
            cfg.trainingSeedsPerGen = 2u;
            cfg.validationSeeds = 2u;
            cfg.testSeeds = 2u;
            cfg.validationInterval = 1u;
            cfg.episodeSecondsOverride = 60.0;
            cfg.workers = nw;

            BrainEvolutionLab lab = {0};
            char err[256] = {0};
            double t0 = nowMs();
            if (!BrainEvolutionLab_Init(&lab, &cfg, err, sizeof(err)) ||
                !BrainEvolutionLab_StepGeneration(&lab, err, sizeof(err))) {
                printf("  [ERR] workers=%u: %s\n", nw, err);
                BrainEvolutionLab_Destroy(&lab);
                continue;
            }
            double elapsedMs = nowMs() - t0;
            uint64_t hits = 0, misses = 0;
            uint32_t entries = 0;
            WorldStaticCache_Stats(&hits, &misses, &entries);
            if (i == 0 && warm == 1) {
                serialMs = elapsedMs;
                refFit = lab.bestTrainingMetrics.meanFitness;
            }
            double speedup = (serialMs > 0.0) ? (serialMs / elapsedMs) : 1.0;
            printf("  workers=%u %-5s elapsed=%7.2fms speedup=%4.2fx eps/s=%6.1f ticks/s=%8.0f "
                   "bestFit=%.5f (diff=%.1e) cache(h=%llu,m=%llu)\n",
                   nw, (i == 0 && warm == 0) ? "COLD" : "WARM",
                   elapsedMs, speedup,
                   lab.historyCount ? lab.history[0].episodesPerSecond : 0.0,
                   lab.historyCount ? lab.history[0].ticksPerSecond : 0.0,
                   lab.bestTrainingMetrics.meanFitness,
                   (double)fabsf(lab.bestTrainingMetrics.meanFitness - refFit),
                   (unsigned long long)hits, (unsigned long long)misses);
            BrainEvolutionLab_Destroy(&lab);
        }
    }
}

int main(void) {
    char err[256] = {0};
    if (!CreatureRecipeRegistry_LoadDirectory("recipes", err, sizeof(err))) {
        fprintf(stderr, "Error cargando recetas: %s\n", err);
        return 1;
    }

    printf("==================================================================================\n");
    printf(" MONSTER ENGINE — PROCEDURAL WORLD BENCHMARK\n");
    printf("==================================================================================\n");

    printf("\n=== 1. Generacion y Memoria Estatica por Receta de Mundo ===\n");
    const char *recipes[] = {
        "worlds/flat.world.json",
        "worlds/lab_valley_small.world.json",
        "worlds/river_valley.world.json",
        "worlds/mountain_lakes.world.json",
        "worlds/rocky_highlands.world.json",
        "worlds/wetlands.world.json"
    };
    for (size_t i = 0; i < sizeof(recipes) / sizeof(recipes[0]); ++i) {
        WorldRecipe rec;
        if (WorldRecipe_LoadFile(recipes[i], &rec, err, sizeof(err))) {
            benchWorldGeneration(rec.id, &rec);
        }
    }
    /* Territorio grande 512 u.m., 1025x1025 */
    {
        WorldRecipe big;
        if (WorldRecipe_LoadFile("worlds/river_valley.world.json", &big, err, sizeof(err))) {
            big.size = 512.0f;
            big.terrain.resolution = 1025u;
            big.recipeHash = WorldRecipe_Fingerprint(&big);
            benchWorldGeneration("large_territory_512", &big);
            big.size = 256.0f;
            big.recipeHash = WorldRecipe_Fingerprint(&big);
            benchWorldGeneration("fine_valley256", &big);
        }
    }

    printf("\n=== 2. Escalabilidad de Simulacion (6 workers, Molecular v2, 300 ticks) ===\n");
    WorldStaticCache_Clear();
    benchArenaScalability("flat_compat_22",     NULL, 64.0f,  0u, 18u,  4u,  40u, 6u, 300u);
    benchArenaScalability("flat_compat_100",    NULL, 128.0f, 0u, 80u,  20u, 180u, 6u, 300u);
    benchArenaScalability("flat_compat_500",    NULL, 256.0f, 0u, 400u, 100u, 700u, 6u, 300u);
    benchArenaScalability("flat_compat_1000",   NULL, 320.0f, 0u, 800u, 200u, 1100u, 6u, 240u);

    benchArenaScalability("proc_small64_22",    "worlds/lab_valley_small.world.json", 0.0f, 0u, 18u,  4u,  40u, 6u, 300u);
    benchArenaScalability("proc_valley256_22",  "worlds/river_valley.world.json",     0.0f, 0u, 18u,  4u,  40u, 6u, 300u);
    benchArenaScalability("proc_valley256_100", "worlds/river_valley.world.json",     0.0f, 0u, 80u,  20u, 180u, 6u, 300u);
    benchArenaScalability("proc_valley256_500", "worlds/river_valley.world.json",     0.0f, 0u, 400u, 100u, 700u, 6u, 300u);
    benchArenaScalability("proc_valley256_1000","worlds/river_valley.world.json",     0.0f, 0u, 800u, 200u, 1100u, 6u, 240u);
    benchArenaScalability("proc_large512_500",  "worlds/river_valley.world.json",     512.0f, 1025u, 400u, 100u, 700u, 6u, 240u);

    benchBrainLabScaling();

    WorldStaticCache_Clear();
    printf("\nBenchmark completado. Peak RSS final: %ld KiB\n", peakRssKiB());
    return 0;
}
