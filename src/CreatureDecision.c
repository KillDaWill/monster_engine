#include "CreatureDecision.h"
#include <math.h>
void CreatureDecision_Update(CreatureMind *m, const AIWorld *w, const CreatureBodyState *b) {
    (void)w;
    if(m->target&&(m->action==ACTION_DRINK||m->action==ACTION_EAT)&&
       m->interactionPhase>=INTERACTION_ALIGN&&m->interactionPhase<=INTERACTION_CONSUME)return;
    if (m->retreating)
        return;
    if (m->time < m->nextDecision || m->time < m->recoveryUntil)
        return;
    m->nextDecision = m->time + fmaxf(.01f, m->profile->decisionInterval);
    const BrainProfile *p = m->profile;
    CreatureAction best =
        p->weights[ACTION_WANDER] > p->weights[ACTION_IDLE] ? ACTION_WANDER : ACTION_IDLE;
    AIObjectId target = 0;
    float score = p->weights[best];
    float thirst = CreatureNeeds_Utility(&m->needs, p, NEED_HYDRATION),
          hunger = CreatureNeeds_Utility(&m->needs, p, NEED_HUNGER),
          fatigue = CreatureNeeds_Utility(&m->needs, p, NEED_REST);
    if (fatigue * p->weights[ACTION_REST] > score) {
        best = ACTION_REST;
        score = fatigue * p->weights[best];
    }
    if (p->needs[NEED_HYDRATION].enabled && p->weights[ACTION_DRINK] > 0 &&
        p->needs[NEED_HYDRATION].satisfactionRate > 0 && b->capabilities.waterInteraction &&
        thirst * p->weights[ACTION_SEARCH_WATER] > score) {
        best = ACTION_SEARCH_WATER;
        score = thirst * p->weights[best];
    }
    if(p->needs[NEED_HUNGER].enabled&&p->weights[ACTION_EAT]>0&&
       p->needs[NEED_HUNGER].satisfactionRate>0&&hunger*p->weights[ACTION_SEARCH_FOOD]>score) {
        best=ACTION_SEARCH_FOOD;score=hunger*p->weights[best];
    }
    for (unsigned i = 0; i < m->perception.count; ++i) {
        const PerceptionEntry *e = &m->perception.entries[i];
        if ((e->id == m->failedTarget && m->time < m->cooldownUntil) ||
            m->time < e->unreachableUntil)
            continue;
        float distance = Vec3_Distance(b->position, e->position);
        CreatureAction a = ACTION_WANDER;
        float utility = 0;
        if (e->type == AI_WATER && e->drinkable && p->needs[NEED_HYDRATION].enabled &&
            p->weights[ACTION_DRINK] > 0 && p->needs[NEED_HYDRATION].satisfactionRate > 0 &&
            b->capabilities.waterInteraction) {
            a = ACTION_APPROACH_WATER;
            utility = thirst * p->weights[a] * (.95f + .05f / (1 + distance));
            if (m->action == ACTION_DRINK && m->target == e->id &&
                m->needs.urgency[NEED_HYDRATION] > p->needs[NEED_HYDRATION].desiredLevel) {
                a = ACTION_DRINK;
                utility = fmaxf(utility, p->weights[ACTION_WANDER] + p->switchMargin + .01f);
            }
        }
        if(e->type==AI_FOOD&&p->needs[NEED_HUNGER].enabled&&p->weights[ACTION_EAT]>0&&
           p->needs[NEED_HUNGER].satisfactionRate>0) {
            a=ACTION_APPROACH_FOOD;
            utility=hunger*p->weights[a]*(.95f+.05f/(1+distance));
        }
        if (e->type == AI_CREATURE && e->group == b->group) {
            if (p->social == SOCIAL_SOLITARY || p->social == SOCIAL_TERRITORIAL) {
                a = ACTION_AVOID_COMPANY;
                utility = p->weights[a] * fmaxf(0, 1 - distance / p->socialDistance);
            } else if (p->social == SOCIAL_AFFILIATIVE) {
                a = p->weights[ACTION_FOLLOW_TARGET] > p->weights[ACTION_SEEK_COMPANY]
                        ? ACTION_FOLLOW_TARGET
                        : ACTION_SEEK_COMPANY;
                utility = CreatureNeeds_Utility(&m->needs, p, NEED_SOCIAL) * p->weights[a] *
                          (.9f + .1f / (1 + distance));
            }
        }
        if (!e->visible && a != ACTION_DRINK)
            utility *= .9f;
        if (utility > score) {
            score = utility;
            best = a;
            target = e->id;
        }
    }
    CreatureGoal goal =
        best == ACTION_SEARCH_WATER || best == ACTION_APPROACH_WATER || best == ACTION_DRINK
            ? GOAL_HYDRATE
        : (best==ACTION_SEARCH_FOOD||best==ACTION_APPROACH_FOOD||best==ACTION_EAT)?GOAL_FEED
        : best == ACTION_REST          ? GOAL_REST
        : best == ACTION_SEEK_COMPANY  ? GOAL_COMPANY
        : best == ACTION_AVOID_COMPANY ? GOAL_AVOID
        : best == ACTION_FOLLOW_TARGET ? GOAL_FOLLOW
                                       : GOAL_EXPLORE;
    if (best == m->action && target == m->target) {
        m->score = score;
        m->goal = goal;
        return;
    }
    bool critical =
        ((best==ACTION_SEARCH_FOOD||best==ACTION_APPROACH_FOOD||best==ACTION_EAT)&&
          p->needs[NEED_HUNGER].enabled&&m->needs.urgency[NEED_HUNGER]>=p->needs[NEED_HUNGER].critical) ||
        (best == ACTION_REST && p->needs[NEED_REST].enabled &&
         m->needs.urgency[NEED_REST] >= p->needs[NEED_REST].critical) ||
        ((best == ACTION_SEARCH_WATER || best == ACTION_APPROACH_WATER || best == ACTION_DRINK) &&
         p->needs[NEED_HYDRATION].enabled &&
         m->needs.urgency[NEED_HYDRATION] >= p->needs[NEED_HYDRATION].critical);
    if (!critical && m->time - m->started < p->commitment)
        return;
    if (m->score > 0 && score < m->score + p->switchMargin && m->target &&
        CreaturePerception_Find(&m->perception, m->target))
        return;
    m->goal = goal;
    m->action = best;
    if(m->target!=target) {m->contactTime=0;m->interactionStarted=0;m->repositionAttempts=0;}
    m->target = target;
    m->score = score;
    m->started = m->time;
    m->interactionStarted = 0;
    m->destinationValid = false;
    m->navigation.count = 0;
}
