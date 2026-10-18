#include "CreatureDecision.h"
#include <math.h>
void CreatureDecision_Update(CreatureMind *m, const AIWorld *w, const CreatureBodyState *b) {
    (void)w;
    /* El peligro visible interrumpe incluso contacto y retirada. Sólo usa memoria. */
    const BrainProfile *ecology=m->profile;
    const PerceptionEntry *danger=NULL;float closest=ecology->dangerDistance;
    for(unsigned i=0;i<m->perception.count;++i) {
        const PerceptionEntry *e=&m->perception.entries[i];
        float d=Vec3_Distance(b->position,e->position);
        if((e->visible||(m->goal==GOAL_FLEE&&m->time-e->observed<3))&&e->available&&(e->category&ecology->threatMask)&&d<closest) {danger=e;closest=d;}
    }
    if(danger&&ecology->weights[ACTION_ESCAPE]>0&&b->energy>.015f) {
        if(m->action!=ACTION_ESCAPE||m->target!=danger->id) {
            m->destinationValid=false;m->navigation.count=0;m->started=m->time;
            m->contactTime=0;m->interactionStarted=0;m->retreating=false;
        }
        m->goal=GOAL_FLEE;m->action=ACTION_ESCAPE;m->target=danger->id;
        m->score=ecology->weights[ACTION_ESCAPE];return;
    }
    if(m->action==ACTION_ESCAPE) {m->score=0;m->nextDecision=m->time;m->started=-10;}
    if(m->target&&(m->action==ACTION_DRINK||m->action==ACTION_EAT)&&
       m->interactionPhase>=INTERACTION_ALIGN&&m->interactionPhase<=INTERACTION_CONSUME)return;
    if((m->action==ACTION_CHASE||m->action==ACTION_ATTACK)&&(!b->canAttack||b->energy<=.08f||b->fatigue>=.9f)) {
        m->target=0;m->score=0;m->nextDecision=m->time;m->destinationValid=false;
    }
    if(m->action==ACTION_APPROACH_MATE) {
        const PerceptionEntry *partner=CreaturePerception_Find(&m->perception,m->target);
        if(!b->mateReady||!partner||!partner->reproductiveReady) {
            m->target=0;m->score=0;m->nextDecision=m->time;m->destinationValid=false;
        }
    }
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
        if(e->type==AI_FOOD&&(!e->foodCategory||(e->available&&(e->foodCategory&p->dietMask)))&&p->needs[NEED_HUNGER].enabled&&p->weights[ACTION_EAT]>0&&
           p->needs[NEED_HUNGER].satisfactionRate>0) {
            a=ACTION_APPROACH_FOOD;
            utility=hunger*p->weights[a]*(.95f+.05f/(1+distance));
        }
        if(e->type==AI_CREATURE&&e->available&&(e->category&p->preyMask)&&
            b->canAttack&&b->energy>.08f&&b->fatigue<.9f&&e->visible) {
            a=ACTION_CHASE;
            float feasibility=e->maximumSpeed>0?fminf(1,b->capabilities.maximumSpeed/e->maximumSpeed):1;
            utility=hunger*p->weights[a]*feasibility/(1+.025f*distance);
        }
        if(e->type==AI_CREATURE&&e->available&&e->reproductiveReady&&b->mateReady&&
            e->species==b->species&&e->group!=b->reproductiveRole) {
            a=ACTION_APPROACH_MATE;utility=p->weights[a]/(1+.03f*distance);
        }
        if (e->type == AI_CREATURE && ((e->species&&b->species)?e->species==b->species:e->group==b->group)) {
            CreatureAction socialAction=ACTION_WANDER;float socialUtility=0;
            if (p->social == SOCIAL_SOLITARY || p->social == SOCIAL_TERRITORIAL) {
                socialAction = ACTION_AVOID_COMPANY;
                socialUtility = p->weights[socialAction] * fmaxf(0, 1 - distance / p->socialDistance);
            } else if (p->social == SOCIAL_AFFILIATIVE) {
                socialAction = p->weights[ACTION_FOLLOW_TARGET] > p->weights[ACTION_SEEK_COMPANY]
                        ? ACTION_FOLLOW_TARGET
                        : ACTION_SEEK_COMPANY;
                socialUtility = CreatureNeeds_Utility(&m->needs, p, NEED_SOCIAL) * p->weights[socialAction] *
                          (.9f + .1f / (1 + distance));
            }
            if(socialUtility>utility){a=socialAction;utility=socialUtility;}
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
        best==ACTION_CHASE||best==ACTION_ATTACK?GOAL_HUNT
        : best==ACTION_APPROACH_MATE?GOAL_MATE
        :         best == ACTION_SEARCH_WATER || best == ACTION_APPROACH_WATER || best == ACTION_DRINK
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
        m->action==ACTION_ESCAPE ||
        ((best==ACTION_SEARCH_FOOD||best==ACTION_APPROACH_FOOD||best==ACTION_EAT)&&
          p->needs[NEED_HUNGER].enabled&&m->needs.urgency[NEED_HUNGER]>=p->needs[NEED_HUNGER].critical) ||
        (best == ACTION_REST && p->needs[NEED_REST].enabled &&
         m->needs.urgency[NEED_REST] >= p->needs[NEED_REST].critical) ||
        ((best == ACTION_SEARCH_WATER || best == ACTION_APPROACH_WATER || best == ACTION_DRINK) &&
         p->needs[NEED_HYDRATION].enabled &&
         m->needs.urgency[NEED_HYDRATION] >= p->needs[NEED_HYDRATION].critical);
    if (!critical && m->time - m->started < p->commitment)
        return;
    if (m->action!=ACTION_ESCAPE && m->score > 0 && score < m->score + p->switchMargin && m->target &&
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
