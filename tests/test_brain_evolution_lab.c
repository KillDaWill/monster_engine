/**
 * @file test_brain_evolution_lab.c
 * @brief Pruebas unitarias y de integración para especies genéricas, entornos, GA, checkpoints y paralelismo.
 */
#include "test_utils.h"
#include "BrainEvolutionLab.h"
#include "EvolutionCheckpointIO.h"
#include "EvolutionEvaluator.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static void test_species_and_environment_data_loading(void) {
    MetabolismDatabase db;
    char err[256] = {0};
    TEST_ASSERT(MetabolismIO_LoadDatabase("metabolism", &db, err, sizeof(err)), "cargar metabolism/");

    EvolutionSpeciesConfig brum, velk, omni;
    TEST_ASSERT(EvolutionSpeciesConfig_LoadFile("species/brumacerno.species.json", &brum, err, sizeof(err)), "cargar brumacerno");
    TEST_ASSERT(EvolutionSpeciesConfig_LoadFile("species/velkar.species.json", &velk, err, sizeof(err)), "cargar velkar");
    TEST_ASSERT(EvolutionSpeciesConfig_LoadFile("species/omnivore_test.species.json", &omni, err, sizeof(err)), "cargar omnivore_test");

    EcosystemSpecies esB, esV, esO;
    TEST_ASSERT(EvolutionSpeciesConfig_BuildEcosystemSpecies(&brum, &db, 1u, &esB, err, sizeof(err)), "compilar brum");
    TEST_ASSERT(EvolutionSpeciesConfig_BuildEcosystemSpecies(&velk, &db, 2u, &esV, err, sizeof(err)), "compilar velk");
    TEST_ASSERT(EvolutionSpeciesConfig_BuildEcosystemSpecies(&omni, &db, 3u, &esO, err, sizeof(err)), "compilar omni");
    TEST_ASSERT(esB.metabolicProfile != esV.metabolicProfile && esO.metabolicProfile != esB.metabolicProfile,
                "tres perfiles metabólicos distintos cargados puramente desde datos");

    /* Rechazar especie con perfil metabólico inexistente. */
    EvolutionSpeciesConfig badSp = omni;
    snprintf(badSp.metabolismProfileId, sizeof(badSp.metabolismProfileId), "nonexistent_profile");
    TEST_ASSERT(!EvolutionSpeciesConfig_BuildEcosystemSpecies(&badSp, &db, 4u, &esO, err, sizeof(err)),
                "rechaza perfil metabólico inexistente");

    /* Validar los 5 entornos de datos. */
    const char *envPaths[5] = {
        "environments/brumacerno_basic.environment.json",
        "environments/brumacerno_mixed.environment.json",
        "environments/velkar_scavenging.environment.json",
        "environments/velkar_hunting.environment.json",
        "environments/molecular_generalization.environment.json"
    };
    for (unsigned i = 0; i < 5u; ++i) {
        EvolutionEnvironmentConfig env;
        TEST_ASSERT(EvolutionEnvironment_LoadFile(envPaths[i], &env, err, sizeof(err)), "cargar entorno");
        TEST_ASSERT(EvolutionEnvironment_ValidateAgainstDatabase(&env, &db, err, sizeof(err)), "validar entorno");
    }
}

static void test_clean_evaluation_reset_and_worker_determinism(void) {
    char err[256] = {0};
    BrainLabConfig cfg1 = BrainLabConfig_Default();
    snprintf(cfg1.speciesPath, sizeof(cfg1.speciesPath), "species/brumacerno.species.json");
    snprintf(cfg1.environmentPath, sizeof(cfg1.environmentPath), "environments/brumacerno_basic.environment.json");
    snprintf(cfg1.outputDir, sizeof(cfg1.outputDir), "/tmp/test_lab_det1");
    cfg1.seed = 101u;
    cfg1.populationSize = 8u;
    cfg1.eliteCount = 2u;
    cfg1.tournamentSize = 3u;
    cfg1.trainingSeedsPerGen = 2u;
    cfg1.validationSeeds = 2u;
    cfg1.testSeeds = 2u;
    cfg1.maxGenerations = 2u;
    cfg1.episodeSecondsOverride = 60.0;
    cfg1.targetValidationFitness = 2.0f; /* Inalcanzable: probar estado no convergido */
    cfg1.workers = 1u;

    BrainLabConfig cfg4 = cfg1;
    snprintf(cfg4.outputDir, sizeof(cfg4.outputDir), "/tmp/test_lab_det4");
    cfg4.workers = 4u;

    BrainEvolutionLab lab1, lab4;
    TEST_ASSERT(BrainEvolutionLab_Init(&lab1, &cfg1, err, sizeof(err)), "init lab 1 worker");
    TEST_ASSERT(BrainEvolutionLab_Init(&lab4, &cfg4, err, sizeof(err)), "init lab 4 workers");

    /* Paso 0: verificar preservación exacta de élites y determinismo 1 vs 4 hilos. */
    TEST_ASSERT(BrainEvolutionLab_StepGeneration(&lab1, err, sizeof(err)), "gen 0 (1 worker)");
    TEST_ASSERT(BrainEvolutionLab_StepGeneration(&lab4, err, sizeof(err)), "gen 0 (4 workers)");
    TEST_ASSERT(FLOAT_NEAR(lab1.history[0].bestTrainFitness, lab4.history[0].bestTrainFitness),
                "best fitness idéntico 1 vs 4 workers");
    TEST_ASSERT(lab1.history[0].bestFingerprint == lab4.history[0].bestFingerprint,
                "mejor huella idéntica 1 vs 4 workers");

    /* Verificar preservación bit-a-bit de los élites en population[0..eliteCount-1]. */
    uint64_t bestFp = lab1.history[0].bestFingerprint;
    TEST_ASSERT(lab1.population[0].metadata.fingerprint == bestFp,
                "élite #0 preservado exactamente en la siguiente generación");

    /* Guardar checkpoint en gen 1 y comprobar recuperación determinista. */
    const char *ckPath = "/tmp/test_lab_checkpoint.json";
    TEST_ASSERT(EvolutionCheckpoint_Save(&lab1, ckPath, err, sizeof(err)), "guardar checkpoint");

    /* Completar gen 1 en lab1. */
    TEST_ASSERT(BrainEvolutionLab_StepGeneration(&lab1, err, sizeof(err)), "gen 1 continuo");
    TEST_ASSERT(lab1.status == LAB_STATUS_VALIDATION_THRESHOLD_NOT_REACHED,
                "estado explícito no convergido cuando no se alcanza umbral");

    /* Reanudar desde checkpoint en un laboratorio limpio y verificar continuación idéntica. */
    BrainEvolutionLab labResumed;
    TEST_ASSERT(BrainEvolutionLab_Init(&labResumed, &cfg1, err, sizeof(err)), "init lab para resume");
    TEST_ASSERT(EvolutionCheckpoint_Load(&labResumed, ckPath, false, err, sizeof(err)), "cargar checkpoint compatible");
    TEST_ASSERT(labResumed.currentGeneration == 1u, "generación restaurada == 1");
    TEST_ASSERT(labResumed.historyCount == 1u, "historial previo restaurado desde checkpoint");
    TEST_ASSERT(FLOAT_NEAR(labResumed.history[0].bestTrainFitness, lab1.history[0].bestTrainFitness),
                "historial gen 0 restaurado coincide exactamente");
    TEST_ASSERT(BrainEvolutionLab_StepGeneration(&labResumed, err, sizeof(err)), "gen 1 reanudada");
    TEST_ASSERT(labResumed.historyCount == 2u, "historial tras gen 1 reanudada == 2");
    TEST_ASSERT(FLOAT_NEAR(labResumed.history[1].bestTrainFitness, lab1.history[1].bestTrainFitness),
                "continuación tras checkpoint produce exactamente el mismo fitness");
    TEST_ASSERT(labResumed.history[1].bestFingerprint == lab1.history[1].bestFingerprint,
                "continuación tras checkpoint produce exactamente la misma huella");

    /* Rechazar carga de checkpoint incompatible (otra especie). */
    BrainLabConfig cfgOther = cfg1;
    snprintf(cfgOther.speciesPath, sizeof(cfgOther.speciesPath), "species/velkar.species.json");
    BrainEvolutionLab labOther;
    TEST_ASSERT(BrainEvolutionLab_Init(&labOther, &cfgOther, err, sizeof(err)), "init lab otra especie");
    TEST_ASSERT(!EvolutionCheckpoint_Load(&labOther, ckPath, false, err, sizeof(err)),
                "rechaza reanudación de checkpoint de otra especie");

    /* Probar evolución mutation-only. */
    BrainLabConfig cfgMut = cfg1;
    cfgMut.mutationOnly = true;
    BrainEvolutionLab labMut;
    TEST_ASSERT(BrainEvolutionLab_Init(&labMut, &cfgMut, err, sizeof(err)), "init mutation_only");
    TEST_ASSERT(BrainEvolutionLab_StepGeneration(&labMut, err, sizeof(err)), "step mutation_only");

    /* Comprobar evaluación repetida limpia (sin fugas de estado digestivo ni recurrente). */
    EvolutionEpisodeMetrics epA, epB;
    const NeuralGenome *champ = BrainEvolutionLab_GetChampion(&lab1);
    TEST_ASSERT(EvolutionEvaluator_RunEpisode(&lab1.stages[0].env, &lab1.species, &lab1.db,
                                              champ, &lab1.config.fitness, 7777u, false,
                                              &epA, err, sizeof(err)), "episodio A");
    TEST_ASSERT(EvolutionEvaluator_RunEpisode(&lab1.stages[0].env, &lab1.species, &lab1.db,
                                              champ, &lab1.config.fitness, 7777u, false,
                                              &epB, err, sizeof(err)), "episodio B");
    TEST_ASSERT(FLOAT_NEAR(epA.fitness, epB.fitness) && epA.ticksSimulated == epB.ticksSimulated,
                "evaluaciones aisladas son 100% repetibles sin estado residual");

    BrainEvolutionLab_Destroy(&lab1);
    BrainEvolutionLab_Destroy(&lab4);
    BrainEvolutionLab_Destroy(&labResumed);
    BrainEvolutionLab_Destroy(&labOther);
    BrainEvolutionLab_Destroy(&labMut);
}

void run_brain_evolution_lab_tests(void) {
    test_species_and_environment_data_loading();
    test_clean_evaluation_reset_and_worker_determinism();
    printf("[PASS] brain_evolution_lab: especies/entornos, determinismo paralelo, élites, mutation-only y checkpoints\n");
}
