/**
 * @file test_world_snapshot.c
 * @brief Pruebas de propiedad inmutable con refcount, resets de EvolutionSimulationRunner con
 *        snapshots retenidos, concurrencia single-flight de WorldStaticCache y checkpoints del Lab.
 */
#include "test_utils.h"
#include "WorldStaticData.h"
#include "WorldGenerator.h"
#include "EvolutionArena.h"
#include "EvolutionSimulationRunner.h"
#include "BrainEvolutionLab.h"
#include "EvolutionCheckpointIO.h"
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct CacheThreadArg {
    const WorldRecipe *recipe;
    const WorldStaticData *result;
} CacheThreadArg;

static void *cacheWorkerThread(void *ptr) {
    CacheThreadArg *arg = (CacheThreadArg *)ptr;
    char err[128] = {0};
    arg->result = WorldStaticCache_Acquire(arg->recipe, err, sizeof(err));
    return NULL;
}

static void test_world_static_cache_parallel_single_flight(void) {
    WorldRecipe rec;
    char err[256] = {0};
    TEST_ASSERT(WorldRecipe_LoadFile("worlds/lab_valley_small.world.json", &rec, err, sizeof(err)),
                "load lab_valley_small");
    rec.seed = 1234u;
    WorldStaticCache_Clear();

    enum { NTHREADS = 6 };
    pthread_t threads[NTHREADS];
    CacheThreadArg args[NTHREADS];
    for (int i = 0; i < NTHREADS; ++i) {
        args[i].recipe = &rec;
        args[i].result = NULL;
        TEST_ASSERT(pthread_create(&threads[i], NULL, cacheWorkerThread, &args[i]) == 0, "create thread");
    }
    for (int i = 0; i < NTHREADS; ++i) {
        pthread_join(threads[i], NULL);
    }

    const WorldStaticData *first = args[0].result;
    TEST_ASSERT(first != NULL, "cache acquired non-null WorldStaticData");
    for (int i = 1; i < NTHREADS; ++i) {
        TEST_ASSERT(args[i].result == first, "all threads share exact same single-flight instance");
    }
    WorldRecipe newer = rec;
    newer.generatorVersion = WORLD_GENERATOR_VERSION_CONTINUOUS;
    const WorldStaticData *version2 = WorldStaticCache_Acquire(&newer, err, sizeof(err));
    TEST_ASSERT(version2 && version2 != first && version2->dataFingerprint != first->dataFingerprint,
                "generator version distinguishes cache entries with the same seed and id");
    newer.generatorVersion = WORLD_GENERATOR_VERSION_MORPHOLOGY;
    const WorldStaticData *version3 = WorldStaticCache_Acquire(&newer, err, sizeof(err));
    TEST_ASSERT(version3 && version3 != first && version3 != version2 &&
                version3->dataFingerprint != version2->dataFingerprint,
                "v3 does not share physical cache identity with v2");
    WorldStaticData_Release(version3);
    WorldStaticData_Release(version2);
    for (int i = 0; i < NTHREADS; ++i) {
        WorldStaticData_Release(args[i].result);
    }
    WorldStaticCache_Clear();
}

static bool runnerStepSync(EvolutionSimulationRunner *r, unsigned ticks) {
    EvolutionCommandAck ack = {0};
    uint64_t cid = EvolutionSimulationRunner_PostStepExact(r, ticks);
    return EvolutionSimulationRunner_WaitCommand(r, cid, 5.0, &ack) && ack.status == EVO_CMD_STATUS_OK;
}

static void test_runner_repeated_world_resets_with_held_snapshots(void) {
    EvolutionArenaConfig cfgA = EvolutionArenaConfig_Default();
    cfgA.seed = 42u;
    cfgA.brumacernos = 8u;
    cfgA.velkares = 2u;
    cfgA.plants = 12u;
    cfgA.workers = 2u;
    snprintf(cfgA.worldRecipePath, sizeof(cfgA.worldRecipePath), "worlds/lab_valley_small.world.json");

    char err[256] = {0};
    EvolutionSimulationRunner *runner = EvolutionSimulationRunner_Create(&cfgA, true, err, sizeof(err));
    TEST_ASSERT(runner != NULL, "create runner on procedural world A");

    TEST_ASSERT(runnerStepSync(runner, 4u), "step sync world A");
    const EvolutionRenderSnapshot *heldA = EvolutionSimulationRunner_AcquireSnapshot(runner);
    TEST_ASSERT(heldA != NULL && heldA->staticWorld != NULL && !heldA->staticWorld->recipe.flat,
                "snapshot A holds procedural WorldStaticData");
    const WorldStaticData *retainedA = WorldStaticData_Retain(heldA->staticWorld);
    uint64_t fpA = heldA->worldFingerprint;
    uint64_t epochA = heldA->worldEpoch;

    /* Resetear a una semilla de mundo distinta mientras heldA SIGUE retenido por el lector (triple-buffer). */
    EvolutionArenaConfig cfgB = cfgA;
    cfgB.worldSeed = 999u;
    cfgB.hasWorldSeedOverride = true;
    uint64_t cmd1 = EvolutionSimulationRunner_PostReset(runner, &cfgB);
    EvolutionCommandAck ack = {0};
    TEST_ASSERT(EvolutionSimulationRunner_WaitCommand(runner, cmd1, 5.0, &ack) &&
                ack.status == EVO_CMD_STATUS_OK, "reset to world B succeeded");
    TEST_ASSERT(runnerStepSync(runner, 4u), "step sync world B");

    /* heldA sigue protegido por el triple-buffer durante el reset de fondo. */
    float sampleA = 0.0f;
    TEST_ASSERT(Terrain_SampleHeight(&heldA->staticWorld->terrain, 0.0f, 0.0f, &sampleA), "heldA intact across reset");
    EvolutionSimulationRunner_ReleaseSnapshot(runner, heldA);

    const EvolutionRenderSnapshot *heldB = EvolutionSimulationRunner_AcquireSnapshot(runner);
    TEST_ASSERT(heldB != NULL && heldB->staticWorld != NULL, "snapshot B holds world B");
    TEST_ASSERT(heldB->worldFingerprint != fpA, "world B has distinct fingerprint");
    TEST_ASSERT(heldB->worldEpoch != epochA, "world B has distinct epoch");
    const WorldStaticData *retainedB = WorldStaticData_Retain(heldB->staticWorld);

    /* Resetear a mundo plano mientras heldB sigue retenido. */
    EvolutionArenaConfig cfgFlat = EvolutionArenaConfig_Default();
    cfgFlat.seed = 42u;
    cfgFlat.brumacernos = 6u;
    cfgFlat.velkares = 2u;
    cfgFlat.plants = 8u;
    cfgFlat.workers = 2u;
    uint64_t cmd2 = EvolutionSimulationRunner_PostReset(runner, &cfgFlat);
    TEST_ASSERT(EvolutionSimulationRunner_WaitCommand(runner, cmd2, 5.0, &ack) &&
                ack.status == EVO_CMD_STATUS_OK, "reset to flat world succeeded");
    TEST_ASSERT(runnerStepSync(runner, 2u), "step sync flat");

    float sampleB = 0.0f;
    TEST_ASSERT(Terrain_SampleHeight(&heldB->staticWorld->terrain, 0.0f, 0.0f, &sampleB), "heldB intact across reset");
    EvolutionSimulationRunner_ReleaseSnapshot(runner, heldB);

    const EvolutionRenderSnapshot *heldFlat = EvolutionSimulationRunner_AcquireSnapshot(runner);
    TEST_ASSERT(heldFlat != NULL && heldFlat->staticWorld == NULL, "flat snapshot has NULL staticWorld");
    EvolutionSimulationRunner_ReleaseSnapshot(runner, heldFlat);
    EvolutionSimulationRunner_Destroy(runner);

    /* Incluso tras destruir el runner, las referencias retenidas (ej. EnvironmentRenderer) siguen válidas. */
    TEST_ASSERT(Terrain_SampleHeight(&retainedA->terrain, 0.0f, 0.0f, &sampleA), "retainedA alive after runner destroy");
    TEST_ASSERT(Terrain_SampleHeight(&retainedB->terrain, 0.0f, 0.0f, &sampleB), "retainedB alive after runner destroy");
    WorldStaticData_Release(retainedA);
    WorldStaticData_Release(retainedB);
}

static void test_lab_determinism_and_checkpoint_world_fingerprint(const char *environmentPath) {
    BrainLabConfig cfg = BrainLabConfig_Default();
    snprintf(cfg.speciesPath, sizeof(cfg.speciesPath), "species/brumacerno.species.json");
    snprintf(cfg.environmentPath, sizeof(cfg.environmentPath), "%s", environmentPath);
    snprintf(cfg.outputDir, sizeof(cfg.outputDir), "/tmp/monster_test_lab_world");
    cfg.seed = 314u;
    cfg.populationSize = 10u;
    cfg.eliteCount = 4u;
    cfg.maxGenerations = 2u;
    cfg.trainingSeedsPerGen = 2u;
    cfg.validationSeeds = 2u;
    cfg.testSeeds = 2u;
    cfg.validationInterval = 1u;
    cfg.episodeSecondsOverride = 20.0;
    cfg.workers = 2u;

    BrainEvolutionLab lab1 = {0}, lab2 = {0};
    char err[256] = {0};
    TEST_ASSERT(BrainEvolutionLab_Init(&lab1, &cfg, err, sizeof(err)), "init lab1 on procedural terrain");
    TEST_ASSERT(BrainEvolutionLab_StepGeneration(&lab1, err, sizeof(err)), "step gen 1 on lab1");

    /* Determinismo: mismo seed con 1 hilo vs 2 hilos debe dar exactamente el mismo bestTrainFitness. */
    BrainLabConfig cfgSerial = cfg;
    cfgSerial.workers = 1u;
    TEST_ASSERT(BrainEvolutionLab_Init(&lab2, &cfgSerial, err, sizeof(err)), "init lab2 serial");
    TEST_ASSERT(BrainEvolutionLab_StepGeneration(&lab2, err, sizeof(err)), "step gen 1 on lab2 serial");
    TEST_ASSERT(fabsf(lab1.bestTrainingMetrics.meanFitness - lab2.bestTrainingMetrics.meanFitness) < 1e-5f,
                "Lab terrain evaluation is deterministic across worker counts");

    /* Guardar checkpoint con huella de mundo y verificar rechazo al intentar cargarlo en entorno plano. */
    const char *ckptPath = "/tmp/monster_test_lab_world_ckpt.json";
    TEST_ASSERT(EvolutionCheckpoint_Save(&lab1, ckptPath, err, sizeof(err)), "save terrain checkpoint");

    BrainLabConfig cfgFlat = cfg;
    snprintf(cfgFlat.environmentPath, sizeof(cfgFlat.environmentPath), "environments/brumacerno_basic.environment.json");
    BrainEvolutionLab labFlat = {0};
    TEST_ASSERT(BrainEvolutionLab_Init(&labFlat, &cfgFlat, err, sizeof(err)), "init flat lab");
    bool loadedMismatch = EvolutionCheckpoint_Load(&labFlat, ckptPath, false, err, sizeof(err));
    TEST_ASSERT(!loadedMismatch, "checkpoint rejects mismatched world/environment fingerprint");

    /* Cargar el mismo checkpoint en un lab compatible debe funcionar. */
    BrainEvolutionLab labResume = {0};
    TEST_ASSERT(BrainEvolutionLab_Init(&labResume, &cfg, err, sizeof(err)), "init resume lab");
    TEST_ASSERT(EvolutionCheckpoint_Load(&labResume, ckptPath, false, err, sizeof(err)),
                "checkpoint loads cleanly on matching procedural world");
    TEST_ASSERT(labResume.currentGeneration == lab1.currentGeneration, "generation restored");

    remove(ckptPath);
    BrainEvolutionLab_Destroy(&lab1);
    BrainEvolutionLab_Destroy(&lab2);
    BrainEvolutionLab_Destroy(&labFlat);
    BrainEvolutionLab_Destroy(&labResume);
}

void run_world_snapshot_tests(void) {
    printf("=== Pruebas de Propiedad Inmutable, Caché Paralela, Resets y Checkpoints de Mundo ===\n");
    test_world_static_cache_parallel_single_flight();
    test_runner_repeated_world_resets_with_held_snapshots();
    test_lab_determinism_and_checkpoint_world_fingerprint("environments/brumacerno_river_valley.environment.json");
    printf("Pruebas de snapshots, concurrencia y checkpoints superadas.\n");
}
