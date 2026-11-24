/**
 * @file BrainEvolutionLab.h
 * @brief Laboratorio evolutivo de cerebros con algoritmo genético generacional, currículo,
 *        evaluación paralela determinista, checkpoints atómicos y validación ciega.
 *
 * Mantiene estrictamente separada la optimización evolutiva artificial de este laboratorio
 * de la selección natural biológica de Evolution Arena.
 */
#ifndef BRAIN_EVOLUTION_LAB_H
#define BRAIN_EVOLUTION_LAB_H

#include "EvolutionEnvironment.h"
#include "EvolutionEvaluator.h"
#include "EvolutionFitness.h"
#include "EvolutionJobSystem.h"
#include "EvolutionSpeciesRegistry.h"
#include "MetabolismIO.h"
#include "NeuralGenome.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BRAIN_LAB_MAX_CURRICULUM_STAGES 8u
#define BRAIN_LAB_MAX_HISTORY 2048u

/** @brief Estado terminal o activo del experimento (nunca etiqueta falsamente como adaptado). */
typedef enum BrainLabStatus {
    LAB_STATUS_READY = 0,
    LAB_STATUS_RUNNING = 1,
    LAB_STATUS_CONVERGED = 2,
    LAB_STATUS_TRAINING_BUDGET_EXHAUSTED = 3,
    LAB_STATUS_VALIDATION_THRESHOLD_NOT_REACHED = 4,
    LAB_STATUS_STAGNATED = 5,
    LAB_STATUS_MANUALLY_STOPPED = 6
} BrainLabStatus;

/** @brief Etapa del currículo progresivo con umbrales documentados de viabilidad. */
typedef struct BrainLabCurriculumStage {
    char envPath[256];
    EvolutionEnvironmentConfig env;
    float advanceMinFitness;
    float advanceMinSurvival;
    unsigned minGenerationsInStage;
    bool completed;
    unsigned completedAtGeneration;
    float bestAchievedFitness;
    float bestAchievedSurvival;
} BrainLabCurriculumStage;

/** @brief Registro de una generación del entrenador (separado de generación biológica). */
typedef struct BrainLabGenerationRecord {
    uint32_t trainerGeneration;
    unsigned curriculumStage;
    float bestTrainFitness;
    float meanTrainFitness;
    float medianTrainFitness;
    float bestTrainSurvival;
    float validationFitness;
    float validationSurvival;
    float validationHealth;
    float validationEnergy;
    float validationHydration;
    bool hasValidation;
    float neuralDiversity;
    double evalSeconds;
    double episodesPerSecond;
    double ticksPerSecond;
    uint64_t bestFingerprint;
    uint64_t totalKills;
    uint64_t totalCarcassesConsumed;
} BrainLabGenerationRecord;

/** @brief Configuración completa y reproducible de un experimento de Brain Evolution Lab. */
typedef struct BrainLabConfig {
    char metabolismDir[256];
    char speciesPath[256];
    char environmentPath[256];
    char initialBrainPath[256];
    char outputDir[256];

    NeuralArchitectureType architecture;
    bool randomFounders;
    bool mutationOnly;
    bool useCurriculum;
    bool useAnatomicalOral;

    uint32_t seed;
    unsigned populationSize;
    unsigned eliteCount;
    unsigned tournamentSize;
    float randomImmigrantFraction;
    NeuralMutationConfig mutation;
    EvolutionFitnessConfig fitness;

    double episodeSecondsOverride; /**< >0 sobrescribe duración del episodio del entorno. */
    unsigned trainingSeedsPerGen;
    unsigned validationSeeds;
    unsigned testSeeds;
    unsigned validationInterval;   /**< Cada cuántas generaciones ejecutar validación (defecto 5). */
    unsigned checkpointInterval;   /**< Cada cuántas generaciones guardar checkpoint (defecto 10). */
    unsigned workers;              /**< 0=auto, 1=serial, 2..16. */

    /* Criterios explícitos de parada y aceptación (Sección 31). */
    unsigned maxGenerations;
    uint64_t maxEvaluatedEpisodes;
    double maxTrainingSeconds;
    unsigned stagnationLimit;
    float targetValidationFitness;
    float minValidationSurvival;
    float minValidationHealth;
    float minValidationEnergy;
    float minValidationHydration;

    /* Currículo opcional explícito (rutas de entornos en orden). */
    char curriculumPaths[BRAIN_LAB_MAX_CURRICULUM_STAGES][256];
    unsigned curriculumCount;
} BrainLabConfig;

/** @brief Estado vivo completo de una sesión de Brain Evolution Lab. */
typedef struct BrainEvolutionLab {
    BrainLabConfig config;
    MetabolismDatabase db;
    EvolutionSpeciesConfig species;
    BrainLabCurriculumStage stages[BRAIN_LAB_MAX_CURRICULUM_STAGES];
    unsigned stageCount;
    unsigned currentStage;
    unsigned generationsInCurrentStage;

    EvolutionJobSystem *jobs;
    unsigned workerCount;

    uint32_t rng;
    uint32_t currentGeneration;
    uint32_t lastImprovementGen;
    uint64_t totalEpisodesEvaluated;
    uint64_t totalTicksSimulated;
    double elapsedTrainingSeconds;

    /* Población actual y métricas evaluadas */
    NeuralGenome *population;
    EvolutionAggregateMetrics *popMetrics;
    unsigned *rankOrder;
    unsigned populationSize;

    /* Mejores candidatos rastreados independientemente */
    bool hasBestTraining;
    NeuralGenome bestTrainingGenome;
    EvolutionAggregateMetrics bestTrainingMetrics;
    uint32_t bestTrainingGeneration;

    bool hasBestValidation;
    NeuralGenome bestValidationGenome;
    EvolutionAggregateMetrics bestValidationMetrics;
    uint32_t bestValidationGeneration;

    bool hasTestEvaluation;
    EvolutionAggregateMetrics testMetrics;
    EvolutionEpisodeMetrics *testEpisodes;
    unsigned testEpisodeCount;

    /* Historial acotado para gráficos, CSV y checkpoints */
    BrainLabGenerationRecord *history;
    unsigned historyCount;
    unsigned historyCapacity;

    BrainLabStatus status;
    bool initialized;
} BrainEvolutionLab;

/** @brief Nombre textual del estado terminal o activo del laboratorio. */
const char *BrainLabStatus_Name(BrainLabStatus status);

/** @brief Configuración inicial recomendada del algoritmo genético (Sección 24). */
BrainLabConfig BrainLabConfig_Default(void);

/** @brief Valida la configuración del laboratorio antes de iniciar. */
bool BrainLabConfig_Validate(const BrainLabConfig *cfg, char *error, size_t errorSize);

/** @brief Inicializa registros, especies, entornos, pool paralelo y población fundadora. */
bool BrainEvolutionLab_Init(BrainEvolutionLab *lab, const BrainLabConfig *cfg,
                            char *error, size_t errorSize);

/** @brief Libera todos los recursos propiedad del laboratorio. */
void BrainEvolutionLab_Destroy(BrainEvolutionLab *lab);

/** @brief Ejecuta exactamente una generación del algoritmo genético (evaluación, ranking, validación, selección, cruce/mutación). */
bool BrainEvolutionLab_StepGeneration(BrainEvolutionLab *lab, char *error, size_t errorSize);

/** @brief Ejecuta validación del mejor candidato actual sobre las semillas de validación disjuntas. */
bool BrainEvolutionLab_RunValidation(BrainEvolutionLab *lab, EvolutionAggregateMetrics *outVal,
                                     char *error, size_t errorSize);

/** @brief Evalúa el campeón final en las semillas de prueba ciegas (jamás vistas en selección). */
bool BrainEvolutionLab_RunFinalTest(BrainEvolutionLab *lab, char *error, size_t errorSize);

/** @brief Evalúa el bucle completo de entrenamiento hasta criterio de parada y ejecuta test final. */
bool BrainEvolutionLab_Run(BrainEvolutionLab *lab, volatile bool *stopFlag,
                           char *error, size_t errorSize);

/** @brief Calcula diversidad neuronal media de la población actual del laboratorio. */
float BrainEvolutionLab_ComputeDiversity(const BrainEvolutionLab *lab);

/** @brief Devuelve puntero de sólo lectura al campeón actual (mejor validado o mejor entrenamiento). */
const NeuralGenome *BrainEvolutionLab_GetChampion(const BrainEvolutionLab *lab);

/** @brief Exporta todos los artefactos de salida (`champion.brain.json`, CSV, reportes JSON). */
bool BrainEvolutionLab_ExportOutputs(const BrainEvolutionLab *lab, char *error, size_t errorSize);

#ifdef __cplusplus
}
#endif

#endif /* BRAIN_EVOLUTION_LAB_H */
