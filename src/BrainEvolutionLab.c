/**
 * @file BrainEvolutionLab.c
 * @brief Algoritmo genético generacional determinista, currículo y exportación del laboratorio.
 */
#include "BrainEvolutionLab.h"
#include "EvolutionCheckpointIO.h"
#include "NeuralBrainIO.h"
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

static void setErr(char *err, size_t errSize, const char *fmt, ...) {
    if (!err || errSize == 0) return;
    va_list args;
    va_start(args, fmt);
    vsnprintf(err, errSize, fmt, args);
    va_end(args);
}

static double nowSeconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static void ensureDir(const char *dir) {
    if (!dir || !dir[0]) return;
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", dir);
    for (char *p = tmp + 1; *p; ++p) {
        if (*p == '/') { *p = '\0'; mkdir(tmp, 0755); *p = '/'; }
    }
    mkdir(tmp, 0755);
}

static uint32_t mixSeed(uint32_t a, uint32_t b, uint32_t c) {
    uint32_t x = a ^ (b * 2654435761u) ^ (c * 2246822519u);
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x ? x : 1u;
}

const char *BrainLabStatus_Name(BrainLabStatus status) {
    switch (status) {
    case LAB_STATUS_READY:                             return "READY";
    case LAB_STATUS_RUNNING:                           return "RUNNING";
    case LAB_STATUS_CONVERGED:                         return "CONVERGED";
    case LAB_STATUS_TRAINING_BUDGET_EXHAUSTED:         return "TRAINING_BUDGET_EXHAUSTED";
    case LAB_STATUS_VALIDATION_THRESHOLD_NOT_REACHED:  return "VALIDATION_THRESHOLD_NOT_REACHED";
    case LAB_STATUS_STAGNATED:                         return "STAGNATED";
    case LAB_STATUS_MANUALLY_STOPPED:                  return "MANUALLY_STOPPED";
    default:                                           return "UNKNOWN";
    }
}

BrainLabConfig BrainLabConfig_Default(void) {
    BrainLabConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    snprintf(cfg.metabolismDir, sizeof(cfg.metabolismDir), "metabolism");
    snprintf(cfg.speciesPath, sizeof(cfg.speciesPath), "species/brumacerno.species.json");
    snprintf(cfg.environmentPath, sizeof(cfg.environmentPath), "environments/brumacerno_mixed.environment.json");
    snprintf(cfg.outputDir, sizeof(cfg.outputDir), "build/brain-evolution/default");
    cfg.architecture = NEURAL_ARCH_CANDIDATE_POLICY;
    cfg.randomFounders = true;
    cfg.mutationOnly = false;
    cfg.useCurriculum = false;
    cfg.useAnatomicalOral = false; /* Modo rápido geométrico o anatómico completo */
    cfg.seed = 42u;
    cfg.populationSize = 128u;
    cfg.eliteCount = 8u;
    cfg.tournamentSize = 4u;
    cfg.randomImmigrantFraction = 0.05f;
    cfg.mutation = NeuralMutationConfig_Default();
    cfg.fitness = EvolutionFitnessConfig_Default();
    cfg.episodeSecondsOverride = 0.0;
    cfg.trainingSeedsPerGen = 3u;
    cfg.validationSeeds = 10u;
    cfg.testSeeds = 20u;
    cfg.validationInterval = 5u;
    cfg.checkpointInterval = 10u;
    cfg.workers = 0u; /* auto */
    cfg.maxGenerations = 200u;
    cfg.maxEvaluatedEpisodes = 0u;
    cfg.maxTrainingSeconds = 0.0;
    cfg.stagnationLimit = 40u;
    cfg.targetValidationFitness = 0.72f;
    cfg.minValidationSurvival = 0.90f;
    cfg.minValidationHealth = 0.75f;
    cfg.minValidationEnergy = 0.40f;
    cfg.minValidationHydration = 0.40f;
    return cfg;
}

bool BrainLabConfig_Validate(const BrainLabConfig *cfg, char *error, size_t errorSize) {
    if (!cfg) { setErr(error, errorSize, "Configuración nula"); return false; }
    if (cfg->architecture != NEURAL_ARCH_CANDIDATE_POLICY) {
        setErr(error, errorSize, "Brain Evolution Lab requiere arquitectura recurrent_candidate_policy (v2)");
        return false;
    }
    if (cfg->populationSize < 4u || cfg->populationSize > 4096u) {
        setErr(error, errorSize, "populationSize debe estar en [4, 4096]");
        return false;
    }
    if (cfg->eliteCount == 0u || cfg->eliteCount >= cfg->populationSize) {
        setErr(error, errorSize, "eliteCount debe estar en [1, populationSize - 1]");
        return false;
    }
    if (cfg->tournamentSize < 2u || cfg->tournamentSize > cfg->populationSize) {
        setErr(error, errorSize, "tournamentSize debe estar en [2, populationSize]");
        return false;
    }
    if (!isfinite(cfg->randomImmigrantFraction) || cfg->randomImmigrantFraction < 0.0f ||
        cfg->randomImmigrantFraction > 0.5f) {
        setErr(error, errorSize, "randomImmigrantFraction debe estar en [0.0, 0.5]");
        return false;
    }
    if (cfg->trainingSeedsPerGen == 0u || cfg->trainingSeedsPerGen > 32u) {
        setErr(error, errorSize, "trainingSeedsPerGen debe estar en [1, 32]");
        return false;
    }
    if (!NeuralMutationConfig_Validate(&cfg->mutation) ||
        !EvolutionFitnessConfig_Validate(&cfg->fitness)) {
        setErr(error, errorSize, "Parámetros de mutación o fitness inválidos");
        return false;
    }
    return true;
}

static bool loadStage(BrainLabCurriculumStage *st, const char *envPath, double episodeOverride,
                      const MetabolismDatabase *db, float advFit, float advSurv,
                      unsigned minGens, char *error, size_t errorSize) {
    memset(st, 0, sizeof(*st));
    snprintf(st->envPath, sizeof(st->envPath), "%s", envPath);
    if (!EvolutionEnvironment_LoadFile(envPath, &st->env, error, errorSize)) return false;
    if (episodeOverride > 0.0) st->env.episodeSeconds = episodeOverride;
    if (!EvolutionEnvironment_ValidateAgainstDatabase(&st->env, db, error, errorSize)) return false;
    st->advanceMinFitness = advFit;
    st->advanceMinSurvival = advSurv;
    st->minGenerationsInStage = minGens;
    return true;
}

bool BrainEvolutionLab_Init(BrainEvolutionLab *lab, const BrainLabConfig *cfg,
                            char *error, size_t errorSize) {
    if (!lab || !BrainLabConfig_Validate(cfg, error, errorSize)) return false;
    memset(lab, 0, sizeof(*lab));
    lab->config = *cfg;
    if (lab->config.mutationOnly) {
        lab->config.mutation.crossoverMode = NEURAL_CROSSOVER_MUTATION_ONLY;
    }
    lab->rng = cfg->seed ? cfg->seed : 42u;

    if (!MetabolismIO_LoadDatabase(cfg->metabolismDir, &lab->db, error, errorSize)) return false;
    if (!EvolutionSpeciesConfig_LoadFile(cfg->speciesPath, &lab->species, error, errorSize)) return false;

    /* Validar compilación de la especie focal antes de arrancar. */
    EcosystemSpecies testSp;
    if (!EvolutionSpeciesConfig_BuildEcosystemSpecies(&lab->species, &lab->db, 1u, &testSp, error, errorSize)) {
        return false;
    }

    /* Cargar etapas de currículo o entorno único. */
    if (cfg->useCurriculum && cfg->curriculumCount > 0u) {
        unsigned n = cfg->curriculumCount < BRAIN_LAB_MAX_CURRICULUM_STAGES
                         ? cfg->curriculumCount : BRAIN_LAB_MAX_CURRICULUM_STAGES;
        for (unsigned i = 0; i < n; ++i) {
            float advFit = (i + 1u < n) ? 0.56f : cfg->targetValidationFitness;
            float advSurv = (i + 1u < n) ? 0.80f : cfg->minValidationSurvival;
            if (!loadStage(&lab->stages[i], cfg->curriculumPaths[i], cfg->episodeSecondsOverride,
                           &lab->db, advFit, advSurv, 3u, error, errorSize)) {
                return false;
            }
        }
        lab->stageCount = n;
    } else {
        if (!loadStage(&lab->stages[0], cfg->environmentPath, cfg->episodeSecondsOverride,
                       &lab->db, cfg->targetValidationFitness, cfg->minValidationSurvival,
                       1u, error, errorSize)) {
            return false;
        }
        lab->stageCount = 1u;
    }

    unsigned workers = cfg->workers;
    if (workers == 0u) workers = EvolutionJobSystem_RecommendedWorkers();
    if (workers > 1u) {
        lab->jobs = EvolutionJobSystem_Create(workers);
    }
    lab->workerCount = lab->jobs ? EvolutionJobSystem_WorkerCount(lab->jobs) : 1u;

    lab->populationSize = cfg->populationSize;
    lab->population = calloc(lab->populationSize, sizeof(*lab->population));
    lab->popMetrics = calloc(lab->populationSize, sizeof(*lab->popMetrics));
    lab->rankOrder = calloc(lab->populationSize, sizeof(*lab->rankOrder));
    lab->historyCapacity = cfg->maxGenerations + 16u;
    if (lab->historyCapacity > BRAIN_LAB_MAX_HISTORY) lab->historyCapacity = BRAIN_LAB_MAX_HISTORY;
    lab->history = calloc(lab->historyCapacity, sizeof(*lab->history));
    if (!lab->population || !lab->popMetrics || !lab->rankOrder || !lab->history) {
        BrainEvolutionLab_Destroy(lab);
        setErr(error, errorSize, "Sin memoria al inicializar BrainEvolutionLab");
        return false;
    }

    /* Inicializar población fundadora: aleatoria o desde cerebro importado. */
    NeuralGenome seedBrain = {0};
    bool hasSeedBrain = false;
    if (cfg->initialBrainPath[0]) {
        if (!NeuralBrain_LoadFile(cfg->initialBrainPath, &seedBrain, error, errorSize)) {
            BrainEvolutionLab_Destroy(lab);
            return false;
        }
        if (seedBrain.architecture != cfg->architecture) {
            BrainEvolutionLab_Destroy(lab);
            setErr(error, errorSize, "El cerebro inicial '%s' no usa arquitectura recurrent_candidate_policy",
                   cfg->initialBrainPath);
            return false;
        }
        hasSeedBrain = true;
    }

    for (unsigned i = 0; i < lab->populationSize; ++i) {
        lab->rankOrder[i] = i;
        if (hasSeedBrain) {
            NeuralGenome_Clone(&lab->population[i], &seedBrain);
            lab->population[i].metadata.lineageId = (uint64_t)(i + 1u);
            if (i > 0u) {
                NeuralMutationConfig initMut = lab->config.mutation;
                initMut.mutationRate = fmaxf(initMut.mutationRate, 0.12f);
                initMut.mutationSigma = fmaxf(initMut.mutationSigma, 0.12f);
                NeuralGenome_Mutate(&lab->population[i], initMut, &lab->rng);
            }
            lab->population[i].metadata.founderFingerprint = lab->population[i].metadata.fingerprint;
        } else {
            if (!NeuralGenome_InitRandomV2(&lab->population[i], &lab->rng, 0.75f,
                                           lab->config.mutation.weightLimit, (uint64_t)(i + 1u))) {
                BrainEvolutionLab_Destroy(lab);
                setErr(error, errorSize, "Error al inicializar fundador aleatorio v2");
                return false;
            }
        }
    }

    lab->status = LAB_STATUS_READY;
    lab->initialized = true;
    return true;
}

void BrainEvolutionLab_Destroy(BrainEvolutionLab *lab) {
    if (!lab) return;
    if (lab->jobs) {
        EvolutionJobSystem_Destroy(lab->jobs);
        lab->jobs = NULL;
    }
    free(lab->population);
    free(lab->popMetrics);
    free(lab->rankOrder);
    free(lab->history);
    free(lab->testEpisodes);
    memset(lab, 0, sizeof(*lab));
}

float BrainEvolutionLab_ComputeDiversity(const BrainEvolutionLab *lab) {
    if (!lab || !lab->population || lab->populationSize < 2u) return 0.0f;
    unsigned n = lab->populationSize;
    uint32_t paramCount = NeuralGenome_ParamCount(&lab->population[0]);
    if (paramCount == 0u || paramCount > NEURAL_MAX_PARAM_COUNT) return 0.0f;
    double sumW[NEURAL_MAX_PARAM_COUNT];
    double sumSqW[NEURAL_MAX_PARAM_COUNT];
    float flat[NEURAL_MAX_PARAM_COUNT];
    memset(sumW, 0, sizeof(double) * paramCount);
    memset(sumSqW, 0, sizeof(double) * paramCount);
    for (unsigned i = 0; i < n; ++i) {
        NeuralGenome_CopyFlatParams(&lab->population[i], flat, paramCount);
        for (uint32_t p = 0; p < paramCount; ++p) {
            double v = (double)flat[p];
            sumW[p] += v;
            sumSqW[p] += v * v;
        }
    }
    double totalVar = 0.0;
    double dn = (double)n;
    for (uint32_t p = 0; p < paramCount; ++p) {
        double mean = sumW[p] / dn;
        double diff = sumSqW[p] - dn * mean * mean;
        if (diff > 0.0) totalVar += diff;
    }
    double meanSq = (2.0 * totalVar) / ((dn - 1.0) * (double)paramCount);
    return meanSq > 0.0 ? (float)sqrt(meanSq) : 0.0f;
}

static const BrainEvolutionLab *g_rankLab = NULL;

static int cmpRankIndex(const void *a, const void *b) {
    unsigned ia = *(const unsigned *)a;
    unsigned ib = *(const unsigned *)b;
    const EvolutionAggregateMetrics *ma = &g_rankLab->popMetrics[ia];
    const EvolutionAggregateMetrics *mb = &g_rankLab->popMetrics[ib];
    if (fabsf(ma->meanFitness - mb->meanFitness) > 1e-6f) {
        return (ma->meanFitness < mb->meanFitness) - (ma->meanFitness > mb->meanFitness);
    }
    if (fabsf(ma->survivalRate - mb->survivalRate) > 1e-6f) {
        return (ma->survivalRate < mb->survivalRate) - (ma->survivalRate > mb->survivalRate);
    }
    uint64_t fpa = g_rankLab->population[ia].metadata.fingerprint;
    uint64_t fpb = g_rankLab->population[ib].metadata.fingerprint;
    if (fpa != fpb) return (fpa > fpb) - (fpa < fpb);
    return (ia > ib) - (ia < ib);
}

static unsigned tournamentSelect(const BrainEvolutionLab *lab, uint32_t *rng) {
    unsigned bestRank = lab->populationSize;
    for (unsigned k = 0; k < lab->config.tournamentSize; ++k) {
        unsigned r = (unsigned)(NeuralGenome_RandomUnit(rng) * (float)lab->populationSize);
        if (r >= lab->populationSize) r = lab->populationSize - 1u;
        if (r < bestRank) bestRank = r; /* Menor rank == mayor aptitud */
    }
    return lab->rankOrder[bestRank];
}

static void generateDisjointSeeds(uint32_t baseSeed, uint32_t domainTag, uint32_t genOrStage,
                                  unsigned count, uint32_t *outSeeds) {
    for (unsigned i = 0; i < count; ++i) {
        outSeeds[i] = mixSeed(baseSeed, domainTag + genOrStage * 131u, i + 1u);
    }
}

bool BrainEvolutionLab_RunValidation(BrainEvolutionLab *lab, EvolutionAggregateMetrics *outVal,
                                     char *error, size_t errorSize) {
    if (!lab || !lab->initialized || !lab->hasBestTraining) return false;
    unsigned nSeeds = lab->config.validationSeeds ? lab->config.validationSeeds : 10u;
    uint32_t *valSeeds = malloc(nSeeds * sizeof(uint32_t));
    if (!valSeeds) return false;
    /* Dominio de semillas de validación completamente disjunto de entrenamiento y test. */
    generateDisjointSeeds(lab->config.seed, 0x56414C00u + lab->currentStage * 17u, 0u, nSeeds, valSeeds);

    const EvolutionEnvironmentConfig *env = &lab->stages[lab->currentStage].env;
    EvolutionAggregateMetrics agg = {0};
    EvolutionEvalThroughput tp = {0};
    bool ok = EvolutionEvaluator_EvaluatePopulation(lab->jobs, env, &lab->species, &lab->db,
                                                    &lab->bestTrainingGenome, 1u,
                                                    valSeeds, nSeeds, &lab->config.fitness,
                                                    lab->config.useAnatomicalOral, &agg, &tp,
                                                    error, errorSize);
    free(valSeeds);
    if (!ok) return false;

    lab->totalEpisodesEvaluated += tp.episodesEvaluated;
    lab->totalTicksSimulated += tp.totalTicks;
    if (outVal) *outVal = agg;

    if (!lab->hasBestValidation ||
        agg.meanFitness > lab->bestValidationMetrics.meanFitness + 1e-6f ||
        (fabsf(agg.meanFitness - lab->bestValidationMetrics.meanFitness) <= 1e-6f &&
         agg.survivalRate > lab->bestValidationMetrics.survivalRate)) {
        NeuralGenome_Clone(&lab->bestValidationGenome, &lab->bestTrainingGenome);
        snprintf(lab->bestValidationGenome.metadata.trainedSpecies,
                 sizeof(lab->bestValidationGenome.metadata.trainedSpecies), "%s", lab->species.id);
        snprintf(lab->bestValidationGenome.metadata.trainedEnvironment,
                 sizeof(lab->bestValidationGenome.metadata.trainedEnvironment), "%s", env->id);
        lab->bestValidationGenome.metadata.trainerGeneration = lab->currentGeneration;
        lab->bestValidationGenome.metadata.validationFitness = agg.meanFitness;
        lab->bestValidationGenome.metadata.validationSurvival = agg.survivalRate;
        snprintf(lab->bestValidationGenome.metadata.origin,
                 sizeof(lab->bestValidationGenome.metadata.origin), "evolved");
        lab->bestValidationMetrics = agg;
        lab->bestValidationGeneration = lab->currentGeneration;
        lab->hasBestValidation = true;
        lab->lastImprovementGen = lab->currentGeneration;
    }
    return true;
}

bool BrainEvolutionLab_StepGeneration(BrainEvolutionLab *lab, char *error, size_t errorSize) {
    if (!lab || !lab->initialized) return false;
    lab->status = LAB_STATUS_RUNNING;
    double tGen0 = nowSeconds();

    BrainLabCurriculumStage *stage = &lab->stages[lab->currentStage];
    unsigned sCount = lab->config.trainingSeedsPerGen;
    uint32_t trainSeeds[32];
    /* Semillas de entrenamiento compartidas por todos los candidatos de esta generación (reduce varianza). */
    generateDisjointSeeds(lab->config.seed, 0x54524100u + lab->currentStage * 31u,
                          lab->currentGeneration, sCount, trainSeeds);

    EvolutionEvalThroughput tp = {0};
    if (!EvolutionEvaluator_EvaluatePopulation(lab->jobs, &stage->env, &lab->species, &lab->db,
                                               lab->population, lab->populationSize,
                                               trainSeeds, sCount, &lab->config.fitness,
                                               lab->config.useAnatomicalOral,
                                               lab->popMetrics, &tp, error, errorSize)) {
        return false;
    }

    lab->totalEpisodesEvaluated += tp.episodesEvaluated;
    lab->totalTicksSimulated += tp.totalTicks;

    /* Ranking determinista. */
    for (unsigned i = 0; i < lab->populationSize; ++i) lab->rankOrder[i] = i;
    g_rankLab = lab;
    qsort(lab->rankOrder, lab->populationSize, sizeof(unsigned), cmpRankIndex);
    g_rankLab = NULL;

    unsigned bestIdx = lab->rankOrder[0];
    const EvolutionAggregateMetrics *bestM = &lab->popMetrics[bestIdx];

    /* Estadísticas de entrenamiento de la generación. */
    double sumFit = 0.0;
    uint64_t genKills = 0u, genCarcasses = 0u;
    for (unsigned i = 0; i < lab->populationSize; ++i) {
        sumFit += (double)lab->popMetrics[i].meanFitness;
        genKills += lab->popMetrics[i].totalKills;
        genCarcasses += lab->popMetrics[i].totalCarcassesConsumed;
    }
    float meanFit = (float)(sumFit / (double)lab->populationSize);
    float medianFit = lab->popMetrics[lab->rankOrder[lab->populationSize / 2u]].meanFitness;

    /* Actualizar mejor candidato de entrenamiento (siempre re-evaluado en esta generación). */
    bool improvedTrain = !lab->hasBestTraining ||
                         bestM->meanFitness > lab->bestTrainingMetrics.meanFitness + 1e-6f;
    NeuralGenome_Clone(&lab->bestTrainingGenome, &lab->population[bestIdx]);
    snprintf(lab->bestTrainingGenome.metadata.trainedSpecies,
             sizeof(lab->bestTrainingGenome.metadata.trainedSpecies), "%s", lab->species.id);
    snprintf(lab->bestTrainingGenome.metadata.trainedEnvironment,
             sizeof(lab->bestTrainingGenome.metadata.trainedEnvironment), "%s", stage->env.id);
    lab->bestTrainingGenome.metadata.trainerGeneration = lab->currentGeneration;
    snprintf(lab->bestTrainingGenome.metadata.origin,
             sizeof(lab->bestTrainingGenome.metadata.origin), "evolved");
    lab->bestTrainingMetrics = *bestM;
    lab->bestTrainingGeneration = lab->currentGeneration;
    lab->hasBestTraining = true;
    if (improvedTrain) lab->lastImprovementGen = lab->currentGeneration;

    if (bestM->meanFitness > stage->bestAchievedFitness) stage->bestAchievedFitness = bestM->meanFitness;
    if (bestM->survivalRate > stage->bestAchievedSurvival) stage->bestAchievedSurvival = bestM->survivalRate;

    /* Validación periódica o al final del presupuesto. */
    bool doVal = (lab->config.validationInterval == 0u) ||
                 ((lab->currentGeneration + 1u) % lab->config.validationInterval == 0u) ||
                 (lab->currentGeneration + 1u >= lab->config.maxGenerations) ||
                 (lab->currentGeneration == 0u);
    EvolutionAggregateMetrics valM = {0};
    bool hasVal = false;
    if (doVal && lab->config.validationSeeds > 0u) {
        if (!BrainEvolutionLab_RunValidation(lab, &valM, error, errorSize)) return false;
        hasVal = true;
    }

    float diversity = BrainEvolutionLab_ComputeDiversity(lab);
    double genSecs = nowSeconds() - tGen0;
    lab->elapsedTrainingSeconds += genSecs;

    /* Registrar historia generacional. */
    if (lab->historyCount < lab->historyCapacity) {
        lab->history[lab->historyCount++] = (BrainLabGenerationRecord){
            .trainerGeneration = lab->currentGeneration,
            .curriculumStage = lab->currentStage,
            .bestTrainFitness = bestM->meanFitness,
            .meanTrainFitness = meanFit,
            .medianTrainFitness = medianFit,
            .bestTrainSurvival = bestM->survivalRate,
            .validationFitness = hasVal ? valM.meanFitness : 0.0f,
            .validationSurvival = hasVal ? valM.survivalRate : 0.0f,
            .validationHealth = hasVal ? valM.meanHealth : 0.0f,
            .validationEnergy = hasVal ? valM.meanEnergy : 0.0f,
            .validationHydration = hasVal ? valM.meanHydration : 0.0f,
            .hasValidation = hasVal,
            .neuralDiversity = diversity,
            .evalSeconds = genSecs,
            .episodesPerSecond = tp.episodesPerSecond,
            .ticksPerSecond = tp.ticksPerSecond,
            .bestFingerprint = lab->population[bestIdx].metadata.fingerprint,
            .totalKills = genKills,
            .totalCarcassesConsumed = genCarcasses
        };
    }

    ++lab->generationsInCurrentStage;

    /* Comprobar avance de currículo basado exclusivamente en umbrales documentados de viabilidad. */
    if (hasVal && lab->currentStage + 1u < lab->stageCount) {
        if (lab->generationsInCurrentStage >= stage->minGenerationsInStage &&
            valM.meanFitness >= stage->advanceMinFitness &&
            valM.survivalRate >= stage->advanceMinSurvival) {
            stage->completed = true;
            stage->completedAtGeneration = lab->currentGeneration;
            /* Guardar checkpoint de frontera de etapa antes de avanzar. */
            if (lab->config.outputDir[0]) {
                char ckPath[512];
                snprintf(ckPath, sizeof(ckPath), "%s/checkpoint_stage_%u.json",
                         lab->config.outputDir, lab->currentStage);
                EvolutionCheckpoint_Save(lab, ckPath, NULL, 0);
            }
            ++lab->currentStage;
            lab->generationsInCurrentStage = 0u;
            /* Reiniciar mejor entrenamiento/validación para no comparar puntuaciones entre entornos distintos. */
            lab->hasBestValidation = false;
            lab->hasBestTraining = false;
            lab->lastImprovementGen = lab->currentGeneration;
        }
    }

    /* Comprobar convergencia en la etapa final. */
    if (hasVal && lab->currentStage + 1u == lab->stageCount) {
        if (valM.meanFitness >= lab->config.targetValidationFitness &&
            valM.survivalRate >= lab->config.minValidationSurvival &&
            valM.meanHealth >= lab->config.minValidationHealth &&
            valM.meanEnergy >= lab->config.minValidationEnergy &&
            valM.meanHydration >= lab->config.minValidationHydration) {
            stage->completed = true;
            stage->completedAtGeneration = lab->currentGeneration;
            lab->status = LAB_STATUS_CONVERGED;
        }
    }

    /* Construir siguiente generación si seguimos entrenando. */
    if (lab->status == LAB_STATUS_RUNNING &&
        lab->currentGeneration + 1u < lab->config.maxGenerations) {
        NeuralGenome *nextPop = calloc(lab->populationSize, sizeof(*nextPop));
        if (!nextPop) {
            setErr(error, errorSize, "Sin memoria al construir siguiente generación");
            return false;
        }

        unsigned elites = lab->config.eliteCount;
        unsigned immigrants = (unsigned)lroundf(lab->config.randomImmigrantFraction * (float)lab->populationSize);
        if (elites + immigrants > lab->populationSize) immigrants = lab->populationSize - elites;

        /* 1) Élites preservados exactamente (bit-a-bit). */
        for (unsigned i = 0; i < elites; ++i) {
            NeuralGenome_Clone(&nextPop[i], &lab->population[lab->rankOrder[i]]);
        }

        /* 2) Descendencia por torneo + mutación / recombinación opcional. */
        unsigned offspringEnd = lab->populationSize - immigrants;
        for (unsigned i = elites; i < offspringEnd; ++i) {
            unsigned pA = tournamentSelect(lab, &lab->rng);
            unsigned pB = lab->config.mutationOnly ? pA : tournamentSelect(lab, &lab->rng);
            NeuralGenome_Inherit(&nextPop[i], &lab->population[pA], &lab->population[pB],
                                 lab->config.mutation, &lab->rng);
        }

        /* 3) Inmigrantes aleatorios para sostener diversidad. */
        for (unsigned i = offspringEnd; i < lab->populationSize; ++i) {
            uint64_t lid = (uint64_t)(lab->currentGeneration + 1u) * 10000u + (uint64_t)i;
            NeuralGenome_InitRandomV2(&nextPop[i], &lab->rng, 0.75f,
                                      lab->config.mutation.weightLimit, lid);
        }

        free(lab->population);
        lab->population = nextPop;
    }

    ++lab->currentGeneration;

    /* Checkpoint periódico en frontera de generación. */
    if (lab->config.outputDir[0] && lab->config.checkpointInterval > 0u &&
        (lab->currentGeneration % lab->config.checkpointInterval == 0u)) {
        char ckPath[512];
        snprintf(ckPath, sizeof(ckPath), "%s/checkpoint.json", lab->config.outputDir);
        EvolutionCheckpoint_Save(lab, ckPath, NULL, 0);
    }

    /* Comprobar criterios de parada explícitos. */
    if (lab->status == LAB_STATUS_RUNNING) {
        if (lab->config.maxTrainingSeconds > 0.0 &&
            lab->elapsedTrainingSeconds >= lab->config.maxTrainingSeconds) {
            lab->status = LAB_STATUS_TRAINING_BUDGET_EXHAUSTED;
        } else if (lab->config.maxEvaluatedEpisodes > 0u &&
                   lab->totalEpisodesEvaluated >= lab->config.maxEvaluatedEpisodes) {
            lab->status = LAB_STATUS_TRAINING_BUDGET_EXHAUSTED;
        } else if (lab->currentGeneration >= lab->config.maxGenerations) {
            lab->status = LAB_STATUS_VALIDATION_THRESHOLD_NOT_REACHED;
        } else if (lab->config.stagnationLimit > 0u &&
                   lab->currentGeneration > lab->lastImprovementGen + lab->config.stagnationLimit) {
            lab->status = LAB_STATUS_STAGNATED;
        }
    }
    return true;
}

const NeuralGenome *BrainEvolutionLab_GetChampion(const BrainEvolutionLab *lab) {
    if (!lab || !lab->initialized) return NULL;
    if (lab->hasBestValidation) return &lab->bestValidationGenome;
    if (lab->hasBestTraining) return &lab->bestTrainingGenome;
    return lab->populationSize > 0u ? &lab->population[0] : NULL;
}

typedef struct BlindTestCtx {
    const EvolutionEnvironmentConfig *env;
    const EvolutionSpeciesConfig *species;
    const MetabolismDatabase *db;
    const NeuralGenome *champ;
    const EvolutionFitnessConfig *fitnessCfg;
    const uint32_t *seeds;
    bool useAnatomicalOral;
    EvolutionEpisodeMetrics *eps;
    int anyFailed;
} BlindTestCtx;

static void blindTestWorker(void *opaque, unsigned start, unsigned end, unsigned workerIdx) {
    (void)workerIdx;
    BlindTestCtx *ctx = (BlindTestCtx *)opaque;
    for (unsigned i = start; i < end; ++i) {
        char err[128] = {0};
        if (!EvolutionEvaluator_RunEpisode(ctx->env, ctx->species, ctx->db, ctx->champ,
                                           ctx->fitnessCfg, ctx->seeds[i], ctx->useAnatomicalOral,
                                           &ctx->eps[i], err, sizeof(err))) {
            __atomic_store_n(&ctx->anyFailed, 1, __ATOMIC_RELAXED);
        }
    }
}

bool BrainEvolutionLab_RunFinalTest(BrainEvolutionLab *lab, char *error, size_t errorSize) {
    if (!lab || !lab->initialized) return false;
    const NeuralGenome *champ = BrainEvolutionLab_GetChampion(lab);
    if (!champ) return false;
    unsigned nSeeds = lab->config.testSeeds ? lab->config.testSeeds : 20u;
    uint32_t *testSeeds = malloc(nSeeds * sizeof(uint32_t));
    EvolutionEpisodeMetrics *eps = calloc(nSeeds, sizeof(*eps));
    if (!testSeeds || !eps) { free(testSeeds); free(eps); return false; }

    /* Dominio de semillas de test ciego, jamás expuesto a la selección evolutiva. */
    generateDisjointSeeds(lab->config.seed, 0x54455354u, 999u, nSeeds, testSeeds);
    const EvolutionEnvironmentConfig *env = &lab->stages[lab->currentStage].env;

    BlindTestCtx ctx = {
        .env = env, .species = &lab->species, .db = &lab->db, .champ = champ,
        .fitnessCfg = &lab->config.fitness, .seeds = testSeeds,
        .useAnatomicalOral = lab->config.useAnatomicalOral, .eps = eps, .anyFailed = 0
    };
    EvolutionJobSystem_ParallelFor(lab->jobs, nSeeds, 2u, blindTestWorker, &ctx);
    free(testSeeds);
    if (ctx.anyFailed) {
        free(eps);
        setErr(error, errorSize, "Error en evaluación de test final ciego");
        return false;
    }
    free(lab->testEpisodes);
    lab->testEpisodes = eps;
    lab->testEpisodeCount = nSeeds;
    EvolutionFitness_Aggregate(eps, nSeeds, &lab->testMetrics);
    lab->hasTestEvaluation = true;
    return true;
}

bool BrainEvolutionLab_Run(BrainEvolutionLab *lab, volatile bool *stopFlag,
                           char *error, size_t errorSize) {
    if (!lab || !lab->initialized) return false;
    lab->status = LAB_STATUS_RUNNING;
    while (lab->status == LAB_STATUS_RUNNING) {
        if (stopFlag && *stopFlag) {
            lab->status = LAB_STATUS_MANUALLY_STOPPED;
            break;
        }
        if (!BrainEvolutionLab_StepGeneration(lab, error, errorSize)) return false;
    }
    if (lab->config.testSeeds > 0u) {
        BrainEvolutionLab_RunFinalTest(lab, error, errorSize);
    }
    return true;
}

bool BrainEvolutionLab_ExportOutputs(const BrainEvolutionLab *lab, char *error, size_t errorSize) {
    if (!lab || !lab->initialized || !lab->config.outputDir[0]) return false;
    ensureDir(lab->config.outputDir);
    char path[600];

    /* 1) Exportar cerebros independientes *.brain.json */
    const NeuralGenome *champ = BrainEvolutionLab_GetChampion(lab);
    if (champ) {
        snprintf(path, sizeof(path), "%s/champion.brain.json", lab->config.outputDir);
        if (!NeuralBrain_SaveFile(champ, path, true, error, errorSize)) return false;
    }
    if (lab->hasBestTraining) {
        snprintf(path, sizeof(path), "%s/best_training.brain.json", lab->config.outputDir);
        if (!NeuralBrain_SaveFile(&lab->bestTrainingGenome, path, true, error, errorSize)) return false;
    }
    if (lab->hasBestValidation) {
        snprintf(path, sizeof(path), "%s/best_validation.brain.json", lab->config.outputDir);
        if (!NeuralBrain_SaveFile(&lab->bestValidationGenome, path, true, error, errorSize)) return false;
    }

    /* 2) Checkpoint final */
    snprintf(path, sizeof(path), "%s/checkpoint.json", lab->config.outputDir);
    if (!EvolutionCheckpoint_Save(lab, path, error, errorSize)) return false;

    /* 3) training_history.csv */
    snprintf(path, sizeof(path), "%s/training_history.csv", lab->config.outputDir);
    FILE *csv = fopen(path, "w");
    if (!csv) { setErr(error, errorSize, "No se pudo escribir '%s'", path); return false; }
    fprintf(csv, "trainer_generation,stage,best_train_fitness,mean_train_fitness,median_train_fitness,"
                 "best_train_survival,val_fitness,val_survival,val_health,val_energy,val_hydration,"
                 "neural_diversity,eval_seconds,episodes_per_sec,ticks_per_sec,kills,carcasses_consumed,best_fingerprint\n");
    for (unsigned i = 0; i < lab->historyCount; ++i) {
        const BrainLabGenerationRecord *r = &lab->history[i];
        fprintf(csv, "%u,%u,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.4f,%.2f,%.1f,%llu,%llu,%016llx\n",
                r->trainerGeneration, r->curriculumStage,
                (double)r->bestTrainFitness, (double)r->meanTrainFitness, (double)r->medianTrainFitness,
                (double)r->bestTrainSurvival, (double)r->validationFitness, (double)r->validationSurvival,
                (double)r->validationHealth, (double)r->validationEnergy, (double)r->validationHydration,
                (double)r->neuralDiversity, r->evalSeconds, r->episodesPerSecond, r->ticksPerSecond,
                (unsigned long long)r->totalKills, (unsigned long long)r->totalCarcassesConsumed,
                (unsigned long long)r->bestFingerprint);
    }
    fclose(csv);

    /* 4) experiment.json y report.json */
    snprintf(path, sizeof(path), "%s/report.json", lab->config.outputDir);
    FILE *rep = fopen(path, "w");
    if (!rep) { setErr(error, errorSize, "No se pudo escribir '%s'", path); return false; }
    const EvolutionEnvironmentConfig *curEnv = &lab->stages[lab->currentStage].env;
    fprintf(rep,
            "{\n"
            "  \"status\": \"%s\",\n"
            "  \"species\": \"%s\",\n"
            "  \"metabolism_profile\": \"%s\",\n"
            "  \"environment\": \"%s\",\n"
            "  \"architecture\": \"%s\",\n"
            "  \"seed\": %u,\n"
            "  \"workers\": %u,\n"
            "  \"population_size\": %u,\n"
            "  \"mutation_only\": %s,\n"
            "  \"trainer_generations_completed\": %u,\n"
            "  \"curriculum_stage\": %u,\n"
            "  \"curriculum_stage_count\": %u,\n"
            "  \"total_episodes_evaluated\": %llu,\n"
            "  \"total_ticks_simulated\": %llu,\n"
            "  \"elapsed_seconds\": %.4f,\n"
            "  \"best_training\": {\"generation\": %u, \"fitness\": %.6f, \"survival\": %.6f, \"ingested_mass\": %.4f, \"energy_absorbed\": %.4f, \"hydration_absorbed\": %.4f},\n"
            "  \"best_validation\": {\"valid\": %s, \"generation\": %u, \"fitness\": %.6f, \"survival\": %.6f, \"health\": %.6f, \"energy\": %.6f, \"hydration\": %.6f, \"toxicity\": %.6f},\n"
            "  \"blind_test\": {\"valid\": %s, \"episodes\": %u, \"mean_fitness\": %.6f, \"median_fitness\": %.6f, \"std_fitness\": %.6f, \"ci95\": %.6f, \"min_fitness\": %.6f, \"max_fitness\": %.6f, \"survival_rate\": %.6f, \"mean_health\": %.6f, \"mean_energy\": %.6f, \"mean_hydration\": %.6f, \"mean_toxicity\": %.6f, \"mean_ingested_mass\": %.4f, \"mean_energy_absorbed\": %.4f, \"mean_hydration_absorbed\": %.4f, \"total_kills\": %llu, \"total_carcasses_consumed\": %llu}\n"
            "}\n",
            BrainLabStatus_Name(lab->status), lab->species.id, lab->species.metabolismProfileId,
            curEnv->id, NeuralArchitecture_Name(lab->config.architecture),
            lab->config.seed, lab->workerCount, lab->populationSize,
            lab->config.mutationOnly ? "true" : "false",
            lab->currentGeneration, lab->currentStage, lab->stageCount,
            (unsigned long long)lab->totalEpisodesEvaluated,
            (unsigned long long)lab->totalTicksSimulated,
            lab->elapsedTrainingSeconds,
            lab->bestTrainingGeneration, (double)lab->bestTrainingMetrics.meanFitness,
            (double)lab->bestTrainingMetrics.survivalRate,
            lab->bestTrainingMetrics.meanIngestedMass,
            lab->bestTrainingMetrics.meanEnergyAbsorbed,
            lab->bestTrainingMetrics.meanHydrationAbsorbed,
            lab->hasBestValidation ? "true" : "false", lab->bestValidationGeneration,
            (double)lab->bestValidationMetrics.meanFitness, (double)lab->bestValidationMetrics.survivalRate,
            (double)lab->bestValidationMetrics.meanHealth, (double)lab->bestValidationMetrics.meanEnergy,
            (double)lab->bestValidationMetrics.meanHydration, (double)lab->bestValidationMetrics.meanToxicity,
            lab->hasTestEvaluation ? "true" : "false", lab->testMetrics.episodeCount,
            (double)lab->testMetrics.meanFitness, (double)lab->testMetrics.medianFitness,
            (double)lab->testMetrics.stdFitness, (double)lab->testMetrics.ci95HalfWidth,
            (double)lab->testMetrics.minFitness, (double)lab->testMetrics.maxFitness,
            (double)lab->testMetrics.survivalRate, (double)lab->testMetrics.meanHealth,
            (double)lab->testMetrics.meanEnergy, (double)lab->testMetrics.meanHydration,
            (double)lab->testMetrics.meanToxicity, lab->testMetrics.meanIngestedMass,
            lab->testMetrics.meanEnergyAbsorbed, lab->testMetrics.meanHydrationAbsorbed,
            (unsigned long long)lab->testMetrics.totalKills,
            (unsigned long long)lab->testMetrics.totalCarcassesConsumed);
    fclose(rep);

    /* 5) validation_results.json (distribución completa de test ciego) */
    snprintf(path, sizeof(path), "%s/validation_results.json", lab->config.outputDir);
    FILE *vr = fopen(path, "w");
    if (vr) {
        fprintf(vr, "{\n  \"episodes\": [\n");
        for (unsigned i = 0; i < lab->testEpisodeCount; ++i) {
            const EvolutionEpisodeMetrics *ep = &lab->testEpisodes[i];
            fprintf(vr,
                    "%s    {\"seed\":%u,\"survived\":%s,\"death_cause\":%d,\"survival_fraction\":%.5f,"
                    "\"fitness\":%.5f,\"health\":%.5f,\"energy\":%.5f,\"hydration\":%.5f,\"toxicity\":%.5f,"
                    "\"ingested_mass\":%.4f,\"energy_absorbed\":%.4f,\"hydration_absorbed\":%.4f,"
                    "\"attacks\":%llu,\"kills\":%llu,\"carcasses_consumed\":%llu}",
                    i ? ",\n" : "", ep->seed, ep->survived ? "true" : "false", (int)ep->deathCause,
                    (double)ep->survivalFraction, (double)ep->fitness,
                    (double)ep->integratedHealth, (double)ep->integratedEnergy,
                    (double)ep->integratedHydration, (double)ep->integratedToxicity,
                    ep->totalIngestedMass, ep->totalEnergyAbsorbed, ep->totalHydrationAbsorbed,
                    (unsigned long long)ep->attackHits, (unsigned long long)ep->preyKills,
                    (unsigned long long)ep->carcassesConsumed);
        }
        fprintf(vr, "\n  ]\n}\n");
        fclose(vr);
    }

    /* 6) experiment.json (configuración reproducible) */
    snprintf(path, sizeof(path), "%s/experiment.json", lab->config.outputDir);
    FILE *ex = fopen(path, "w");
    if (ex) {
        fprintf(ex,
                "{\n"
                "  \"species_path\": \"%s\",\n"
                "  \"environment_path\": \"%s\",\n"
                "  \"metabolism_dir\": \"%s\",\n"
                "  \"architecture\": \"%s\",\n"
                "  \"founders\": \"%s\",\n"
                "  \"population\": %u,\n"
                "  \"elite_count\": %u,\n"
                "  \"tournament_size\": %u,\n"
                "  \"mutation_rate\": %.4f,\n"
                "  \"mutation_sigma\": %.4f,\n"
                "  \"mutation_only\": %s,\n"
                "  \"training_seeds\": %u,\n"
                "  \"validation_seeds\": %u,\n"
                "  \"test_seeds\": %u,\n"
                "  \"seed\": %u\n"
                "}\n",
                lab->config.speciesPath, lab->config.environmentPath, lab->config.metabolismDir,
                NeuralArchitecture_Name(lab->config.architecture),
                lab->config.initialBrainPath[0] ? "imported" : "random",
                lab->config.populationSize, lab->config.eliteCount, lab->config.tournamentSize,
                (double)lab->config.mutation.mutationRate, (double)lab->config.mutation.mutationSigma,
                lab->config.mutationOnly ? "true" : "false",
                lab->config.trainingSeedsPerGen, lab->config.validationSeeds,
                lab->config.testSeeds, lab->config.seed);
        fclose(ex);
    }
    return true;
}
