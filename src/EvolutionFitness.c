/**
 * @file EvolutionFitness.c
 * @brief Cálculo determinista y agregación estadística de aptitud fisiológica.
 */
#include "EvolutionFitness.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int cmpFloat(const void *a, const void *b) {
    float x = *(const float *)a;
    float y = *(const float *)b;
    return (x > y) - (x < y);
}

EvolutionFitnessConfig EvolutionFitnessConfig_Default(void) {
    return (EvolutionFitnessConfig){
        .survivalWeight = 0.45f,
        .healthWeight = 0.20f,
        .energyWeight = 0.20f,
        .hydrationWeight = 0.15f,
        .toxicityPenalty = 0.15f
    };
}

bool EvolutionFitnessConfig_Validate(const EvolutionFitnessConfig *cfg) {
    if (!cfg) return false;
    return isfinite(cfg->survivalWeight) && cfg->survivalWeight >= 0.0f &&
           isfinite(cfg->healthWeight) && cfg->healthWeight >= 0.0f &&
           isfinite(cfg->energyWeight) && cfg->energyWeight >= 0.0f &&
           isfinite(cfg->hydrationWeight) && cfg->hydrationWeight >= 0.0f &&
           isfinite(cfg->toxicityPenalty) && cfg->toxicityPenalty >= 0.0f;
}

float EvolutionFitness_Compute(const EvolutionFitnessConfig *cfg,
                               const EvolutionEpisodeMetrics *m) {
    if (!cfg || !m) return 0.0f;
    float score = cfg->survivalWeight * m->survivalFraction +
                  cfg->healthWeight * m->integratedHealth +
                  cfg->energyWeight * m->integratedEnergy +
                  cfg->hydrationWeight * m->integratedHydration -
                  cfg->toxicityPenalty * m->integratedToxicity;
    if (!isfinite(score)) return 0.0f;
    return score;
}

void EvolutionFitness_Aggregate(const EvolutionEpisodeMetrics *episodes, unsigned count,
                                EvolutionAggregateMetrics *out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (!episodes || count == 0u) return;

    float *fits = malloc(count * sizeof(float));
    double sumFit = 0.0, sumSurv = 0.0, sumH = 0.0, sumE = 0.0, sumW = 0.0, sumT = 0.0;
    double sumIng = 0.0, sumEAbs = 0.0, sumWAbs = 0.0;
    float minF = episodes[0].fitness, maxF = episodes[0].fitness;

    for (unsigned i = 0; i < count; ++i) {
        const EvolutionEpisodeMetrics *ep = &episodes[i];
        float f = isfinite(ep->fitness) ? ep->fitness : 0.0f;
        if (fits) fits[i] = f;
        sumFit += (double)f;
        if (f < minF) minF = f;
        if (f > maxF) maxF = f;
        sumSurv += ep->survived ? 1.0 : 0.0;
        sumH += (double)ep->integratedHealth;
        sumE += (double)ep->integratedEnergy;
        sumW += (double)ep->integratedHydration;
        sumT += (double)ep->integratedToxicity;
        sumIng += ep->totalIngestedMass;
        sumEAbs += ep->totalEnergyAbsorbed;
        sumWAbs += ep->totalHydrationAbsorbed;
        out->totalKills += ep->preyKills;
        out->totalCarcassesConsumed += ep->carcassesConsumed;
        for (unsigned a = 0; a < NEURAL_V2_ACTION_COUNT; ++a) {
            out->totalActionCounts[a] += ep->actionCounts[a];
        }
    }

    double invN = 1.0 / (double)count;
    out->episodeCount = count;
    out->meanFitness = (float)(sumFit * invN);
    out->minFitness = minF;
    out->maxFitness = maxF;
    out->survivalRate = (float)(sumSurv * invN);
    out->meanHealth = (float)(sumH * invN);
    out->meanEnergy = (float)(sumE * invN);
    out->meanHydration = (float)(sumW * invN);
    out->meanToxicity = (float)(sumT * invN);
    out->meanIngestedMass = sumIng * invN;
    out->meanEnergyAbsorbed = sumEAbs * invN;
    out->meanHydrationAbsorbed = sumWAbs * invN;

    double varSum = 0.0;
    for (unsigned i = 0; i < count; ++i) {
        double d = (double)episodes[i].fitness - (double)out->meanFitness;
        varSum += d * d;
    }
    double std = (count > 1u) ? sqrt(varSum / (double)(count - 1u)) : 0.0;
    out->stdFitness = (float)std;
    out->ci95HalfWidth = (count > 1u) ? (float)(1.96 * std / sqrt((double)count)) : 0.0f;

    if (fits) {
        qsort(fits, count, sizeof(float), cmpFloat);
        out->medianFitness = (count % 2u == 1u)
                                 ? fits[count / 2u]
                                 : 0.5f * (fits[count / 2u - 1u] + fits[count / 2u]);
        free(fits);
    } else {
        out->medianFitness = out->meanFitness;
    }
}

const char *EvolutionBaselinePolicy_Name(EvolutionBaselinePolicy policy) {
    switch (policy) {
    case EVO_BASELINE_ALWAYS_IDLE:      return "always_idle";
    case EVO_BASELINE_ALWAYS_REST:      return "always_rest";
    case EVO_BASELINE_ALWAYS_EXPLORE:   return "always_explore";
    case EVO_BASELINE_ALWAYS_ATTACK:    return "always_attack";
    case EVO_BASELINE_CONSUME_NEAREST:  return "consume_nearest";
    default:                            return "unknown";
    }
}
