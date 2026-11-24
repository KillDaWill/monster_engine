#include "CreatureMind.h"
#include "CreatureActions.h"
#include "CreatureDecision.h"
#include <math.h>
#include <string.h>
float CreatureMind_Random(CreatureMind *m) {
    uint32_t x = m->random;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    m->random = x;
    return (x >> 8) / 16777216.f;
}
void CreatureMind_Init(CreatureMind *m, const BrainProfile *p, AIObjectId self, uint32_t seed) {
    memset(m, 0, sizeof(*m));
    m->controllerType = CREATURE_CONTROLLER_UTILITY;
    NeuralControllerState_Reset(&m->neuralState, NeuralControllerConfig_Default());
    m->profile = BrainProfile_Validate(p) ? p : NULL;
    if (!m->profile)
        return;
    m->action=p->weights[ACTION_WANDER]>0?ACTION_WANDER:ACTION_IDLE;
    m->self = self;
    m->random = seed ? seed : 1;
    m->started = -p->commitment;
    m->perception.nextRefresh = (self % 12u) * (1.0 / 60.0);
    m->nextDecision = (self % 12u) * (1.0 / 60.0);
}
void CreatureMind_Reset(CreatureMind *m) {
    if (!m) return;
    NeuralBrain_Destroy(&m->neuralBrain);
    memset(m, 0, sizeof(*m));
}
void CreatureMind_AssignProfile(CreatureMind *m, const BrainProfile *p) {
    CreatureNeeds old = m->needs;
    bool external=m->externalPhysiology,transactional=m->transactionalIntake;
    CreatureControllerType oldType = m->controllerType;
    NeuralControllerConfig oldNeuralCfg = m->neuralState.config;
    NeuralGenome oldGenome;
    bool hadNeural = (oldType == CREATURE_CONTROLLER_NEURAL) &&
                     NeuralGenome_Clone(&oldGenome, &m->neuralBrain.genome);
    uint32_t seed = m->random;
    AIObjectId self = m->self;
    double time = m->time;
    NeuralBrain_Destroy(&m->neuralBrain);
    CreatureMind_Init(m, p, self, seed);
    m->externalPhysiology=external;m->transactionalIntake=transactional;
    if (!m->profile) {
        if (hadNeural) NeuralGenome_Destroy(&oldGenome);
        return;
    }
    m->time = time;
    for (int i = 0; i < NEED_COUNT; ++i)
        if (p->needs[i].enabled)
            m->needs.urgency[i] = old.urgency[i];
    if (hadNeural) {
        m->neuralState.config = oldNeuralCfg;
        CreatureMind_AssignNeuralGenome(m, &oldGenome);
        NeuralGenome_Destroy(&oldGenome);
    }
}
void CreatureMind_Update(CreatureMind *m, const AIWorld *w, const CreatureBodyState *b, float dt) {
    if (!m->profile || !isfinite(dt) || dt <= 0)
        return;
    m->time += dt;
    if(!m->externalPhysiology)CreatureNeeds_Update(&m->needs, m->profile, dt);
    CreaturePerception_Update(&m->perception, &m->profile->perception, w, m->self, b->eye,
                              b->visionYaw, m->time);
    if (m->target &&
        (!AIWorld_Get(w, m->target) || !CreaturePerception_Find(&m->perception, m->target)))
        CreatureActions_Fail(m);
    if (m->controllerType == CREATURE_CONTROLLER_NEURAL) {
        NeuralController_Update(m, w, b);
    } else {
        CreatureDecision_Update(m, w, b);
    }
    CreatureActions_Update(m, w, b, dt);
}

float CreatureMind_GetNeed(const CreatureMind *m, CreatureNeed k) {
    return m && m->profile && k >= 0 && k < NEED_COUNT && m->profile->needs[k].enabled
               ? m->needs.urgency[k]
               : 0;
}
CreatureAction CreatureMind_GetAction(const CreatureMind *m) { return m ? m->action : ACTION_REST; }

CreatureGoal CreatureMind_GetGoal(const CreatureMind *m) { return m ? m->goal : GOAL_RECOVER; }

void CreatureMind_ConfirmIntake(CreatureMind *m,float amount) {
    if(m&&m->profile&&m->transactionalIntake&&isfinite(amount)&&amount>0) {
        CreatureNeeds_Satisfy(&m->needs,m->profile,m->intakeNeed,fminf(amount,m->intakeRequested));
        if (m->controllerType == CREATURE_CONTROLLER_NEURAL) {
            m->neuralState.pendingNutrientIntake =
                fminf(1.0f, m->neuralState.pendingNutrientIntake + amount * 5.0f);
        }
    }
    if(m)m->intakeRequested=0;
}

bool CreatureMind_AssignNeuralGenome(CreatureMind *m, const NeuralGenome *genome) {
    if (!m || !NeuralGenome_Validate(genome)) return false;
    NeuralBrain nextBrain;
    if (!NeuralBrain_Init(&nextBrain, genome)) return false;
    NeuralBrain_Destroy(&m->neuralBrain);
    m->neuralBrain = nextBrain;
    m->controllerType = CREATURE_CONTROLLER_NEURAL;

    NeuralControllerConfig cfg = m->neuralState.config;
    if (!NeuralControllerConfig_Validate(&cfg)) cfg = NeuralControllerConfig_Default();
    NeuralControllerState_Reset(&m->neuralState, cfg);
    m->neuralState.intentStarted = m->time - (double)cfg.commitmentDuration;

    /* Cancelar acciones, rutas y solicitudes de interacción previas sin transferir memoria ajena. */
    m->intakeRequested = 0.0f;
    m->desiredFoodCategory = 0u;
    m->target = 0u;
    m->failedTarget = 0u;
    m->cooldownUntil = 0.0;
    m->recoveryUntil = 0.0;
    m->retreating = false;
    m->destinationValid = false;
    m->contactTime = 0.0f;
    m->interactionStarted = 0.0;
    m->repositionAttempts = 0u;
    m->interactionPhase = INTERACTION_CANCELLED;
    m->interaction = (InteractionTarget){0};
    m->navigation.count = 0u;
    m->navigation.index = 0u;
    m->navigation.progress = (NavigationProgress){0};
    m->intent = (CreatureIntent){0};
    m->goal = GOAL_EXPLORE;
    m->action = ACTION_IDLE;
    m->score = 0.0f;
    m->started = m->time;
    m->nextDecision = m->time;
    for (unsigned i = 0; i < m->perception.count; ++i) {
        m->perception.entries[i].unreachableUntil = 0.0;
    }
    return true;
}

const NeuralGenome *CreatureMind_GetNeuralGenome(const CreatureMind *m) {
    if (!m || m->controllerType != CREATURE_CONTROLLER_NEURAL || !m->neuralBrain.genome.valid)
        return NULL;
    return &m->neuralBrain.genome;
}

void CreatureMind_SetNeuralConfig(CreatureMind *m, NeuralControllerConfig config) {
    if (!m || !NeuralControllerConfig_Validate(&config)) return;
    m->neuralState.config = config;
}

void CreatureMind_RecordFailure(CreatureMind *m, NeuralInteractionOutcome outcome) {
    if (!m) return;
    m->neuralState.pendingActionFailure = 1.0f;
    m->neuralState.lastOutcome = outcome;
    m->neuralState.lastOutcomeTime = m->time;
}

void CreatureMind_RecordDamage(CreatureMind *m, float damage) {
    if (!m || !isfinite(damage) || damage <= 0.0f) return;
    m->neuralState.pendingDamageReceived =
        fminf(1.0f, m->neuralState.pendingDamageReceived + damage * 2.5f);
    m->neuralState.lastOutcome = NEURAL_OUTCOME_TOOK_DAMAGE;
    m->neuralState.lastOutcomeTime = m->time;
}

void CreatureMind_RecordOutcome(CreatureMind *m, NeuralInteractionOutcome outcome) {
    if (!m) return;
    m->neuralState.lastOutcome = outcome;
    m->neuralState.lastOutcomeTime = m->time;
}
