/**
 * @file CreatureMetabolism.c
 * @brief Motor de digestión progresiva, conservación de masa, desechos y toxicidad.
 */
#include "CreatureMetabolism.h"
#include <math.h>
#include <string.h>

static float clamp01(float v) {
    if (!isfinite(v) || v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

void CreatureMetabolism_Init(MetabolicState *state, const MetabolicProfile *profile,
                             float initialNutrients) {
    if (!state) return;
    float cap = (profile && profile->valid) ? profile->digestiveCapacity : 4.0f;
    MetabolicState_Init(state, cap, initialNutrients);
}

float CreatureMetabolism_MaxAcceptableMass(const MetabolicState *state,
                                           const MetabolicProfile *profile,
                                           float requestedMass, float dt) {
    if (!state || !state->initialized || !profile || !profile->valid ||
        !isfinite(requestedMass) || requestedMass <= 0.0f ||
        !isfinite(dt) || dt <= 0.0f) {
        return 0.0f;
    }
    /* Bloqueo físico de saciedad: con el tracto >= 92% lleno no se puede seguir ingiriendo. */
    float fullness = MetabolicState_Fullness(state);
    if (fullness >= 0.92f) return 0.0f;

    float remCap = MetabolicState_RemainingCapacity(state);
    if (remCap <= 1e-6f) return 0.0f;
    float satScale = (fullness > 0.75f) ? fmaxf(0.15f, (0.92f - fullness) / 0.17f) : 1.0f;
    float rateLimit = profile->maximumIngestionRate * satScale * dt;
    float allowed = fminf(requestedMass, fminf(rateLimit, remCap));
    return allowed > 0.0f ? allowed : 0.0f;
}

bool CreatureMetabolism_Ingest(MetabolicState *state,
                               const MetabolicProfile *profile,
                               SubstanceHandle substanceHandle,
                               const SubstanceComponent *components,
                               unsigned componentCount,
                               float acceptedMass,
                               double nowSeconds) {
    if (!state || !state->initialized || !profile || !profile->valid ||
        !components || componentCount == 0u ||
        !isfinite(acceptedMass) || acceptedMass <= 0.0f) {
        return false;
    }
    float addedTotal = 0.0f;
    for (unsigned i = 0; i < componentCount; ++i) {
        float compMass = acceptedMass * components[i].fraction;
        if (compMass > 1e-8f) {
            MetabolicState_AddMolecule(state, components[i].molecule, compMass);
            addedTotal += compMass;
        }
    }
    if (addedTotal <= 0.0f) return false;
    state->lastIngestedSubstance = substanceHandle;
    state->lastIngestedMass = addedTotal;
    state->lastIngestionTime = nowSeconds;
    state->recentIngestedMass += addedTotal;
    state->totalIngestedMass += (double)addedTotal;
    return true;
}

void CreatureMetabolism_Step(MetabolicState *state,
                             const MetabolicProfile *profile,
                             CreaturePhysiology *physiology,
                             const PhysiologyProfile *physProfile,
                             float dt,
                             MetabolicStepOutcome *outOutcome) {
    MetabolicStepOutcome outcome;
    memset(&outcome, 0, sizeof(outcome));
    if (!state || !state->initialized || !profile || !profile->valid ||
        !physiology || physiology->cause != DEATH_NONE ||
        !isfinite(dt) || dt <= 0.0f) {
        if (outOutcome) *outOutcome = outcome;
        return;
    }

    /* Reiniciar deltas instantáneos del paso actual. */
    state->recentEnergyAbsorbed = 0.0f;
    state->recentHydrationAbsorbed = 0.0f;
    state->recentNutrientsAbsorbed = 0.0f;
    state->recentToxicityDelta = 0.0f;
    state->recentMetabolicDamage = 0.0f;
    state->recentDigestiveEnergyCost = 0.0f;
    state->recentWasteEliminated = 0.0f;
    state->recentRejectedMass = 0.0f;

    /* Ruta rápida cuando no hay digestión activa, ni residuos, ni toxicidad ni malestar. */
    if (state->activeCount == 0u && state->wasteMass <= 1e-7f &&
        state->toxicity <= 1e-7f && state->digestiveDiscomfort <= 1e-7f) {
        float maint = profile->nutrientMaintenanceRate * dt;
        state->nutrientReserve = clamp01(state->nutrientReserve - maint);
        if (state->nutrientReserve > 0.30f && physiology->energy > 0.45f &&
            physiology->hydration > 0.45f && physiology->health < 1.0f) {
            physiology->health = clamp01(physiology->health + profile->nutrientRecoveryBonus * dt);
        }
        if (outOutcome) *outOutcome = outcome;
        return;
    }

    float toxBefore = state->toxicity;

    /* Acumulador temporal de productos de transformación explícita (no reprocesar en el mismo tick). */
    MoleculeHandle spawnedMol[METABOLIC_STATE_MAX_ACTIVE];
    float spawnedMass[METABOLIC_STATE_MAX_ACTIVE];
    unsigned spawnedCount = 0u;

    /* 1) Procesar únicamente componentes activos en el tracto digestivo (O(activeCount)). */
    for (uint16_t i = 0; i < state->activeCount; ++i) {
        MetabolicDigestiveEntry *entry = &state->entries[i];
        if (entry->mass <= 1e-7f || entry->molecule >= MOLECULE_MAX_COUNT) continue;

        int16_t rIdx = profile->ruleIndexByMolecule[entry->molecule];
        if (rIdx < 0 || (uint16_t)rIdx >= profile->ruleCount) {
            /* Material no procesable: transita a desecho a tasa configurable, nunca bloquea para siempre. */
            float passage = fminf(entry->mass, profile->unprocessablePassageRate * dt);
            entry->mass = fmaxf(0.0f, entry->mass - passage);
            state->wasteMass += passage;
            outcome.rejectedMass += passage;
            state->digestiveDiscomfort = clamp01(state->digestiveDiscomfort +
                                                 passage * profile->unprocessableDiscomfortFactor);
            continue;
        }

        const MetabolicProcessingRule *rule = &profile->rules[rIdx];
        float rateScale = (rule->hydrationYield > 0.0f && profile->waterAbsorptionRate > 0.0f)
                              ? profile->waterAbsorptionRate : 1.0f;
        float attempted = fminf(entry->mass, rule->rate * rateScale * dt);
        if (attempted <= 1e-8f) continue;

        entry->mass = fmaxf(0.0f, entry->mass - attempted);

        float effective = attempted * rule->efficiency;
        float rejected = attempted - effective;

        /* Coste energético digestivo */
        float digCost = attempted * rule->digestiveEnergyCost;
        outcome.digestiveEnergyCost += digCost;

        /* Efectos fisiológicos de la fracción eficazmente procesada */
        float eGain = effective * rule->energyYield;
        float hGain = effective * rule->hydrationYield;
        float nGain = effective * rule->nutrientYield;
        float toxGain = effective * rule->toxicityYield * profile->toxinSensitivity;
        float discGain = attempted * rule->discomfortYield;

        outcome.processedMass += attempted;
        outcome.energyGenerated += eGain;
        outcome.hydrationGenerated += hGain;
        outcome.nutrientsAbsorbed += nGain;
        outcome.toxicityAdded += toxGain;

        /* Conservación de masa: productos transformados + residuos + masa absorbida. */
        float transformedMass = 0.0f;
        for (uint8_t o = 0; o < rule->outputCount; ++o) {
            float prod = effective * rule->outputs[o].massRatio;
            if (prod > 1e-8f) {
                transformedMass += prod;
                if (spawnedCount < METABOLIC_STATE_MAX_ACTIVE) {
                    spawnedMol[spawnedCount] = rule->outputs[o].molecule;
                    spawnedMass[spawnedCount] = prod;
                    ++spawnedCount;
                } else {
                    state->wasteMass += prod;
                }
            }
        }
        float wasteFromEff = effective * rule->wasteFraction;
        float absorbed = fmaxf(0.0f, effective - transformedMass - wasteFromEff);
        outcome.absorbedMass += absorbed;
        outcome.rejectedMass += rejected;
        state->wasteMass += rejected + wasteFromEff;
        state->digestiveDiscomfort = clamp01(state->digestiveDiscomfort + discGain);
    }

    /* Añadir productos moleculares generados por transformaciones explícitas. */
    for (unsigned s = 0; s < spawnedCount; ++s) {
        MetabolicState_AddMolecule(state, spawnedMol[s], spawnedMass[s]);
    }
    MetabolicState_Compact(state);

    /* 2) Eliminación progresiva de residuos. */
    if (state->wasteMass > 1e-7f && profile->wasteEliminationRate > 0.0f) {
        float elim = fminf(state->wasteMass, profile->wasteEliminationRate * dt);
        state->wasteMass = fmaxf(0.0f, state->wasteMass - elim);
        outcome.wasteEliminated = elim;
    }

    /* 3) Acumular toxicidad y ejecutar detoxificación progresiva. */
    state->toxicity = fmaxf(0.0f, state->toxicity + outcome.toxicityAdded);
    if (state->toxicity > 1e-7f && profile->detoxificationRate > 0.0f) {
        float detox = fminf(state->toxicity, profile->detoxificationRate * dt);
        float detoxCost = detox * profile->detoxificationEnergyCost;
        if (physiology->energy >= detoxCost) {
            state->toxicity = fmaxf(0.0f, state->toxicity - detox);
            outcome.detoxified = detox;
            outcome.digestiveEnergyCost += detoxCost;
        } else if (detoxCost > 1e-7f) {
            float frac = clamp01(physiology->energy / detoxCost);
            float actualDetox = detox * frac;
            state->toxicity = fmaxf(0.0f, state->toxicity - actualDetox);
            outcome.detoxified = actualDetox;
            outcome.digestiveEnergyCost += physiology->energy;
        }
    }

    /* 4) Disipación gradual del malestar digestivo. */
    if (state->digestiveDiscomfort > 0.0f && profile->discomfortDecayRate > 0.0f) {
        state->digestiveDiscomfort = clamp01(state->digestiveDiscomfort -
                                             profile->discomfortDecayRate * dt);
    }

    /* 5) Aplicar consecuencias fisiológicas a energía, hidratación, nutrientes y salud. */
    float netEnergy = outcome.energyGenerated - outcome.digestiveEnergyCost;
    physiology->energy = clamp01(physiology->energy + netEnergy);
    physiology->hydration = clamp01(physiology->hydration + outcome.hydrationGenerated);

    float maint = profile->nutrientMaintenanceRate * dt;
    state->nutrientReserve = clamp01(state->nutrientReserve + outcome.nutrientsAbsorbed - maint);

    /* Beneficio real de nutrientes en recuperación tisular (no decorativo). */
    if (state->nutrientReserve > 0.30f && physiology->energy > 0.45f &&
        physiology->hydration > 0.45f && physiology->health < 1.0f &&
        state->toxicity <= profile->toxicityThreshold) {
        physiology->health = clamp01(physiology->health + profile->nutrientRecoveryBonus * dt);
    }

    /* Indigestión severa por atracón / sobreingestión sostenida. */
    if (state->digestiveDiscomfort > 0.65f) {
        float over = state->digestiveDiscomfort - 0.65f;
        float overCost = over * 0.018f * dt;
        physiology->energy = clamp01(physiology->energy - overCost);
        outcome.digestiveEnergyCost += overCost;
        if (state->digestiveDiscomfort > 0.85f) {
            float gDmg = (state->digestiveDiscomfort - 0.85f) * 0.025f * dt;
            float beforeH = physiology->health;
            physiology->health = clamp01(physiology->health - gDmg);
            outcome.metabolicDamage += (beforeH - physiology->health);
        }
    }

    /* Daño progresivo por exceso de toxicidad y causa de muerte dedicada DEATH_TOXICITY. */
    if (state->toxicity > profile->toxicityThreshold) {
        float excess = state->toxicity - profile->toxicityThreshold;
        float damage = excess * profile->toxicityDamageRate * dt;
        if (damage > 0.0f) {
            float beforeH = physiology->health;
            physiology->health = clamp01(physiology->health - damage);
            outcome.metabolicDamage += (beforeH - physiology->health);
            if (physiology->health <= 0.0f && physiology->cause == DEATH_NONE) {
                physiology->cause = DEATH_TOXICITY;
                physiology->stage = LIFE_DEAD;
                outcome.diedFromToxicity = true;
            }
        }
    }

    (void)physProfile;
    outcome.toxicityDelta = state->toxicity - toxBefore;

    /* 6) Actualizar retroalimentación reciente y acumuladores de vida. */
    state->recentEnergyAbsorbed = outcome.energyGenerated;
    state->recentHydrationAbsorbed = outcome.hydrationGenerated;
    state->recentNutrientsAbsorbed = outcome.nutrientsAbsorbed;
    state->recentToxicityDelta = outcome.toxicityDelta;
    state->recentMetabolicDamage = outcome.metabolicDamage;
    state->recentDigestiveEnergyCost = outcome.digestiveEnergyCost;
    state->recentWasteEliminated = outcome.wasteEliminated;
    state->recentRejectedMass = outcome.rejectedMass;

    state->totalProcessedMass += (double)outcome.processedMass;
    state->totalAbsorbedMass += (double)outcome.absorbedMass;
    state->totalRejectedMass += (double)outcome.rejectedMass;
    state->totalWasteEliminated += (double)outcome.wasteEliminated;
    state->totalEnergyGenerated += (double)outcome.energyGenerated;
    state->totalHydrationGenerated += (double)outcome.hydrationGenerated;
    state->totalNutrientsAbsorbed += (double)outcome.nutrientsAbsorbed;
    state->totalDigestiveEnergySpent += (double)outcome.digestiveEnergyCost;
    state->totalToxinExposure += (double)outcome.toxicityAdded;
    state->totalDetoxified += (double)outcome.detoxified;
    state->totalMetabolicDamage += (double)outcome.metabolicDamage;

    if (outOutcome) *outOutcome = outcome;
}
