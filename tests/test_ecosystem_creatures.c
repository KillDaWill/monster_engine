/** @file test_ecosystem_creatures.c
 * @brief Pruebas del adaptador visual compartido del ecosistema.
 */
#include "EcosystemVisual.h"
#include "EcosystemScenario.h"
#include "EcosystemAnatomy.h"
#include "CreatureRecipes.h"
#include "Creature.h"
#include "test_utils.h"
#include <sched.h>
#include <time.h>
#include <math.h>
#include <stdlib.h>

static void Scenario(EcosystemWorld* world, unsigned herbivores, unsigned carnivores, unsigned capacity) {
    EcosystemConfig config = EcosystemConfig_Default();
    config.herbivores = herbivores;
    config.carnivores = carnivores;
    config.capacity = capacity;
    TEST_ASSERT(EcosystemScenario_Create(world, &config), "crear escenario reproducible");
}

static EcosystemVisualCacheKey KeyFor(const EcosystemEntity* entity, const EcosystemWorld* world, VisualTier tier) {
    CreatureVisualProfile profile = VisualVariation_Profile(&entity->genome, entity->seed);
    uint32_t stage = !entity->anatomicalBody && entity->physiology.stage == LIFE_JUVENILE ? CREATURE_STAGE_JUVENILE : CREATURE_STAGE_ADULT;
    return (EcosystemVisualCacheKey){.recipeId = world->species[entity->speciesIndex].recipeId,
        .stage = stage, .morphologyBucket = profile.bucket, .tier = tier};
}

static void Lifecycle(void) {
    EcosystemWorld world;
    Scenario(&world, 2, 1, 8);
    EcosystemVisual visual = {0};
    TEST_ASSERT(EcosystemVisual_Init(&visual, &world), "inicializar visual ecológico");
    TEST_ASSERT(EcosystemVisual_SlotCount(&visual) == world.count && EcosystemVisual_Validate(&visual), "sincronización inicial válida");
    for (unsigned i = 0; i < world.capacity; ++i) if (world.entities[i].id)
        EcosystemWorld_RequestDeath(&world, world.entities[i].id, DEATH_REMOVED);
    EcosystemWorld_Step(&world);
    TEST_ASSERT(EcosystemVisual_Sync(&visual) && EcosystemVisual_SlotCount(&visual) == 0 && EcosystemVisual_Validate(&visual),
        "muerte retira todas las ranuras");
    EcosystemWorld_Free(&world);
    Scenario(&world, 2, 1, 8);
    TEST_ASSERT(EcosystemVisual_BindWorld(&visual, &world) && EcosystemVisual_Validate(&visual), "reset no conserva visuales huérfanos");
    EcosystemVisual_Shutdown(&visual);
    EcosystemWorld_Free(&world);
}

static void CacheIdentity(void) {
    EcosystemWorld world;
    Scenario(&world, 1, 0, 4);
    EcosystemVisual visual = {0};
    TEST_ASSERT(EcosystemVisual_Init(&visual, &world), "inicializar caché");
    const EcosystemEntity* entity = NULL;
    for (unsigned i = 0; i < world.capacity; ++i) if (world.entities[i].id) { entity = &world.entities[i]; break; }
    TEST_ASSERT(entity != NULL, "entidad para caché");
    EcosystemVisualCacheKey key = KeyFor(entity, &world, VISUAL_TIER_FAR);
    EcosystemVisual_PrewarmVisible(&visual);
    const Mesh* first = EcosystemVisual_RequestMesh(&visual, key);
    /* El trabajador necesita tiempo real de CPU; contar sched_yield no fija
     * ningún plazo y fallaba según la carga concurrente de la máquina. */
    for (unsigned i = 0; i < 10000 && first == EcosystemVisual_FallbackMesh(&visual); ++i) {
        EcosystemVisual_Update(&visual, .1f);
        struct timespec pause={0,1000000};nanosleep(&pause,NULL);
        first = EcosystemVisual_RequestMesh(&visual, key);
    }
    const Mesh* second = EcosystemVisual_RequestMesh(&visual, key);
    TEST_ASSERT(first != EcosystemVisual_FallbackMesh(&visual) && first == second, "una clave devuelve una única malla compartida");
    TEST_ASSERT(EcosystemVisual_CacheHitCount(&visual) > 0, "hit de caché observable");
    EcosystemWorld_Step(&world);EcosystemVisual_Update(&visual,1.f/60);
    EcosystemVisualDrawPacket packet;
    TEST_ASSERT(EcosystemVisual_GetDrawPackets(&visual,&packet,1)==1&&packet.ready,"pose visible publicada");
    Mesh deformed=Mesh_Create();
    TEST_ASSERT(EcosystemVisual_DeformBody(&packet,&deformed),"la ruta CPU de pose y crecimiento funciona sin GL");
    for(size_t vertex=0;vertex<deformed.vertexCount;++vertex) {
        Vector3 p=deformed.vertices[vertex].position,n=deformed.vertices[vertex].normal;
        TEST_ASSERT(isfinite(Vec3_LengthSq(p))&&isfinite(Vec3_LengthSq(n)),"deformación CPU sin NaN ni infinito");
    }
    TEST_ASSERT(packet.footError<.03f,"la pose inicial apoya los pies con residuo acotado");
    float adultHeight=0;
    for(size_t vertex=0;vertex<deformed.vertexCount;++vertex)
        adultHeight=fmaxf(adultHeight,deformed.vertices[vertex].position.y*packet.scale);
    world.entities[0].physiology.age=0;
    EcosystemWorld_Step(&world);EcosystemVisual_Update(&visual,1.f/60);
    EcosystemVisual_GetDrawPackets(&visual,&packet,1);
    TEST_ASSERT(packet.bodyMesh==first&&EcosystemVisual_DeformBody(&packet,&deformed),
        "el neonato reutiliza la misma malla adulta con crecimiento anatómico");
    float newbornHeight=0;
    for(size_t vertex=0;vertex<deformed.vertexCount;++vertex)
        newbornHeight=fmaxf(newbornHeight,deformed.vertices[vertex].position.y*packet.scale);
    TEST_ASSERT(newbornHeight<adultHeight*.85f,"el pivote auxiliar del cuello no conserva el tamaño adulto");
    TEST_ASSERT(packet.stanceFootError<.03f,"la cría apoya las patas sin estirarlas fuera del rig");
    Mesh_Free(&deformed);
    EcosystemVisual_Shutdown(&visual);
    EcosystemWorld_Free(&world);
}

static void LodPolicy(void) {
    EcosystemWorld small;
    Scenario(&small, 1, 0, 4);
    EcosystemVisual smallVisual = {0};
    TEST_ASSERT(EcosystemVisual_Init(&smallVisual, &small), "inicializar LOD pequeño");
    AIObjectId smallId = 0;
    for (unsigned i = 0; i < small.capacity; ++i) if (small.entities[i].id) { smallId = small.entities[i].id; break; }
    TEST_ASSERT(EcosystemVisual_SetProjection(&smallVisual, smallId, (EcosystemVisualProjection){130.0f, true}), "proyección media");
    EcosystemVisual_Update(&smallVisual, .1f);
    EcosystemVisualDrawPacket smallPacket;
    EcosystemVisual_GetDrawPackets(&smallVisual, &smallPacket, 1);
    TEST_ASSERT(smallPacket.tier == VISUAL_TIER_MID, "umbral FAR a MID");
    EcosystemVisual_SetProjection(&smallVisual, smallId, (EcosystemVisualProjection){100.0f, true});
    EcosystemVisual_Update(&smallVisual, .1f);
    EcosystemVisual_GetDrawPackets(&smallVisual, &smallPacket, 1);
    TEST_ASSERT(smallPacket.tier == VISUAL_TIER_MID, "histéresis MID conserva nivel");
    EcosystemVisual_SetProjection(&smallVisual, smallId, (EcosystemVisualProjection){90.0f, true});
    EcosystemVisual_Update(&smallVisual, .1f);
    EcosystemVisual_GetDrawPackets(&smallVisual, &smallPacket, 1);
    TEST_ASSERT(smallPacket.tier == VISUAL_TIER_FAR, "umbral MID a FAR");
    EcosystemVisual_Shutdown(&smallVisual);
    EcosystemWorld_Free(&small);

    EcosystemWorld world;
    Scenario(&world, 65, 0, 72);
    EcosystemVisual visual = {0};
    TEST_ASSERT(EcosystemVisual_Init(&visual, &world), "inicializar población grande");
    for (unsigned i = 0; i < world.capacity; ++i) if (world.entities[i].id)
        TEST_ASSERT(EcosystemVisual_SetProjection(&visual, world.entities[i].id,
            (EcosystemVisualProjection){.projectedPixels = 400.0f, .visible = true}), "proyección de cámara");
    EcosystemVisual_Update(&visual, .1f);
    EcosystemVisualDrawPacket packets[ECOSYSTEM_VISUAL_MAX_SLOTS];
    size_t count = EcosystemVisual_GetDrawPackets(&visual, packets, ECOSYSTEM_VISUAL_MAX_SLOTS);
    size_t mid = 0, near = 0;
    for (size_t i = 0; i < count; ++i) { mid += packets[i].tier == VISUAL_TIER_MID; near += packets[i].tier == VISUAL_TIER_NEAR; }
    TEST_ASSERT(count > 64 && mid <= 8 && near == 0, "política N>64 limita MID y desactiva NEAR automático");
    TEST_ASSERT(EcosystemVisual_RequestTier(&visual, packets[0].entityId, VISUAL_TIER_NEAR), "selección explícita cercana");
    EcosystemVisual_Update(&visual, .1f);
    count = EcosystemVisual_GetDrawPackets(&visual, packets, ECOSYSTEM_VISUAL_MAX_SLOTS);
    near = 0;
    for (size_t i = 0; i < count; ++i) near += packets[i].tier == VISUAL_TIER_NEAR;
    TEST_ASSERT(near == 1, "selección explícita permite NEAR");
    EcosystemVisual_Shutdown(&visual);
    EcosystemWorld_Free(&world);
}

static void Determinism(void) {
    EcosystemWorld a, b;
    Scenario(&a, 8, 2, 32);
    Scenario(&b, 8, 2, 32);
    TEST_ASSERT(a.count == b.count, "dos escenarios tienen la misma población");
    for (unsigned i = 0; i < a.capacity; ++i) if (a.entities[i].id) {
        EcosystemVisualCacheKey ka = KeyFor(&a.entities[i], &a, VISUAL_TIER_FAR);
        EcosystemVisualCacheKey kb = KeyFor(&b.entities[i], &b, VISUAL_TIER_FAR);
        TEST_ASSERT(EcosystemVisual_CacheKeyHash(ka) == EcosystemVisual_CacheKeyHash(kb), "semilla igual produce clave igual");
    }
    EcosystemVisual visual={0};
    TEST_ASSERT(EcosystemVisual_Init(&visual,&a),"presentación sólo en uno de los mundos equivalentes");
    for(unsigned step=0;step<120;++step) {
        EcosystemWorld_Step(&a);EcosystemWorld_Step(&b);EcosystemVisual_Update(&visual,1.f/60);
    }
    TEST_ASSERT(a.random==b.random&&a.statistics.eventHash==b.statistics.eventHash,
        "la presentación no consume PRNG ni altera eventos ecológicos");
    for(unsigned i=0;i<a.capacity;++i)if(a.entities[i].id) {
        TEST_ASSERT(a.entities[i].mind.random==b.entities[i].mind.random&&
            Vec3_Distance(a.entities[i].body.position,b.entities[i].body.position)<1e-6f&&
            Vec3_Distance(a.entities[i].body.mouth,b.entities[i].body.mouth)<1e-6f,
            "movimiento y boca idénticos con y sin presentación");
    }
    EcosystemVisual_Shutdown(&visual);
    EcosystemWorld_Free(&a);
    EcosystemWorld_Free(&b);
}

static void AnatomicalContactAndGrowth(void) {
    for(unsigned species=0;species<2;++species) {
        EcosystemWorld world;
        Scenario(&world,species?0:1,species?1:0,4);
        EcosystemEntity* e=&world.entities[0];
        TEST_ASSERT(e->anatomicalBody,"el escenario tiene cuerpo oral anatómico sin GL");
        Vector3 goal=e->body.mouth;goal.y=.04f;
        Vector3 direction=Vec3_Normalize(Vec3_Create(sinf(e->body.yaw),-1,cosf(e->body.yaw)));
        e->mind.intent.interact=true;
        e->mind.intent.interaction=(InteractionTarget){.valid=true,.objectId=73,.type=INTERACTION_EAT,
            .position=goal,.normal={0,1,0},.approach=direction,.positionTolerance=.09f,.orientationTolerance=1.4f};
        for(unsigned step=0;step<360;++step)EcosystemAnatomy_Update(e,1.f/60);
        if(e->body.reach.status!=REACH_REACHED)fprintf(stderr,"contacto receta %u error=%f altura=%f crouch=%f\n",species+3,e->body.reach.error,e->body.mouth.y,e->anatomicalBody->crouch);
        TEST_ASSERT(e->body.reach.status==REACH_REACHED&&Vec3_Distance(e->body.mouth,goal)<=.09f,
            "la boca anatómica alcanza comida al nivel del suelo");
        e->mind.intent.interaction.position.y=100;
        for(unsigned step=0;step<120;++step)EcosystemAnatomy_Update(e,1.f/60);
        TEST_ASSERT(e->body.reach.status!=REACH_REACHED,"ningún objetivo imposible declara contacto");
        const EcosystemSpecies* profile=&world.species[e->speciesIndex];
        size_t joints=e->anatomicalBody->rig.skeleton.jointCount;
        for(unsigned stage=0;stage<=4;++stage) {
            e->physiology.age=profile->physiology.maturityAge*(double)stage/4;
            e->physiology.stage=stage<4?LIFE_JUVENILE:LIFE_ADULT;
            TEST_ASSERT(EcosystemAnatomy_Configure(e,profile),"crecimiento compila un cuerpo válido");
            const EcosystemAnatomy* body=e->anatomicalBody;
            TEST_ASSERT(body->rig.skeleton.jointCount==joints&&Skeleton_Validate(&body->rig.skeleton),
                "neonato, juvenil y adulto conservan topología de rig");
            TEST_ASSERT(body->rig.limbCount==4,"el neonato nace con cuatro miembros completos");
            for(size_t leg=0;leg<body->rig.limbCount;++leg) {
                int foot=body->rig.limbs[leg].endEffectorJoint;
                Vector3 p=body->pose.joints[foot].worldPosition;
                TEST_ASSERT(isfinite(p.x)&&isfinite(p.y)&&isfinite(p.z),"pies finitos durante todo el desarrollo");
            }
        }
        EcosystemWorld_Free(&world);
    }
}

static void NewbornClearance(void) {
    EcosystemWorld world;
    TEST_ASSERT(EcosystemWorld_Init(&world,4,4,1,42,1./60,120),"mundo para huellas de crecimiento");
    EcosystemSpecies species=EcosystemSpecies_Default(1,1,ECO_PLANT,0,0);species.radius=.95f;
    TEST_ASSERT(EcosystemWorld_AddSpecies(&world,species),"perfil de colisión adulta");
    TEST_ASSERT(EcosystemWorld_Spawn(&world,1,Vec3_Zero(),200,.8f,NULL),"progenitor inicial");
    TEST_ASSERT(!EcosystemWorld_Spawn(&world,1,Vec3_Create(1.5f,0,0),200,.8f,NULL),"otro adulto no cabe atravesando su tronco");
    AIObjectId child=EcosystemWorld_Spawn(&world,1,Vec3_Create(1.5f,0,0),0,.8f,NULL);
    TEST_ASSERT(child&&EcosystemWorld_Get(&world,child)->body.capabilities.clearance<.6f,
        "la cría cabe con su radio real en lugar del radio adulto");
    EcosystemWorld_Free(&world);
}

static size_t ComponentRoot(size_t* parent,size_t index) {
    while(parent[index]!=index) {parent[index]=parent[parent[index]];index=parent[index];}
    return index;
}

static void AppendageMeshContinuity(void) {
    EcosystemWorld world;Scenario(&world,0,1,4);
    EcosystemVisual visual={0};
    TEST_ASSERT(EcosystemVisual_Init(&visual,&world),"visual para continuidad caudal");
    const EcosystemEntity* entity=&world.entities[0];
    for(unsigned bucket=0;bucket<VISUAL_MORPHOLOGY_BUCKETS;++bucket)
    for(unsigned tier=VISUAL_TIER_NEAR;tier<VISUAL_TIER_COUNT;++tier) {
        EcosystemVisualCacheKey key=KeyFor(entity,&world,(VisualTier)tier);
        key.morphologyBucket=bucket;
        const Mesh* mesh=EcosystemVisual_RequestMesh(&visual,key);
        for(unsigned attempt=0;attempt<10000&&mesh==EcosystemVisual_FallbackMesh(&visual);++attempt) {
            EcosystemVisual_Update(&visual,0);
            struct timespec pause={0,1000000};nanosleep(&pause,NULL);
            mesh=EcosystemVisual_RequestMesh(&visual,key);
        }
        TEST_ASSERT(mesh!=EcosystemVisual_FallbackMesh(&visual),"cola mallada en cada LOD");
        size_t* parent=malloc(mesh->vertexCount*sizeof(*parent));
        TEST_ASSERT(parent,"componentes de la cola sin borrar geometría");
        for(size_t i=0;i<mesh->vertexCount;++i)parent[i]=i;
        for(size_t i=0;i+2<mesh->indexCount;i+=3)for(unsigned edge=1;edge<3;++edge)
            parent[ComponentRoot(parent,mesh->indices[i+edge])]=ComponentRoot(parent,mesh->indices[i]);
        size_t root=SIZE_MAX,stations=0;
        CreatureRecipe recipe;CreaturePhenotype phenotype;
        const CreatureRecipe* base=CreatureRecipes_Velkar();
        TEST_ASSERT(VisualVariation_ApplyBucket(base,&base->adult,bucket,0,&recipe,&phenotype),"cubeta felina canónica");
        Monster monster=Monster_Create();
        TEST_ASSERT(Creature_BuildMonster(&monster,&recipe,&phenotype),"grafo de la plantilla felina");
        const AnatomyGraph* graph=&monster.anatomyGraph;
        for(size_t node=0;node<graph->nodeCount;++node)if(graph->nodes[node].region==ANATOMY_REGION_TAIL) {
            size_t nearest=0;float distance=INFINITY;
            for(size_t vertex=0;vertex<mesh->vertexCount;++vertex) {
                float d=Vec3_LengthSq(Vec3_Sub(mesh->vertices[vertex].position,graph->nodes[node].center));
                if(d<distance){distance=d;nearest=vertex;}
            }
            size_t component=ComponentRoot(parent,nearest);
            if(root==SIZE_MAX)root=component;
            TEST_ASSERT(component==root,"raíz y extremo caudal pertenecen a una superficie conectada");
            ++stations;
        }
        TEST_ASSERT(stations>=6,"se comprueba toda la cadena caudal");
        for(size_t leg=0;leg<monster.animation->rig.limbCount;++leg) {
            const LimbRig* limb=&monster.animation->rig.limbs[leg];
            for(size_t joint=1;joint<limb->chain.jointCount;++joint) {
                AnatomyId id=monster.animation->rig.skeleton.joints[limb->chain.jointIndices[joint]].anatomyId;
                const AnatomyNode* node=AnatomyGraph_FindNode(graph,id);
                TEST_ASSERT(node,"estación de miembro identificable");
                size_t nearest=0;float distance=INFINITY;
                for(size_t v=0;v<mesh->vertexCount;++v) {
                    float d=Vec3_LengthSq(Vec3_Sub(mesh->vertices[v].position,node->center));
                    if(d<distance){distance=d;nearest=v;}
                }
                float radius=fmaxf(node->widthRadius,node->heightRadius);
                TEST_ASSERT(sqrtf(distance)<radius*1.8f+.025f,"cada articulación tiene superficie visible en todos los LOD y cubetas");
                TEST_ASSERT(ComponentRoot(parent,nearest)==root,"las cuatro patas permanecen unidas al cuerpo en todos los LOD");
            }
        }
        Monster_Free(&monster);
        free(parent);
    }
    EcosystemVisual_Shutdown(&visual);EcosystemWorld_Free(&world);
}

static void Picking(void) {
    Mesh mesh=Mesh_Create();
    TEST_ASSERT(Mesh_ReserveVertices(&mesh,3)&&Mesh_ReserveIndices(&mesh,3),"malla de selección");
    mesh.vertexCount=mesh.indexCount=3;
    mesh.vertices[0].position=Vec3_Create(-1,-1,0);mesh.vertices[1].position=Vec3_Create(1,-1,0);
    mesh.vertices[2].position=Vec3_Create(0,1,0);
    for(unsigned i=0;i<3;++i)mesh.indices[i]=i;
    EcosystemVisualDrawPacket packets[2]={
        {.entityId=42,.bodyMesh=&mesh,.position={0,0,5},.scale=2},
        {.entityId=73,.bodyMesh=&mesh,.position={0,0,2},.scale=.5f}};
    Vector3 origin=Vec3_Zero(),direction=Vec3_Create(0,0,1);
    TEST_ASSERT(EcosystemVisual_PickRay(packets,2,origin,direction,.1f,100)==73,"solapamiento selecciona la superficie cercana, independientemente del orden");
    packets[0].position.z=1;
    TEST_ASSERT(EcosystemVisual_PickRay(packets,2,origin,direction,.1f,100)==42,"cambio de profundidad y escala");
    TEST_ASSERT(!EcosystemVisual_PickRay(packets,2,Vec3_Create(10,10,0),direction,.1f,100),"pulsar el fondo no selecciona");
    TEST_ASSERT(!EcosystemVisual_PickRay(packets,2,origin,Vec3_Create(0,0,-1),.1f,100),"no seleccionar detrás de cámara");
    TEST_ASSERT(!EcosystemVisual_PickRay(packets,2,origin,direction,3,4),"respetar planos de recorte");
    packets[0].bodyMesh=NULL;packets[1].bodyMesh=NULL;
    SkeletonPose rest={.jointCount=1},pose={.jointCount=1};
    rest.joints[0].worldRotation=pose.joints[0].worldRotation=Quat_Identity();
    pose.joints[0].worldPosition=Vec3_Create(4,0,0);
    packets[1].headMesh=&mesh;packets[1].restPose=&rest;packets[1].pose=&pose;
    packets[1].headJoint=0;packets[1].headScale=Vec3_Create(.7f,1.3f,1);
    TEST_ASSERT(EcosystemVisual_PickRay(packets,2,Vec3_Create(2,0,0),direction,.1f,100)==73,"la selección de cabeza sigue la pose y el crecimiento");
    TEST_ASSERT(!EcosystemVisual_PickRay(packets,2,origin,direction,.1f,100),"no seleccionar la cabeza en su posición de reposo");
    Mesh_Free(&mesh);
}

static void CarnivoreAnimations(void) {
    EcosystemWorld world;Scenario(&world,0,1,4);
    EcosystemEntity* entity=&world.entities[0];
    EcosystemVisual visual={0};
    TEST_ASSERT(EcosystemVisual_Init(&visual,&world),"visual para animaciones del depredador");
    EcosystemVisualCacheKey key=KeyFor(entity,&world,VISUAL_TIER_FAR);
    const Mesh* mesh=EcosystemVisual_RequestMesh(&visual,key);
    for(unsigned attempt=0;attempt<10000&&mesh==EcosystemVisual_FallbackMesh(&visual);++attempt) {
        EcosystemVisual_Update(&visual,0);
        struct timespec pause={0,1000000};nanosleep(&pause,NULL);
        mesh=EcosystemVisual_RequestMesh(&visual,key);
    }
    TEST_ASSERT(mesh!=EcosystemVisual_FallbackMesh(&visual),"plantilla para animar");
    const CreatureAction actions[]={ACTION_IDLE,ACTION_WANDER,ACTION_CHASE,ACTION_ESCAPE,
        ACTION_ATTACK,ACTION_EAT,ACTION_DRINK,ACTION_REST,ACTION_IDLE};
    float standing=0;
    for(size_t action=0;action<sizeof(actions)/sizeof(*actions);++action) {
        entity->mind.action=actions[action];entity->mind.intent.interact=false;
        bool moving=actions[action]==ACTION_WANDER||actions[action]==ACTION_CHASE||actions[action]==ACTION_ESCAPE;
        float travel[4]={0},jawOpening=0;
        EcosystemVisualDrawPacket packet;SkeletonPose previous={0};
        for(unsigned frame=0;frame<120;++frame) {
            if(moving)entity->body.position.z+=(actions[action]==ACTION_WANDER?.3f:.8f)/60;
            entity->body.yaw=0;
            EcosystemAnatomy_Update(entity,1.f/60);
            world.clock.seconds+=1./60;++world.clock.tick;
            TEST_ASSERT(EcosystemVisual_Update(&visual,1.f/60),"animación ecológica por tiempo biológico");
            TEST_ASSERT(EcosystemVisual_GetDrawPackets(&visual,&packet,1)==1&&packet.ready,"pose completa publicada");
            TEST_ASSERT(packet.mouthError<.01f,"las animaciones conservan el anclaje oral autoritativo");
            TEST_ASSERT(packet.stanceFootError<.05f,"las animaciones conservan los apoyos dentro del alcance del rig");
            for(size_t j=0;j<packet.pose->jointCount;++j)
                TEST_ASSERT(isfinite(Vec3_LengthSq(packet.pose->joints[j].worldPosition)),"ninguna animación produce articulaciones inválidas");
            for(size_t leg=0;leg<4;++leg) {
                int foot=entity->anatomicalBody->rig.limbs[leg].endEffectorJoint;
                if(frame)travel[leg]+=Vec3_Distance(previous.joints[foot].worldPosition,packet.pose->joints[foot].worldPosition);
            }
            jawOpening=fmaxf(jawOpening,fabsf(packet.pose->joints[packet.jawJoint].localRotation.x));
            previous=*packet.pose;
        }
        if(moving)for(unsigned leg=0;leg<4;++leg)
            TEST_ASSERT(travel[leg]>.1f,"las cuatro patas caminan y corren, sin miembros congelados");
        if(actions[action]==ACTION_ATTACK||actions[action]==ACTION_EAT||actions[action]==ACTION_DRINK)
            TEST_ASSERT(jawOpening>.001f,"ataque, alimentación y bebida articulan la mandíbula");
        float height=packet.pose->joints[packet.headJoint].worldPosition.y;
        if(action==0)standing=height;
        if(actions[action]==ACTION_REST)TEST_ASSERT(height<standing-.01f,"el descanso desciende el cuerpo con transición");
        Mesh deformed=Mesh_Create();
        TEST_ASSERT(EcosystemVisual_DeformBody(&packet,&deformed),"superficie animada para comprobar selección");
        Quaternion yaw=Quat_FromAxisAngle(Vec3_Create(0,1,0),packet.yaw);
        for(size_t triangle=0;triangle+2<deformed.indexCount;triangle+=3*(1+deformed.indexCount/48)) {
            Vector3 a=deformed.vertices[deformed.indices[triangle]].position;
            Vector3 b=deformed.vertices[deformed.indices[triangle+1]].position;
            Vector3 c=deformed.vertices[deformed.indices[triangle+2]].position;
            Vector3 normal=Vec3_Cross(Vec3_Sub(b,a),Vec3_Sub(c,a));
            if(Vec3_LengthSq(normal)<1e-12f)continue;
            normal=Quat_RotateVector(yaw,Vec3_Normalize(normal));
            Vector3 center=Vec3_Add(packet.position,Quat_RotateVector(yaw,Vec3_Scale(Vec3_Add(a,Vec3_Add(b,c)),packet.scale/3)));
            TEST_ASSERT(EcosystemVisual_PickRay(&packet,1,Vec3_Add(center,normal),Vec3_Scale(normal,-1),.001f,2)==entity->id,
                "las cotas de selección no excluyen triángulos de ninguna animación");
        }
        Mesh_Free(&deformed);
    }
    EcosystemVisual_Shutdown(&visual);EcosystemWorld_Free(&world);
}

void run_ecosystem_creatures_tests(void) {
    Lifecycle();
    CacheIdentity();
    LodPolicy();
    Determinism();
    AnatomicalContactAndGrowth();
    NewbornClearance();
    AppendageMeshContinuity();
    Picking();
    CarnivoreAnimations();
}
