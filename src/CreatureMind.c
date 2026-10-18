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
    m->profile = BrainProfile_Validate(p) ? p : NULL;
    if (!m->profile)
        return;
    m->action=p->weights[ACTION_WANDER]>0?ACTION_WANDER:ACTION_IDLE;
    m->self = self;
    m->random = seed ? seed : 1;
    m->started = -p->commitment;
    m->perception.nextRefresh = (self % 7) * .013;
    m->nextDecision = (self % 5) * .017;
}
void CreatureMind_Reset(CreatureMind *m) { memset(m, 0, sizeof(*m)); }
void CreatureMind_AssignProfile(CreatureMind *m, const BrainProfile *p) {
    CreatureNeeds old = m->needs;
    uint32_t seed = m->random;
    AIObjectId self = m->self;
    double time = m->time;
    CreatureMind_Init(m, p, self, seed);
    if (!m->profile)
        return;
    m->time = time;
    for (int i = 0; i < NEED_COUNT; ++i)
        if (p->needs[i].enabled)
            m->needs.urgency[i] = old.urgency[i];
}
void CreatureMind_Update(CreatureMind *m, const AIWorld *w, const CreatureBodyState *b, float dt) {
    if (!m->profile || !isfinite(dt) || dt <= 0)
        return;
    m->time += dt;
    CreatureNeeds_Update(&m->needs, m->profile, dt);
    CreaturePerception_Update(&m->perception, &m->profile->perception, w, m->self, b->eye,
                              b->visionYaw, m->time);
    if (m->target &&
        (!AIWorld_Get(w, m->target) || !CreaturePerception_Find(&m->perception, m->target)))
        CreatureActions_Fail(m);
    CreatureDecision_Update(m, w, b);
    CreatureActions_Update(m, w, b, dt);
}

float CreatureMind_GetNeed(const CreatureMind *m, CreatureNeed k) {
    return m && m->profile && k >= 0 && k < NEED_COUNT && m->profile->needs[k].enabled
               ? m->needs.urgency[k]
               : 0;
}
CreatureAction CreatureMind_GetAction(const CreatureMind *m) { return m ? m->action : ACTION_REST; }

CreatureGoal CreatureMind_GetGoal(const CreatureMind *m) { return m ? m->goal : GOAL_RECOVER; }
