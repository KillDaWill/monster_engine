/**
 * @file EvolutionEvaluator.c
 * @brief Ejecución headless determinista y paralela de episodios de evaluación.
 */
#include "EvolutionEvaluator.h"
#include "CreatureActions.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double nowSeconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static void applyBaselineOverride(EcosystemWorld *w, EcosystemEntity *focal,
                                  EvolutionBaselinePolicy policy) {
    if (!w || !focal || focal->physiology.cause) return;
    CreatureMind *m = &focal->mind;
    if (policy == EVO_BASELINE_ALWAYS_IDLE) {
        m->action = ACTION_IDLE;
        m->target = 0u;
    } else if (policy == EVO_BASELINE_ALWAYS_REST) {
        m->action = ACTION_REST;
        m->target = 0u;
    } else if (policy == EVO_BASELINE_ALWAYS_EXPLORE) {
        if (m->action != ACTION_WANDER) {
            m->action = ACTION_WANDER;
            m->target = 0u;
            m->destinationValid = false;
        }
    } else if (policy == EVO_BASELINE_ALWAYS_ATTACK) {
        AIObjectId nearest = 0u;
        float bestD = 1e30f;
        for (unsigned i = 0; i < m->perception.count; ++i) {
            const PerceptionEntry *pe = &m->perception.entries[i];
            if (pe->type == AI_CREATURE && pe->available && pe->visible) {
                float d = Vec3_Distance(focal->body.position, pe->position);
                if (d < bestD) { bestD = d; nearest = pe->id; }
            }
        }
        if (nearest) {
            if (m->target != nearest || (m->action != ACTION_CHASE && m->action != ACTION_ATTACK)) {
                m->action = ACTION_CHASE;
                m->target = nearest;
                m->destinationValid = false;
            }
        } else if (m->action != ACTION_WANDER) {
            m->action = ACTION_WANDER;
            m->target = 0u;
            m->destinationValid = false;
        }
    } else if (policy == EVO_BASELINE_CONSUME_NEAREST) {
        AIObjectId nearest = 0u;
        bool isLiquid = false;
        float bestD = 1e30f;
        for (unsigned i = 0; i < m->perception.count; ++i) {
            const PerceptionEntry *pe = &m->perception.entries[i];
            if (((pe->type == AI_FOOD && pe->available) || (pe->type == AI_WATER && pe->drinkable)) &&
                m->time >= pe->unreachableUntil) {
                float d = Vec3_Distance(focal->body.position, pe->position);
                if (d < bestD) {
                    bestD = d;
                    nearest = pe->id;
                    isLiquid = (pe->substancePhase == (uint8_t)SUBSTANCE_PHASE_LIQUID) || (pe->type == AI_WATER);
                }
            }
        }
        if (nearest) {
            CreatureAction want = isLiquid ? ACTION_APPROACH_WATER : ACTION_APPROACH_FOOD;
            CreatureAction active = isLiquid ? ACTION_DRINK : ACTION_EAT;
            if (m->target != nearest || (m->action != want && m->action != active)) {
                m->action = want;
                m->target = nearest;
                m->destinationValid = false;
            }
        } else if (m->action != ACTION_WANDER) {
            m->action = ACTION_WANDER;
            m->target = 0u;
            m->destinationValid = false;
        }
    }
}

static bool runEpisodeInternal(const EvolutionEnvironmentConfig *env,
                               const EvolutionSpeciesConfig *focalSpecies,
                               const MetabolismDatabase *db,
                               const NeuralGenome *genome,
                               bool useBaseline,
                               EvolutionBaselinePolicy baselinePolicy,
                               const EvolutionFitnessConfig *fitnessCfg,
                               uint32_t episodeSeed,
                               bool useAnatomicalOral,
                               EvolutionEpisodeMetrics *outMetrics,
                               char *error, size_t errorSize) {
    if (!env || !focalSpecies || !db || !fitnessCfg || !outMetrics) return false;
    memset(outMetrics, 0, sizeof(*outMetrics));
    outMetrics->seed = episodeSeed;
    outMetrics->horizonSeconds = env->episodeSeconds;

    EcosystemWorld w;
    AIObjectId focalId = 0u;
    if (!EvolutionEnvironment_BuildEpisodeWorld(&w, env, focalSpecies, db,
                                                useBaseline ? NULL : genome,
                                                episodeSeed, useAnatomicalOral,
                                                &focalId, error, errorSize)) {
        return false;
    }

    if (useBaseline) {
        EcosystemEntity *fe = EcosystemWorld_Get(&w, focalId);
        if (fe) {
            fe->mind.controllerType = CREATURE_CONTROLLER_UTILITY;
            for (int a = 0; a < ACTION_COUNT; ++a) fe->brain.weights[a] = 0.0f;
            fe->brain.weights[ACTION_EAT] = 1.0f;
            fe->brain.weights[ACTION_DRINK] = 1.0f;
            fe->brain.needs[NEED_HUNGER].enabled = true;
            fe->brain.needs[NEED_HUNGER].satisfactionRate = 0.2f;
            fe->brain.needs[NEED_HYDRATION].enabled = true;
            fe->brain.needs[NEED_HYDRATION].satisfactionRate = 0.2f;
            fe->brain.needs[NEED_REST].enabled = false;
            fe->mind.nextDecision = 1e9; /* Evitar que CreatureDecision sobreescriba la política trivial */
        }
    }

    uint64_t totalTicks = (uint64_t)llround(env->episodeSeconds / env->stepSeconds);
    if (totalTicks == 0u) totalTicks = 1u;

    double sumHealth = 0.0, sumEnergy = 0.0, sumHyd = 0.0, sumTox = 0.0;
    uint64_t aliveTicks = 0u;
    double prevIngested = 0.0;
    uint64_t prevKills = 0u;
    uint64_t prevAttacks = 0u;
    bool died = false;
    CreatureDeathCause deathCause = DEATH_NONE;

    for (uint64_t t = 0; t < totalTicks; ++t) {
        EcosystemEntity *fe = EcosystemWorld_Get(&w, focalId);
        if (!fe || fe->physiology.cause != DEATH_NONE) {
            died = true;
            if (fe && fe->physiology.cause != DEATH_NONE) deathCause = fe->physiology.cause;
            break;
        }
        if (useBaseline && (t % 12u == 0u)) {
            fe->mind.nextDecision = 1e9;
            applyBaselineOverride(&w, fe, baselinePolicy);
        }

        AIObjectId currentTarget = fe->mind.target;
        EcosystemWorld_Step(&w);
        ++outMetrics->ticksSimulated;

        if (w.statistics.attacks > prevAttacks) {
            outMetrics->attackHits += (w.statistics.attacks - prevAttacks);
            prevAttacks = w.statistics.attacks;
        }
        if (w.statistics.kills > prevKills) {
            outMetrics->preyKills += (w.statistics.kills - prevKills);
            prevKills = w.statistics.kills;
        }

        fe = EcosystemWorld_Get(&w, focalId);
        if (!fe || fe->physiology.cause != DEATH_NONE) {
            died = true;
            if (fe && fe->physiology.cause != DEATH_NONE) {
                deathCause = fe->physiology.cause;
            } else {
                /* Determinar causa a partir de los contadores de muerte del mundo. */
                for (int c = 1; c < DEATH_CAUSE_COUNT; ++c) {
                    if (w.statistics.deathCauses[c] > 0u) deathCause = (CreatureDeathCause)c;
                }
            }
            break;
        }

        ++aliveTicks;
        sumHealth += (double)fe->physiology.health;
        sumEnergy += (double)fe->physiology.energy;
        sumHyd += (double)fe->physiology.hydration;
        sumTox += (double)fminf(1.0f, fe->metabolism.toxicity);

        if (fe->metabolism.totalIngestedMass > prevIngested + 1e-6) {
            /* Rastrear causalidad caza -> cadáver -> consumo sin otorgar recompensa oculta. */
            if (currentTarget) {
                const EcosystemResource *res = EcosystemWorld_Resource(&w, currentTarget);
                if (res && res->killerCreatureId == focalId) {
                    ++outMetrics->carcassesConsumed;
                }
            }
            prevIngested = fe->metabolism.totalIngestedMass;
        }

        outMetrics->finalHealth = fe->physiology.health;
        outMetrics->finalEnergy = fe->physiology.energy;
        outMetrics->finalHydration = fe->physiology.hydration;
        outMetrics->finalToxicity = fe->metabolism.toxicity;
        outMetrics->totalIngestedMass = fe->metabolism.totalIngestedMass;
        outMetrics->totalEnergyAbsorbed = fe->metabolism.totalEnergyGenerated;
        outMetrics->totalHydrationAbsorbed = fe->metabolism.totalHydrationGenerated;
        outMetrics->totalNutrientsAbsorbed = fe->metabolism.totalNutrientsAbsorbed;
        outMetrics->totalMetabolicDamage = fe->metabolism.totalMetabolicDamage;
        outMetrics->totalToxinExposure = fe->metabolism.totalToxinExposure;
        for (unsigned a = 0; a < NEURAL_V2_ACTION_COUNT; ++a) {
            outMetrics->actionCounts[a] = fe->mind.neuralState.v2.actionCounts[a];
        }
    }

    /* El tiempo posterior a la muerte aporta exactamente cero a los términos positivos (Sección 27). */
    double invHorizonTicks = 1.0 / (double)totalTicks;
    outMetrics->survived = !died;
    outMetrics->deathCause = died ? deathCause : DEATH_NONE;
    outMetrics->survivalSeconds = (double)aliveTicks * env->stepSeconds;
    outMetrics->survivalFraction = (float)((double)aliveTicks * invHorizonTicks);
    outMetrics->integratedHealth = (float)(sumHealth * invHorizonTicks);
    outMetrics->integratedEnergy = (float)(sumEnergy * invHorizonTicks);
    outMetrics->integratedHydration = (float)(sumHyd * invHorizonTicks);
    outMetrics->integratedToxicity = (float)(sumTox * invHorizonTicks);
    outMetrics->fitness = EvolutionFitness_Compute(fitnessCfg, outMetrics);

    EcosystemWorld_Free(&w);
    return true;
}

bool EvolutionEvaluator_RunEpisode(const EvolutionEnvironmentConfig *env,
                                   const EvolutionSpeciesConfig *focalSpecies,
                                   const MetabolismDatabase *db,
                                   const NeuralGenome *genome,
                                   const EvolutionFitnessConfig *fitnessCfg,
                                   uint32_t episodeSeed,
                                   bool useAnatomicalOral,
                                   EvolutionEpisodeMetrics *outMetrics,
                                   char *error, size_t errorSize) {
    return runEpisodeInternal(env, focalSpecies, db, genome, false, EVO_BASELINE_ALWAYS_IDLE,
                              fitnessCfg, episodeSeed, useAnatomicalOral, outMetrics, error, errorSize);
}

bool EvolutionEvaluator_RunBaselineEpisode(const EvolutionEnvironmentConfig *env,
                                           const EvolutionSpeciesConfig *focalSpecies,
                                           const MetabolismDatabase *db,
                                           EvolutionBaselinePolicy policy,
                                           const EvolutionFitnessConfig *fitnessCfg,
                                           uint32_t episodeSeed,
                                           bool useAnatomicalOral,
                                           EvolutionEpisodeMetrics *outMetrics,
                                           char *error, size_t errorSize) {
    return runEpisodeInternal(env, focalSpecies, db, NULL, true, policy,
                              fitnessCfg, episodeSeed, useAnatomicalOral, outMetrics, error, errorSize);
}

typedef struct PopEvalCtx {
    const EvolutionEnvironmentConfig *env;
    const EvolutionSpeciesConfig *species;
    const MetabolismDatabase *db;
    const NeuralGenome *genomes;
    const uint32_t *seeds;
    unsigned seedCount;
    const EvolutionFitnessConfig *fitnessCfg;
    bool useAnatomicalOral;
    EvolutionEpisodeMetrics *results; /* [popSize * seedCount] */
    int anyFailed;
} PopEvalCtx;

static void popEvalWorker(void *opaque, unsigned start, unsigned end, unsigned workerIdx) {
    (void)workerIdx;
    PopEvalCtx *ctx = (PopEvalCtx *)opaque;
    for (unsigned idx = start; idx < end; ++idx) {
        unsigned gIdx = idx / ctx->seedCount;
        unsigned sIdx = idx % ctx->seedCount;
        char err[128] = {0};
        if (!EvolutionEvaluator_RunEpisode(ctx->env, ctx->species, ctx->db,
                                           &ctx->genomes[gIdx], ctx->fitnessCfg,
                                           ctx->seeds[sIdx], ctx->useAnatomicalOral,
                                           &ctx->results[idx], err, sizeof(err))) {
            __atomic_store_n(&ctx->anyFailed, 1, __ATOMIC_RELAXED);
        }
    }
}

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
                                           char *error, size_t errorSize) {
    if (!env || !focalSpecies || !db || !genomes || !seeds || !fitnessCfg || !outPerGenome ||
        populationSize == 0u || seedCount == 0u) {
        if (error && errorSize) snprintf(error, errorSize, "Argumentos inválidos en EvaluatePopulation");
        return false;
    }

    unsigned totalEpisodes = populationSize * seedCount;
    EvolutionEpisodeMetrics *results = calloc(totalEpisodes, sizeof(*results));
    if (!results) {
        if (error && errorSize) snprintf(error, errorSize, "Sin memoria para resultados de episodios");
        return false;
    }

    PopEvalCtx ctx = {
        .env = env, .species = focalSpecies, .db = db,
        .genomes = genomes, .seeds = seeds, .seedCount = seedCount,
        .fitnessCfg = fitnessCfg, .useAnatomicalOral = useAnatomicalOral,
        .results = results, .anyFailed = 0
    };

    double t0 = nowSeconds();
    EvolutionJobSystem_ParallelFor(jobs, totalEpisodes, 2u, popEvalWorker, &ctx);
    double elapsed = nowSeconds() - t0;

    if (ctx.anyFailed) {
        free(results);
        if (error && errorSize) snprintf(error, errorSize, "Fallo en al menos un episodio de evaluación");
        return false;
    }

    /* Reducción determinista en orden canónico de genoma y semilla. */
    uint64_t totalTicks = 0u;
    unsigned creaturesPerEpisode = 1u;
    for (unsigned b = 0; b < env->backgroundGroupCount; ++b) creaturesPerEpisode += env->backgrounds[b].count;
    for (unsigned g = 0; g < populationSize; ++g) {
        EvolutionFitness_Aggregate(&results[g * seedCount], seedCount, &outPerGenome[g]);
        for (unsigned s = 0; s < seedCount; ++s) {
            totalTicks += results[g * seedCount + s].ticksSimulated;
        }
    }

    if (outThroughput) {
        double secs = elapsed > 1e-9 ? elapsed : 1e-9;
        double simCreatureSecs = (double)totalTicks * env->stepSeconds * (double)creaturesPerEpisode;
        *outThroughput = (EvolutionEvalThroughput){
            .totalTicks = totalTicks,
            .totalCreatureSeconds = simCreatureSecs,
            .episodesEvaluated = totalEpisodes,
            .wallSeconds = elapsed,
            .ticksPerSecond = (double)totalTicks / secs,
            .creatureSecondsPerSecond = simCreatureSecs / secs,
            .episodesPerSecond = (double)totalEpisodes / secs
        };
    }

    free(results);
    return true;
}
