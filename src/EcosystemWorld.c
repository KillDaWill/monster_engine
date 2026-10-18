#include "EcosystemWorld.h"
#include "CreatureActions.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
static void invalidate(EcosystemWorld *,AIObjectId);
static void statistics(EcosystemWorld *);
static float clamp(float v,float a,float b) {return fmaxf(a,fminf(b,v));}
static bool finitePosition(Vector3 p) {return isfinite(p.x)&&isfinite(p.y)&&isfinite(p.z);}
static void event(EcosystemWorld *w,unsigned kind,AIObjectId a,AIObjectId b) {
    if(w->eventOutput)fprintf(w->eventOutput,"%llu,%.9f,%u,%u,%u\n",(unsigned long long)w->clock.tick,w->clock.seconds,kind,a,b);
    uint64_t v[]={w->clock.tick,kind,a,b};
    for(unsigned i=0;i<4;++i){w->statistics.eventHash^=v[i];w->statistics.eventHash*=UINT64_C(1099511628211);}
}
EcosystemSpecies EcosystemSpecies_Default(unsigned id,unsigned category,unsigned diet,unsigned prey,unsigned threats) {
    EcosystemSpecies s={.id=id,.category=category,.brain=*BrainProfile_Find("forager"),
        .physiology=CreaturePhysiology_Default(),.reproduction=CreatureReproduction_Default(),
        .speed=prey?2.6f:2,.radius=.23f,.oralReach=.8f,.attackRange=.7f,
        .damage=.36f,.attackCooldown=.8f,.attackCost=.008f,.edibleBiomass=4,.nutrition=.28f};
    snprintf(s.name,sizeof(s.name),"perfil_%u",id);
    s.brain.name="ecologico";s.brain.dietMask=diet;s.brain.preyMask=prey;s.brain.threatMask=threats;
    s.brain.dangerDistance=5;s.brain.perception.range=14;s.brain.perception.horizontalFOV=5.5f;
    s.brain.perception.verticalFOV=3.14f;s.brain.perception.memoryDuration=30;
    s.brain.needs[NEED_HUNGER].exponent=1;s.brain.needs[NEED_HYDRATION].exponent=1;
    s.brain.weights[ACTION_WANDER]=.08f;s.brain.weights[ACTION_SEARCH_FOOD]=.7f;
    s.brain.weights[ACTION_APPROACH_FOOD]=2.4f;s.brain.weights[ACTION_SEARCH_WATER]=.65f;
    s.brain.weights[ACTION_APPROACH_WATER]=2.3f;s.brain.weights[ACTION_REST]=1.5f;
    s.brain.weights[ACTION_CHASE]=prey?2.5f:0;s.brain.weights[ACTION_ESCAPE]=threats?5:0;
    s.brain.weights[ACTION_APPROACH_MATE]=.6f;s.brain.switchMargin=.04f;s.brain.commitment=.5f;
    s.brain.needs[NEED_HUNGER].satisfactionRate=.12f;
    s.brain.needs[NEED_HYDRATION].satisfactionRate=.15f;
    s.physiology.maximumAge=1800;s.physiology.oldAge=1300;
    return s;
}
bool EcosystemWorld_Init(EcosystemWorld *w,unsigned cap,unsigned resources,unsigned species,uint32_t seed,double step,double day) {
    if(!w)return false;
    memset(w,0,sizeof(*w));
    if(!cap||cap>100000||!resources||resources>100000||!species||species>1024||
        !SimulationClock_Init(&w->clock,step,day,64))return false;
    if(!AIWorld_InitCapacity(&w->observations,24,cap+resources))return false;
    w->entities=calloc(cap,sizeof(*w->entities));w->resources=calloc(resources,sizeof(*w->resources));
    w->species=calloc(species,sizeof(*w->species));w->birthQueue=calloc(cap,sizeof(*w->birthQueue));
    w->order=calloc(cap,sizeof(*w->order));
    if(!w->entities||!w->resources||!w->species||!w->birthQueue||!w->order){EcosystemWorld_Free(w);return false;}
    w->capacity=cap;w->resourceCapacity=resources;w->speciesCapacity=species;
    w->random=seed?seed:1;w->initialSeed=seed;w->statistics.eventHash=UINT64_C(1469598103934665603);
    return true;
}
void EcosystemWorld_Free(EcosystemWorld *w) {
    if(!w)return;
    free(w->entities);free(w->resources);free(w->species);free(w->birthQueue);free(w->order);
    AIWorld_Destroy(&w->observations);memset(w,0,sizeof(*w));
}
unsigned EcosystemWorld_AddSpecies(EcosystemWorld *w,EcosystemSpecies s) {
    if(!w||w->updating||!s.id||!s.category||w->speciesCount==w->speciesCapacity||!BrainProfile_Validate(&s.brain)||
       !isfinite(s.speed)||s.speed<=0||!isfinite(s.radius)||s.radius<=0||s.radius>4||
       !isfinite(s.oralReach)||s.oralReach<=0||!isfinite(s.attackRange)||s.attackRange<0||
       !isfinite(s.damage)||s.damage<0||!isfinite(s.attackCooldown)||s.attackCooldown<=0||
       !isfinite(s.attackCost)||s.attackCost<0||!isfinite(s.edibleBiomass)||s.edibleBiomass<0||
       !isfinite(s.nutrition)||s.nutrition<=0)return 0;
    if(!CreaturePhysiology_ValidateProfile(&s.physiology)||!CreatureReproduction_ValidateProfile(&s.reproduction)||
       s.attackRange>s.oralReach)return 0;
    for(unsigned i=0;i<w->speciesCount;++i)if(w->species[i].id==s.id)return 0;
    s.name[sizeof(s.name)-1]=0;w->species[w->speciesCount]=s;
    w->species[w->speciesCount].brain.name=w->species[w->speciesCount].name;
    ++w->speciesCount;return s.id;
}
EcosystemEntity *EcosystemWorld_Get(EcosystemWorld *w,AIObjectId id) {
    if(!w||!id)return NULL;
    for(unsigned i=0;i<w->capacity;++i)if(w->entities[i].id==id)return &w->entities[i];
    return NULL;
}
static PhysiologyProfile physiologicalProfile(const EcosystemWorld *w,const EcosystemEntity *e) {
    PhysiologyProfile p=w->species[e->speciesIndex].physiology;p.maturityAge*=e->genome.maturation;return p;
}
static void updateBody(EcosystemWorld *w,EcosystemEntity *e) {
    const EcosystemSpecies *s=&w->species[e->speciesIndex];
    PhysiologyProfile p=physiologicalProfile(w,e);
    e->developmentScale=e->physiology.stage==LIFE_JUVENILE?.55f+.45f*(float)(e->physiology.age/p.maturityAge):1;
    float size=e->genome.size*e->developmentScale;
    e->body.group=s->id;e->body.species=s->id;e->body.reproductiveRole=e->reproduction.role;
    e->body.energy=e->physiology.energy;e->body.fatigue=e->physiology.fatigue;
    e->body.canAttack=s->brain.preyMask&&e->physiology.energy>.03f;
    e->body.attackRange=s->attackRange*size;
    e->body.mateReady=CreatureReproduction_Ready(&e->reproduction,&s->reproduction,&e->physiology,w->clock.seconds);
    e->body.capabilities=(CreatureCapabilities){.ground=true,.waterInteraction=true,
        .contactTolerance=.09f*size,.maximumOralReach=s->oralReach*size*e->genome.morphology,.clearance=s->radius*size,
        .bankClearance=s->radius*size,.drinkReach=.09f*size,.mouthForward=.48f*size*e->genome.morphology,
        .maximumSpeed=s->speed*e->genome.speed*(.6f+.4f*e->developmentScale)*(1-.45f*e->physiology.fatigue),.maximumTurn=3};
    e->body.eye=Vec3_Add(e->body.position,Vec3_Create(0,.65f*size,0));
    e->body.visionYaw=e->body.yaw;
    AIWorld_SetAgentState(&w->observations,e->id,e->reproduction.role,e->body.capabilities.maximumSpeed);
    AIWorld_Move(&w->observations,e->id,e->body.position);
    AIWorld_SetBounds(&w->observations,e->id,e->body.capabilities.clearance,.65f*size);
    AIWorld_SetObservation(&w->observations,e->id,s->id,s->category,0,!e->physiology.cause,e->body.mateReady);
}
static bool spawnPosition(EcosystemWorld *w,Vector3 p,float radius) {
    if(!AIWorld_Walkable(&w->observations,p,radius,false))return false;
    for(unsigned i=0;i<w->capacity;++i) {
        EcosystemEntity *e=&w->entities[i];
        if(e->id&&Vec3_Distance(p,e->body.position)<radius+e->body.capabilities.clearance)return false;
    }
    return true;
}
AIObjectId EcosystemWorld_Spawn(EcosystemWorld *w,unsigned species,Vector3 position,double age,float energy,const CreatureGenome *genome) {
    if(!w||w->updating||!finitePosition(position)||fabsf(position.y)>.001f||!isfinite(age)||age<0||!isfinite(energy)||energy<0||energy>1)return 0;
    CreatureGenome g=genome?*genome:CreatureGenome_Default();
    if(!CreatureGenome_Validate(&g))return 0;
    unsigned si=0;while(si<w->speciesCount&&w->species[si].id!=species)++si;
    if(si==w->speciesCount)return 0;
    if(w->count>=w->capacity||!spawnPosition(w,position,w->species[si].radius*g.size)) {++w->statistics.failedSpawns;return 0;}
    unsigned slot=0;while(w->entities[slot].id)++slot;
    AIObjectId id=AIWorld_Add(&w->observations,(AIWorldObject){.type=AI_CREATURE,.group=w->observations.nextId%2,.position=position,.visible=true,.walkable=true});
    if(!id){++w->statistics.failedSpawns;return 0;}
    EcosystemEntity *e=&w->entities[slot];memset(e,0,sizeof(*e));e->id=id;e->speciesIndex=si;e->genome=g;
    e->seed=(uint32_t)(CreatureGenome_Random(&w->random)*16777216)+id;
    e->reproduction.role=id%2;e->brain=w->species[si].brain;e->brain.perception.range*=g.perception;
    e->physiology=CreaturePhysiology_Create(age,energy);PhysiologyProfile p=physiologicalProfile(w,e);
    e->physiology.stage=age<p.maturityAge?LIFE_JUVENILE:age<p.oldAge?LIFE_ADULT:LIFE_OLD;
    e->body.position=position;e->body.yaw=CreatureGenome_Random(&w->random)*6.2831853f;
    e->oralLocal=Vec3_Create(0,.45f*g.size,.48f*g.size);
    e->body.mouth=Vec3_Add(position,e->oralLocal);e->body.mouthDirection=Vec3_Create(0,0,1);
    CreatureMind_Init(&e->mind,&e->brain,id,e->seed);e->mind.externalPhysiology=true;e->mind.transactionalIntake=true;
    e->mind.time=w->clock.seconds;e->mind.nextDecision+=w->clock.seconds;
    e->mind.perception.nextRefresh+=w->clock.seconds;e->mind.started+=w->clock.seconds;
    ++w->count;updateBody(w,e);
    float size=e->genome.size*e->developmentScale;
    e->oralLocal=Vec3_Create(0,0,.48f*size*e->genome.morphology);
    e->body.mouth=Vec3_Add(position,Vec3_Create(sinf(e->body.yaw)*e->oralLocal.z,.45f*size,cosf(e->body.yaw)*e->oralLocal.z));
    e->body.mouthDirection=Vec3_Create(sinf(e->body.yaw),0,cosf(e->body.yaw));
    event(w,1,id,species);if(!w->updating)statistics(w);return id;
}
bool EcosystemWorld_RequestDeath(EcosystemWorld *w,AIObjectId id,CreatureDeathCause cause) {
    EcosystemEntity *e=EcosystemWorld_Get(w,id);
    if(!e||e->physiology.cause||cause<=DEATH_NONE||cause>=DEATH_CAUSE_COUNT)return false;
    e->physiology.cause=cause;e->physiology.stage=LIFE_DEAD;e->physiology.health=0;return true;
}
EcosystemResource *EcosystemWorld_Resource(EcosystemWorld *w,AIObjectId id) {
    if(!w||!id)return NULL;
    for(unsigned i=0;i<w->resourceCapacity;++i)if(w->resources[i].id==id)return &w->resources[i];
    return NULL;
}
AIObjectId EcosystemWorld_AddResource(EcosystemWorld *w,EcosystemResource r,Vector3 p,float radius) {
    if(!w||!finitePosition(p)||!isfinite(radius)||radius<=0||!isfinite(r.biomass)||r.biomass<0||
       !isfinite(r.maximumBiomass)||r.maximumBiomass<r.biomass||!isfinite(r.regenerationRate)||r.regenerationRate<0||
       !isfinite(r.nutritionalValue)||r.nutritionalValue<=0||
       (r.category!=ECO_PLANT&&r.category!=ECO_WATER&&r.category!=ECO_MEAT)||
       (r.infinite&&r.category!=ECO_WATER)||
       (r.category==ECO_MEAT&&(!isfinite(r.remainingLifetime)||r.remainingLifetime<=0)))return 0;
    unsigned i=0;while(i<w->resourceCapacity&&w->resources[i].id)++i;
    if(i==w->resourceCapacity)return 0;
    r.id=AIWorld_Add(&w->observations,(AIWorldObject){.type=r.category==ECO_WATER?AI_WATER:AI_FOOD,
        .position=p,.radius=radius,.height=r.category==ECO_WATER?0:.04f,.visible=true,
        .walkable=r.category!=ECO_WATER,.drinkable=r.category==ECO_WATER&&r.accessible});
    if(!r.id)return 0;
    w->resources[i]=r;if(!w->updating)statistics(w);
    AIWorld_SetObservation(&w->observations,r.id,r.originalSpecies,0,r.category,EcosystemResource_Available(&r),false);
    return r.id;
}
bool EcosystemWorld_RemoveResource(EcosystemWorld *w,AIObjectId id) {
    EcosystemResource *r=EcosystemWorld_Resource(w,id);if(!r)return false;
    AIWorld_Remove(&w->observations,id);invalidate(w,id);memset(r,0,sizeof(*r));if(!w->updating)statistics(w);return true;
}
static void invalidate(EcosystemWorld *w,AIObjectId id) {
    for(unsigned i=0;i<w->capacity;++i) {
        EcosystemEntity *e=&w->entities[i];if(!e->id)continue;
        CreaturePerception *p=&e->mind.perception;
        for(unsigned j=0;j<p->count;) {
            if(p->entries[j].id==id)p->entries[j]=p->entries[--p->count];else ++j;
        }
        if(e->mind.target==id)CreatureActions_Fail(&e->mind);
        /* La gestación ya fecundada conserva genoma y ascendencia del progenitor muerto. */
        if(e->reproduction.partner==id&&!e->reproduction.gestating)e->reproduction.partner=0;
    }
}
float EcosystemWorld_Consume(EcosystemWorld *w,AIObjectId id,AIObjectId resource,float requested) {
    EcosystemEntity *e=EcosystemWorld_Get(w,id);EcosystemResource *r=EcosystemWorld_Resource(w,resource);
    if(!e||!r||e->physiology.cause||!isfinite(requested)||requested<=0)return 0;
    CreatureMind *m=&e->mind;CreatureBodyState *b=&e->body;InteractionTarget target;
    bool water=r->category==ECO_WATER;
    if(m->target!=resource||m->intakeRequested<=0||m->contactTime<.25f||m->interactionPhase!=INTERACTION_CONSUME||
       m->action!=(water?ACTION_DRINK:ACTION_EAT)||m->intakeNeed!=(water?NEED_HYDRATION:NEED_HUNGER)||
       (!water&&!(e->brain.dietMask&r->category))||!EcosystemResource_Available(r)||
       (water&&!b->capabilities.waterInteraction)||
       !AIWorld_InteractionTarget(&w->observations,resource,b->mouth,b->capabilities.contactTolerance,&target)||
       target.blocked||b->speed>.04f||b->reach.objectId!=resource||b->reach.status!=REACH_REACHED||
       Vec3_Distance(b->mouth,target.position)>target.positionTolerance||
       Vec3_Dot(b->mouthDirection,m->interaction.approach)<cosf(m->interaction.orientationTolerance))return 0;
    float value=water?1:r->nutritionalValue;
    float deficit=1-(water?e->physiology.hydration:e->physiology.energy);
    float actual=EcosystemResource_Consume(r,fminf(requested,fminf(m->intakeRequested,deficit)/value));
    if(actual>0) {
        if(water){CreaturePhysiology_Drink(&e->physiology,actual);w->statistics.waterConsumed+=actual;++w->statistics.drinks;}
        else {CreaturePhysiology_Feed(&e->physiology,actual*value);w->statistics.foodConsumed+=actual;
            if(r->category==ECO_PLANT)w->statistics.plantConsumed+=actual;else w->statistics.meatConsumed+=actual;
            ++w->statistics.meals;}
        CreatureMind_ConfirmIntake(m,actual*value);event(w,water?4:3,id,resource);
    }
    AIWorld_SetObservation(&w->observations,r->id,r->originalSpecies,0,r->category,EcosystemResource_Available(r),false);
    return actual;
}
bool EcosystemWorld_Attack(EcosystemWorld *w,AIObjectId attacker,AIObjectId prey) {
    EcosystemEntity *a=EcosystemWorld_Get(w,attacker),*b=EcosystemWorld_Get(w,prey);
    if(!a||!b||a==b||a->physiology.cause||b->physiology.cause)return false;
    const EcosystemSpecies *s=&w->species[a->speciesIndex],*t=&w->species[b->speciesIndex];
    Vector3 delta=Vec3_Sub(b->body.position,a->body.position);
    Vector3 front=Vec3_Create(sinf(a->body.yaw),0,cosf(a->body.yaw));
    if(a->mind.action!=ACTION_ATTACK||a->mind.target!=prey||!a->body.canAttack||
       !(s->brain.preyMask&t->category)||!(s->brain.dietMask&ECO_MEAT)||
       w->clock.seconds<a->nextAttack||a->physiology.energy<s->attackCost||
       Vec3_Length(delta)>fminf(a->body.attackRange,a->body.capabilities.maximumOralReach)||
       Vec3_Dot(front,Vec3_Normalize(delta))<.6f||
       !AIWorld_ClearSegment(&w->observations,a->body.mouth,b->body.position,0,prey))return false;
    a->nextAttack=w->clock.seconds+s->attackCooldown;a->physiology.energy-=s->attackCost;
    b->physiology.health=fmaxf(0,b->physiology.health-s->damage);
    ++w->statistics.attacks;event(w,5,attacker,prey);
    if(b->physiology.health<=0&&EcosystemWorld_RequestDeath(w,prey,DEATH_PREDATION))++w->statistics.kills;
    return true;
}
static void needs(EcosystemEntity *e) {
    e->mind.needs.urgency[NEED_HUNGER]=e->brain.needs[NEED_HUNGER].enabled?1-e->physiology.energy:0;
    e->mind.needs.urgency[NEED_HYDRATION]=e->brain.needs[NEED_HYDRATION].enabled?1-e->physiology.hydration:0;
    e->mind.needs.urgency[NEED_REST]=e->brain.needs[NEED_REST].enabled?e->physiology.fatigue:0;
}
static void mouth(EcosystemWorld *w,EcosystemEntity *e,float dt) {
    float size=e->genome.size*e->developmentScale;
    Vector3 pivot=Vec3_Add(e->body.position,Vec3_Create(0,.45f*size,0));
    Vector3 front=Vec3_Create(sinf(e->body.yaw),0,cosf(e->body.yaw));
    Vector3 goal=Vec3_Add(pivot,Vec3_Scale(front,.48f*size*e->genome.morphology));
    InteractionTarget target=e->mind.intent.interaction;
    bool interacting=e->mind.intent.interact&&target.valid;
    if(interacting) {
        InteractionTarget fresh;
        if(!AIWorld_InteractionTarget(&w->observations,target.objectId,e->body.mouth,e->body.capabilities.contactTolerance,&fresh))interacting=false;
        else {target.blocked=fresh.blocked;goal=target.position;}
    }
    Vector3 radial=Vec3_Sub(goal,pivot);float reach=e->body.capabilities.maximumOralReach;
    bool possible=Vec3_Length(radial)<=reach;
    if(!possible)goal=Vec3_Add(pivot,Vec3_Scale(Vec3_Normalize(radial),reach));
    /* Cuerpo analítico: dos segmentos orales iguales admiten puntas en la esfera
     * de alcance. La punta se transporta en el marco corporal y limita velocidad.
     * Nunca se declara contacto si queda residual, bloqueo o velocidad oral. */
    Vector3 old=Vec3_Add(pivot,Vec3_Create(e->oralLocal.x*cosf(e->body.yaw)+e->oralLocal.z*sinf(e->body.yaw),
        e->oralLocal.y,-e->oralLocal.x*sinf(e->body.yaw)+e->oralLocal.z*cosf(e->body.yaw)));
    Vector3 delta=Vec3_Sub(goal,old);float length=Vec3_Length(delta);
    Vector3 next=Vec3_Add(old,Vec3_Scale(Vec3_Normalize(delta),fminf(length,.9f*size*dt)));
    radial=Vec3_Sub(next,pivot);
    if(Vec3_Length(radial)>reach)next=Vec3_Add(pivot,Vec3_Scale(Vec3_Normalize(radial),reach));
    e->oralMotion=Vec3_Distance(old,next)/dt;
    Vector3 local=Vec3_Sub(next,pivot);
    e->oralLocal=Vec3_Create(local.x*cosf(e->body.yaw)-local.z*sinf(e->body.yaw),local.y,
        local.x*sinf(e->body.yaw)+local.z*cosf(e->body.yaw));
    e->oralPivot=pivot;
    Vector3 axis=Vec3_Sub(next,pivot);float distance=Vec3_Length(axis);
    Vector3 perpendicular=Vec3_Cross(Vec3_Normalize(axis),Vec3_Create(0,1,0));
    if(Vec3_LengthSq(perpendicular)<1e-6f)perpendicular=Vec3_Create(1,0,0);
    float bend=sqrtf(fmaxf(0,reach*reach*.25f-distance*distance*.25f));
    e->oralElbow=Vec3_Add(Vec3_Add(pivot,Vec3_Scale(axis,.5f)),Vec3_Scale(Vec3_Normalize(perpendicular),bend));
    Vector3 desired=interacting?target.approach:front;
    Vector3 turn=Vec3_Sub(desired,e->body.mouthDirection);
    e->body.mouthDirection=Vec3_Normalize(Vec3_Add(e->body.mouthDirection,Vec3_Scale(turn,fminf(1,4*dt))));
    e->body.mouth=next;
    e->body.reach=(ReachResult){.objectId=interacting?target.objectId:0,.mouth=next,
        .direction=e->body.mouthDirection,.stability=e->oralMotion,
        .error=Vec3_Distance(next,goal),.status=!interacting?REACH_INVALID:target.blocked?REACH_BLOCKED:
        !possible?REACH_UNREACHABLE:Vec3_Distance(next,target.position)<=target.positionTolerance?REACH_REACHED:REACH_APPROACHING};
}
static float segmentDistance(Vector3 from,Vector3 to,Vector3 p) {
    Vector3 d=Vec3_Sub(to,from);float l=Vec3_LengthSq(d);
    float t=l>1e-8f?clamp(Vec3_Dot(Vec3_Sub(p,from),d)/l,0,1):0;
    return Vec3_Distance(Vec3_Add(from,Vec3_Scale(d,t)),p);
}
static void movement(EcosystemWorld *w,EcosystemEntity *e,float dt) {
    CreatureBodyState *b=&e->body;CreatureMotorCommand c=CreatureMotor_Resolve(&e->mind.intent,b);
    /* Evitación local acotada: desviar intención alrededor de un disco próximo,
     * antes del barrido duro. Ambos sentidos de circulación prefieren su derecha.
     * El giro y la velocidad siguen limitados por el mismo motor corporal. */
    if(c.speed>.04f) {
        Vector3 direction=Vec3_Create(sinf(c.heading),0,cosf(c.heading));
        float look=fminf(Vec3_Distance(b->position,e->mind.intent.destination),fmaxf(.35f,c.speed*.4f));
        Vector3 ahead=Vec3_Add(b->position,Vec3_Scale(direction,look));
        for(unsigned k=0;k<w->count;++k) {
            EcosystemEntity *other=&w->entities[w->order[k]];
            if(!other->id||other==e||other->physiology.cause)continue;
            if(other->id==e->mind.target&&(e->mind.action==ACTION_CHASE||e->mind.action==ACTION_APPROACH_MATE))continue;
            float gap=b->capabilities.clearance+other->body.capabilities.clearance+.06f;
            if(segmentDistance(b->position,ahead,other->body.position)>=gap)continue;
            Vector3 right=Vec3_Create(direction.z,0,-direction.x);
            bool found=false;
            for(unsigned side=0;side<2;++side) {
                Vector3 avoid=Vec3_Add(b->position,Vec3_Add(Vec3_Scale(direction,.3f),Vec3_Scale(right,(side?-1:1)*(gap+.2f))));
                if(AIWorld_SegmentWalkable(&w->observations,b->position,avoid,b->capabilities.clearance,false)) {
                    Vector3 d=Vec3_Sub(avoid,b->position);c.heading=atan2f(d.x,d.z);found=true;break;
                }
            }
            if(!found)c.speed=0;
            break;
        }
    }
    float turn=remainderf(c.heading-b->yaw,6.2831853f);
    b->yaw+=clamp(turn,-b->capabilities.maximumTurn*dt,b->capabilities.maximumTurn*dt);
    float speed=c.speed*fmaxf(0,cosf(turn));
    Vector3 next=Vec3_Add(b->position,Vec3_Scale(Vec3_Create(sinf(b->yaw),0,cosf(b->yaw)),speed*dt));
    bool clear=AIWorld_SegmentWalkable(&w->observations,b->position,next,b->capabilities.clearance,false);
    for(unsigned i=0;clear&&i<w->capacity;++i) {
        EcosystemEntity *other=&w->entities[i];
        if(!other->id||other==e||other->physiology.cause)continue;
        float radii=b->capabilities.clearance+other->body.capabilities.clearance;
        if(segmentDistance(b->position,next,other->body.position)<radii*.98f)clear=false;
    }
    e->previousPosition=b->position;
    if(clear)b->position=next;
    b->speed=Vec3_Distance(b->position,e->previousPosition)/dt;
    mouth(w,e,dt);
}
static void reproduce(EcosystemWorld *w,EcosystemEntity *a) {
    if(!a->body.mateReady||a->mind.action!=ACTION_APPROACH_MATE||a->physiology.cause)return;
    EcosystemEntity *b=EcosystemWorld_Get(w,a->mind.target);
    const ReproductionProfile *r=&w->species[a->speciesIndex].reproduction;
    if(!b||b==a||b->speciesIndex!=a->speciesIndex||a->reproduction.role==b->reproduction.role||
       b->mind.action!=ACTION_APPROACH_MATE||b->mind.target!=a->id||
       !CreatureReproduction_Ready(&a->reproduction,r,&a->physiology,w->clock.seconds)||
       !CreatureReproduction_Ready(&b->reproduction,r,&b->physiology,w->clock.seconds)||
       Vec3_Distance(a->body.position,b->body.position)>a->body.capabilities.clearance+b->body.capabilities.clearance+.25f||
       !AIWorld_LineOfSight(&w->observations,a->body.eye,b->body.eye,b->id))return;
    a->reproduction.partner=b->id;b->reproduction.partner=a->id;
    a->physiology.energy-=r->energyCost;b->physiology.energy-=r->energyCost;
    a->reproduction.cooldownUntil=b->reproduction.cooldownUntil=w->clock.seconds+r->cooldown;
    EcosystemEntity *mother=a->reproduction.role==0?a:b,*father=mother==a?b:a;
    mother->reproduction.gestating=true;mother->reproduction.due=w->clock.seconds+r->gestation;
    mother->reproduction.mateGenome=father->genome;mother->reproduction.mateGeneration=father->generation;
    mother->reproduction.gestationParents[0]=mother->id;mother->reproduction.gestationParents[1]=father->id;
    /* La reserva exclusiva se libera tras fecundación; gestación/cooldown bloquean nuevas parejas. */
    mother->reproduction.partner=0;father->reproduction.partner=0;
    a->body.mateReady=b->body.mateReady=false;
    ++w->statistics.matings;event(w,6,a->id,b->id);
}
static void statistics(EcosystemWorld *w) {
    EcosystemStatistics *s=&w->statistics;
    s->population=w->count;s->juveniles=0;s->averageAge=s->averageHealth=s->plantBiomass=0;
    for(unsigned i=0;i<w->capacity;++i)if(w->entities[i].id) {
        EcosystemEntity *e=&w->entities[i];s->averageAge+=e->physiology.age;s->averageHealth+=e->physiology.health;
        s->juveniles+=e->physiology.stage==LIFE_JUVENILE;
    }
    if(w->count){s->averageAge/=w->count;s->averageHealth/=w->count;}
    for(unsigned i=0;i<w->resourceCapacity;++i)if(w->resources[i].id&&w->resources[i].category==ECO_PLANT)s->plantBiomass+=w->resources[i].biomass;
}
static void commit(EcosystemWorld *w) {
    for(unsigned k=0;k<w->count;++k) {
        EcosystemEntity *e=&w->entities[w->order[k]];
        if(!e->id||e->physiology.cause)continue;
        const ReproductionProfile *r=&w->species[e->speciesIndex].reproduction;
        if(e->reproduction.gestating&&w->clock.seconds+1e-6>=e->reproduction.due) {
            for(unsigned j=0;j<r->offspring;++j) {
                if(w->birthCount==w->capacity){++w->statistics.failedBirths;continue;}
                EcosystemBirth *birth=&w->birthQueue[w->birthCount++];
                *birth=(EcosystemBirth){.speciesIndex=e->speciesIndex,.generation=1+(e->generation>e->reproduction.mateGeneration?e->generation:e->reproduction.mateGeneration),.position=e->body.position,
                    .parents={e->reproduction.gestationParents[0],e->reproduction.gestationParents[1]},
                    .genome=CreatureGenome_Inherit(&e->genome,&e->reproduction.mateGenome,&w->random,r->mutationProbability,r->mutationAmplitude)};
            }
            e->reproduction.gestating=false;e->reproduction.cooldownUntil=w->clock.seconds+r->cooldown;
        }
    }
    unsigned before=w->count;
    for(unsigned k=0;k<before;++k) {
        EcosystemEntity *e=&w->entities[w->order[k]];
        if(!e->id||!e->physiology.cause)continue;
        const EcosystemSpecies *s=&w->species[e->speciesIndex];
        AIObjectId id=e->id;Vector3 position=e->body.position;CreatureDeathCause cause=e->physiology.cause;
        AIWorld_Remove(&w->observations,id);invalidate(w,id);
        ++w->statistics.deaths;++w->statistics.deathCauses[cause];event(w,2,id,cause);
        memset(e,0,sizeof(*e));--w->count;
        if(cause!=DEATH_REMOVED&&s->edibleBiomass>0) {
            AIObjectId carcass=EcosystemWorld_AddResource(w,(EcosystemResource){.category=ECO_MEAT,.originalSpecies=s->id,
                .biomass=s->edibleBiomass,.maximumBiomass=s->edibleBiomass,.nutritionalValue=s->nutrition,
                .remainingLifetime=180,.accessible=true},position,s->radius*1.5f);
            if(!carcass)event(w,9,id,0);
        }
    }
    w->updating=false;
    for(unsigned i=0;i<w->birthCount;++i) {
        EcosystemBirth *birth=&w->birthQueue[i];AIObjectId id=0;
        for(unsigned j=0;j<24&&!id&&w->count<w->capacity;++j) {
            float angle=CreatureGenome_Random(&w->random)*6.2831853f,distance=.65f+CreatureGenome_Random(&w->random)*2;
            Vector3 p=Vec3_Add(birth->position,Vec3_Create(sinf(angle)*distance,0,cosf(angle)*distance));
            if(!spawnPosition(w,p,w->species[birth->speciesIndex].radius*birth->genome.size))continue;
            id=EcosystemWorld_Spawn(w,w->species[birth->speciesIndex].id,p,0,.8f,&birth->genome);
        }
        if(id) {
            EcosystemEntity *child=EcosystemWorld_Get(w,id);child->generation=birth->generation;
            if(child->generation>w->statistics.maximumGeneration)w->statistics.maximumGeneration=child->generation;
            child->reproduction.parents[0]=birth->parents[0];child->reproduction.parents[1]=birth->parents[1];
            ++w->statistics.births;event(w,7,id,birth->parents[0]);
        } else {++w->statistics.failedBirths;event(w,8,birth->parents[0],0);}
    }
    w->birthCount=0;
}
static void tick(EcosystemWorld *w) {
    float dt=(float)w->clock.fixedStep;w->updating=true;
    for(unsigned i=0;i<w->resourceCapacity;++i) {
        EcosystemResource *r=&w->resources[i];if(!r->id)continue;
        EcosystemResource_Update(r,dt);
        if(r->category==ECO_MEAT&&r->biomass<=0){AIObjectId id=r->id;EcosystemWorld_RemoveResource(w,id);invalidate(w,id);continue;}
        AIWorld_SetObservation(&w->observations,r->id,r->originalSpecies,0,r->category,EcosystemResource_Available(r),false);
    }
    unsigned count=0;
    for(unsigned i=0;i<w->capacity;++i)if(w->entities[i].id) {
        unsigned j=count++;while(j&&w->entities[w->order[j-1]].id>w->entities[i].id){w->order[j]=w->order[j-1];--j;}
        w->order[j]=i;
    }
    for(unsigned k=0;k<count;++k) {
        EcosystemEntity *e=&w->entities[w->order[k]];PhysiologyProfile p=physiologicalProfile(w,e);
        p.basalCost*=powf(e->genome.size,.75f)*(e->reproduction.gestating?1.2f:1);
        CreaturePhysiology_Update(&e->physiology,&p,dt,e->body.speed,e->genome.efficiency,
            e->mind.action==ACTION_REST&&e->body.speed<.04f);
        needs(e);updateBody(w,e);
    }
    /* Todas las mentes observan el mismo snapshot anterior al movimiento. */
    for(unsigned k=0;k<count;++k) {
        EcosystemEntity *e=&w->entities[w->order[k]];if(e->physiology.cause)continue;
        unsigned failures=e->mind.actionFailures,nav=e->mind.navigation.failures;
        CreatureAction old=e->mind.action;
        /* La fisiología autoriza hambre/sed/fatiga; el impulso social conserva
         * su integración mental independiente con el mismo tiempo simulado. */
        const NeedDefinition *social=&e->brain.needs[NEED_SOCIAL];
        e->mind.needs.urgency[NEED_SOCIAL]=social->enabled?
            clamp(e->mind.needs.urgency[NEED_SOCIAL]+social->rate*dt,0,1):0;
        CreatureMind_Update(&e->mind,&w->observations,&e->body,dt);
        w->statistics.actionFailures+=e->mind.actionFailures-failures;
        w->statistics.navigationFailures+=e->mind.navigation.failures-nav;
        if(old!=ACTION_ESCAPE&&e->mind.action==ACTION_ESCAPE)++w->statistics.escapes;
    }
    for(unsigned k=0;k<count;++k) {
        EcosystemEntity *e=&w->entities[w->order[k]];if(!e->physiology.cause)movement(w,e,dt);
    }
    for(unsigned k=0;k<count;++k)updateBody(w,&w->entities[w->order[k]]);
    /* Recursos, daño y aceptación mutua: prioridad determinista por ID ascendente. */
    for(unsigned k=0;k<count;++k) {
        EcosystemEntity *e=&w->entities[w->order[k]];if(e->physiology.cause)continue;
        if(e->mind.intakeRequested>0) {
            EcosystemResource *r=EcosystemWorld_Resource(w,e->mind.target);
            if(r)EcosystemWorld_Consume(w,e->id,r->id,e->mind.intakeRequested/(r->category==ECO_WATER?1:r->nutritionalValue));
            if(!r||!EcosystemResource_Available(r))CreatureActions_Fail(&e->mind);
        }
        if(e->mind.action==ACTION_ATTACK)EcosystemWorld_Attack(w,e->id,e->mind.target);
        reproduce(w,e);needs(e);
    }
    commit(w);statistics(w);
}
void EcosystemWorld_Step(EcosystemWorld *w) {if(w&&SimulationClock_Tick(&w->clock))tick(w);}
unsigned EcosystemWorld_Advance(EcosystemWorld *w,double seconds) {
    if(!w)return 0;
    SimulationClock_Accumulate(&w->clock,seconds);unsigned n=0;
    while(n<w->clock.maxStepsPerFrame&&SimulationClock_Pop(&w->clock)){tick(w);++n;}
    w->clock.behind=w->clock.accumulator+1e-12>=w->clock.fixedStep;return n;
}
bool EcosystemWorld_WriteJSON(const EcosystemWorld *w,FILE *f) {
    if(!w||!f)return false;
    const EcosystemStatistics *s=&w->statistics;
    fprintf(f,"{\n  \"seed\":%u,\"tick\":%llu,\"seconds\":%.9f,\"days\":%.9f,\n",w->initialSeed,
        (unsigned long long)w->clock.tick,w->clock.seconds,SimulationClock_Days(&w->clock));
    fprintf(f,"  \"population\":%u,\"juveniles\":%u,\"births\":%llu,\"deaths\":%llu,\"kills\":%llu,\"attacks\":%llu,\"matings\":%llu,\n",
        w->count,s->juveniles,(unsigned long long)s->births,(unsigned long long)s->deaths,
        (unsigned long long)s->kills,(unsigned long long)s->attacks,(unsigned long long)s->matings);
    fprintf(f,"  \"food_consumed\":%.9f,\"water_consumed\":%.9f,\"plant_biomass\":%.9f,\"average_age\":%.9f,\"average_health\":%.9f,\n",
        s->foodConsumed,s->waterConsumed,s->plantBiomass,s->averageAge,s->averageHealth);
    fprintf(f,"  \"action_failures\":%llu,\"navigation_failures\":%llu,\"failed_births\":%llu,\"escapes\":%llu,\"event_hash\":\"%016llx\",\n  \"death_causes\":[",
        (unsigned long long)s->actionFailures,(unsigned long long)s->navigationFailures,(unsigned long long)s->failedBirths,
        (unsigned long long)s->escapes,(unsigned long long)s->eventHash);
    for(unsigned i=0;i<DEATH_CAUSE_COUNT;++i)fprintf(f,"%s%llu",i?",":"",(unsigned long long)s->deathCauses[i]);
    fprintf(f,"],\n  \"plant_consumed\":%.9f,\"meat_consumed\":%.9f,\n",s->plantConsumed,s->meatConsumed);
    fprintf(f,"  \"maximum_generation\":%u,\n  \"species\":[",s->maximumGeneration);
    for(unsigned i=0;i<w->speciesCount;++i) {
        unsigned n=0;for(unsigned j=0;j<w->capacity;++j)n+=w->entities[j].id&&w->entities[j].speciesIndex==i;
        fprintf(f,"%s{\"id\":%u,\"population\":%u}",i?",":"",w->species[i].id,n);
    }
    fprintf(f,"]\n}\n");return !ferror(f);
}
bool EcosystemWorld_WriteCSV(const EcosystemWorld *w,FILE *f,bool header) {
    if(!w||!f)return false;
    if(header) {
        fprintf(f,"tick,seconds,population,births,deaths,kills,plant_biomass,food_consumed,water_consumed,average_age,average_health");
        for(unsigned i=0;i<w->speciesCount;++i)fprintf(f,",species_%u",w->species[i].id);
        fprintf(f,"\n");
    }
    const EcosystemStatistics *s=&w->statistics;
    fprintf(f,"%llu,%.6f,%u,%llu,%llu,%llu,%.6f,%.6f,%.6f,%.6f,%.6f",
        (unsigned long long)w->clock.tick,w->clock.seconds,w->count,(unsigned long long)s->births,
        (unsigned long long)s->deaths,(unsigned long long)s->kills,s->plantBiomass,s->foodConsumed,s->waterConsumed,s->averageAge,s->averageHealth);
    for(unsigned i=0;i<w->speciesCount;++i) {
        unsigned n=0;for(unsigned j=0;j<w->capacity;++j)n+=w->entities[j].id&&w->entities[j].speciesIndex==i;
        fprintf(f,",%u",n);
    }
    fprintf(f,"\n");return !ferror(f);
}

void EcosystemWorld_SetEventOutput(EcosystemWorld *w,FILE *f) {
    if(!w)return;
    w->eventOutput=f;if(f)fprintf(f,"tick,seconds,event,actor,target\n");
}
