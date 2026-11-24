/**
 * @file EvolutionFitness.h
 * @brief Función de aptitud configurable basada únicamente en viabilidad fisiológica.
 *
 * Pertenece exclusivamente a Brain Evolution Lab; jamás influye en la simulación
 * ecológica ordinaria de Evolution Arena. No premia nombres de alimentos ni acciones
 * específicas por especie.
 */
#ifndef EVOLUTION_FITNESS_H
#define EVOLUTION_FITNESS_H

#include "CreaturePhysiology.h"
#include "NeuralControllerV2.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Coeficientes experimentales configurables de aptitud fisiológica normalizada. */
typedef struct EvolutionFitnessConfig {
    float survivalWeight;    /**< Defecto 0.45 */
    float healthWeight;      /**< Defecto 0.20 */
    float energyWeight;      /**< Defecto 0.20 */
    float hydrationWeight;   /**< Defecto 0.15 */
    float toxicityPenalty;   /**< Defecto 0.15 */
} EvolutionFitnessConfig;

/** @brief Métricas completas recolectadas en un episodio de evaluación aislado. */
typedef struct EvolutionEpisodeMetrics {
    uint32_t seed;
    double horizonSeconds;
    double survivalSeconds;
    uint64_t ticksSimulated;
    float survivalFraction;
    float integratedHealth;
    float integratedEnergy;
    float integratedHydration;
    float integratedToxicity;
    float finalHealth;
    float finalEnergy;
    float finalHydration;
    float finalToxicity;
    bool survived;
    CreatureDeathCause deathCause;

    /* Diagnósticos observacionales (nunca usados como premios directos por especie/comida). */
    double totalIngestedMass;
    double totalEnergyAbsorbed;
    double totalHydrationAbsorbed;
    double totalNutrientsAbsorbed;
    double totalMetabolicDamage;
    double totalToxinExposure;
    uint64_t attackHits;
    uint64_t preyKills;
    uint64_t carcassesConsumed; /**< Ingesta desde cadáver generado por depredación del focal. */
    uint64_t actionCounts[NEURAL_V2_ACTION_COUNT];

    float fitness;
} EvolutionEpisodeMetrics;

/** @brief Resumen estadístico agregado sobre múltiples semillas/episodios. */
typedef struct EvolutionAggregateMetrics {
    unsigned episodeCount;
    float meanFitness;
    float medianFitness;
    float stdFitness;
    float ci95HalfWidth;
    float minFitness;
    float maxFitness;
    float survivalRate;
    float meanHealth;
    float meanEnergy;
    float meanHydration;
    float meanToxicity;
    double meanIngestedMass;
    double meanEnergyAbsorbed;
    double meanHydrationAbsorbed;
    uint64_t totalKills;
    uint64_t totalCarcassesConsumed;
    uint64_t totalActionCounts[NEURAL_V2_ACTION_COUNT];
} EvolutionAggregateMetrics;

/** @brief Políticas triviales de referencia para validar que no hay explotación de fitness. */
typedef enum EvolutionBaselinePolicy {
    EVO_BASELINE_ALWAYS_IDLE = 0,
    EVO_BASELINE_ALWAYS_REST = 1,
    EVO_BASELINE_ALWAYS_EXPLORE = 2,
    EVO_BASELINE_ALWAYS_ATTACK = 3,
    EVO_BASELINE_CONSUME_NEAREST = 4
} EvolutionBaselinePolicy;

/** @brief Configuración por defecto de la función de aptitud fisiológica. */
EvolutionFitnessConfig EvolutionFitnessConfig_Default(void);

/** @brief Valida finitud y no negatividad de los coeficientes de aptitud. */
bool EvolutionFitnessConfig_Validate(const EvolutionFitnessConfig *cfg);

/** @brief Calcula la puntuación de aptitud normalizada a partir de los integrales del episodio. */
float EvolutionFitness_Compute(const EvolutionFitnessConfig *cfg,
                               const EvolutionEpisodeMetrics *metrics);

/** @brief Agrega deterministamente una lista de resultados de episodios (media, mediana, IC95). */
void EvolutionFitness_Aggregate(const EvolutionEpisodeMetrics *episodes, unsigned count,
                                EvolutionAggregateMetrics *out);

/** @brief Nombre diagnóstico de una política trivial de referencia. */
const char *EvolutionBaselinePolicy_Name(EvolutionBaselinePolicy policy);

#ifdef __cplusplus
}
#endif

#endif /* EVOLUTION_FITNESS_H */
