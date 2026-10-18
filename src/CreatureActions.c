#include "CreatureActions.h"
#include <math.h>
void CreatureActions_Cancel(CreatureMind *m) {
    if(!m)return;
    m->intakeRequested=0;
    m->retreating=false;m->navigation.count=0;
    m->target=0;m->contactTime=0;m->interactionStarted=0;m->destinationValid=false;
    m->interactionPhase=INTERACTION_CANCELLED;m->intent=(CreatureIntent){0};
    m->action=m->profile&&m->profile->weights[ACTION_WANDER]>0?ACTION_WANDER:ACTION_IDLE;
    m->score=0;m->nextDecision=m->time;
}
void CreatureActions_Fail(CreatureMind *m) {
    m->intakeRequested=0;m->intent=(CreatureIntent){0};
    if (m->target) {
        m->failedTarget = m->target;
        m->cooldownUntil = m->time + 8;
        for (unsigned i = 0; i < m->perception.count; ++i)
            if (m->perception.entries[i].id == m->target)
                m->perception.entries[i].unreachableUntil = m->time + 8;
    }
    m->recoveryUntil = m->time + .5;
    m->target = 0;
    m->retreating = false;
    m->goal = GOAL_RECOVER;
    m->action = m->profile->weights[ACTION_WANDER] > 0 ? ACTION_WANDER : ACTION_IDLE;
    m->score = 0;
    m->destinationValid = false;
    m->interactionStarted = 0;
    m->contactTime=0;
    m->interactionPhase=INTERACTION_UNREACHABLE;
    m->navigation.count = 0;
    m->navigation.progress = (NavigationProgress){0};
    ++m->actionFailures;
    m->nextDecision = m->time + .5;
}
static bool explore(CreatureMind *m, const AIWorld *w, const CreatureBodyState *b) {
    for (int i = 0; i < 16; ++i) {
        float angle = CreatureMind_Random(m) * 6.2831853f,
              r = 4 + CreatureMind_Random(m) * m->profile->explorationRadius;
        Vector3 p = Vec3_Add(b->position, Vec3_Create(sinf(angle) * r, 0, cosf(angle) * r));
        if (AIWorld_Walkable(w, p, b->capabilities.clearance, b->capabilities.swimming)) {
            m->destination = p;
            return true;
        }
    }
    return false;
}
void CreatureActions_Update(CreatureMind *m, const AIWorld *w, const CreatureBodyState *b,
                            float dt) {
    m->intent = (CreatureIntent){0};
    m->intakeRequested=0;
    if (m->time < m->recoveryUntil)
        return;
    if (m->retreating) {
        if (Vec3_Distance(b->position, m->retreatGoal) < .3f ||
            Vec3_Distance(b->position,m->retreatStart) >=
                Vec3_Distance(m->retreatGoal,m->retreatStart)-.3f) {
            m->retreating = false;
            m->destinationValid = false;
            m->score = 0;
            m->nextDecision = m->time;
        } else {
            NavigationGoalStatus progress = NavigationProgress_Update(&m->navigation.progress,
                b->position,m->retreatGoal,b->yaw,b->capabilities.maximumSpeed,
                b->capabilities.maximumTurn,.3f,dt);
            if(progress == NAVIGATION_STALLED) {
                CreatureActions_Fail(m);
                return;
            }
            m->intent.moving = true;
            m->intent.reverse = true;
            /* Retirada por la huella ya recorrida. Girar persiguiendo un punto
             * cercano desplazaba la raíz alrededor del centro de apoyos. */
            m->intent.destination = Vec3_Sub(b->position,
                Vec3_Scale(Vec3_Create(sinf(b->yaw),0,cosf(b->yaw)),1));
            m->intent.speed = b->capabilities.maximumSpeed * .6f;
            return;
        }
    }
    const BrainProfile *p = m->profile;
    const PerceptionEntry *e = CreaturePerception_Find(&m->perception, m->target);
    if (m->action == ACTION_IDLE)
        return;
    if (m->action == ACTION_REST) {
        if (b->speed > .04f)
            return;
        CreatureNeeds_Satisfy(&m->needs, p, NEED_REST, p->needs[NEED_REST].satisfactionRate * dt);
        if (m->needs.urgency[NEED_REST] < p->needs[NEED_REST].desiredLevel) {
            m->action = m->profile->weights[ACTION_WANDER] > 0 ? ACTION_WANDER : ACTION_IDLE;
            m->score = 0;
            m->nextDecision = m->time;
        }
        return;
    }
    bool food=m->action==ACTION_APPROACH_FOOD||m->action==ACTION_EAT;
    bool resource=food||m->action==ACTION_APPROACH_WATER||m->action==ACTION_DRINK;
    if(resource) {
        CreatureNeed need=food?NEED_HUNGER:NEED_HYDRATION;
        CreatureAction consume=food?ACTION_EAT:ACTION_DRINK;
        if(!e||!p->needs[need].enabled||p->weights[consume]<=0||p->needs[need].satisfactionRate<=0||
           (!food&&!b->capabilities.waterInteraction)) { CreatureActions_Fail(m);return; }
        float tolerance=b->capabilities.contactTolerance;
        if(e->radius>0)tolerance=fminf(tolerance,e->radius*.25f);
        InteractionTarget target;
        if(!AIWorld_InteractionTarget(w,m->target,b->mouth,tolerance,&target)) {CreatureActions_Fail(m);return;}
        const AIWorldObject *object=AIWorld_Get(w,m->target);
        if(!food&&object&&Vec3_LengthSq(object->approachDirection)<.5f) {
            /* La boca entra desde el borde: exigir una orientación vertical
             * ignoraba el acceso tangencial de muchas anatomías. */
            Vector3 direction=Vec3_Sub(target.position,b->position);
            direction=Vec3_Sub(direction,Vec3_Scale(target.normal,Vec3_Dot(direction,target.normal)));
            if(Vec3_LengthSq(direction)<1e-8f)direction=Vec3_Create(sinf(b->yaw),0,cosf(b->yaw));
            target.approach=Vec3_Normalize(Vec3_Sub(Vec3_Normalize(direction),target.normal));
        }
        bool aligned=m->interaction.objectId==m->target&&m->interactionPhase>=INTERACTION_ALIGN&&
            m->interactionPhase<=INTERACTION_CONSUME;
        if(aligned&&m->interaction.revision==target.revision) {
            target.position=m->interaction.position;target.approach=m->interaction.approach;
            target.blocked=!AIWorld_LineOfSight(w,b->mouth,target.position,m->target);
        }
        if(!aligned||m->interaction.objectId!=target.objectId||
           Vec3_Distance(m->interaction.position,target.position)>target.positionTolerance)m->contactTime=0;
        m->interaction=target;
        if(target.blocked) {CreatureActions_Fail(m);return;}
        m->intent.lookTarget=target.position;
        float distance=Vec3_Distance(b->mouth,target.position);
        Vector3 planar=Vec3_Sub(b->position,e->position);planar.y=0;
        bool near=distance<=target.positionTolerance||aligned||
            (!m->repositionAttempts&&Vec3_Length(planar)<e->radius+b->capabilities.clearance+.1f)||
            (!m->repositionAttempts&&distance<b->capabilities.maximumOralReach*.35f)||
            (m->destinationValid&&Vec3_Distance(b->position,m->destination)<.2f);
        if(near) {
            m->intent.lookAt=true;m->intent.interact=true;m->intent.interaction=target;
            if(m->interactionStarted==0)m->interactionStarted=m->time;
            if(!aligned&&!food)++m->drinks;
            m->action=consume;
            if(!aligned)m->interactionPhase=INTERACTION_ALIGN;
            if(b->speed>.04f) {m->contactTime=0;return;}
            m->interactionPhase=INTERACTION_REACH;
            bool contact=b->reach.status==REACH_REACHED&&b->reach.objectId==m->target&&
                distance<=target.positionTolerance&&
                Vec3_Dot(b->mouthDirection,target.approach)>=cosf(target.orientationTolerance)&&
                b->reach.stability<.15f;
            if(contact) {
                m->contactTime+=dt;m->interactionPhase=INTERACTION_STABLE_CONTACT;
                if(m->contactTime>=.25f) {
                    m->interactionPhase=INTERACTION_CONSUME;
                    if(m->transactionalIntake) {
                        m->intakeNeed=need;
                        if(m->needs.urgency[need]>p->needs[need].desiredLevel)
                            m->intakeRequested=p->needs[need].satisfactionRate*dt;
                    } else CreatureNeeds_Satisfy(&m->needs,p,need,p->needs[need].satisfactionRate*dt);
                    if(m->needs.urgency[need]<=p->needs[need].desiredLevel) {
                        if(food)++m->completedMeals;else ++m->completedDrinks;
                        m->interactionPhase=INTERACTION_WITHDRAW;m->intent.interact=false;
                        m->retreating=true;m->retreatStart=b->position;
                        m->retreatGoal=Vec3_Sub(b->position,Vec3_Scale(Vec3_Create(sinf(b->yaw),0,cosf(b->yaw)),2));
                        m->navigation.progress=(NavigationProgress){0};
                        m->target=0;m->action=p->weights[ACTION_WANDER]>0?ACTION_WANDER:ACTION_IDLE;
                        m->score=0;m->destinationValid=false;m->nextDecision=m->time;
                    }
                }
            } else m->contactTime=0;
            if(!contact&&((m->time-m->interactionStarted>.5f&&b->reach.objectId==m->target&&(b->reach.status==REACH_UNREACHABLE||b->reach.status==REACH_BLOCKED))||
               m->time-m->interactionStarted>6)) {
                if(m->repositionAttempts++>=2) {CreatureActions_Fail(m);return;}
                m->interactionPhase=INTERACTION_REPOSITION;m->intent.interact=false;
                m->action=food?ACTION_APPROACH_FOOD:ACTION_APPROACH_WATER;
                m->interactionStarted=0;m->destinationValid=false;
            } else return;
        }
        m->contactTime=0;
        m->action=food?ACTION_APPROACH_FOOD:ACTION_APPROACH_WATER;
        m->interactionPhase=INTERACTION_APPROACH;
        if(Vec3_Distance(e->position,m->approachResourcePosition)>.15f)m->destinationValid=false;
        Vector3 d=Vec3_Sub(b->position,e->position);d.y=0;
        if(Vec3_LengthSq(d)<.001f)d=Vec3_Create(0,0,1);
        float angle=m->repositionAttempts*.6f;
        float x=d.x*cosf(angle)-d.z*sinf(angle);d.z=d.x*sinf(angle)+d.z*cosf(angle);d.x=x;
        float offset=object&&object->walkable?fmaxf(.1f,b->capabilities.mouthForward):
            e->radius+(Vec3_Length(planar)<e->radius+b->capabilities.clearance+2?b->capabilities.bankClearance+.02f:b->capabilities.clearance+.08f);
        Vector3 bank=Vec3_Add(e->position,Vec3_Scale(Vec3_Normalize(d),offset));
        bank.y=b->position.y;
        if(!m->destinationValid) {
            m->destination=bank;m->approachResourcePosition=e->position;m->destinationValid=false;
        }
    } else if(m->action==ACTION_CHASE||m->action==ACTION_ATTACK||
              m->action==ACTION_ESCAPE||m->action==ACTION_APPROACH_MATE) {
        if(!e||!e->available||m->time-m->started>25) {CreatureActions_Fail(m);return;}
        bool escape=m->action==ACTION_ESCAPE;
        if(!escape&&!e->visible) {CreatureActions_Fail(m);return;}
        Vector3 delta=Vec3_Sub(e->position,b->position);delta.y=0;
        float distance=Vec3_Length(delta);
        float stop=m->action==ACTION_APPROACH_MATE?b->capabilities.clearance+e->radius+.15f:b->attackRange;
        if(!escape&&distance<=stop) {
            m->intent.lookAt=true;m->intent.lookTarget=e->position;
            if(m->action!=ACTION_APPROACH_MATE)m->action=ACTION_ATTACK;
            return;
        }
        if(m->action==ACTION_ATTACK)m->action=ACTION_CHASE;
        Vector3 goal;
        if(escape) {
            Vector3 away=Vec3_Scale(Vec3_Normalize(delta),-1);
            if(distance<.001f)away=Vec3_Create(1,0,0);
            goal=Vec3_Add(b->position,Vec3_Scale(away,3));
            bool valid=false;
            for(unsigned k=0;k<8;++k) {
                float angle=k*.78539816f;
                Vector3 d=Vec3_Create(away.x*cosf(angle)-away.z*sinf(angle),0,away.x*sinf(angle)+away.z*cosf(angle));
                goal=Vec3_Add(b->position,Vec3_Scale(d,3));
                if(AIWorld_Walkable(w,goal,b->capabilities.clearance,b->capabilities.swimming)) {valid=true;break;}
            }
            if(!valid) {CreatureActions_Fail(m);return;}
        } else goal=m->action==ACTION_CHASE?e->position:Vec3_Sub(e->position,Vec3_Scale(Vec3_Normalize(delta),stop*.85f));
        if(!m->destinationValid||Vec3_Distance(goal,m->destination)>.5f) {
            m->destination=goal;m->destinationValid=false;
        }
    } else if ((m->action == ACTION_SEEK_COMPANY || m->action == ACTION_FOLLOW_TARGET ||
                m->action == ACTION_AVOID_COMPANY) &&
               e) {
        Vector3 d = Vec3_Sub(b->position, e->position);
        d.y = 0;
        float distance = Vec3_Length(d);
        if (Vec3_LengthSq(d) < .001f)
            d = Vec3_Create(1, 0, 0);
        bool avoid = m->action == ACTION_AVOID_COMPANY;
        if ((avoid && distance >= p->socialDistance) || (!avoid && distance <= p->socialDistance)) {
            CreatureNeeds_Satisfy(&m->needs, p, NEED_SOCIAL,
                                  p->needs[NEED_SOCIAL].satisfactionRate * dt);
            m->score = 0;
            return;
        }
        Vector3 goal = Vec3_Add(e->position,
                                Vec3_Scale(Vec3_Normalize(d), p->socialDistance + (avoid ? 2 : 0)));
        if (!m->destinationValid || Vec3_Distance(goal, m->destination) > 2) {
            m->destination = goal;
            m->destinationValid = false;
        }
    } else if (!m->destinationValid && !explore(m, w, b)) {
        CreatureActions_Fail(m);
        return;
    }
    float r = b->capabilities.clearance;
    if ((m->action == ACTION_APPROACH_WATER || m->action==ACTION_APPROACH_FOOD) && e &&
        Vec3_Distance(b->position, e->position) < e->radius + b->capabilities.clearance + 2)
        r = fminf(r, fmaxf(.2f, b->capabilities.bankClearance));
    if (!m->destinationValid || fabsf(m->navigation.clearance - r) > .3f ||
        m->navigation.revision != w->revision) {
        if (!CreatureNavigation_Plan(&m->navigation, w, b->position, m->destination, r,
                                     b->capabilities.swimming)) {
            CreatureActions_Fail(m);
            /* La huella direccional de una orilla puede ser menor que el disco
             * de planificación. Salir por la huella anterior antes de replantear. */
            if(!AIWorld_Walkable(w,b->position,r,b->capabilities.swimming)) {
                m->retreating=true;
                m->retreatStart=b->position;
                m->retreatGoal=Vec3_Sub(b->position,
                    Vec3_Scale(Vec3_Create(sinf(b->yaw),0,cosf(b->yaw)),r+2));
            }
            return;
        }
        m->destinationValid = true;
    }
    Vector3 waypoint;
    NavigationGoalStatus status =
        CreatureNavigation_Update(&m->navigation, b->position, b->yaw, b->capabilities.maximumSpeed,
                                  b->capabilities.maximumTurn, dt, &waypoint);
    if ((m->action == ACTION_APPROACH_WATER || m->action==ACTION_APPROACH_FOOD) && Vec3_Distance(b->position, m->destination) < .12f) {
        m->intent.lookAt = true;
        m->intent.lookTarget=m->interaction.position;
        if(m->interactionStarted==0)m->interactionStarted=m->time;
        if (m->time - m->interactionStarted > 8)
            CreatureActions_Fail(m);
        return;
    }
    if (status == NAVIGATION_STALLED) {
        ++m->navigation.failures;
        CreatureActions_Fail(m);
        return;
    }
    if (status == NAVIGATION_ARRIVED) {
        if (m->action == ACTION_APPROACH_WATER || m->action==ACTION_APPROACH_FOOD) {
            m->intent.lookAt = true;
            m->intent.lookTarget =
                Vec3_Add(e->position,
                         Vec3_Scale(Vec3_Normalize(Vec3_Sub(b->position, e->position)), e->radius));
            if(m->interactionStarted==0)m->interactionStarted=m->time;
            if (m->time - m->interactionStarted > 8)
                CreatureActions_Fail(m);
        } else {
            m->destinationValid = false;
            m->score = 0;
        }
        return;
    }
    m->intent.moving = true;
    m->intent.pursuing=m->action==ACTION_CHASE;
    m->interactionStarted = 0;
    m->intent.destination = waypoint;
    m->intent.speed = b->capabilities.maximumSpeed;
}
