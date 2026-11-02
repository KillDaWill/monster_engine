/** @file test_module_dependencies.c
 * @brief Dependencias anatómicas, rutas periféricas y límites del plan corporal. */
#include "Creature.h"
#include "CreatureRig.h"
#include "AttachmentPath.h"
#include "HeadModule.h"
#include "Limb.h"
#include "Ornament.h"
#include "Monster.h"
#include "MonsterSDF.h"
#include "test_utils.h"
#include <math.h>

static CreatureRecipe WithSpine(void) {
    CreatureRecipe r = *CreatureRecipes_Lizard();
    r.adult.ornamentCount = 1;
    r.adult.ornaments[0] = (OrnamentPhenotype){.archetype=ORNAMENT_ARCHETYPE_SPINE,
        .length=.4f,.baseRadius=.12f,.tipRadius=.04f,.development=1};
    r.bodyPlan.modules[r.bodyPlan.moduleCount++] = (CreatureModuleInstance){
        .instanceId=400,.kind=CREATURE_MODULE_ORNAMENT,
        .attachment={2,HEAD_SLOT_CRANIAL_DORSAL}};
    r.seed = r.larva = r.juvenile = r.adult;
    return r;
}

static void Reverse(BodyPlan* plan) {
    for (size_t i=0;i<plan->moduleCount/2;++i) {
        CreatureModuleInstance tmp=plan->modules[i];
        plan->modules[i]=plan->modules[plan->moduleCount-i-1];
        plan->modules[plan->moduleCount-i-1]=tmp;
    }
}

void run_module_dependencies_tests(void) {
    CreatureRecipe r=WithSpine();
    AnatomyGraph a,b;
    TEST_ASSERT(Creature_ResolveAnatomy(&r,&r.adult,&a),"Referencia con ranura craneal");
    Reverse(&r.bodyPlan);
    size_t order[CREATURE_MAX_MODULES], again[CREATURE_MAX_MODULES];
    TEST_ASSERT(CreatureRecipe_ResolutionOrder(&r,order) && CreatureRecipe_ResolutionOrder(&r,again),"Orden de dependencias");
    for(size_t i=0;i<r.bodyPlan.moduleCount;++i)TEST_ASSERT(order[i]==again[i],"Orden determinista");
    TEST_ASSERT(Creature_ResolveAnatomy(&r,&r.adult,&b) && AnatomyGraph_TopologyCompatible(&a,&b),"Hijo anterior al anfitrión");
    for(size_t i=0;i<a.nodeCount;++i) {
        const AnatomyNode* n=AnatomyGraph_FindNode(&b,a.nodes[i].id);
        TEST_ASSERT(n && Vec3_Distance(n->center,a.nodes[i].center)<1e-6f,"Geometría e identidades independientes del orden");
    }
    TEST_ASSERT(Creature_ResolveAnatomy(&r,&r.adult,&a) &&
        AnatomyGraph_Fingerprint(&a)==AnatomyGraph_Fingerprint(&b),"Huella determinista");

    /* La ruta usa los anfitriones reales, no la antigua ranura auxiliar. */
    CreatureModuleInstance* child=&r.bodyPlan.modules[0];
    child->pathAttached=true;child->attachment=(AttachmentRef){0};
    child->pathNormal=Vec3_Create(0,1,0);child->pathU=.5f;
    const uint32_t hosts[]={1,10,20};
    for(unsigned h=0;h<3;++h) {
        child->pathNodeA=Anatomy_MakeId(hosts[h],1);child->pathNodeB=Anatomy_MakeId(hosts[h],2);
        TEST_ASSERT(CreatureRecipe_Validate(&r,&r.adult) && Creature_ResolveAnatomy(&r,&r.adult,&a),"Ruta axial, miembro o cola en orden inverso");
    }
    child->pathNodeA=Anatomy_MakeId(2,HEAD_NODE_MAIN);child->pathNodeB=Anatomy_MakeId(1,1);
    TEST_ASSERT(Creature_ResolveAnatomy(&r,&r.adult,&a),"Ruta con dos anfitriones, incluida cabeza");
    CreatureRecipe valid=r;
    child->pathNodeA=Anatomy_MakeId(999,1);
    TEST_ASSERT(!CreatureRecipe_Validate(&r,&r.adult),"Extremo de ruta ausente rechazado");
    r=valid;child=&r.bodyPlan.modules[0];child->pathNodeA=Anatomy_MakeId(400,1);
    TEST_ASSERT(!CreatureRecipe_Validate(&r,&r.adult),"Autodependencia rechazada");
    r=valid;r.bodyPlan.modules[0].pathU=NAN;
    TEST_ASSERT(!CreatureRecipe_Validate(&r,&r.adult),"Parámetro no finito rechazado");
    r=valid;r.bodyPlan.modules[0].pathNormal=Vec3_Zero();
    TEST_ASSERT(!CreatureRecipe_Validate(&r,&r.adult),"Normal nula rechazada");
    r=valid;r.bodyPlan.modules[0].pathNodeB=Anatomy_MakeId(1,65535);
    TEST_ASSERT(!Creature_ResolveAnatomy(&r,&r.adult,&a),"Nodo local ausente rechazado");
    r=valid;r.bodyPlan.root.localNodeId=65535;
    TEST_ASSERT(!Creature_ResolveAnatomy(&r,&r.adult,&a),"Raíz anatómica ausente rechazada");

    r=WithSpine();size_t last=r.bodyPlan.moduleCount-1;
    r.adult.ornamentCount=2;r.adult.ornaments[1]=r.adult.ornaments[0];
    CreatureModuleInstance next=r.bodyPlan.modules[last];next.instanceId=401;next.phenotypeIndex=1;
    next.membranePrevious=400;r.bodyPlan.modules[r.bodyPlan.moduleCount++]=next;
    Reverse(&r.bodyPlan);
    TEST_ASSERT(Creature_ResolveAnatomy(&r,&r.adult,&a),"Predecesor de lámina resuelto antes del hijo");
    r.bodyPlan.modules[1].membranePrevious=401;
    TEST_ASSERT(!CreatureRecipe_Validate(&r,&r.adult),"Ciclo de dos módulos rechazado");
    r.bodyPlan.modules[1].membranePrevious=999;
    TEST_ASSERT(!CreatureRecipe_Validate(&r,&r.adult),"Soporte de lámina ausente rechazado");
    r.bodyPlan.modules[1].membranePrevious=10;
    TEST_ASSERT(!CreatureRecipe_Validate(&r,&r.adult),"Soporte no ornamental rechazado");

    /* Cadena que ocupa exactamente los 64 bits, sin ampliar estructuras. */
    r=WithSpine();last=r.bodyPlan.moduleCount-1;
    while(r.bodyPlan.moduleCount<CREATURE_MAX_MODULES) {
        next=r.bodyPlan.modules[last];next.instanceId++;
        next.phenotypeIndex=(unsigned)r.adult.ornamentCount;
        next.membranePrevious=r.bodyPlan.modules[last].instanceId;
        r.adult.ornaments[r.adult.ornamentCount++]=r.adult.ornaments[0];
        r.bodyPlan.modules[r.bodyPlan.moduleCount++]=next;++last;
    }
    Reverse(&r.bodyPlan);
    TEST_ASSERT(CreatureRecipe_Validate(&r,&r.adult) && Creature_ResolveAnatomy(&r,&r.adult,&a),"Capacidad exacta de 64 módulos");
    r.bodyPlan.moduleCount++;
    TEST_ASSERT(!CreatureRecipe_Validate(&r,&r.adult),"Capacidad excedida rechazada");

    /* Marco arbitrario: normal oblicua, tramo vertical y fallo sin publicar. */
    AnatomyGraph_Init(&a);
    TEST_ASSERT(AnatomyGraph_AddNode(&a,(AnatomyNode){.id=1,.widthRadius=.3f,.heightRadius=.5f}) &&
        AnatomyGraph_AddNode(&a,(AnatomyNode){.id=2,.center={0,2,0},.widthRadius=.4f,.heightRadius=.6f}),"Estaciones de prueba");
    next=(CreatureModuleInstance){.pathAttached=true,.pathNodeA=1,.pathNodeB=2,.pathU=.3f,.pathNormal={1,0,1}};
    AttachmentSlot slot={0};
    TEST_ASSERT(AttachmentPath_ResolveNodes(&a,&next,1,&slot) && fabsf(Vec3_Dot(slot.up,slot.forward))<1e-6f &&
        FLOAT_NEAR(Vec3_Length(slot.side),1),"Marco local ortonormal");
    Vector3 previous=slot.position;next.pathNormal=Vec3_Create(0,1,0);
    TEST_ASSERT(!AttachmentPath_ResolveNodes(&a,&next,1,&slot) && Vec3_Distance(previous,slot.position)==0,"Normal paralela rechazada sin publicar");
    next.pathNormal=Vec3_Create(INFINITY,0,1);
    TEST_ASSERT(!AttachmentPath_ResolveNodes(&a,&next,1,&slot),"Normal infinita rechazada");
    next.pathNormal=Vec3_Create(1,0,0);a.nodes[1].center=a.nodes[0].center;
    TEST_ASSERT(!AttachmentPath_ResolveNodes(&a,&next,1,&slot),"Tramo colapsado rechazado");

    /* Integración con rig, superficie y SDF en las cuatro etapas. */
    r=WithSpine();Reverse(&r.bodyPlan);
    Monster m=Monster_Create();MonsterSDF sdf=MonsterSDF_Create();
    for(unsigned stage=0;stage<4;++stage) {
        TEST_ASSERT(Creature_BuildMonster(&m,&r,CreatureRecipe_GetStage(&r,(CreatureStage)stage)) &&
            MonsterSDF_Build(&sdf,&m,MonsterSDF_DefaultConfig()),"Integración de plan desordenado");
        TEST_ASSERT(m.surfaceMapping.tagCount==m.anatomyGraph.nodeCount,"Propiedad superficial completa");
        Rig rig;
        TEST_ASSERT(CreatureRig_Build(&m,&rig) && rig.limbCount==4 && rig.tailCount==1,"Rig de módulos reordenados");
        for(size_t i=0;i<rig.limbCount;++i)
            TEST_ASSERT(rig.limbs[i].role==(i<2?LIMB_FORE:LIMB_HIND) &&
                rig.limbs[i].side==(i%2?LIMB_RIGHT:LIMB_LEFT),"Orden locomotor canónico independiente del JSON");
    }
    MonsterSDF_Free(&sdf);Monster_Free(&m);
    printf("[PASS] test_module_dependencies: orden, rutas, ciclos, marcos, capacidad y pipeline\n");
}
