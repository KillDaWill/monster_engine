#include "EcosystemResources.h"
#include <math.h>
#include <string.h>

static float clamp01(float v) {
    if (!isfinite(v) || v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

EcosystemResource EcosystemResource_FromSubstance(const SubstanceDef *substance,
                                                  float initialMass,
                                                  float maximumMass,
                                                  ResourceRegenPolicy regenPolicy,
                                                  float regenerationRate,
                                                  ResourceDecayPolicy decayPolicy,
                                                  double lifetimeSeconds,
                                                  float decayRate,
                                                  MoleculeHandle decayTargetMolecule) {
    EcosystemResource r;
    memset(&r, 0, sizeof(r));
    r.molecular = true;
    r.accessible = true;
    r.nutritionalValue = 1.0f; /* No se usa en modo molecular; se conserva finito para compatibilidad. */
    r.biomass = fmaxf(0.0f, initialMass);
    r.maximumBiomass = fmaxf(r.biomass, maximumMass);
    r.regenPolicy = regenPolicy;
    r.regenerationRate = fmaxf(0.0f, regenerationRate);
    r.infinite = (regenPolicy == RESOURCE_REGEN_INFINITE);
    r.respawnDelaySeconds = 20.0f;
    r.minRelocateDistance = 7.0f;
    r.rarityWeight = 1.0f;
    r.decayPolicy = decayPolicy;
    r.remainingLifetime = fmax(0.0, lifetimeSeconds);
    r.decayRate = fmaxf(0.0f, decayRate);
    r.decayTargetMolecule = decayTargetMolecule;
    r.substanceHandle = SUBSTANCE_INVALID_HANDLE;

    if (substance && substance->valid) {
        r.substanceHandle = substance->handle;
        r.phase = substance->phase;
        r.presentationModel = substance->model;
        r.hardness = substance->hardness;
        r.color[0] = substance->color[0];
        r.color[1] = substance->color[1];
        r.color[2] = substance->color[2];
        r.componentCount = substance->componentCount;
        memcpy(r.composition, substance->components,
               sizeof(SubstanceComponent) * substance->componentCount);
        r.sensorySignature = substance->cachedSignature;
        /* Categoría de geometría/presentación únicamente (nunca dicta compatibilidad metabólica). */
        if (substance->phase == SUBSTANCE_PHASE_LIQUID) {
            r.category = ECO_WATER;
        } else if (substance->model == SUBSTANCE_MODEL_CARCASS) {
            r.category = ECO_MEAT;
        } else {
            r.category = ECO_PLANT;
        }
    } else {
        r.category = ECO_PLANT;
        r.phase = SUBSTANCE_PHASE_SOLID;
        r.presentationModel = SUBSTANCE_MODEL_GENERIC;
    }
    return r;
}

bool EcosystemResource_Available(const EcosystemResource *r) {
    return r && r->id && r->accessible && !r->depleted && (r->infinite || r->biomass > 1e-6f);
}

float EcosystemResource_AvailabilityFraction(const EcosystemResource *r) {
    if (!EcosystemResource_Available(r)) return 0.0f;
    if (r->infinite) return 1.0f;
    if (r->maximumBiomass <= 1e-6f) return r->biomass > 1e-6f ? 1.0f : 0.0f;
    return clamp01(r->biomass / r->maximumBiomass);
}

bool EcosystemResource_ShouldRemove(const EcosystemResource *r) {
    if (!r || !r->id) return false;
    if (!r->molecular) {
        return r->category == ECO_MEAT && r->biomass <= 0.0f;
    }
    if (r->decayPolicy != RESOURCE_DECAY_NONE) {
        return r->biomass <= 1e-6f || r->remainingLifetime <= 0.0;
    }
    return false;
}

void EcosystemResource_UpdateMolecular(EcosystemResource *r, const MoleculeRegistry *molecules, float dt) {
    if (!r || !r->id || !isfinite(dt) || dt <= 0.0f) return;
    if (!r->molecular) {
        EcosystemResource_Update(r, dt);
        return;
    }

    if (r->decayPolicy == RESOURCE_DECAY_MASS_LOSS) {
        if (r->remainingLifetime <= (double)dt) {
            r->remainingLifetime = 0.0;
            r->biomass = 0.0f;
        } else {
            r->biomass *= fmaxf(0.0f, 1.0f - dt / (float)r->remainingLifetime);
            r->remainingLifetime -= (double)dt;
        }
        return;
    }

    if (r->decayPolicy == RESOURCE_DECAY_DECOMPOSE) {
        if (r->remainingLifetime <= (double)dt) {
            r->remainingLifetime = 0.0;
            r->biomass = 0.0f;
            return;
        }
        /* Pérdida gradual de masa por descomposición ambiental y tiempo de vida acotado. */
        float lifeDecay = dt / (float)r->remainingLifetime;
        r->biomass = fmaxf(0.0f, r->biomass * (1.0f - lifeDecay * 0.35f));
        r->remainingLifetime -= (double)dt;

        /* Transformar conservativamente una fracción de orgánicos en decay_products (sin crear masa). */
        if (r->decayRate > 0.0f && r->decayTargetMolecule != MOLECULE_INVALID_HANDLE &&
            r->componentCount > 0u) {
            float converted = 0.0f;
            float rateStep = clamp01(r->decayRate * dt);
            int targetIdx = -1;
            for (uint16_t i = 0; i < r->componentCount; ++i) {
                if (r->composition[i].molecule == r->decayTargetMolecule) {
                    targetIdx = (int)i;
                    continue;
                }
                const MoleculeDef *mdef = molecules ? MoleculeRegistry_Get(molecules, r->composition[i].molecule) : NULL;
                /* Degradar compuestos orgánicos (aroma orgánico, lípidos o dulzor > 0.1). */
                bool organic = mdef && (mdef->sensory.channels[SENSORY_ORGANIC_AROMA] > 0.10f ||
                                        mdef->sensory.channels[SENSORY_LIPID_AROMA] > 0.10f ||
                                        mdef->sensory.channels[SENSORY_SWEETNESS] > 0.10f);
                if (organic && r->composition[i].fraction > 1e-4f) {
                    float delta = r->composition[i].fraction * rateStep;
                    r->composition[i].fraction = fmaxf(0.0f, r->composition[i].fraction - delta);
                    converted += delta;
                }
            }
            if (converted > 1e-7f) {
                r->hasCustomComposition = true;
                if (targetIdx >= 0) {
                    r->composition[targetIdx].fraction += converted;
                } else if (r->componentCount < SUBSTANCE_MAX_COMPONENTS) {
                    targetIdx = (int)r->componentCount;
                    r->composition[r->componentCount++] = (SubstanceComponent){
                        .molecule = r->decayTargetMolecule,
                        .fraction = converted
                    };
                }
                float decayFrac = (targetIdx >= 0) ? r->composition[targetIdx].fraction : 0.0f;
                /* Recomputar firma sensorial sólo cuando la composición cambie significativamente. */
                if (molecules && fabsf(decayFrac - r->lastSignatureDecayFraction) >= 0.015f) {
                    SubstanceRegistry_ComputeSignature(molecules, r->composition, r->componentCount,
                                                       NULL, &r->sensorySignature);
                    r->lastSignatureDecayFraction = decayFrac;
                    ++r->compositionRevision;
                }
            }
        }
        return;
    }

    if (r->regenPolicy == RESOURCE_REGEN_DEPLETE_RELOCATE && !r->infinite) {
        if (!r->depleted && r->biomass <= 1e-4f) {
            r->biomass = 0.0f;
            r->depleted = true;
            r->respawnTimer = fmaxf(1.0f, r->respawnDelaySeconds);
        }
        if (r->depleted) {
            if (r->respawnTimer > dt) {
                r->respawnTimer -= dt;
            } else {
                r->respawnTimer = 0.0f;
                r->needsRelocation = true;
            }
        }
        return;
    }

    if (r->regenPolicy == RESOURCE_REGEN_LINEAR && !r->infinite) {
        r->biomass = fminf(r->maximumBiomass, fmaxf(0.0f, r->biomass + r->regenerationRate * dt));
    }
}

void EcosystemResource_Update(EcosystemResource *r, float dt) {
    if (!r || !r->id || !isfinite(dt) || dt <= 0.0f) return;
    if (r->molecular) {
        EcosystemResource_UpdateMolecular(r, NULL, dt);
        return;
    }
    if (r->category == ECO_MEAT) {
        if (r->remainingLifetime <= (double)dt) {
            r->remainingLifetime = 0.0;
            r->biomass = 0.0f;
        } else {
            r->biomass *= fmaxf(0.0f, 1.0f - dt / (float)r->remainingLifetime);
            r->remainingLifetime -= (double)dt;
        }
    } else if (!r->infinite) {
        r->biomass = fminf(r->maximumBiomass, fmaxf(0.0f, r->biomass + r->regenerationRate * dt));
    }
}

float EcosystemResource_Consume(EcosystemResource *r, float requested) {
    if (!EcosystemResource_Available(r) || !isfinite(requested) || requested <= 0.0f) return 0.0f;
    float actual = r->infinite ? requested : fminf(requested, r->biomass);
    if (r->infinite) {
        r->externalMassIntroduced += (double)actual;
    } else {
        r->biomass = fmaxf(0.0f, r->biomass - actual);
        if (r->molecular && r->regenPolicy == RESOURCE_REGEN_DEPLETE_RELOCATE && r->biomass <= 1e-4f) {
            r->biomass = 0.0f;
            r->depleted = true;
            r->respawnTimer = fmaxf(1.0f, r->respawnDelaySeconds);
        }
    }
    return actual;
}
