#include "EcosystemWorld.h"
#include "CreatureActions.h"
#include "EcosystemAnatomy.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/stat.h>
#include <errno.h>

static void invalidate(EcosystemWorld *,AIObjectId);
static void statistics(EcosystemWorld *);
static float clamp(float v,float a,float b) {return fmaxf(a,fminf(b,v));}
static bool finitePosition(Vector3 p) {return isfinite(p.x)&&isfinite(p.y)&&isfinite(p.z);}

static void event(EcosystemWorld *w,unsigned kind,AIObjectId a,AIObjectId b) {
    if(w->eventOutput)fprintf(w->eventOutput,"%llu,%.9f,%u,%u,%u\n",(unsigned long long)w->clock.tick,w->clock.seconds,kind,a,b);
    uint64_t v[]={w->clock.tick,kind,a,b};
    for(unsigned i=0;i<4;++i){w->statistics.eventHash^=v[i];w->statistics.eventHash*=UINT64_C(1099511628211);}
}

static void recordIntervention(EcosystemWorld *w,const char *action,AIObjectId a,AIObjectId b,
                               uint64_t oldA,uint64_t newA,uint64_t oldB,uint64_t newB) {
    if(!w)return;
    EcosystemIntervention *slot=&w->interventions[w->interventionCount % ECOSYSTEM_INTERVENTION_LOG_CAPACITY];
    memset(slot,0,sizeof(*slot));
    slot->tick=w->clock.tick;
    slot->seconds=w->clock.seconds;
    snprintf(slot->action,sizeof(slot->action),"%s",action?action:"assign");
    slot->actorA=a;slot->actorB=b;
    slot->oldFingerprintA=oldA;slot->newFingerprintA=newA;
    slot->oldFingerprintB=oldB;slot->newFingerprintB=newB;
    ++w->interventionCount;
}

static EcosystemLineage *findOrCreateLineage(EcosystemWorld *w,uint64_t lineageId,
                                             uint64_t founderFp,unsigned speciesId) {
    if(!w||!w->lineages||!lineageId)return NULL;
    for(unsigned i=0;i<w->lineageCount;++i) {
        if(w->lineages[i].lineageId==lineageId)return &w->lineages[i];
    }
    if(w->lineageCount>=ECOSYSTEM_MAX_LINEAGES)return NULL;
    EcosystemLineage *l=&w->lineages[w->lineageCount++];
    memset(l,0,sizeof(*l));
    l->lineageId=lineageId;
    l->founderFingerprint=founderFp;
    l->speciesId=speciesId;
    l->firstSeenSeconds=w->clock.seconds;
    l->lastActiveSeconds=w->clock.seconds;
    return l;
}

static void registerFounderGenome(EcosystemWorld *w,const NeuralGenome *g) {
    if(!w||!w->founders||!NeuralGenome_Validate(g))return;
    uint64_t lid=g->metadata.lineageId;
    if(!lid)return;
    for(unsigned i=0;i<w->founderCount;++i) {
        if(w->founders[i].lineageId==lid)return;
    }
    if(w->founderCount>=ECOSYSTEM_MAX_FOUNDERS)return;
    EcosystemFounderRecord *rec=&w->founders[w->founderCount++];
    rec->lineageId=lid;
    rec->fingerprint=g->metadata.fingerprint;
    NeuralGenome_Clone(&rec->genome,g);
}

static float computeFounderDistance(const EcosystemWorld *w,const NeuralGenome *g) {
    if(!w||!w->founders||!NeuralGenome_Validate(g))return 0.0f;
    uint64_t lid=g->metadata.lineageId;
    for(unsigned i=0;i<w->founderCount;++i) {
        if(w->founders[i].lineageId==lid)return NeuralGenome_Distance(g,&w->founders[i].genome);
    }
    if(w->founderCount>0)return NeuralGenome_Distance(g,&w->founders[0].genome);
    return 0.0f;
}

static void updateArchiveEntry(EcosystemArchiveEntry *dst,const EcosystemWorld *w,
                               const EcosystemEntity *e,bool stillLiving) {
    if(!dst||!w||!e||e->mind.controllerType!=CREATURE_CONTROLLER_NEURAL)return;
    const NeuralGenome *g=CreatureMind_GetNeuralGenome(&e->mind);
    if(!g)return;
    dst->valid=true;
    dst->stillLiving=stillLiving;
    dst->entityId=e->id;
    dst->speciesId=w->species[e->speciesIndex].id;
    dst->generation=e->generation;
    dst->offspringCount=e->offspringCount;
    dst->survivalSeconds=e->physiology.age;
    dst->lineageId=g->metadata.lineageId;
    NeuralGenome_Clone(&dst->genome,g);
}

static void considerForArchives(EcosystemWorld *w,const EcosystemEntity *e,bool stillLiving) {
    if(!w||!e||e->mind.controllerType!=CREATURE_CONTROLLER_NEURAL)return;
    if(!CreatureMind_GetNeuralGenome(&e->mind))return;
    if(!w->archiveBestOffspring.valid ||
       e->offspringCount>w->archiveBestOffspring.offspringCount ||
       (e->offspringCount==w->archiveBestOffspring.offspringCount &&
        e->physiology.age>w->archiveBestOffspring.survivalSeconds)) {
        updateArchiveEntry(&w->archiveBestOffspring,w,e,stillLiving);
    }
    if(!w->archiveLongestSurvival.valid ||
       e->physiology.age>w->archiveLongestSurvival.survivalSeconds) {
        updateArchiveEntry(&w->archiveLongestSurvival,w,e,stillLiving);
    }
    if(!w->archiveHighestGeneration.valid ||
       e->generation>w->archiveHighestGeneration.generation ||
       (e->generation==w->archiveHighestGeneration.generation &&
        e->offspringCount>w->archiveHighestGeneration.offspringCount)) {
        updateArchiveEntry(&w->archiveHighestGeneration,w,e,stillLiving);
    }
}

EcosystemSpecies EcosystemSpecies_Default(unsigned id,unsigned category,unsigned diet,unsigned prey,unsigned threats) {
    bool carnivore=category==ECO_MEAT;
    /* Discos conservadores para las secciones torácicas de las recetas.
     * La longitud visual se calibra desde el eje corporal, sin contar la cola.
     * El alcance oral se valida además contra las articulaciones reales. */
    EcosystemSpecies s={.id=id,.category=category,.recipeId=carnivore?4u:3u,
        .brain=*BrainProfile_Find("forager"),
        .physiology=CreaturePhysiology_Default(),.reproduction=CreatureReproduction_Default(),
        .speed=carnivore?2.8f:2.2f,.radius=carnivore?.206f:.278f,
        .oralReach=carnivore?.785f:1.589f,.attackRange=carnivore?.69f:1.398f,
        .damage=.36f,.attackCooldown=.8f,.attackCost=.008f,
        .edibleBiomass=carnivore?2.332f:5.515f,.nutrition=carnivore?.480f:.203f};
    snprintf(s.name,sizeof(s.name),"perfil_%u",id);
    /* Separación explícita: dieta y capacidad de ataque pertenecen a la fisiología. */
    s.physiology.dietMask=diet;
    s.physiology.preyMask=prey;
    s.physiology.canAttackPhysically=(prey!=0);
    s.physiology.waterInteraction=true;
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

bool EcosystemWorld_InitArena(EcosystemWorld *w,unsigned cap,unsigned resources,unsigned species,
                              float arenaHalfSize,uint32_t seed,double step,double day) {
    if(!w)return false;
    memset(w,0,sizeof(*w));
    if(!cap||cap>100000||!resources||resources>100000||!species||species>1024||
        !isfinite(arenaHalfSize)||arenaHalfSize<4.0f||arenaHalfSize>500.0f||
        !SimulationClock_Init(&w->clock,step,day,64))return false;
    if(!AIWorld_InitCapacity(&w->observations,arenaHalfSize,cap+resources+ECOSYSTEM_MAX_OBSTACLES))return false;
    w->entities=calloc(cap,sizeof(*w->entities));w->resources=calloc(resources,sizeof(*w->resources));
    w->species=calloc(species,sizeof(*w->species));w->birthQueue=calloc(cap,sizeof(*w->birthQueue));
    w->order=calloc(cap,sizeof(*w->order));
    w->lineages=calloc(ECOSYSTEM_MAX_LINEAGES,sizeof(*w->lineages));
    w->founders=calloc(ECOSYSTEM_MAX_FOUNDERS,sizeof(*w->founders));
    if(!w->entities||!w->resources||!w->species||!w->birthQueue||!w->order||!w->lineages||!w->founders){
        EcosystemWorld_Free(w);return false;
    }
    w->capacity=cap;w->resourceCapacity=resources;w->speciesCapacity=species;
    w->random=seed?seed:1;w->initialSeed=seed;w->statistics.eventHash=UINT64_C(1469598103934665603);
    w->defaultController=CREATURE_CONTROLLER_UTILITY;
    w->neuralMutation=NeuralMutationConfig_Default();
    w->neuralController=NeuralControllerConfig_Default();
    w->nextLineageId=1u;
    snprintf(w->founderModeName,sizeof(w->founderModeName),"utility");
    return true;
}

bool EcosystemWorld_Init(EcosystemWorld *w,unsigned cap,unsigned resources,unsigned species,uint32_t seed,double step,double day) {
    return EcosystemWorld_InitArena(w,cap,resources,species,24.0f,seed,step,day);
}

void EcosystemWorld_Free(EcosystemWorld *w) {
    if(!w)return;
    if(w->entities)for(unsigned i=0;i<w->capacity;++i) {
        NeuralBrain_Destroy(&w->entities[i].mind.neuralBrain);
        NeuralGenome_Destroy(&w->entities[i].reproduction.gestationNeuralGenomes[0]);
        NeuralGenome_Destroy(&w->entities[i].reproduction.gestationNeuralGenomes[1]);
        free(w->entities[i].anatomicalBody);
    }
    free(w->entities);free(w->resources);free(w->species);free(w->birthQueue);free(w->order);
    free(w->lineages);free(w->founders);
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
    /* Compatibilidad con código heredado que sólo configuraba dietMask/preyMask en BrainProfile. */
    if(s.physiology.dietMask==0&&s.brain.dietMask!=0)s.physiology.dietMask=s.brain.dietMask;
    if(s.physiology.preyMask==0&&s.brain.preyMask!=0)s.physiology.preyMask=s.brain.preyMask;
    if(!s.physiology.canAttackPhysically&&s.physiology.preyMask!=0&&s.damage>0&&s.attackRange>0)
        s.physiology.canAttackPhysically=true;
    s.name[sizeof(s.name)-1]=0;w->species[w->speciesCount]=s;
    w->species[w->speciesCount].brain.name=w->species[w->speciesCount].name;
    ++w->speciesCount;return s.id;
}

AIObjectId EcosystemWorld_AddObstacle(EcosystemWorld *w,Vector3 position,float radius,float height) {
    if(!w||w->updating||w->obstacleCount>=ECOSYSTEM_MAX_OBSTACLES||
       !finitePosition(position)||!isfinite(radius)||radius<=0.05f||radius>10.0f||
       !isfinite(height)||height<=0.05f||height>20.0f)return 0;
    if(fabsf(position.x)+radius>w->observations.halfSize||
       fabsf(position.z)+radius>w->observations.halfSize)return 0;
    AIObjectId id=AIWorld_Add(&w->observations,(AIWorldObject){
        .type=AI_OBSTACLE,.position=position,.radius=radius,.height=height,
        .visible=true,.occludes=true,.walkable=false,.drinkable=false
    });
    if(!id)return 0;
    w->obstacles[w->obstacleCount++]=(EcosystemObstacle){
        .id=id,.position=position,.radius=radius,.height=height
    };
    return id;
}

EcosystemEntity *EcosystemWorld_Get(EcosystemWorld *w,AIObjectId id) {
    if(!w||!id)return NULL;
    for(unsigned i=0;i<w->capacity;++i)if(w->entities[i].id==id)return &w->entities[i];
    return NULL;
}

const EcosystemEntity *EcosystemWorld_GetConst(const EcosystemWorld *w,AIObjectId id) {
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
    e->body.energy=e->physiology.energy;
    e->body.hydration=e->physiology.hydration;
    e->body.fatigue=e->physiology.fatigue;
    e->body.health=e->physiology.health;
    e->body.normalizedAge=p.maximumAge>0?clamp((float)(e->physiology.age/p.maximumAge),0,1):0;
    e->body.canAttack=s->physiology.canAttackPhysically&&e->physiology.energy>.03f;
    e->body.attackRange=s->attackRange*size;
    e->body.mateReady=CreatureReproduction_Ready(&e->reproduction,&s->reproduction,&e->physiology,w->clock.seconds);
    e->body.capabilities=(CreatureCapabilities){.ground=true,.waterInteraction=s->physiology.waterInteraction,
        .contactTolerance=.09f*size,.maximumOralReach=s->oralReach*size*e->genome.morphology,.clearance=s->radius*size,
        .bankClearance=s->radius*size,.drinkReach=.09f*size,.mouthForward=.48f*size*e->genome.morphology,
        .maximumSpeed=s->speed*e->genome.speed*(.6f+.4f*e->developmentScale)*(1-.45f*e->physiology.fatigue),.maximumTurn=s->turnRate>0?s->turnRate:3};
    e->body.eye=Vec3_Add(e->body.position,Vec3_Create(0,.65f*size,0));
    e->body.visionYaw=e->body.yaw;
    AIWorld_SetAgentState(&w->observations,e->id,e->reproduction.role,e->body.capabilities.maximumSpeed);
    AIWorld_Move(&w->observations,e->id,e->body.position);
    AIWorld_SetBounds(&w->observations,e->id,e->body.capabilities.clearance,.65f*size);
    AIWorld_SetObservation(&w->observations,e->id,s->id,s->category,0,!e->physiology.cause,e->body.mateReady);
    if(s->anatomicalOral&&EcosystemAnatomy_Configure(e,s)&&e->anatomicalBody) {
        const EcosystemAnatomy* a=e->anatomicalBody;
        float scale=EcosystemAnatomy_Scale(a,e);
        Vector3 oral=Vec3_Add(a->rest.joints[a->rig.headJoint].worldPosition,a->rig.oralPoint);
        e->body.capabilities.mouthForward=(oral.z-a->center.z)*scale;
    }
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
    double maturity=w->species[si].physiology.maturityAge*g.maturation;
    float development=age<maturity?.55f+.45f*(float)(age/maturity):1;
    if(w->count>=w->capacity||!spawnPosition(w,position,w->species[si].radius*g.size*development)) {++w->statistics.failedSpawns;return 0;}
    unsigned slot=0;while(w->entities[slot].id)++slot;
    AIObjectId id=AIWorld_Add(&w->observations,(AIWorldObject){.type=AI_CREATURE,.group=w->observations.nextId%2,.position=position,.visible=true,.walkable=true});
    if(!id){++w->statistics.failedSpawns;return 0;}
    EcosystemEntity *e=&w->entities[slot];memset(e,0,sizeof(*e));e->id=id;e->speciesIndex=si;e->genome=g;
    e->bornAt=w->clock.seconds;
    e->seed=(uint32_t)(CreatureGenome_Random(&w->random)*16777216)+id;
    e->reproduction.role=id%2;e->brain=w->species[si].brain;e->brain.perception.range*=g.perception;
    e->physiology=CreaturePhysiology_Create(age,energy);PhysiologyProfile p=physiologicalProfile(w,e);
    e->physiology.stage=age<p.maturityAge?LIFE_JUVENILE:age<p.oldAge?LIFE_ADULT:LIFE_OLD;
    e->body.position=position;e->body.yaw=CreatureGenome_Random(&w->random)*6.2831853f;
    e->oralLocal=Vec3_Create(0,.45f*g.size,.48f*g.size);
    e->body.mouth=Vec3_Add(position,e->oralLocal);e->body.mouthDirection=Vec3_Create(0,0,1);
    CreatureMind_Init(&e->mind,&e->brain,id,e->seed);e->mind.externalPhysiology=true;e->mind.transactionalIntake=true;
    CreatureMind_SetNeuralConfig(&e->mind,w->neuralController);
    e->mind.time=w->clock.seconds;e->mind.nextDecision+=w->clock.seconds;
    e->mind.perception.nextRefresh+=w->clock.seconds;e->mind.started+=w->clock.seconds;
    ++w->count;updateBody(w,e);
    float size=e->genome.size*e->developmentScale;
    e->oralLocal=Vec3_Create(0,0,.48f*size*e->genome.morphology);
    e->body.mouth=Vec3_Add(position,Vec3_Create(sinf(e->body.yaw)*e->oralLocal.z,.45f*size,cosf(e->body.yaw)*e->oralLocal.z));
    e->body.mouthDirection=Vec3_Create(sinf(e->body.yaw),0,cosf(e->body.yaw));
    if(e->anatomicalBody)EcosystemAnatomy_Update(e,0);
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
        /* La gestación ya fecundada conserva genoma físico y neuronal del progenitor muerto. */
        if(e->reproduction.partner==id&&!e->reproduction.gestating)e->reproduction.partner=0;
    }
}

float EcosystemWorld_Consume(EcosystemWorld *w,AIObjectId id,AIObjectId resource,float requested) {
    EcosystemEntity *e=EcosystemWorld_Get(w,id);EcosystemResource *r=EcosystemWorld_Resource(w,resource);
    if(!e||!r||e->physiology.cause||!isfinite(requested)||requested<=0)return 0;
    const EcosystemSpecies *sp=&w->species[e->speciesIndex];
    CreatureMind *m=&e->mind;CreatureBodyState *b=&e->body;InteractionTarget target;
    bool water=r->category==ECO_WATER;
    if(m->target!=resource||m->intakeRequested<=0||m->contactTime<.25f||m->interactionPhase!=INTERACTION_CONSUME||
       m->action!=(water?ACTION_DRINK:ACTION_EAT)||m->intakeNeed!=(water?NEED_HYDRATION:NEED_HUNGER)||
       !EcosystemResource_Available(r)||
       (water&&!b->capabilities.waterInteraction)||
       !AIWorld_InteractionTarget(&w->observations,resource,b->mouth,b->capabilities.contactTolerance,&target)||
       target.blocked||b->speed>.04f||b->reach.objectId!=resource||b->reach.status!=REACH_REACHED||
       Vec3_Distance(b->mouth,target.position)>target.positionTolerance||
       Vec3_Dot(b->mouthDirection,m->interaction.approach)<cosf(m->interaction.orientationTolerance))return 0;
    /* La compatibilidad digestiva pertenece al cuerpo/especie, nunca al cerebro asignado. */
    if(!water&&!(sp->physiology.dietMask&r->category)) {
        ++w->statistics.failedInteractions;
        if(m->controllerType==CREATURE_CONTROLLER_NEURAL) {
            CreatureActions_Fail(m);
            CreatureMind_RecordFailure(m,NEURAL_OUTCOME_FAILED_DIET);
        }
        return 0;
    }
    float value=water?1:r->nutritionalValue;
    float deficit=1-(water?e->physiology.hydration:e->physiology.energy);
    if(deficit<=0)return 0;
    float actual=EcosystemResource_Consume(r,fminf(requested,fminf(m->intakeRequested,deficit)/value));
    if(actual>0) {
        if(water){
            CreaturePhysiology_Drink(&e->physiology,actual);w->statistics.waterConsumed+=actual;++w->statistics.drinks;
            if(m->controllerType==CREATURE_CONTROLLER_NEURAL)CreatureMind_RecordOutcome(m,NEURAL_OUTCOME_DRANK_WATER);
        } else {
            CreaturePhysiology_Feed(&e->physiology,actual*value);w->statistics.foodConsumed+=actual;
            if(r->category==ECO_PLANT){
                w->statistics.plantConsumed+=actual;
                if(m->controllerType==CREATURE_CONTROLLER_NEURAL)CreatureMind_RecordOutcome(m,NEURAL_OUTCOME_CONSUMED_PLANT);
            } else {
                w->statistics.meatConsumed+=actual;
                if(m->controllerType==CREATURE_CONTROLLER_NEURAL)CreatureMind_RecordOutcome(m,NEURAL_OUTCOME_CONSUMED_MEAT);
            }
            ++w->statistics.meals;
        }
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
    if(a->mind.action!=ACTION_ATTACK||a->mind.target!=prey)return false;
    /* La capacidad física de ataque y presa pertenece a la anatomía/fisiología de la especie. */
    if(!a->body.canAttack||!s->physiology.canAttackPhysically||
       !(s->physiology.preyMask&t->category)||!(s->physiology.dietMask&ECO_MEAT)) {
        ++w->statistics.failedInteractions;
        if(a->mind.controllerType==CREATURE_CONTROLLER_NEURAL) {
            CreatureActions_Fail(&a->mind);
            CreatureMind_RecordFailure(&a->mind,NEURAL_OUTCOME_FAILED_ATTACK);
        }
        return false;
    }
    if(w->clock.seconds<a->nextAttack||a->physiology.energy<s->attackCost||
       Vec3_Length(delta)>fminf(a->body.attackRange,a->body.capabilities.maximumOralReach)||
       Vec3_Dot(front,Vec3_Normalize(delta))<.6f||
       !AIWorld_ClearSegment(&w->observations,a->body.mouth,b->body.position,0,prey))return false;
    if(a->anatomicalBody) {
        Vector3 center=Vec3_Add(b->body.position,Vec3_Create(0,.55f*b->developmentScale*b->genome.size,0));
        if(a->body.reach.objectId!=prey||a->body.reach.status!=REACH_REACHED||
            Vec3_Distance(a->body.mouth,center)>b->body.capabilities.clearance+.15f)return false;
    }
    a->nextAttack=w->clock.seconds+s->attackCooldown;a->physiology.energy-=s->attackCost;
    b->physiology.health=fmaxf(0,b->physiology.health-s->damage);
    if(a->mind.controllerType==CREATURE_CONTROLLER_NEURAL)
        CreatureMind_RecordOutcome(&a->mind,NEURAL_OUTCOME_ATTACK_HIT);
    if(b->mind.controllerType==CREATURE_CONTROLLER_NEURAL)
        CreatureMind_RecordDamage(&b->mind,s->damage);
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
    if(e->anatomicalBody){
        CreatureIntent saved=e->mind.intent;
        const EcosystemEntity* prey=e->mind.action==ACTION_ATTACK?EcosystemWorld_Get(w,e->mind.target):NULL;
        if(prey) {
            Vector3 center=Vec3_Add(prey->body.position,Vec3_Create(0,.55f*prey->developmentScale*prey->genome.size,0));
            Vector3 point=Vec3_Add(center,Vec3_Scale(Vec3_Normalize(Vec3_Sub(e->body.mouth,center)),prey->body.capabilities.clearance*.96f));
            Vector3 approach=Vec3_Normalize(Vec3_Sub(point,e->body.mouth));
            if(Vec3_LengthSq(approach)<.5f)approach=Vec3_Create(sinf(e->body.yaw),0,cosf(e->body.yaw));
            e->mind.intent.interact=true;
            e->mind.intent.interaction=(InteractionTarget){.valid=true,.objectId=prey->id,.type=INTERACTION_EAT,
                .position=point,.normal={0,1,0},.approach=approach,.positionTolerance=.12f,
                .orientationTolerance=1.4f,.moving=true};
        }
        EcosystemAnatomy_Update(e,dt);e->mind.intent=saved;return;
    }
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
    if(c.speed>.04f) {
        Vector3 direction=Vec3_Create(sinf(c.heading),0,cosf(c.heading));
        float look=fminf(Vec3_Distance(b->position,e->mind.intent.destination),fmaxf(.35f,c.speed*.4f));
        Vector3 ahead=Vec3_Add(b->position,Vec3_Scale(direction,look));
        for(unsigned k=0;k<w->count;++k) {
            EcosystemEntity *other=&w->entities[w->order[k]];
            if(!other->id||other==e||other->physiology.cause)continue;
            if(other->id==e->mind.target&&(e->mind.action==ACTION_CHASE||e->mind.action==ACTION_ATTACK||e->mind.action==ACTION_APPROACH_MATE))continue;
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
    float distance=Vec3_Distance(b->position,e->mind.intent.destination);
    float curvature=2*fabsf(sinf(turn))/fmaxf(b->capabilities.clearance,distance);
    if(curvature>.001f)speed=copysignf(fminf(fabsf(speed),b->capabilities.maximumTurn/curvature),speed);
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
    /* Preservar los genomas neuronales de AMBOS progenitores en el instante de la concepción. */
    const NeuralGenome *motherBrain=CreatureMind_GetNeuralGenome(&mother->mind);
    const NeuralGenome *fatherBrain=CreatureMind_GetNeuralGenome(&father->mind);
    if(motherBrain&&fatherBrain) {
        bool okM=NeuralGenome_Clone(&mother->reproduction.gestationNeuralGenomes[0],motherBrain);
        bool okF=NeuralGenome_Clone(&mother->reproduction.gestationNeuralGenomes[1],fatherBrain);
        mother->reproduction.hasGestationNeuralGenomes=okM&&okF;
    } else {
        mother->reproduction.hasGestationNeuralGenomes=false;
    }
    mother->reproduction.partner=0;father->reproduction.partner=0;
    a->body.mateReady=b->body.mateReady=false;
    if(a->mind.controllerType==CREATURE_CONTROLLER_NEURAL)CreatureMind_RecordOutcome(&a->mind,NEURAL_OUTCOME_MATED);
    if(b->mind.controllerType==CREATURE_CONTROLLER_NEURAL)CreatureMind_RecordOutcome(&b->mind,NEURAL_OUTCOME_MATED);
    ++w->statistics.matings;event(w,6,a->id,b->id);
}

static void statistics(EcosystemWorld *w) {
    EcosystemStatistics *s=&w->statistics;
    s->population=w->count;s->juveniles=0;
    s->averageAge=s->averageHealth=s->averageEnergy=s->averageGeneration=s->plantBiomass=0;
    s->averageFounderDistance=0;s->maxFounderDistance=0;
    unsigned neuralCount=0;
    for(unsigned i=0;i<w->lineageCount;++i)w->lineages[i].livingCount=0;
    for(unsigned i=0;i<w->capacity;++i)if(w->entities[i].id) {
        EcosystemEntity *e=&w->entities[i];
        s->averageAge+=e->physiology.age;
        s->averageHealth+=e->physiology.health;
        s->averageEnergy+=e->physiology.energy;
        s->averageGeneration+=e->generation;
        s->juveniles+=e->physiology.stage==LIFE_JUVENILE;
        if(e->physiology.age>s->maxObservedSurvival)s->maxObservedSurvival=e->physiology.age;
        if(e->mind.controllerType==CREATURE_CONTROLLER_NEURAL) {
            ++neuralCount;
            s->averageFounderDistance+=e->founderDistance;
            if(e->founderDistance>s->maxFounderDistance)s->maxFounderDistance=e->founderDistance;
            const NeuralGenome *g=CreatureMind_GetNeuralGenome(&e->mind);
            if(g&&g->metadata.lineageId) {
                EcosystemLineage *l=findOrCreateLineage(w,g->metadata.lineageId,
                    g->metadata.founderFingerprint,w->species[e->speciesIndex].id);
                if(l){++l->livingCount;l->lastActiveSeconds=w->clock.seconds;if(e->generation>l->maxGeneration)l->maxGeneration=e->generation;}
            }
        }
    }
    if(w->count){
        s->averageAge/=w->count;
        s->averageHealth/=w->count;
        s->averageEnergy/=w->count;
        s->averageGeneration/=w->count;
    }
    if(neuralCount){s->averageFounderDistance/=neuralCount;}
    s->activeLineages=0;
    for(unsigned i=0;i<w->lineageCount;++i)if(w->lineages[i].livingCount>0)++s->activeLineages;
    s->averageSurvivalDuration=s->completedLifetimes?(s->survivalDurationSum/s->completedLifetimes):s->averageAge;
    for(unsigned i=0;i<w->resourceCapacity;++i)if(w->resources[i].id&&w->resources[i].category==ECO_PLANT)s->plantBiomass+=w->resources[i].biomass;
}

void EcosystemWorld_ComputeNeuralMetrics(EcosystemWorld *w) {
    if(!w)return;
    const NeuralGenome *living[256];
    unsigned n=0;
    for(unsigned i=0;i<w->capacity&&n<256;++i) {
        if(!w->entities[i].id)continue;
        const NeuralGenome *g=CreatureMind_GetNeuralGenome(&w->entities[i].mind);
        if(g)living[n++]=g;
    }
    if(n<2){w->statistics.neuralDiversity=0.0;return;}
    double sum=0.0;
    unsigned pairs=0;
    unsigned stride=n>48?(n/24):1;
    for(unsigned i=0;i<n;i+=stride) {
        for(unsigned j=i+1;j<n;j+=stride) {
            sum+=(double)NeuralGenome_Distance(living[i],living[j]);
            ++pairs;
        }
    }
    w->statistics.neuralDiversity=pairs?(sum/pairs):0.0;
}

static void commit(EcosystemWorld *w) {
    for(unsigned k=0;k<w->count;++k) {
        EcosystemEntity *e=&w->entities[w->order[k]];
        if(!e->id||e->physiology.cause)continue;
        const ReproductionProfile *r=&w->species[e->speciesIndex].reproduction;
        if(e->reproduction.gestating&&w->clock.seconds+1e-6>=e->reproduction.due) {
            float physProb=w->disablePhysicalMutation?0.0f:r->mutationProbability;
            float physAmp=w->disablePhysicalMutation?0.0f:r->mutationAmplitude;
            for(unsigned j=0;j<r->offspring;++j) {
                if(w->birthCount==w->capacity){++w->statistics.failedBirths;continue;}
                EcosystemBirth *birth=&w->birthQueue[w->birthCount++];
                memset(birth,0,sizeof(*birth));
                birth->speciesIndex=e->speciesIndex;
                birth->generation=1+(e->generation>e->reproduction.mateGeneration?e->generation:e->reproduction.mateGeneration);
                birth->position=e->body.position;
                birth->parents[0]=e->reproduction.gestationParents[0];
                birth->parents[1]=e->reproduction.gestationParents[1];
                birth->genome=CreatureGenome_Inherit(&e->genome,&e->reproduction.mateGenome,&w->random,physProb,physAmp);
                if(e->reproduction.hasGestationNeuralGenomes) {
                    if(NeuralGenome_Inherit(&birth->neuralGenome,
                                            &e->reproduction.gestationNeuralGenomes[0],
                                            &e->reproduction.gestationNeuralGenomes[1],
                                            w->neuralMutation,&w->random)) {
                        birth->neuralGenome.metadata.generation=birth->generation;
                        birth->neuralGenome.metadata.parentIds[0]=birth->parents[0];
                        birth->neuralGenome.metadata.parentIds[1]=birth->parents[1];
                        birth->hasNeuralGenome=true;
                    }
                }
            }
            e->reproduction.gestating=false;
            e->reproduction.hasGestationNeuralGenomes=false;
            NeuralGenome_Destroy(&e->reproduction.gestationNeuralGenomes[0]);
            NeuralGenome_Destroy(&e->reproduction.gestationNeuralGenomes[1]);
            e->reproduction.cooldownUntil=w->clock.seconds+r->cooldown;
        }
    }
    unsigned before=w->count;
    for(unsigned k=0;k<before;++k) {
        EcosystemEntity *e=&w->entities[w->order[k]];
        if(!e->id||!e->physiology.cause)continue;
        const EcosystemSpecies *s=&w->species[e->speciesIndex];
        AIObjectId id=e->id;Vector3 position=e->body.position;CreatureDeathCause cause=e->physiology.cause;
        considerForArchives(w,e,false);
        w->statistics.survivalDurationSum+=e->physiology.age;
        ++w->statistics.completedLifetimes;
        if(e->physiology.age>w->statistics.maxObservedSurvival)w->statistics.maxObservedSurvival=e->physiology.age;
        AIWorld_Remove(&w->observations,id);invalidate(w,id);
        ++w->statistics.deaths;++w->statistics.deathCauses[cause];event(w,2,id,cause);
        NeuralBrain_Destroy(&e->mind.neuralBrain);
        NeuralGenome_Destroy(&e->reproduction.gestationNeuralGenomes[0]);
        NeuralGenome_Destroy(&e->reproduction.gestationNeuralGenomes[1]);
        free(e->anatomicalBody);memset(e,0,sizeof(*e));--w->count;
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
            if(!spawnPosition(w,p,w->species[birth->speciesIndex].radius*birth->genome.size*.55f))continue;
            id=EcosystemWorld_Spawn(w,w->species[birth->speciesIndex].id,p,0,.8f,&birth->genome);
        }
        if(id) {
            EcosystemEntity *child=EcosystemWorld_Get(w,id);child->generation=birth->generation;
            if(child->generation>w->statistics.maximumGeneration)w->statistics.maximumGeneration=child->generation;
            child->reproduction.parents[0]=birth->parents[0];child->reproduction.parents[1]=birth->parents[1];
            unsigned genBucket=child->generation<ECOSYSTEM_MAX_GENERATION_BUCKETS?child->generation:(ECOSYSTEM_MAX_GENERATION_BUCKETS-1);
            ++w->statistics.birthsPerGeneration[genBucket];
            if(birth->hasNeuralGenome) {
                CreatureMind_AssignNeuralGenome(&child->mind,&birth->neuralGenome);
                child->founderDistance=computeFounderDistance(w,&birth->neuralGenome);
                EcosystemLineage *l=findOrCreateLineage(w,birth->neuralGenome.metadata.lineageId,
                    birth->neuralGenome.metadata.founderFingerprint,w->species[birth->speciesIndex].id);
                if(l){++l->totalMembers;++l->totalOffspring;if(child->generation>l->maxGeneration)l->maxGeneration=child->generation;}
            }
            for(unsigned pi=0;pi<2;++pi) {
                EcosystemEntity *par=EcosystemWorld_Get(w,birth->parents[pi]);
                if(par)++par->offspringCount;
            }
            ++w->statistics.births;event(w,7,id,birth->parents[0]);
        } else {++w->statistics.failedBirths;event(w,8,birth->parents[0],0);}
        NeuralGenome_Destroy(&birth->neuralGenome);
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
        float healthBefore=e->physiology.health;
        CreaturePhysiology_Update(&e->physiology,&p,dt,e->body.speed,e->genome.efficiency,
            e->mind.action==ACTION_REST&&e->body.speed<.04f);
        if(e->mind.controllerType==CREATURE_CONTROLLER_NEURAL && e->physiology.health<healthBefore) {
            CreatureMind_RecordDamage(&e->mind,healthBefore-e->physiology.health);
        }
        needs(e);updateBody(w,e);
    }
    /* Todas las mentes observan el mismo snapshot anterior al movimiento. */
    for(unsigned k=0;k<count;++k) {
        EcosystemEntity *e=&w->entities[w->order[k]];if(e->physiology.cause)continue;
        unsigned failures=e->mind.actionFailures,nav=e->mind.navigation.failures;
        uint64_t decisionsBefore=e->mind.neuralState.decisionCount;
        CreatureAction old=e->mind.action;
        const NeedDefinition *social=&e->brain.needs[NEED_SOCIAL];
        e->mind.needs.urgency[NEED_SOCIAL]=social->enabled?
            clamp(e->mind.needs.urgency[NEED_SOCIAL]+social->rate*dt,0,1):0;
        CreatureMind_Update(&e->mind,&w->observations,&e->body,dt);
        w->statistics.actionFailures+=e->mind.actionFailures-failures;
        w->statistics.navigationFailures+=e->mind.navigation.failures-nav;
        if(e->mind.controllerType==CREATURE_CONTROLLER_NEURAL &&
           e->mind.neuralState.decisionCount>decisionsBefore) {
            NeuralActionIntent intent=e->mind.neuralState.selectedIntent;
            if(intent>=0&&intent<ACTION_INTENT_COUNT)++w->statistics.intentSelections[intent];
        }
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
    /* Diversidad neuronal muestreada periódicamente (1 Hz) para evitar sobrecoste por tick. */
    if(w->clock.tick%60u==0u)EcosystemWorld_ComputeNeuralMetrics(w);
}

void EcosystemWorld_Step(EcosystemWorld *w) {if(w&&SimulationClock_Tick(&w->clock))tick(w);}

unsigned EcosystemWorld_Advance(EcosystemWorld *w,double seconds) {
    if(!w)return 0;
    SimulationClock_Accumulate(&w->clock,seconds);unsigned n=0;
    while(n<w->clock.maxStepsPerFrame&&SimulationClock_Pop(&w->clock)){tick(w);++n;}
    w->clock.behind=w->clock.accumulator+1e-12>=w->clock.fixedStep;return n;
}

bool EcosystemWorld_AssignBrain(EcosystemWorld *w,AIObjectId id,const NeuralGenome *genome,const char *reason) {
    if(!w||w->updating||!NeuralGenome_Validate(genome))return false;
    EcosystemEntity *e=EcosystemWorld_Get(w,id);
    if(!e||e->physiology.cause)return false;
    const NeuralGenome *prev=CreatureMind_GetNeuralGenome(&e->mind);
    uint64_t oldFp=prev?prev->metadata.fingerprint:0u;
    NeuralGenome copy;
    if(!NeuralGenome_Clone(&copy,genome))return false;
    if(copy.metadata.lineageId==0u)copy.metadata.lineageId=w->nextLineageId++;
    if(!CreatureMind_AssignNeuralGenome(&e->mind,&copy)){NeuralGenome_Destroy(&copy);return false;}
    registerFounderGenome(w,&copy);
    e->founderDistance=computeFounderDistance(w,&copy);
    EcosystemLineage *l=findOrCreateLineage(w,copy.metadata.lineageId,
        copy.metadata.founderFingerprint,w->species[e->speciesIndex].id);
    if(l&&!prev){++l->totalMembers;}
    uint64_t newFp=e->mind.neuralBrain.genome.metadata.fingerprint;
    if(reason&&reason[0])recordIntervention(w,reason,id,0,oldFp,newFp,0,0);
    NeuralGenome_Destroy(&copy);
    statistics(w);
    return true;
}

bool EcosystemWorld_CloneBrain(EcosystemWorld *w,AIObjectId sourceId,AIObjectId targetId) {
    if(!w||w->updating)return false;
    const EcosystemEntity *src=EcosystemWorld_GetConst(w,sourceId);
    EcosystemEntity *dst=EcosystemWorld_Get(w,targetId);
    if(!src||!dst||src->physiology.cause||dst->physiology.cause)return false;
    const NeuralGenome *srcGenome=CreatureMind_GetNeuralGenome(&src->mind);
    if(!srcGenome)return false;
    NeuralGenome copy;
    if(!NeuralGenome_Clone(&copy,srcGenome))return false;
    snprintf(copy.metadata.origin,sizeof(copy.metadata.origin),"cloned");
    const NeuralGenome *prevDst=CreatureMind_GetNeuralGenome(&dst->mind);
    uint64_t oldDstFp=prevDst?prevDst->metadata.fingerprint:0u;
    bool ok=CreatureMind_AssignNeuralGenome(&dst->mind,&copy);
    if(ok) {
        dst->founderDistance=computeFounderDistance(w,&copy);
        recordIntervention(w,"clone",sourceId,targetId,srcGenome->metadata.fingerprint,
                           srcGenome->metadata.fingerprint,oldDstFp,copy.metadata.fingerprint);
        statistics(w);
    }
    NeuralGenome_Destroy(&copy);
    return ok;
}

bool EcosystemWorld_SwapBrains(EcosystemWorld *w,AIObjectId idA,AIObjectId idB) {
    if(!w||w->updating)return false;
    EcosystemEntity *a=EcosystemWorld_Get(w,idA),*b=EcosystemWorld_Get(w,idB);
    if(!a||!b||a->physiology.cause||b->physiology.cause)return false;
    const NeuralGenome *gA=CreatureMind_GetNeuralGenome(&a->mind);
    const NeuralGenome *gB=CreatureMind_GetNeuralGenome(&b->mind);
    if(!gA||!gB)return false;
    if(a==b)return true;
    /* Intercambio atómico: clonar y validar ambos antes de modificar ninguno. */
    NeuralGenome copyA,copyB;
    if(!NeuralGenome_Clone(&copyA,gA))return false;
    if(!NeuralGenome_Clone(&copyB,gB)){NeuralGenome_Destroy(&copyA);return false;}
    uint64_t fpA=copyA.metadata.fingerprint,fpB=copyB.metadata.fingerprint;
    bool okA=CreatureMind_AssignNeuralGenome(&a->mind,&copyB);
    bool okB=okA&&CreatureMind_AssignNeuralGenome(&b->mind,&copyA);
    if(!okB) {
        /* Restauración de seguridad ante fallo improbable. */
        CreatureMind_AssignNeuralGenome(&a->mind,&copyA);
        CreatureMind_AssignNeuralGenome(&b->mind,&copyB);
        NeuralGenome_Destroy(&copyA);NeuralGenome_Destroy(&copyB);
        return false;
    }
    a->founderDistance=computeFounderDistance(w,&copyB);
    b->founderDistance=computeFounderDistance(w,&copyA);
    recordIntervention(w,"swap",idA,idB,fpA,fpB,fpB,fpA);
    NeuralGenome_Destroy(&copyA);NeuralGenome_Destroy(&copyB);
    statistics(w);
    return true;
}

bool EcosystemWorld_ResetBrainRandom(EcosystemWorld *w,AIObjectId id) {
    if(!w||w->updating)return false;
    EcosystemEntity *e=EcosystemWorld_Get(w,id);
    if(!e||e->physiology.cause)return false;
    NeuralGenome fresh;
    uint64_t lid=w->nextLineageId++;
    if(!NeuralGenome_InitRandom(&fresh,&w->random,0.8f,w->neuralMutation.weightLimit,lid))return false;
    bool ok=EcosystemWorld_AssignBrain(w,id,&fresh,"reset_random");
    NeuralGenome_Destroy(&fresh);
    return ok;
}

bool EcosystemWorld_ExportBrain(const EcosystemWorld *w,AIObjectId id,const char *path,
                                bool overwrite,char *error,size_t errorSize) {
    if(!w)return false;
    const EcosystemEntity *e=EcosystemWorld_GetConst(w,id);
    if(!e||e->physiology.cause) {
        if(error&&errorSize)snprintf(error,errorSize,"Individuo %u no existe o no está vivo",id);
        return false;
    }
    const NeuralGenome *g=CreatureMind_GetNeuralGenome(&e->mind);
    if(!g) {
        if(error&&errorSize)snprintf(error,errorSize,"Individuo %u no posee cerebro neuronal",id);
        return false;
    }
    return NeuralBrain_SaveFile(g,path,overwrite,error,errorSize);
}

bool EcosystemWorld_ImportBrain(EcosystemWorld *w,AIObjectId id,const char *path,
                                char *error,size_t errorSize) {
    if(!w||w->updating) {
        if(error&&errorSize)snprintf(error,errorSize,"No se puede importar durante un tick activo");
        return false;
    }
    EcosystemEntity *e=EcosystemWorld_Get(w,id);
    if(!e||e->physiology.cause) {
        if(error&&errorSize)snprintf(error,errorSize,"Individuo %u no existe o no está vivo",id);
        return false;
    }
    NeuralGenome loaded;
    if(!NeuralBrain_LoadFile(path,&loaded,error,errorSize))return false;
    bool ok=EcosystemWorld_AssignBrain(w,id,&loaded,"import");
    NeuralGenome_Destroy(&loaded);
    if(!ok&&error&&errorSize)snprintf(error,errorSize,"Fallo al asignar el genoma importado");
    return ok;
}

static bool ensureDirectory(const char *dir) {
    if(!dir||!dir[0])return false;
    char tmp[512];
    snprintf(tmp,sizeof(tmp),"%s",dir);
    for(char *p=tmp+1;*p;++p) {
        if(*p=='/') {
            *p='\0';
            mkdir(tmp,0755);
            *p='/';
        }
    }
    if(mkdir(tmp,0755)!=0&&errno!=EEXIST)return false;
    return true;
}

bool EcosystemWorld_ExportArchiveBrains(const EcosystemWorld *w,const char *directory,
                                        char *error,size_t errorSize) {
    if(!w||!directory||!directory[0])return false;
    if(!ensureDirectory(directory)) {
        if(error&&errorSize)snprintf(error,errorSize,"No se pudo crear directorio '%s'",directory);
        return false;
    }
    /* Combinar registros históricos con individuos aún vivos sin alterar la simulación. */
    EcosystemArchiveEntry bestOff=w->archiveBestOffspring;
    EcosystemArchiveEntry longSurv=w->archiveLongestSurvival;
    EcosystemArchiveEntry highGen=w->archiveHighestGeneration;
    EcosystemArchiveEntry oldLineage={0};
    double oldestLineageBirth=1e30;

    for(unsigned i=0;i<w->capacity;++i) {
        const EcosystemEntity *e=&w->entities[i];
        if(!e->id||e->physiology.cause||e->mind.controllerType!=CREATURE_CONTROLLER_NEURAL)continue;
        const NeuralGenome *g=CreatureMind_GetNeuralGenome(&e->mind);
        if(!g)continue;
        if(!bestOff.valid||e->offspringCount>bestOff.offspringCount||
           (e->offspringCount==bestOff.offspringCount&&e->physiology.age>bestOff.survivalSeconds)) {
            updateArchiveEntry(&bestOff,w,e,true);
        }
        if(!longSurv.valid||e->physiology.age>longSurv.survivalSeconds) {
            updateArchiveEntry(&longSurv,w,e,true);
        }
        if(!highGen.valid||e->generation>highGen.generation||
           (e->generation==highGen.generation&&e->offspringCount>highGen.offspringCount)) {
            updateArchiveEntry(&highGen,w,e,true);
        }
        double firstSeen=w->clock.seconds;
        for(unsigned li=0;li<w->lineageCount;++li) {
            if(w->lineages[li].lineageId==g->metadata.lineageId){firstSeen=w->lineages[li].firstSeenSeconds;break;}
        }
        if(!oldLineage.valid||firstSeen<oldestLineageBirth||
           (fabs(firstSeen-oldestLineageBirth)<1e-6&&e->generation>oldLineage.generation)) {
            oldestLineageBirth=firstSeen;
            updateArchiveEntry(&oldLineage,w,e,true);
        }
    }

    const struct { const char *file; const char *criterion; const EcosystemArchiveEntry *entry; } items[]={
        {"best_offspring.brain.json","highest_viable_offspring",&bestOff},
        {"longest_survival.brain.json","longest_observed_survival",&longSurv},
        {"highest_generation.brain.json","highest_generation_reached",&highGen},
        {"oldest_surviving_lineage.brain.json","oldest_surviving_lineage",&oldLineage}
    };

    char manifestPath[512];
    snprintf(manifestPath,sizeof(manifestPath),"%s/archive_manifest.json",directory);
    FILE *mf=fopen(manifestPath,"wb");
    if(!mf) {
        if(error&&errorSize)snprintf(error,errorSize,"No se pudo crear '%s'",manifestPath);
        return false;
    }
    fprintf(mf,"{\n  \"seed\": %u,\n  \"seconds\": %.6f,\n  \"founder_mode\": \"%s\",\n  \"archives\": [\n",
            w->initialSeed,w->clock.seconds,w->founderModeName);
    bool first=true;
    for(unsigned i=0;i<4;++i) {
        if(!items[i].entry->valid)continue;
        char fullPath[512];
        snprintf(fullPath,sizeof(fullPath),"%s/%s",directory,items[i].file);
        if(!NeuralBrain_SaveFile(&items[i].entry->genome,fullPath,true,error,errorSize)){fclose(mf);return false;}
        fprintf(mf,"%s    {\"file\": \"%s\", \"criterion\": \"%s\", \"entity_id\": %u, \"species_id\": %u, "
                   "\"generation\": %u, \"offspring\": %u, \"survival_seconds\": %.6f, \"still_living\": %s, "
                   "\"lineage_id\": %llu, \"fingerprint\": \"%016llx\"}",
                first?"":",\n",items[i].file,items[i].criterion,
                items[i].entry->entityId,items[i].entry->speciesId,items[i].entry->generation,
                items[i].entry->offspringCount,items[i].entry->survivalSeconds,
                items[i].entry->stillLiving?"true":"false",
                (unsigned long long)items[i].entry->lineageId,
                (unsigned long long)items[i].entry->genome.metadata.fingerprint);
        first=false;
    }
    fprintf(mf,"\n  ]\n}\n");
    fclose(mf);
    return true;
}

bool EcosystemWorld_WriteJSON(const EcosystemWorld *w,FILE *f) {
    if(!w||!f)return false;
    const EcosystemStatistics *s=&w->statistics;
    fprintf(f,"{\n  \"seed\":%u,\"tick\":%llu,\"seconds\":%.9f,\"days\":%.9f,\n",w->initialSeed,
        (unsigned long long)w->clock.tick,w->clock.seconds,SimulationClock_Days(&w->clock));
    fprintf(f,"  \"controller\":\"%s\",\"founder_mode\":\"%s\",\"arena_width\":%.3f,\"arena_depth\":%.3f,\"obstacles\":%u,\n",
        w->defaultController==CREATURE_CONTROLLER_NEURAL?"neural":"utility",
        w->founderModeName,w->observations.halfSize*2.0f,w->observations.halfSize*2.0f,w->obstacleCount);
    fprintf(f,"  \"population\":%u,\"juveniles\":%u,\"births\":%llu,\"deaths\":%llu,\"kills\":%llu,\"attacks\":%llu,\"matings\":%llu,\n",
        w->count,s->juveniles,(unsigned long long)s->births,(unsigned long long)s->deaths,
        (unsigned long long)s->kills,(unsigned long long)s->attacks,(unsigned long long)s->matings);
    fprintf(f,"  \"food_consumed\":%.9f,\"water_consumed\":%.9f,\"plant_biomass\":%.9f,\"average_age\":%.9f,\"average_health\":%.9f,\"average_energy\":%.9f,\n",
        s->foodConsumed,s->waterConsumed,s->plantBiomass,s->averageAge,s->averageHealth,s->averageEnergy);
    fprintf(f,"  \"action_failures\":%llu,\"navigation_failures\":%llu,\"failed_interactions\":%llu,\"failed_births\":%llu,\"escapes\":%llu,\"event_hash\":\"%016llx\",\n  \"death_causes\":[",
        (unsigned long long)s->actionFailures,(unsigned long long)s->navigationFailures,
        (unsigned long long)s->failedInteractions,(unsigned long long)s->failedBirths,
        (unsigned long long)s->escapes,(unsigned long long)s->eventHash);
    for(unsigned i=0;i<DEATH_CAUSE_COUNT;++i)fprintf(f,"%s%llu",i?",":"",(unsigned long long)s->deathCauses[i]);
    fprintf(f,"],\n  \"plant_consumed\":%.9f,\"meat_consumed\":%.9f,\n",s->plantConsumed,s->meatConsumed);
    fprintf(f,"  \"maximum_generation\":%u,\"average_generation\":%.6f,\"average_survival_seconds\":%.6f,\"max_survival_seconds\":%.6f,\n",
        s->maximumGeneration,s->averageGeneration,s->averageSurvivalDuration,s->maxObservedSurvival);
    fprintf(f,"  \"neural_diversity\":%.9f,\"average_founder_distance\":%.9f,\"max_founder_distance\":%.9f,\"active_lineages\":%u,\"interventions\":%llu,\n",
        s->neuralDiversity,s->averageFounderDistance,s->maxFounderDistance,s->activeLineages,(unsigned long long)w->interventionCount);
    fprintf(f,"  \"intent_frequencies\":{");
    for(unsigned i=0;i<ACTION_INTENT_COUNT;++i)
        fprintf(f,"%s\"%s\":%llu",i?",":"",NeuralActionIntent_Name((NeuralActionIntent)i),(unsigned long long)s->intentSelections[i]);
    fprintf(f,"},\n  \"births_per_generation\":[");
    unsigned maxGenBucket=s->maximumGeneration<ECOSYSTEM_MAX_GENERATION_BUCKETS?s->maximumGeneration:(ECOSYSTEM_MAX_GENERATION_BUCKETS-1);
    for(unsigned i=0;i<=maxGenBucket;++i)
        fprintf(f,"%s%llu",i?",":"",(unsigned long long)s->birthsPerGeneration[i]);
    fprintf(f,"],\n  \"species\":[");
    for(unsigned i=0;i<w->speciesCount;++i) {
        unsigned n=0;for(unsigned j=0;j<w->capacity;++j)n+=w->entities[j].id&&w->entities[j].speciesIndex==i;
        fprintf(f,"%s{\"id\":%u,\"name\":\"%s\",\"recipe_id\":%u,\"population\":%u}",
                i?",":"",w->species[i].id,w->species[i].name,w->species[i].recipeId,n);
    }
    fprintf(f,"]\n}\n");return !ferror(f);
}

bool EcosystemWorld_WriteCSV(const EcosystemWorld *w,FILE *f,bool header) {
    if(!w||!f)return false;
    if(header) {
        fprintf(f,"tick,seconds,population,births,deaths,kills,plant_biomass,food_consumed,water_consumed,average_age,average_health,average_energy,max_generation,neural_diversity,founder_distance");
        for(unsigned i=0;i<w->speciesCount;++i)fprintf(f,",species_%u",w->species[i].id);
        fprintf(f,"\n");
    }
    const EcosystemStatistics *s=&w->statistics;
    fprintf(f,"%llu,%.6f,%u,%llu,%llu,%llu,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%u,%.6f,%.6f",
        (unsigned long long)w->clock.tick,w->clock.seconds,w->count,(unsigned long long)s->births,
        (unsigned long long)s->deaths,(unsigned long long)s->kills,s->plantBiomass,s->foodConsumed,s->waterConsumed,
        s->averageAge,s->averageHealth,s->averageEnergy,s->maximumGeneration,s->neuralDiversity,s->averageFounderDistance);
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
