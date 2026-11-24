/**
 * @file NeuralControllerV2.c
 * @brief Controlador neuronal v2 genérico con evaluación compartida de candidatos.
 */
#include "NeuralControllerV2.h"
#include "CreatureActions.h"
#include "CreatureMind.h"
#include "SubstanceRegistry.h"
#include <math.h>
#include <string.h>

static float clampf(float v, float lo, float hi) {
    if (!isfinite(v)) return lo;
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

const char *NeuralV2Action_Name(NeuralV2ActionType action) {
    static const char *names[NEURAL_V2_ACTION_COUNT] = {
        "IDLE",
        "EXPLORE",
        "REST",
        "CONSUME_TARGET",
        "ATTACK_TARGET",
        "FLEE_TARGET",
        "FOLLOW_TARGET",
        "MATE_TARGET"
    };
    return (action >= 0 && action < NEURAL_V2_ACTION_COUNT) ? names[action] : "INVALID";
}

const char *NeuralV2ContextSensor_Name(unsigned index) {
    static const char *names[NEURAL_V2_CONTEXT_INPUTS] = {
        "ENERGY",       "HYDRATION",    "HEALTH",       "FATIGUE",
        "AGE_NORM",     "DIGEST_FULL",  "NUTRIENTS",    "TOXICITY",
        "DISCOMFORT",   "SPEED_NORM",   "MATE_READY",   "CAN_ATTACK",
        "WATER_CAP",    "FB_INGEST",    "FB_ENERGY",    "FB_WATER",
        "FB_NUTRIENT",  "FB_TOX_DELTA", "FB_DAMAGE",    "FB_FAIL",
        "ENV_RES_CNT",  "ENV_RES_DIST", "ENV_CRE_CNT",  "ENV_OTH_DIST"
    };
    return index < NEURAL_V2_CONTEXT_INPUTS ? names[index] : "UNKNOWN";
}

typedef struct RankedCandidate {
    const PerceptionEntry *entry;
    float dist;
} RankedCandidate;

static bool candidateBefore(const RankedCandidate *a, const RankedCandidate *b) {
    if (a->entry->visible != b->entry->visible) return a->entry->visible;
    if (fabsf(a->dist - b->dist) > 1e-4f) return a->dist < b->dist;
    return a->entry->id < b->entry->id;
}

static void insertRanked(RankedCandidate *arr, unsigned *count, unsigned maxCap,
                         const PerceptionEntry *entry, float dist) {
    RankedCandidate cand = {.entry = entry, .dist = dist};
    unsigned n = *count;
    unsigned pos = n;
    while (pos > 0 && candidateBefore(&cand, &arr[pos - 1u])) {
        if (pos < maxCap) arr[pos] = arr[pos - 1u];
        --pos;
    }
    if (pos < maxCap) {
        arr[pos] = cand;
        if (n < maxCap) *count = n + 1u;
    }
}

static float entryConfidence(const PerceptionEntry *e, double memDuration, double now) {
    if (e->visible) return 1.0f;
    double age = now - e->observed;
    if (age < 0.0) age = 0.0;
    double dur = memDuration > 0.01 ? memDuration : 0.01;
    return clampf(0.5f * (1.0f - (float)(age / dur)), 0.1f, 0.5f);
}

void NeuralControllerV2_Encode(const CreaturePerception *perception,
                               const PerceptionProfile *perceptionProfile,
                               const CreatureBodyState *body,
                               double now,
                               const NeuralMetabolicFeedback *feedback,
                               NeuralV2EncodedStep *out) {
    if (!out || !body) return;
    memset(out, 0, sizeof(*out));

    float range = perceptionProfile ? fmaxf(perceptionProfile->range, 0.5f) : 14.0f;
    float invRange = 1.0f / range;
    double memDuration = perceptionProfile ? (double)perceptionProfile->memoryDuration : 30.0;
    float maxSpeed = fmaxf(body->capabilities.maximumSpeed, 0.05f);
    float sYaw = sinf(body->yaw);
    float cYaw = cosf(body->yaw);

    /* 1) Recolectar candidatos observables de forma determinista sin consultar metabolismo. */
    RankedCandidate resCands[NEURAL_V2_MAX_RESOURCE_CANDIDATES];
    RankedCandidate creCands[NEURAL_V2_MAX_CREATURE_CANDIDATES];
    unsigned resN = 0u, creN = 0u;
    unsigned totalResSeen = 0u, totalCreSeen = 0u;
    float nearestResDist = range;
    float nearestOtherDist = range;

    if (perception) {
        for (unsigned i = 0; i < perception->count; ++i) {
            const PerceptionEntry *e = &perception->entries[i];
            float dx = e->position.x - body->position.x;
            float dz = e->position.z - body->position.z;
            float dist = sqrtf(dx * dx + dz * dz);

            if ((e->type == AI_FOOD && e->available) ||
                (e->type == AI_WATER && (e->available || e->drinkable))) {
                ++totalResSeen;
                if (dist < nearestResDist) nearestResDist = dist;
                if (now >= e->unreachableUntil) {
                    insertRanked(resCands, &resN, NEURAL_V2_MAX_RESOURCE_CANDIDATES, e, dist);
                }
            } else if (e->type == AI_CREATURE && e->available) {
                ++totalCreSeen;
                bool sameSpecies = (e->species && body->species)
                                       ? (e->species == body->species)
                                       : (e->group == body->group);
                if (!sameSpecies && dist < nearestOtherDist) nearestOtherDist = dist;
                insertRanked(creCands, &creN, NEURAL_V2_MAX_CREATURE_CANDIDATES, e, dist);
            }
        }
    }

    /* 2) Vector de contexto interno + retroalimentación metabólica + resumen ambiental. */
    float *ctx = out->contextInputs;
    ctx[0]  = clampf(body->energy, 0.0f, 1.0f);
    ctx[1]  = clampf(body->hydration, 0.0f, 1.0f);
    ctx[2]  = clampf(body->health, 0.0f, 1.0f);
    ctx[3]  = clampf(body->fatigue, 0.0f, 1.0f);
    ctx[4]  = clampf(body->normalizedAge, 0.0f, 1.0f);
    ctx[5]  = clampf(body->digestiveFullness, 0.0f, 1.0f);
    ctx[6]  = clampf(body->nutrientReserve, 0.0f, 1.0f);
    ctx[7]  = clampf(body->toxicity, 0.0f, 1.0f);
    ctx[8]  = clampf(body->digestiveDiscomfort, 0.0f, 1.0f);
    ctx[9]  = clampf(body->speed / maxSpeed, 0.0f, 1.0f);
    ctx[10] = body->mateReady ? 1.0f : 0.0f;
    ctx[11] = body->canAttack ? 1.0f : 0.0f;
    ctx[12] = body->capabilities.waterInteraction ? 1.0f : 0.0f;
    if (feedback) {
        ctx[13] = clampf(feedback->ingestedMass, 0.0f, 1.0f);
        ctx[14] = clampf(feedback->energyAbsorbed, 0.0f, 1.0f);
        ctx[15] = clampf(feedback->waterAbsorbed, 0.0f, 1.0f);
        ctx[16] = clampf(feedback->nutrientsAbsorbed, 0.0f, 1.0f);
        ctx[17] = clampf(feedback->toxicityDelta, -1.0f, 1.0f);
        ctx[18] = clampf(feedback->damageReceived, 0.0f, 1.0f);
        ctx[19] = clampf(feedback->interactionFailure, 0.0f, 1.0f);
    }
    ctx[20] = clampf((float)totalResSeen / (float)NEURAL_V2_MAX_RESOURCE_CANDIDATES, 0.0f, 1.0f);
    ctx[21] = totalResSeen > 0u ? clampf(nearestResDist * invRange, 0.0f, 1.0f) : 1.0f;
    ctx[22] = clampf((float)totalCreSeen / (float)NEURAL_V2_MAX_CREATURE_CANDIDATES, 0.0f, 1.0f);
    ctx[23] = (nearestOtherDist < range - 1e-4f) ? clampf(nearestOtherDist * invRange, 0.0f, 1.0f) : 1.0f;

    /* 3) Codificar descriptores observables de recursos candidatos. */
    out->resourceCount = resN;
    for (unsigned i = 0; i < resN; ++i) {
        const PerceptionEntry *e = resCands[i].entry;
        float dist = resCands[i].dist;
        float dx = e->position.x - body->position.x;
        float dz = e->position.z - body->position.z;
        float sinB = 0.0f, cosB = 1.0f;
        if (dist > 1e-6f) {
            cosB = clampf((dx * sYaw + dz * cYaw) / dist, -1.0f, 1.0f);
            sinB = clampf((dx * cYaw - dz * sYaw) / dist, -1.0f, 1.0f);
        }
        uint8_t phase = e->molecularResource
                            ? e->substancePhase
                            : (e->type == AI_WATER ? (uint8_t)SUBSTANCE_PHASE_LIQUID
                                                   : (uint8_t)SUBSTANCE_PHASE_SOLID);
        out->resourceIds[i] = e->id;
        out->resourcePhases[i] = phase;

        float *f = out->resourceFeatures[i];
        f[0] = entryConfidence(e, memDuration, now);
        f[1] = clampf(dist * invRange, 0.0f, 1.0f);
        f[2] = sinB;
        f[3] = cosB;
        f[4] = clampf(e->radius * 0.4f, 0.0f, 1.0f);
        f[5] = e->molecularResource ? clampf(e->availabilityFraction, 0.0f, 1.0f)
                                    : (e->available ? 1.0f : 0.0f);
        f[6] = (phase == SUBSTANCE_PHASE_LIQUID) ? 1.0f : 0.0f;
        f[7] = e->molecularResource ? clampf(e->hardness, 0.0f, 1.0f) : 0.15f;
        for (unsigned s = 0; s < SENSORY_SIGNATURE_CHANNELS; ++s) {
            f[8u + s] = clampf(e->sensorySignature.channels[s], 0.0f, 1.0f);
        }
    }

    /* 4) Codificar descriptores observables de criaturas candidatas. */
    out->creatureCount = creN;
    for (unsigned i = 0; i < creN; ++i) {
        const PerceptionEntry *e = creCands[i].entry;
        float dist = creCands[i].dist;
        float dx = e->position.x - body->position.x;
        float dz = e->position.z - body->position.z;
        float sinB = 0.0f, cosB = 1.0f;
        float radVel = 0.0f, tanVel = 0.0f;
        if (dist > 1e-6f) {
            float invD = 1.0f / dist;
            float nx = dx * invD, nz = dz * invD;
            cosB = clampf(nx * sYaw + nz * cYaw, -1.0f, 1.0f);
            sinB = clampf(nx * cYaw - nz * sYaw, -1.0f, 1.0f);
            /* Velocidad relativa proyectada sobre la dirección hacia el candidato. */
            float rvx = e->velocity.x - sYaw * body->speed;
            float rvz = e->velocity.z - cYaw * body->speed;
            float scaleV = 1.0f / fmaxf(maxSpeed, 1.0f);
            radVel = clampf((rvx * nx + rvz * nz) * scaleV, -1.0f, 1.0f);
            tanVel = clampf((rvx * (-nz) + rvz * nx) * scaleV, -1.0f, 1.0f);
        }
        bool sameSpecies = (e->species && body->species)
                               ? (e->species == body->species)
                               : (e->group == body->group);
        bool mateCompat = sameSpecies && e->reproductiveReady && (e->group != body->reproductiveRole);

        out->creatureIds[i] = e->id;
        out->creatureVisible[i] = e->visible;
        out->creatureMateCompatible[i] = mateCompat;

        float *f = out->creatureFeatures[i];
        f[0]  = entryConfidence(e, memDuration, now);
        f[1]  = clampf(dist * invRange, 0.0f, 1.0f);
        f[2]  = sinB;
        f[3]  = cosB;
        f[4]  = radVel;
        f[5]  = tanVel;
        f[6]  = clampf(e->radius * 0.6f, 0.0f, 1.0f);
        f[7]  = sameSpecies ? 1.0f : 0.0f;
        f[8]  = mateCompat ? 1.0f : 0.0f;
        f[9]  = e->visible ? 1.0f : 0.0f;
        f[10] = clampf(e->maximumSpeed / maxSpeed, 0.0f, 1.5f) * (2.0f / 3.0f);
    }
}

static bool scoreBeats(float candScore, NeuralV2ActionType candAct, AIObjectId candTarget,
                       float bestScore, NeuralV2ActionType bestAct, AIObjectId bestTarget) {
    if (candScore > bestScore + 1e-6f) return true;
    if (fabsf(candScore - bestScore) <= 1e-6f) {
        /* Desempate determinista independiente del orden de ranura: menor acción, luego menor ID. */
        if (candAct != bestAct) return candAct < bestAct;
        return candTarget < bestTarget;
    }
    return false;
}

static NeuralActionIntent mapV2ToLegacyIntentStat(NeuralV2ActionType act) {
    switch (act) {
    case NEURAL_V2_ACTION_IDLE:           return ACTION_INTENT_IDLE;
    case NEURAL_V2_ACTION_EXPLORE:        return ACTION_INTENT_EXPLORE;
    case NEURAL_V2_ACTION_REST:           return ACTION_INTENT_REST;
    case NEURAL_V2_ACTION_CONSUME_TARGET: return ACTION_INTENT_FEED_PLANT;
    case NEURAL_V2_ACTION_ATTACK_TARGET:  return ACTION_INTENT_HUNT;
    case NEURAL_V2_ACTION_FLEE_TARGET:    return ACTION_INTENT_FLEE;
    case NEURAL_V2_ACTION_FOLLOW_TARGET:  return ACTION_INTENT_FOLLOW;
    case NEURAL_V2_ACTION_MATE_TARGET:    return ACTION_INTENT_MATE;
    default:                              return ACTION_INTENT_IDLE;
    }
}

void NeuralControllerV2_Update(CreatureMind *m, const AIWorld *w, const CreatureBodyState *b) {
    (void)w;
    if (!m || !b || !m->profile) return;
    if (!m->neuralBrain.genome.valid ||
        m->neuralBrain.genome.architecture != NEURAL_ARCH_CANDIDATE_POLICY) {
        m->action = ACTION_IDLE;
        m->goal = GOAL_EXPLORE;
        m->target = 0u;
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

    /* 1) Enclavar y consumir la retroalimentación metabólica y física acumulada. */
    NeuralMetabolicFeedback fb = st->v2.pendingFeedback;
    fb.interactionFailure = fmaxf(fb.interactionFailure, st->pendingActionFailure);
    fb.damageReceived = fmaxf(fb.damageReceived, st->pendingDamageReceived);
    st->v2.lastFeedback = fb;
    memset(&st->v2.pendingFeedback, 0, sizeof(st->v2.pendingFeedback));
    st->pendingActionFailure = 0.0f;
    st->pendingNutrientIntake = 0.0f;
    st->pendingDamageReceived = 0.0f;

    /* 2) Codificar estado interno y candidatos observables (sin datos privilegiados). */
    NeuralV2EncodedStep enc;
    NeuralControllerV2_Encode(&m->perception, &m->profile->perception, b, m->time, &fb, &enc);

    /* 3) Ejecutar inferencia v2 (evaluadores compartidos solo para candidatos presentes). */
    float globalScores[NEURAL_V2_GLOBAL_ACTIONS];
    float resScores[NEURAL_V2_MAX_RESOURCE_CANDIDATES];
    float creScores[NEURAL_V2_MAX_CREATURE_CANDIDATES][NEURAL_V2_CRE_ACTIONS];
    if (!NeuralBrain_EvaluateV2(&m->neuralBrain.genome, &m->neuralBrain.runtime,
                                enc.contextInputs,
                                (const float (*)[NEURAL_V2_RES_FEAT])enc.resourceFeatures, enc.resourceCount,
                                (const float (*)[NEURAL_V2_CRE_FEAT])enc.creatureFeatures, enc.creatureCount,
                                globalScores, resScores, creScores)) {
        m->action = ACTION_IDLE;
        m->goal = GOAL_EXPLORE;
        m->target = 0u;
        return;
    }

    /* Ruido opcional determinista con PRNG privado. */
    if (st->config.explorationNoise > 0.0f) {
        float sigma = st->config.explorationNoise;
        for (unsigned g = 0; g < NEURAL_V2_GLOBAL_ACTIONS; ++g)
            globalScores[g] += NeuralGenome_RandomGaussian(&m->random) * sigma;
        for (unsigned r = 0; r < enc.resourceCount; ++r)
            resScores[r] += NeuralGenome_RandomGaussian(&m->random) * sigma;
        for (unsigned c = 0; c < enc.creatureCount; ++c)
            for (unsigned a = 0; a < NEURAL_V2_CRE_ACTIONS; ++a)
                creScores[c][a] += NeuralGenome_RandomGaussian(&m->random) * sigma;
    }

    /* 4) Comparar acciones globales vs parejas (acción, candidato) con desempate estable por ID. */
    NeuralV2ActionType bestAct = NEURAL_V2_ACTION_IDLE;
    AIObjectId bestTarget = 0u;
    uint8_t bestResPhase = (uint8_t)SUBSTANCE_PHASE_SOLID;
    bool bestCreVisible = false;
    bool bestCreMateCompat = false;
    float bestScore = globalScores[0];

    for (unsigned g = 1u; g < NEURAL_V2_GLOBAL_ACTIONS; ++g) {
        if (scoreBeats(globalScores[g], (NeuralV2ActionType)g, 0u, bestScore, bestAct, bestTarget)) {
            bestScore = globalScores[g];
            bestAct = (NeuralV2ActionType)g;
            bestTarget = 0u;
        }
    }

    for (unsigned r = 0; r < enc.resourceCount; ++r) {
        if (scoreBeats(resScores[r], NEURAL_V2_ACTION_CONSUME_TARGET, enc.resourceIds[r],
                       bestScore, bestAct, bestTarget)) {
            bestScore = resScores[r];
            bestAct = NEURAL_V2_ACTION_CONSUME_TARGET;
            bestTarget = enc.resourceIds[r];
            bestResPhase = enc.resourcePhases[r];
        }
    }

    static const NeuralV2ActionType creActionMap[NEURAL_V2_CRE_ACTIONS] = {
        NEURAL_V2_ACTION_ATTACK_TARGET,
        NEURAL_V2_ACTION_FLEE_TARGET,
        NEURAL_V2_ACTION_FOLLOW_TARGET,
        NEURAL_V2_ACTION_MATE_TARGET
    };
    for (unsigned c = 0; c < enc.creatureCount; ++c) {
        for (unsigned a = 0; a < NEURAL_V2_CRE_ACTIONS; ++a) {
            NeuralV2ActionType act = creActionMap[a];
            if (scoreBeats(creScores[c][a], act, enc.creatureIds[c], bestScore, bestAct, bestTarget)) {
                bestScore = creScores[c][a];
                bestAct = act;
                bestTarget = enc.creatureIds[c];
                bestCreVisible = enc.creatureVisible[c];
                bestCreMateCompat = enc.creatureMateCompatible[c];
            }
        }
    }

    /* 5) Compromiso genérico simétrico para evitar oscilación (sin anular decisión indefinidamente). */
    NeuralV2ActionType chosenAct = bestAct;
    AIObjectId chosenTarget = bestTarget;
    uint8_t chosenResPhase = bestResPhase;
    bool chosenCreVisible = bestCreVisible;
    bool chosenCreMateCompat = bestCreMateCompat;
    float chosenScore = bestScore;

    if (st->decisionCount > 0u &&
        (bestAct != st->v2.selectedAction || bestTarget != st->v2.selectedTargetId)) {
        /* Localizar la puntuación actual del plan vigente si sigue existiendo. */
        bool currentFound = false;
        float currentScore = -1e30f;
        uint8_t curResPhase = (uint8_t)SUBSTANCE_PHASE_SOLID;
        bool curCreVis = false, curCreMate = false;

        if (st->v2.selectedAction <= NEURAL_V2_ACTION_REST) {
            currentFound = true;
            currentScore = globalScores[st->v2.selectedAction];
        } else if (st->v2.selectedAction == NEURAL_V2_ACTION_CONSUME_TARGET) {
            for (unsigned r = 0; r < enc.resourceCount; ++r) {
                if (enc.resourceIds[r] == st->v2.selectedTargetId) {
                    currentFound = true;
                    currentScore = resScores[r];
                    curResPhase = enc.resourcePhases[r];
                    break;
                }
            }
        } else {
            for (unsigned c = 0; c < enc.creatureCount; ++c) {
                if (enc.creatureIds[c] == st->v2.selectedTargetId) {
                    unsigned aIdx = (unsigned)(st->v2.selectedAction - NEURAL_V2_ACTION_ATTACK_TARGET);
                    if (aIdx < NEURAL_V2_CRE_ACTIONS) {
                        currentFound = true;
                        currentScore = creScores[c][aIdx];
                        curCreVis = enc.creatureVisible[c];
                        curCreMate = enc.creatureMateCompatible[c];
                    }
                    break;
                }
            }
        }

        if (currentFound) {
            bool inActiveConsume = (st->v2.selectedAction == NEURAL_V2_ACTION_CONSUME_TARGET) &&
                                   (m->target == st->v2.selectedTargetId) &&
                                   (m->interactionPhase >= INTERACTION_ALIGN &&
                                    m->interactionPhase <= INTERACTION_CONSUME);
            bool committed = (m->time - st->intentStarted) < (double)st->config.commitmentDuration;
            if (inActiveConsume && bestScore < currentScore + st->config.switchMargin * 1.5f) {
                chosenAct = st->v2.selectedAction;
                chosenTarget = st->v2.selectedTargetId;
                chosenScore = currentScore;
                chosenResPhase = curResPhase;
            } else if (committed && bestScore < currentScore + st->config.switchMargin) {
                chosenAct = st->v2.selectedAction;
                chosenTarget = st->v2.selectedTargetId;
                chosenScore = currentScore;
                chosenResPhase = curResPhase;
                chosenCreVisible = curCreVis;
                chosenCreMateCompat = curCreMate;
            }
        }
    }

    /* 6) Registrar estado de inspección con valores reales de inferencia. */
    bool changed = (st->decisionCount == 0u) ||
                   (chosenAct != st->v2.selectedAction) ||
                   (chosenTarget != st->v2.selectedTargetId);
    st->v2.active = true;
    st->v2.selectedAction = chosenAct;
    st->v2.selectedTargetId = chosenTarget;
    st->v2.selectedScore = chosenScore;
    memcpy(st->v2.globalScores, globalScores, sizeof(globalScores));
    st->v2.resourceCount = enc.resourceCount;
    for (unsigned r = 0; r < enc.resourceCount; ++r) {
        st->v2.resourceIds[r] = enc.resourceIds[r];
        st->v2.resourceScores[r] = resScores[r];
    }
    st->v2.creatureCount = enc.creatureCount;
    for (unsigned c = 0; c < enc.creatureCount; ++c) {
        st->v2.creatureIds[c] = enc.creatureIds[c];
        for (unsigned a = 0; a < NEURAL_V2_CRE_ACTIONS; ++a) {
            st->v2.creatureScores[c][a] = creScores[c][a];
        }
    }
    ++st->v2.actionCounts[chosenAct];

    st->selectedIntent = mapV2ToLegacyIntentStat(chosenAct);
    st->selectedScore = chosenScore;
    if (changed) st->intentStarted = m->time;
    ++st->decisionCount;
    ++st->intentCounts[st->selectedIntent];

    /* 7) Traducir intención genérica a ejecución física (sin filtrar por dieta ni utilidad). */
    CreatureGoal newGoal = GOAL_EXPLORE;
    CreatureAction newAction = ACTION_IDLE;
    AIObjectId newTarget = 0u;

    switch (chosenAct) {
    case NEURAL_V2_ACTION_IDLE:
        newGoal = GOAL_EXPLORE;
        newAction = ACTION_IDLE;
        break;
    case NEURAL_V2_ACTION_EXPLORE:
        newGoal = GOAL_EXPLORE;
        newAction = ACTION_WANDER;
        break;
    case NEURAL_V2_ACTION_REST:
        newGoal = GOAL_REST;
        newAction = ACTION_REST;
        break;
    case NEURAL_V2_ACTION_CONSUME_TARGET:
        newTarget = chosenTarget;
        if (chosenResPhase == (uint8_t)SUBSTANCE_PHASE_LIQUID) {
            newGoal = GOAL_HYDRATE;
            newAction = (m->action == ACTION_DRINK && m->target == newTarget)
                            ? ACTION_DRINK : ACTION_APPROACH_WATER;
        } else {
            newGoal = GOAL_FEED;
            newAction = (m->action == ACTION_EAT && m->target == newTarget)
                            ? ACTION_EAT : ACTION_APPROACH_FOOD;
        }
        break;
    case NEURAL_V2_ACTION_ATTACK_TARGET:
        newGoal = GOAL_HUNT;
        if (chosenTarget && chosenCreVisible) {
            newTarget = chosenTarget;
            newAction = (m->action == ACTION_ATTACK && m->target == newTarget)
                            ? ACTION_ATTACK : ACTION_CHASE;
        } else {
            newAction = ACTION_WANDER;
        }
        break;
    case NEURAL_V2_ACTION_FLEE_TARGET:
        newGoal = GOAL_FLEE;
        if (chosenTarget) {
            newTarget = chosenTarget;
            newAction = ACTION_ESCAPE;
        } else {
            newAction = ACTION_WANDER;
        }
        break;
    case NEURAL_V2_ACTION_FOLLOW_TARGET:
        newGoal = GOAL_FOLLOW;
        if (chosenTarget) {
            newTarget = chosenTarget;
            newAction = ACTION_FOLLOW_TARGET;
        } else {
            newAction = ACTION_WANDER;
        }
        break;
    case NEURAL_V2_ACTION_MATE_TARGET:
        newGoal = GOAL_MATE;
        if (b->mateReady && chosenTarget && chosenCreMateCompat && chosenCreVisible) {
            newTarget = chosenTarget;
            newAction = ACTION_APPROACH_MATE;
        } else {
            newAction = ACTION_WANDER;
            if (!b->mateReady) {
                st->v2.pendingFeedback.interactionFailure =
                    fmaxf(st->v2.pendingFeedback.interactionFailure, 0.25f);
                st->lastOutcome = NEURAL_OUTCOME_FAILED_MATE;
                st->lastOutcomeTime = m->time;
            }
        }
        break;
    default:
        newGoal = GOAL_EXPLORE;
        newAction = ACTION_IDLE;
        break;
    }

    bool samePlan = (newAction == m->action) && (newTarget == m->target);
    m->goal = newGoal;
    m->score = chosenScore;
    m->desiredFoodCategory = 0u;

    if (samePlan) return;

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
