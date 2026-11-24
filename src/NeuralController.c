/**
 * @file NeuralController.c
 * @brief Codificación sensorial (32), inferencia RNN a 5 Hz y traducción a acciones (10).
 */
#include "NeuralController.h"
#include "CreatureActions.h"
#include "CreatureMind.h"
#include "EcosystemResources.h"
#include <float.h>
#include <math.h>
#include <string.h>

static float clampf(float v, float lo, float hi) {
    if (!isfinite(v)) return lo;
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

NeuralControllerConfig NeuralControllerConfig_Default(void) {
    return (NeuralControllerConfig){
        .decisionInterval = 0.2f,    /* 5 Hz exactos */
        .commitmentDuration = 0.6f,  /* 3 ticks de decisión */
        .switchMargin = 0.10f,
        .explorationNoise = 0.0f
    };
}

bool NeuralControllerConfig_Validate(const NeuralControllerConfig *config) {
    if (!config) return false;
    return isfinite(config->decisionInterval) && config->decisionInterval >= 0.02f &&
           config->decisionInterval <= 5.0f &&
           isfinite(config->commitmentDuration) && config->commitmentDuration >= 0.0f &&
           config->commitmentDuration <= 10.0f &&
           isfinite(config->switchMargin) && config->switchMargin >= 0.0f &&
           config->switchMargin <= 5.0f &&
           isfinite(config->explorationNoise) && config->explorationNoise >= 0.0f &&
           config->explorationNoise <= 2.0f;
}

void NeuralControllerState_Reset(NeuralControllerState *state, NeuralControllerConfig config) {
    if (!state) return;
    memset(state, 0, sizeof(*state));
    state->config = NeuralControllerConfig_Validate(&config) ? config : NeuralControllerConfig_Default();
    state->selectedIntent = ACTION_INTENT_IDLE;
}

const char *NeuralActionIntent_Name(NeuralActionIntent intent) {
    static const char *names[ACTION_INTENT_COUNT] = {
        "IDLE", "EXPLORE", "REST", "HYDRATE", "FEED_PLANT",
        "FEED_MEAT", "HUNT", "FLEE", "MATE", "FOLLOW"
    };
    return (intent >= 0 && intent < ACTION_INTENT_COUNT) ? names[intent] : "INVALID";
}

const char *NeuralSensor_Name(unsigned index) {
    static const char *names[NEURAL_RNN_INPUTS] = {
        "ENERGY",     "HYDRATION",  "FATIGUE",    "HEALTH",
        "AGE_NORM",   "MATE_READY", "SPEED_NORM", "CAN_ATTACK",
        "WATER_CAP",  "FAIL_FB",    "INTAKE_FB",  "DAMAGE_FB",
        "PLANT_CONF", "PLANT_DIST", "PLANT_SIN",  "PLANT_COS",
        "MEAT_CONF",  "MEAT_DIST",  "MEAT_SIN",   "MEAT_COS",
        "WATER_CONF", "WATER_DIST", "WATER_SIN",  "WATER_COS",
        "CONSP_CONF", "CONSP_DIST", "CONSP_SIN",  "CONSP_COS",
        "OTHER_CONF", "OTHER_DIST", "OTHER_SIN",  "OTHER_COS"
    };
    return index < NEURAL_RNN_INPUTS ? names[index] : "UNKNOWN";
}

const char *NeuralInteractionOutcome_Name(NeuralInteractionOutcome outcome) {
    switch (outcome) {
    case NEURAL_OUTCOME_NONE:           return "NONE";
    case NEURAL_OUTCOME_CONSUMED_PLANT: return "CONSUMED_PLANT";
    case NEURAL_OUTCOME_CONSUMED_MEAT:  return "CONSUMED_MEAT";
    case NEURAL_OUTCOME_DRANK_WATER:    return "DRANK_WATER";
    case NEURAL_OUTCOME_ATTACK_HIT:     return "ATTACK_HIT";
    case NEURAL_OUTCOME_MATED:          return "MATED";
    case NEURAL_OUTCOME_FAILED_ACTION:  return "FAILED_ACTION";
    case NEURAL_OUTCOME_FAILED_DIET:    return "FAILED_DIET";
    case NEURAL_OUTCOME_FAILED_ATTACK:  return "FAILED_ATTACK";
    case NEURAL_OUTCOME_FAILED_MATE:    return "FAILED_MATE";
    case NEURAL_OUTCOME_TOOK_DAMAGE:    return "TOOK_DAMAGE";
    default:                            return "UNKNOWN";
    }
}

typedef struct CandidateBest {
    const PerceptionEntry *entry;
    bool notCooling;
    float dist;
} CandidateBest;

static bool isBetterCandidate(const CandidateBest *cur, const PerceptionEntry *cand,
                              bool notCooling, float dist) {
    if (!cur->entry) return true;
    /* Prioridad determinista: sin cooldown > visible > más cercano > menor ID. */
    if (notCooling != cur->notCooling) return notCooling;
    if (cand->visible != cur->entry->visible) return cand->visible;
    if (fabsf(dist - cur->dist) > 1e-4f) return dist < cur->dist;
    return cand->id < cur->entry->id;
}

static void encodeExternalGroup(const PerceptionEntry *entry, const CreatureBodyState *body,
                                float range, double memoryDuration, double now, float out[4]) {
    if (!entry) {
        out[0] = 0.0f;
        out[1] = 1.0f;
        out[2] = 0.0f;
        out[3] = 0.0f;
        return;
    }
    float confidence = 1.0f;
    if (!entry->visible) {
        double memAge = now - entry->observed;
        if (memAge < 0.0) memAge = 0.0;
        double dur = memoryDuration > 0.01 ? memoryDuration : 0.01;
        confidence = clampf(0.5f * (1.0f - (float)(memAge / dur)), 0.1f, 0.5f);
    }
    float dx = entry->position.x - body->position.x;
    float dz = entry->position.z - body->position.z;
    float planar = sqrtf(dx * dx + dz * dz);
    float invRange = 1.0f / fmaxf(range, 0.01f);
    float normDist = clampf(planar * invRange, 0.0f, 1.0f);
    float sinB = 0.0f, cosB = 1.0f;
    if (planar > 1e-6f) {
        float sYaw = sinf(body->yaw);
        float cYaw = cosf(body->yaw);
        cosB = clampf((dx * sYaw + dz * cYaw) / planar, -1.0f, 1.0f);
        sinB = clampf((dx * cYaw - dz * sYaw) / planar, -1.0f, 1.0f);
    }
    out[0] = confidence;
    out[1] = normDist;
    out[2] = sinB;
    out[3] = cosB;
}

void NeuralController_EncodeSensors(const CreaturePerception *perception,
                                    const PerceptionProfile *perceptionProfile,
                                    const CreatureBodyState *body,
                                    double now,
                                    const float pendingFeedback[3],
                                    float outInputs[NEURAL_RNN_INPUTS],
                                    NeuralSensorCandidates *outCandidates) {
    if (!outInputs || !body) return;
    memset(outInputs, 0, sizeof(float) * NEURAL_RNN_INPUTS);
    NeuralSensorCandidates cands = {0};

    float maxSpeed = fmaxf(body->capabilities.maximumSpeed, 0.01f);
    outInputs[0]  = clampf(body->energy, 0.0f, 1.0f);
    outInputs[1]  = clampf(body->hydration, 0.0f, 1.0f);
    outInputs[2]  = clampf(body->fatigue, 0.0f, 1.0f);
    outInputs[3]  = clampf(body->health, 0.0f, 1.0f);
    outInputs[4]  = clampf(body->normalizedAge, 0.0f, 1.0f);
    outInputs[5]  = body->mateReady ? 1.0f : 0.0f;
    outInputs[6]  = clampf(body->speed / maxSpeed, 0.0f, 1.0f);
    outInputs[7]  = body->canAttack ? 1.0f : 0.0f;
    outInputs[8]  = body->capabilities.waterInteraction ? 1.0f : 0.0f;
    outInputs[9]  = pendingFeedback ? clampf(pendingFeedback[0], 0.0f, 1.0f) : 0.0f;
    outInputs[10] = pendingFeedback ? clampf(pendingFeedback[1], 0.0f, 1.0f) : 0.0f;
    outInputs[11] = pendingFeedback ? clampf(pendingFeedback[2], 0.0f, 1.0f) : 0.0f;

    CandidateBest bestPlant = {0}, bestMeat = {0}, bestWater = {0};
    CandidateBest bestConspecific = {0}, bestMate = {0}, bestOther = {0};

    if (perception) {
        for (unsigned i = 0; i < perception->count; ++i) {
            const PerceptionEntry *e = &perception->entries[i];
            float dx = e->position.x - body->position.x;
            float dz = e->position.z - body->position.z;
            float dist = sqrtf(dx * dx + dz * dz);
            bool notCooling = (now >= e->unreachableUntil);

            if (e->type == AI_FOOD && e->available) {
                if (e->foodCategory == ECO_PLANT) {
                    if (isBetterCandidate(&bestPlant, e, notCooling, dist)) {
                        bestPlant = (CandidateBest){e, notCooling, dist};
                    }
                } else if (e->foodCategory == ECO_MEAT) {
                    if (isBetterCandidate(&bestMeat, e, notCooling, dist)) {
                        bestMeat = (CandidateBest){e, notCooling, dist};
                    }
                }
            } else if (e->type == AI_WATER && e->drinkable) {
                if (isBetterCandidate(&bestWater, e, notCooling, dist)) {
                    bestWater = (CandidateBest){e, notCooling, dist};
                }
            } else if (e->type == AI_CREATURE && e->available) {
                bool sameSpecies = (e->species && body->species)
                                       ? (e->species == body->species)
                                       : (e->group == body->group);
                if (sameSpecies) {
                    if (isBetterCandidate(&bestConspecific, e, notCooling, dist)) {
                        bestConspecific = (CandidateBest){e, notCooling, dist};
                    }
                    bool compatibleMate = e->reproductiveReady && (e->group != body->reproductiveRole);
                    if (compatibleMate && isBetterCandidate(&bestMate, e, notCooling, dist)) {
                        bestMate = (CandidateBest){e, notCooling, dist};
                    }
                } else {
                    if (isBetterCandidate(&bestOther, e, notCooling, dist)) {
                        bestOther = (CandidateBest){e, notCooling, dist};
                    }
                }
            }
        }
    }

    /* Si la criatura está lista para reproducirse y percibe una pareja compatible,
     * el grupo conspecífico publica esa pareja para que el cerebro pueda evaluarla. */
    const PerceptionEntry *reportedConspecific =
        (body->mateReady && bestMate.entry) ? bestMate.entry : bestConspecific.entry;

    float range = perceptionProfile ? perceptionProfile->range : 14.0f;
    double memDuration = perceptionProfile ? (double)perceptionProfile->memoryDuration : 30.0;

    encodeExternalGroup(bestPlant.entry, body, range, memDuration, now, &outInputs[12]);
    encodeExternalGroup(bestMeat.entry, body, range, memDuration, now, &outInputs[16]);
    encodeExternalGroup(bestWater.entry, body, range, memDuration, now, &outInputs[20]);
    encodeExternalGroup(reportedConspecific, body, range, memDuration, now, &outInputs[24]);
    encodeExternalGroup(bestOther.entry, body, range, memDuration, now, &outInputs[28]);

    cands.plantId = (bestPlant.entry && bestPlant.notCooling) ? bestPlant.entry->id : 0u;
    cands.meatId = (bestMeat.entry && bestMeat.notCooling) ? bestMeat.entry->id : 0u;
    cands.waterId = (bestWater.entry && bestWater.notCooling) ? bestWater.entry->id : 0u;
    cands.conspecificId = reportedConspecific ? reportedConspecific->id : 0u;
    cands.mateCandidateId = (bestMate.entry && bestMate.notCooling && bestMate.entry->visible)
                                ? bestMate.entry->id : 0u;
    cands.otherSpeciesId = bestOther.entry ? bestOther.entry->id : 0u;
    cands.otherSpeciesVisible = bestOther.entry ? bestOther.entry->visible : false;

    if (outCandidates) *outCandidates = cands;
}

static bool intentMatchesActiveInteraction(const CreatureMind *m, NeuralActionIntent intent) {
    if (!m->target || m->interactionPhase < INTERACTION_ALIGN ||
        m->interactionPhase > INTERACTION_CONSUME)
        return false;
    if (intent == ACTION_INTENT_HYDRATE) {
        return m->action == ACTION_DRINK || m->action == ACTION_APPROACH_WATER;
    }
    if (intent == ACTION_INTENT_FEED_PLANT) {
        return (m->action == ACTION_EAT || m->action == ACTION_APPROACH_FOOD) &&
               m->desiredFoodCategory == ECO_PLANT;
    }
    if (intent == ACTION_INTENT_FEED_MEAT) {
        return (m->action == ACTION_EAT || m->action == ACTION_APPROACH_FOOD) &&
               m->desiredFoodCategory == ECO_MEAT;
    }
    return false;
}

void NeuralController_Update(CreatureMind *m, const AIWorld *w, const CreatureBodyState *b) {
    (void)w;
    if (!m || !b || !m->profile) return;
    if (!m->neuralBrain.genome.valid ||
        m->neuralBrain.genome.architecture != NEURAL_ARCH_FIXED_RNN ||
        m->neuralBrain.genome.inputCount != NEURAL_RNN_INPUTS ||
        m->neuralBrain.genome.outputCount != NEURAL_RNN_OUTPUTS) {
        m->action = ACTION_IDLE;
        m->goal = GOAL_EXPLORE;
        m->target = 0;
        return;
    }
    if (m->retreating) return;
    if (m->time < m->recoveryUntil) return;
    if (m->time < m->nextDecision) return;

    NeuralControllerState *st = &m->neuralState;
    if (!NeuralControllerConfig_Validate(&st->config)) {
        st->config = NeuralControllerConfig_Default();
    }
    m->nextDecision = m->time + (double)st->config.decisionInterval;

    /* Enclavar retroalimentación real acumulada desde la decisión anterior. */
    float feedback[3] = {
        clampf(st->pendingActionFailure, 0.0f, 1.0f),
        clampf(st->pendingNutrientIntake, 0.0f, 1.0f),
        clampf(st->pendingDamageReceived, 0.0f, 1.0f)
    };
    st->lastFeedback[0] = feedback[0];
    st->lastFeedback[1] = feedback[1];
    st->lastFeedback[2] = feedback[2];
    st->pendingActionFailure = 0.0f;
    st->pendingNutrientIntake = 0.0f;
    st->pendingDamageReceived = 0.0f;

    float inputs[NEURAL_RNN_INPUTS];
    NeuralSensorCandidates cands = {0};
    NeuralController_EncodeSensors(&m->perception, &m->profile->perception, b,
                                   m->time, feedback, inputs, &cands);
    st->candidates = cands;

    float outputs[NEURAL_RNN_OUTPUTS];
    if (!NeuralBrain_Evaluate(&m->neuralBrain.genome, &m->neuralBrain.runtime,
                              inputs, NEURAL_RNN_INPUTS, outputs, NEURAL_RNN_OUTPUTS)) {
        m->action = ACTION_IDLE;
        m->goal = GOAL_EXPLORE;
        m->target = 0;
        return;
    }

    float noisyOutputs[NEURAL_RNN_OUTPUTS];
    for (unsigned o = 0; o < NEURAL_RNN_OUTPUTS; ++o) {
        noisyOutputs[o] = outputs[o];
        if (st->config.explorationNoise > 0.0f) {
            noisyOutputs[o] += NeuralGenome_RandomGaussian(&m->random) * st->config.explorationNoise;
        }
    }

    /* Argmax determinista (desempate estable por menor índice). */
    NeuralActionIntent bestIntent = ACTION_INTENT_IDLE;
    float bestScore = noisyOutputs[0];
    for (unsigned o = 1; o < ACTION_INTENT_COUNT; ++o) {
        if (noisyOutputs[o] > bestScore) {
            bestScore = noisyOutputs[o];
            bestIntent = (NeuralActionIntent)o;
        }
    }

    /* Compromiso genérico simétrico: previene oscilación sin inyectar prioridades manuales. */
    NeuralActionIntent chosen = bestIntent;
    float chosenScore = bestScore;
    if (st->decisionCount > 0 && bestIntent != st->selectedIntent) {
        float currentScore = noisyOutputs[st->selectedIntent];
        bool targetStillValid = (m->target == 0) ||
                                (CreaturePerception_Find(&m->perception, m->target) != NULL);
        bool inInteraction = intentMatchesActiveInteraction(m, st->selectedIntent);
        bool committed = (m->time - st->intentStarted) < (double)st->config.commitmentDuration;
        if (targetStillValid && inInteraction &&
            bestScore < currentScore + st->config.switchMargin * 1.5f) {
            chosen = st->selectedIntent;
            chosenScore = currentScore;
        } else if (targetStillValid && committed &&
                   bestScore < currentScore + st->config.switchMargin) {
            chosen = st->selectedIntent;
            chosenScore = currentScore;
        }
    }

    bool intentChanged = (st->decisionCount == 0) || (chosen != st->selectedIntent);
    st->selectedIntent = chosen;
    st->selectedScore = chosenScore;
    if (intentChanged) {
        st->intentStarted = m->time;
    }
    ++st->decisionCount;
    ++st->intentCounts[chosen];

    /* Traducción de intención conductual a objetivo, acción y target reales. */
    CreatureGoal newGoal = GOAL_EXPLORE;
    CreatureAction newAction = ACTION_IDLE;
    AIObjectId newTarget = 0u;
    unsigned newFoodCategory = 0u;

    switch (chosen) {
    case ACTION_INTENT_IDLE:
        newGoal = GOAL_EXPLORE;
        newAction = ACTION_IDLE;
        break;
    case ACTION_INTENT_EXPLORE:
        newGoal = GOAL_EXPLORE;
        newAction = ACTION_WANDER;
        break;
    case ACTION_INTENT_REST:
        newGoal = GOAL_REST;
        newAction = ACTION_REST;
        break;
    case ACTION_INTENT_HYDRATE:
        newGoal = GOAL_HYDRATE;
        if (cands.waterId) {
            newTarget = cands.waterId;
            newAction = (m->action == ACTION_DRINK && m->target == newTarget)
                            ? ACTION_DRINK : ACTION_APPROACH_WATER;
        } else {
            newAction = ACTION_SEARCH_WATER;
        }
        break;
    case ACTION_INTENT_FEED_PLANT:
        newGoal = GOAL_FEED;
        newFoodCategory = ECO_PLANT;
        if (cands.plantId) {
            newTarget = cands.plantId;
            newAction = (m->action == ACTION_EAT && m->target == newTarget &&
                         m->desiredFoodCategory == ECO_PLANT)
                            ? ACTION_EAT : ACTION_APPROACH_FOOD;
        } else {
            newAction = ACTION_SEARCH_FOOD;
        }
        break;
    case ACTION_INTENT_FEED_MEAT:
        newGoal = GOAL_FEED;
        newFoodCategory = ECO_MEAT;
        if (cands.meatId) {
            newTarget = cands.meatId;
            newAction = (m->action == ACTION_EAT && m->target == newTarget &&
                         m->desiredFoodCategory == ECO_MEAT)
                            ? ACTION_EAT : ACTION_APPROACH_FOOD;
        } else {
            newAction = ACTION_SEARCH_FOOD;
        }
        break;
    case ACTION_INTENT_HUNT:
        newGoal = GOAL_HUNT;
        if (cands.otherSpeciesId && cands.otherSpeciesVisible) {
            newTarget = cands.otherSpeciesId;
            newAction = (m->action == ACTION_ATTACK && m->target == newTarget)
                            ? ACTION_ATTACK : ACTION_CHASE;
        } else {
            newAction = ACTION_WANDER;
        }
        break;
    case ACTION_INTENT_FLEE:
        newGoal = GOAL_FLEE;
        if (cands.otherSpeciesId) {
            newTarget = cands.otherSpeciesId;
            newAction = ACTION_ESCAPE;
        } else if (cands.conspecificId) {
            newTarget = cands.conspecificId;
            newAction = ACTION_ESCAPE;
        } else {
            newAction = ACTION_WANDER;
        }
        break;
    case ACTION_INTENT_MATE:
        newGoal = GOAL_MATE;
        if (b->mateReady && cands.mateCandidateId) {
            newTarget = cands.mateCandidateId;
            newAction = ACTION_APPROACH_MATE;
        } else {
            newAction = ACTION_WANDER;
            if (!b->mateReady) {
                /* Intentar reproducirse sin estar fisiológicamente listo no produce beneficio. */
                st->pendingActionFailure = fmaxf(st->pendingActionFailure, 0.25f);
                st->lastOutcome = NEURAL_OUTCOME_FAILED_MATE;
                st->lastOutcomeTime = m->time;
            }
        }
        break;
    case ACTION_INTENT_FOLLOW:
        newGoal = GOAL_FOLLOW;
        if (cands.conspecificId) {
            newTarget = cands.conspecificId;
            newAction = ACTION_FOLLOW_TARGET;
        } else if (cands.otherSpeciesId) {
            newTarget = cands.otherSpeciesId;
            newAction = ACTION_FOLLOW_TARGET;
        } else {
            newAction = ACTION_WANDER;
        }
        break;
    default:
        newGoal = GOAL_EXPLORE;
        newAction = ACTION_IDLE;
        break;
    }

    bool samePlan = (newAction == m->action) && (newTarget == m->target) &&
                    (newFoodCategory == m->desiredFoodCategory);
    m->goal = newGoal;
    m->score = chosenScore;
    m->desiredFoodCategory = newFoodCategory;

    if (samePlan) {
        return;
    }

    if (m->target != newTarget || m->action != newAction) {
        m->contactTime = 0.0f;
        m->interactionStarted = 0.0;
        m->repositionAttempts = 0u;
        m->interactionPhase = INTERACTION_SEARCH;
        m->destinationValid = false;
        m->navigation.count = 0u;
        m->started = m->time;
    }
    m->action = newAction;
    m->target = newTarget;
}
