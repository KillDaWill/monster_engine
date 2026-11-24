/**
 * @file EvolutionEvaluator.h
 * @brief Evaluador paralelo determinista de episodios aislados sobre el núcleo autoritativo.
 *
 * Usa exactamente el mismo @ref EcosystemWorld que Evolution Arena, sin dependencias
 * gráficas y con paralelismo a nivel de episodio (evitando sobre-suscripción de hilos).
 */
#ifndef EVOLUTION_EVALUATOR_H
#define EVOLUTION_EVALUATOR_H

#include "EvolutionEnvironment.h"
#include "EvolutionFitness.h"
#include "EvolutionJobSystem.h"
#include "EvolutionSpeciesRegistry.h"
#include "MetabolismIO.h"
#include "NeuralGenome.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Métricas de rendimiento medidas durante un lote de evaluaciones. */
typedef struct EvolutionEvalThroughput {
    uint64_t totalTicks;
    double totalCreatureSeconds;
    uint64_t episodesEvaluated;
    double wallSeconds;
    double ticksPerSecond;
    double creatureSecondsPerSecond;
    double episodesPerSecond;
} EvolutionEvalThroughput;

/** @brief Evalúa un genoma neuronal v2 en un episodio aislado determinista. */
bool EvolutionEvaluator_RunEpisode(const EvolutionEnvironmentConfig *env,
                                   const EvolutionSpeciesConfig *focalSpecies,
                                   const MetabolismDatabase *db,
                                   const NeuralGenome *genome,
                                   const EvolutionFitnessConfig *fitnessCfg,
                                   uint32_t episodeSeed,
                                   bool useAnatomicalOral,
                                   EvolutionEpisodeMetrics *outMetrics,
                                   char *error, size_t errorSize);

/** @brief Evalúa una política trivial de referencia (Sección 27.1) en un episodio aislado. */
bool EvolutionEvaluator_RunBaselineEpisode(const EvolutionEnvironmentConfig *env,
                                           const EvolutionSpeciesConfig *focalSpecies,
                                           const MetabolismDatabase *db,
                                           EvolutionBaselinePolicy policy,
                                           const EvolutionFitnessConfig *fitnessCfg,
                                           uint32_t episodeSeed,
                                           bool useAnatomicalOral,
                                           EvolutionEpisodeMetrics *outMetrics,
                                           char *error, size_t errorSize);

/**
 * @brief Evalúa una población completa de @p populationSize genomas sobre @p seedCount
 *        semillas en paralelo usando @p jobs (paralelismo a nivel de episodio, 1 hilo interno por mundo).
 *
 * El resultado es 100% determinista e independiente del orden de finalización de los trabajadores.
 */
bool EvolutionEvaluator_EvaluatePopulation(EvolutionJobSystem *jobs,
                                           const EvolutionEnvironmentConfig *env,
                                           const EvolutionSpeciesConfig *focalSpecies,
                                           const MetabolismDatabase *db,
                                           const NeuralGenome *genomes,
                                           unsigned populationSize,
                                           const uint32_t *seeds,
                                           unsigned seedCount,
                                           const EvolutionFitnessConfig *fitnessCfg,
                                           bool useAnatomicalOral,
                                           EvolutionAggregateMetrics *outPerGenome,
                                           EvolutionEvalThroughput *outThroughput,
                                           char *error, size_t errorSize);

#ifdef __cplusplus
}
#endif

#endif /* EVOLUTION_EVALUATOR_H */
