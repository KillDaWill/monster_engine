/** @file test_ecosystem.c
 * @brief Invariantes ecológicas, contactos, herencia y escenarios sin render. */
#include "EcosystemScenario.h"
#include "CreatureDecision.h"
#include "CreatureActions.h"
#include "test_utils.h"
#include <math.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
static void init(EcosystemWorld *w,unsigned cap) {
    TEST_ASSERT(EcosystemWorld_Init(w,cap,16,2,137,1./60,120),"reserva de mundo");
    TEST_ASSERT(EcosystemWorld_AddSpecies(w,EcosystemSpecies_Default(1,1,ECO_PLANT,0,2)),"perfil herbívoro");
    TEST_ASSERT(EcosystemWorld_AddSpecies(w,EcosystemSpecies_Default(2,2,ECO_MEAT,1,0)),"perfil carnívoro");
}
static AIObjectId spawn(EcosystemWorld *w,unsigned species,float x,float z,double age,float energy) {
    AIObjectId id=EcosystemWorld_Spawn(w,species,Vec3_Create(x,0,z),age,energy,NULL);
    TEST_ASSERT(id,"creación de individuo");return id;
}
static void steps(EcosystemWorld *w,unsigned n) {for(unsigned i=0;i<n;++i)EcosystemWorld_Step(w);}
static void clockTests(void) {
    SimulationClock c;
    TEST_ASSERT(!SimulationClock_Init(&c,NAN,120,64),"rechaza reloj NaN");
    TEST_ASSERT(!SimulationClock_Init(&c,0,120,64),"rechaza paso nulo");
    TEST_ASSERT(SimulationClock_Init(&c,1./60,120,4),"reloj fijo");
    SimulationClock_Accumulate(&c,1);for(unsigned i=0;i<4;++i)TEST_ASSERT(SimulationClock_Pop(&c),"procesa deuda");
    TEST_ASSERT(c.behind&&c.tick==4&&c.accumulator>.9,"no descarta tiempo atrasado");
    c.paused=true;double accumulator=c.accumulator;
    SimulationClock_Accumulate(&c,10);TEST_ASSERT(!SimulationClock_Pop(&c)&&!SimulationClock_Tick(&c)&&c.tick==4&&c.accumulator==accumulator,"pausa congela reloj");
    EcosystemWorld a,b;init(&a,8);init(&b,8);spawn(&a,1,0,0,200,.6f);spawn(&b,1,0,0,200,.6f);
    for(unsigned i=0;i<300;++i)EcosystemWorld_Advance(&a,1./30);
    for(unsigned i=0;i<1440;++i)EcosystemWorld_Advance(&b,1./144);
    TEST_ASSERT(a.clock.tick==600&&b.clock.tick==600,"presentación 30 y 144 Hz conserva ticks");
    TEST_ASSERT(a.statistics.eventHash==b.statistics.eventHash&&a.entities[0].mind.random==b.entities[0].mind.random&&
        Vec3_Distance(a.entities[0].body.position,b.entities[0].body.position)==0&&a.entities[0].physiology.energy==b.entities[0].physiology.energy,"determinismo entre frecuencias");
    TEST_ASSERT(a.entities[0].mind.needs.urgency[NEED_SOCIAL]>.1f,"impulso social avanza con tiempo simulado externo");
    EcosystemEntity saved=a.entities[0];EcosystemStatistics stats=a.statistics;
    a.clock.paused=true;EcosystemWorld_Advance(&a,500);steps(&a,100);
    TEST_ASSERT(!memcmp(&saved,&a.entities[0],sizeof(saved))&&!memcmp(&stats,&a.statistics,sizeof(stats)),"pausa congela toda biología y mente");
    EcosystemWorld_Free(&a);EcosystemWorld_Free(&b);
    init(&a,8);init(&b,8);spawn(&a,1,0,0,200,.6f);spawn(&b,1,0,0,200,.6f);
    b.clock.speed=4;for(unsigned i=0;i<600;++i)EcosystemWorld_Advance(&a,1./60);
    for(unsigned i=0;i<150;++i)EcosystemWorld_Advance(&b,1./60);
    TEST_ASSERT(a.clock.tick==b.clock.tick&&Vec3_Distance(a.entities[0].body.position,b.entities[0].body.position)==0,"4x equivale a mismos pasos");
    EcosystemWorld_Free(&a);EcosystemWorld_Free(&b);
}
static void physiologyTests(void) {
    PhysiologyProfile profile=CreaturePhysiology_Default();CreaturePhysiology p=CreaturePhysiology_Create(0,.2f);
    CreaturePhysiology_Update(&p,&profile,1,2,1,false);
    TEST_ASSERT(p.energy<.2f&&p.hydration<.85f&&p.fatigue>0,"metabolismo y movimiento gastan reservas");
    float before=p.energy;CreaturePhysiology_Feed(&p,.3f);TEST_ASSERT(FLOAT_NEAR(p.energy,before+.3f),"nutrición real");
    CreaturePhysiology_Feed(&p,INFINITY);CreaturePhysiology_Feed(&p,-1);
    TEST_ASSERT(FLOAT_NEAR(p.energy,before+.3f),"sin nutrición inválida");
    p.energy=0;p.hydration=1;profile.hydrationCost=0;profile.recoveryRate=0;
    for(unsigned i=0;i<10000&&!p.cause;++i)CreaturePhysiology_Update(&p,&profile,.1f,0,1,false);
    TEST_ASSERT(p.cause==DEATH_STARVATION&&p.stage==LIFE_DEAD,"inanición progresiva mata");
    p=CreaturePhysiology_Create(200,1);p.hydration=0;profile.basalCost=0;
    for(unsigned i=0;i<10000&&!p.cause;++i)CreaturePhysiology_Update(&p,&profile,.1f,0,1,false);
    TEST_ASSERT(p.cause==DEATH_DEHYDRATION,"deshidratación mata");
    p=CreaturePhysiology_Create(profile.maximumAge,1);CreaturePhysiology_Update(&p,&profile,.1f,0,1,false);
    TEST_ASSERT(p.cause==DEATH_AGE,"envejecimiento cronológico");
    p=CreaturePhysiology_Create(0,1);for(unsigned i=0;i<1000;++i)CreaturePhysiology_Update(&p,&profile,.001f,0,1,false);
    TEST_ASSERT(!p.cause&&p.health==1&&p.energy>=0&&p.energy<=1,"animal sano sin muerte artificial");
}
static void contact(EcosystemEntity *e,AIObjectId food,bool water) {
    CreatureMind *m=&e->mind;
    m->target=food;m->action=water?ACTION_DRINK:ACTION_EAT;
    m->intakeNeed=water?NEED_HYDRATION:NEED_HUNGER;m->intakeRequested=1;m->contactTime=.3f;
    m->interactionPhase=INTERACTION_CONSUME;m->interaction=(InteractionTarget){.objectId=food,.valid=true,
        .normal={0,1,0},.approach={0,-1,0},.positionTolerance=.09f,.orientationTolerance=1.4f};
    e->body.mouth=Vec3_Create(0,water?0:.04f,0);e->body.mouthDirection=Vec3_Create(0,-1,0);
    e->body.reach=(ReachResult){.objectId=food,.status=REACH_REACHED};e->body.speed=0;
}
static void economyTests(void) {
    EcosystemWorld w;init(&w,8);
    AIObjectId a=spawn(&w,1,-.5f,0,200,.2f),b=spawn(&w,1,.5f,0,200,.2f);
    AIObjectId food=EcosystemWorld_AddResource(&w,(EcosystemResource){.category=ECO_PLANT,.biomass=.1f,
        .maximumBiomass=1,.regenerationRate=.1f,.nutritionalValue=.5f,.accessible=true},Vec3_Zero(),1);
    TEST_ASSERT(food,"planta registrada");
    EcosystemEntity *ea=EcosystemWorld_Get(&w,a),*eb=EcosystemWorld_Get(&w,b);
    contact(ea,food,false);contact(eb,food,false);
    float actual=EcosystemWorld_Consume(&w,a,food,1)+EcosystemWorld_Consume(&w,b,food,1);
    TEST_ASSERT(FLOAT_NEAR(actual,.1f)&&FLOAT_NEAR(ea->physiology.energy+eb->physiology.energy,.45f),"dos bocas no crean biomasa ni energía");
    TEST_ASSERT(EcosystemWorld_Resource(&w,food)->biomass==0&&EcosystemWorld_Consume(&w,b,food,1)==0,"depleción sin nutrición");
    EcosystemResource_Update(EcosystemWorld_Resource(&w,food),1000);
    TEST_ASSERT(EcosystemWorld_Resource(&w,food)->biomass==1,"regeneración acotada");
    contact(eb,food,false);eb->body.mouth.y=8;
    TEST_ASSERT(EcosystemWorld_Consume(&w,b,food,1)==0,"contacto 3D imposible no consume");
    contact(eb,food,false);eb->body.reach.status=REACH_UNREACHABLE;
    TEST_ASSERT(EcosystemWorld_Consume(&w,b,food,1)==0,"fallo corporal no consume");
    contact(eb,food,false);eb->mind.contactTime=.1f;
    TEST_ASSERT(EcosystemWorld_Consume(&w,b,food,1)==0,"exige contacto sostenido");
    TEST_ASSERT(EcosystemWorld_RemoveResource(&w,food),"retira recurso");
    steps(&w,1);TEST_ASSERT(eb->mind.target!=food&&!AIWorld_Get(&w.observations,food),"objetivo retirado invalidado");
    AIObjectId water=EcosystemWorld_AddResource(&w,(EcosystemResource){.category=ECO_WATER,.infinite=true,.accessible=false,.nutritionalValue=1},Vec3_Zero(),1);
    contact(ea,water,true);TEST_ASSERT(EcosystemWorld_Consume(&w,a,water,1)==0,"agua inaccesible no hidrata");
    EcosystemResource *r=EcosystemWorld_Resource(&w,water);r->accessible=true;ea->physiology.hydration=.2f;
    /* La disponibilidad física y observada se publican en el siguiente tick. */
    EcosystemWorld_RemoveResource(&w,water);
    water=EcosystemWorld_AddResource(&w,(EcosystemResource){.category=ECO_WATER,.infinite=true,.accessible=true,.nutritionalValue=1},Vec3_Zero(),1);
    contact(ea,water,true);TEST_ASSERT(EcosystemWorld_Consume(&w,a,water,.2f)>.19f,"agua explícitamente infinita transfiere");
    CreatureMind_AssignProfile(&ea->mind,&ea->brain);
    TEST_ASSERT(ea->mind.externalPhysiology&&ea->mind.transactionalIntake,"reasignar cerebro conserva contratos ecológicos");
    EcosystemWorld_Free(&w);
}
static void lifecycleTests(void) {
    EcosystemWorld w;init(&w,2);AIObjectId last=0;
    for(unsigned i=0;i<1000;++i) {
        AIObjectId id=spawn(&w,1,0,0,200,.7f);TEST_ASSERT(id>last,"ID no se reutiliza");
        TEST_ASSERT(EcosystemWorld_RequestDeath(&w,id,DEATH_REMOVED),"muerte solicitada");
        TEST_ASSERT(!EcosystemWorld_RequestDeath(&w,id,DEATH_REMOVED),"muerte idempotente");
        steps(&w,1);TEST_ASSERT(!EcosystemWorld_Get(&w,id)&&w.count==0,"retiro al commit");last=id;
    }
    spawn(&w,1,-1,0,200,.7f);spawn(&w,1,1,0,200,.7f);
    TEST_ASSERT(!EcosystemWorld_Spawn(&w,1,Vec3_Create(3,0,0),200,.7f,NULL)&&w.count==2,"capacidad falla sin corrupción");
    TEST_ASSERT(!EcosystemWorld_Spawn(&w,1,Vec3_Create(NAN,0,0),200,.7f,NULL),"posición no finita rechazada");
    EcosystemWorld_Free(&w);
    TEST_ASSERT(!EcosystemWorld_Init(&w,UINT_MAX,1,1,1,.01,120),"capacidad extrema rechazada");
    AIWorld world;TEST_ASSERT(AIWorld_InitCapacity(&world,100,400),"registro mayor que 128");
    for(unsigned i=0;i<300;++i)TEST_ASSERT(AIWorld_Add(&world,(AIWorldObject){.type=AI_CREATURE,.walkable=true,.visible=true}),"300 entidades");
    unsigned revision=world.revision;
    AIWorld_Add(&world,(AIWorldObject){.type=AI_FOOD,.walkable=true});
    TEST_ASSERT(world.revision==revision,"planta y animal no invalidan navegación estática");
    AIWorld_Destroy(&world);
}
static void avoidanceTests(void) {
    EcosystemWorld w;init(&w,4);
    AIObjectId ids[]={spawn(&w,1,-2,0,200,.9f),spawn(&w,1,2,0,200,.9f)};
    for(unsigned i=0;i<2;++i) {
        EcosystemEntity *e=EcosystemWorld_Get(&w,ids[i]);
        for(unsigned j=0;j<ACTION_COUNT;++j)e->brain.weights[j]=0;
        e->brain.weights[ACTION_WANDER]=1;
        e->brain.threatMask=0;
        e->mind.action=ACTION_WANDER;e->mind.nextDecision=1e9;
        e->body.yaw=i?-1.5707963f:1.5707963f;
        e->mind.destination=Vec3_Create(i?-2:2,0,0);e->mind.destinationValid=true;
        TEST_ASSERT(CreatureNavigation_Plan(&e->mind.navigation,&w.observations,e->body.position,
            e->mind.destination,e->body.capabilities.clearance,false),"trayecto de cruce");
    }
    bool crossed=false;
    for(unsigned i=0;i<900&&!crossed;++i) {
        steps(&w,1);EcosystemEntity *a=EcosystemWorld_Get(&w,ids[0]),*b=EcosystemWorld_Get(&w,ids[1]);
        TEST_ASSERT(Vec3_Distance(a->body.position,b->body.position)>=.45f,"discos sin solapamiento durante cruce");
        crossed=a->body.position.x>.5f&&b->body.position.x<-.5f;
        float segment=a->body.capabilities.maximumOralReach*.5f;
        TEST_ASSERT(fabsf(Vec3_Distance(a->oralPivot,a->oralElbow)-segment)<1e-4f&&
            fabsf(Vec3_Distance(a->oralElbow,a->body.mouth)-segment)<1e-4f,"cadena oral conserva ambas longitudes");
    }
    TEST_ASSERT(crossed,"evitación lateral permite cruzar sin quedarse bloqueados");
    EcosystemWorld_Free(&w);
}
static void predationTests(void) {
    EcosystemWorld w;init(&w,4);
    AIObjectId predator=spawn(&w,2,0,0,200,.5f),prey=spawn(&w,1,0,2,200,.8f);
    EcosystemEntity *a=EcosystemWorld_Get(&w,predator),*b=EcosystemWorld_Get(&w,prey);
    a->mind.action=ACTION_ATTACK;a->mind.target=prey;a->body.yaw=0;
    TEST_ASSERT(!EcosystemWorld_Attack(&w,predator,prey),"no ataque fuera de alcance");
    b->body.position.z=.6f;a->body.yaw=3.14f;
    TEST_ASSERT(!EcosystemWorld_Attack(&w,predator,prey),"no mordisco hacia atrás");a->body.yaw=0;
    TEST_ASSERT(EcosystemWorld_Attack(&w,predator,prey)&&FLOAT_NEAR(b->physiology.health,.64f),"daño validado");
    TEST_ASSERT(!EcosystemWorld_Attack(&w,predator,prey),"cooldown impide doble ataque");
    a->nextAttack=0;TEST_ASSERT(EcosystemWorld_Attack(&w,predator,prey),"presa sobrevive segundo mordisco");
    a->nextAttack=0;TEST_ASSERT(EcosystemWorld_Attack(&w,predator,prey)&&b->physiology.cause==DEATH_PREDATION,"muerte por salud cero");
    a->nextAttack=0;TEST_ASSERT(!EcosystemWorld_Attack(&w,predator,prey),"no matar dos veces");
    steps(&w,1);TEST_ASSERT(!EcosystemWorld_Get(&w,prey)&&w.statistics.kills==1,"commit de muerte");
    AIObjectId carcass=0;for(unsigned i=0;i<w.resourceCapacity;++i)if(w.resources[i].category==ECO_MEAT)carcass=w.resources[i].id;
    TEST_ASSERT(carcass,"cadáver comestible generado");
    contact(a,carcass,false);const AIWorldObject *o=AIWorld_Get(&w.observations,carcass);
    a->body.mouth=Vec3_Add(o->position,Vec3_Create(0,.04f,0));
    TEST_ASSERT(EcosystemWorld_Consume(&w,predator,carcass,.1f)>0,"carnívoro consume cadáver real");
    EcosystemWorld_Free(&w);
    init(&w,4);
    /* Diferencial de capacidad explícito para forzar captura, además de huida. */
    w.species[1].speed=3.2f;
    predator=spawn(&w,2,0,-3,200,.3f);prey=spawn(&w,1,0,0,200,.8f);
    a=EcosystemWorld_Get(&w,predator);b=EcosystemWorld_Get(&w,prey);
    a->body.yaw=0;b->body.yaw=3.14f;
    bool chased=false,fled=false;float initial=3;
    for(unsigned i=0;i<1800&&EcosystemWorld_Get(&w,prey);++i) {
        steps(&w,1);chased|=a->mind.action==ACTION_CHASE;fled|=b->mind.action==ACTION_ESCAPE;
    }
    TEST_ASSERT(chased&&fled,"descubrimiento, persecución y escape autónomos");
    TEST_ASSERT(Vec3_Distance(a->body.position,Vec3_Create(0,0,-3))>initial,"persecución desplaza cuerpo");
    TEST_ASSERT(w.statistics.kills>0,"caza autónoma produce muerte");
    EcosystemWorld_Free(&w);
    init(&w,4);predator=spawn(&w,2,0,-3,200,.3f);prey=spawn(&w,1,0,0,200,.8f);
    a=EcosystemWorld_Get(&w,predator);a->body.yaw=0;
    AIWorld_Add(&w.observations,(AIWorldObject){.type=AI_OBSTACLE,.position={0,0,0},.radius=1.4f,.height=2});
    bool abandoned=false;
    for(unsigned i=0;i<600;++i) {
        steps(&w,1);const PerceptionEntry *entry=CreaturePerception_Find(&a->mind.perception,prey);
        if(entry&&entry->unreachableUntil>a->mind.time){abandoned=true;break;}
    }
    TEST_ASSERT(abandoned&&w.statistics.attacks==0,"presa encerrada se abandona con cooldown y sin daño");
    EcosystemWorld_Free(&w);
}
static void reproductionSafetyTests(void) {
    EcosystemWorld w;init(&w,2);
    AIObjectId one=spawn(&w,1,0,0,200,.95f),two=spawn(&w,1,0,.65f,200,.95f);
    EcosystemEntity *a=EcosystemWorld_Get(&w,one),*b=EcosystemWorld_Get(&w,two);
    a->body.yaw=0;b->body.yaw=3.14159265f;
    b->brain.weights[ACTION_APPROACH_MATE]=0;
    steps(&w,120);
    TEST_ASSERT(w.statistics.matings==0,"una petición unilateral no fecunda");
    b->brain.weights[ACTION_APPROACH_MATE]=.6f;
    Vector3 toward=Vec3_Sub(a->body.position,b->body.position);
    b->body.yaw=atan2f(toward.x,toward.z);b->mind.nextDecision=0;b->mind.perception.nextRefresh=0;

    bool paired=false;
    for(unsigned i=0;i<600&&!paired;++i){steps(&w,1);paired=w.statistics.matings>0;}
    TEST_ASSERT(paired,"aceptación independiente de ambos participantes");
    EcosystemEntity *mother=a->reproduction.gestating?a:b,*father=mother==a?b:a;
    double due=mother->reproduction.due;
    w.clock.paused=true;steps(&w,1000);
    TEST_ASSERT(mother->reproduction.due==due&&w.statistics.births==0,"pausa congela gestación");
    w.clock.paused=false;mother->reproduction.due=w.clock.seconds+.05;
    steps(&w,4);TEST_ASSERT(w.statistics.births==0&&w.statistics.failedBirths==1&&w.count==2,"nacimiento con capacidad llena se rechaza limpiamente");
    mother->reproduction.gestating=true;mother->reproduction.due=w.clock.seconds+.1;
    AIObjectId fatherId=father->id,motherId=mother->id;
    TEST_ASSERT(EcosystemWorld_RequestDeath(&w,fatherId,DEATH_REMOVED),"retira progenitor fecundante");
    steps(&w,8);TEST_ASSERT(w.statistics.births==1&&w.count==2&&!EcosystemWorld_Get(&w,fatherId),"gestación conserva herencia si muere el fecundante");
    bool child=false;
    for(unsigned i=0;i<w.capacity;++i)if(w.entities[i].id!=motherId&&w.entities[i].id) {
        child=true;TEST_ASSERT(w.entities[i].reproduction.parents[0]==motherId&&w.entities[i].reproduction.parents[1]==fatherId,"ascendencia persiste tras muerte");
    }
    TEST_ASSERT(child,"hijo registrado tras commit");
    mother=EcosystemWorld_Get(&w,motherId);mother->reproduction.gestating=true;mother->reproduction.due=w.clock.seconds+.05;
    EcosystemWorld_RequestDeath(&w,motherId,DEATH_REMOVED);steps(&w,8);
    TEST_ASSERT(w.statistics.births==1,"muerte gestante cancela gestación");
    EcosystemWorld_Free(&w);
}
static void reproductionTests(void) {
    ReproductionProfile profile=CreatureReproduction_Default();CreatureReproduction r={0};
    CreaturePhysiology p=CreaturePhysiology_Create(0,.9f);
    TEST_ASSERT(!CreatureReproduction_Ready(&r,&profile,&p,0),"juvenil no reproduce");
    p.stage=LIFE_ADULT;p.age=200;p.health=.2f;
    TEST_ASSERT(!CreatureReproduction_Ready(&r,&profile,&p,0),"enfermo no reproduce");
    p.health=1;p.energy=.2f;TEST_ASSERT(!CreatureReproduction_Ready(&r,&profile,&p,0),"energía insuficiente");
    p.energy=1;r.cooldownUntil=10;TEST_ASSERT(!CreatureReproduction_Ready(&r,&profile,&p,0),"cooldown reproductivo");
    CreatureGenome a=CreatureGenome_Default(),b=a;b.size=1.4f;b.speed=.7f;uint32_t seed=42;
    for(unsigned i=0;i<10000;++i) {
        CreatureGenome child=CreatureGenome_Inherit(&a,&b,&seed,1,1);
        TEST_ASSERT(CreatureGenome_Validate(&child),"mutación permanece acotada");
    }
    EcosystemWorld w;init(&w,8);AIObjectId mother=spawn(&w,1,0,0,200,.9f),father=spawn(&w,1,0,1,200,.9f);
    EcosystemEntity *m=EcosystemWorld_Get(&w,mother),*f=EcosystemWorld_Get(&w,father);
    m->reproduction.role=0;f->reproduction.role=1;
    /* Condiciones iniciales: ambos observan la misma identidad y se acercan por utilidad. */
    AIWorld_Remove(&w.observations,mother); /* No simula muerte; este caso comprueba invalidación. */
    m->mind.target=father;f->mind.target=mother;steps(&w,1);
    TEST_ASSERT(f->mind.target!=mother,"pareja retirada no se usa");
    EcosystemWorld_Free(&w);
    EcosystemConfig c=EcosystemConfig_Default();EcosystemConfig_Scenario(&c,"B");c.capacity=48;
    TEST_ASSERT(EcosystemScenario_Create(&w,&c),"escenario de reproducción");
    steps(&w,1800);TEST_ASSERT(w.statistics.matings>0&&w.statistics.births==0,"handshake autónomo antes de terminar gestación");
    steps(&w,10800);TEST_ASSERT(w.statistics.births>0,"gestación produce descendencia autónoma");
    bool adultChild=false;
    for(unsigned i=0;i<w.capacity;++i)if(w.entities[i].id&&w.entities[i].reproduction.parents[0]) {
        EcosystemEntity *e=&w.entities[i];
        TEST_ASSERT(e->id!=e->reproduction.parents[0]&&e->id!=e->reproduction.parents[1]&&e->mind.self==e->id,"hijo con identidad y mente propias");
        TEST_ASSERT(CreatureGenome_Validate(&e->genome),"genoma heredado válido");
        adultChild|=e->physiology.stage==LIFE_ADULT;
    }
    TEST_ASSERT(adultChild,"hijos crecen hasta madurez");EcosystemWorld_Free(&w);
}
void run_ecosystem_tests(void) {
    clockTests();physiologyTests();economyTests();lifecycleTests();avoidanceTests();predationTests();reproductionSafetyTests();reproductionTests();
    puts("[PASS] ecosystem: reloj, economía, fisiología, IDs, caza y reproducción");
}
