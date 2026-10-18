#include "EcosystemScenario.h"
#include <math.h>
#include <string.h>
EcosystemConfig EcosystemConfig_Default(void) {
    return (EcosystemConfig){.seed=42,.herbivores=8,.carnivores=2,.plants=20,.capacity=256,
        .step=1./60,.day=120,.days=30,.scenario="reference"};
}
bool EcosystemConfig_Scenario(EcosystemConfig *c,const char *name) {
    if(!strcmp(name,"A")||!strcmp(name,"scarce")) {c->scenario="scarce";c->herbivores=12;c->carnivores=0;c->plants=2;}
    else if(!strcmp(name,"B")||!strcmp(name,"growth")) {c->scenario="growth";c->herbivores=8;c->carnivores=0;c->plants=30;}
    else if(!strcmp(name,"C")||!strcmp(name,"predator")) {c->scenario="predator";c->herbivores=16;c->carnivores=2;c->plants=25;}
    else if(!strcmp(name,"D")||!strcmp(name,"no-prey")) {c->scenario="no-prey";c->herbivores=0;c->carnivores=4;c->plants=20;}
    else if(!strcmp(name,"E")||!strcmp(name,"generations")) {c->scenario="generations";c->herbivores=12;c->carnivores=2;c->plants=30;}
    else if(!strcmp(name,"reference"))c->scenario="reference";
    else return false;
    return true;
}
bool EcosystemScenario_Create(EcosystemWorld *w,const EcosystemConfig *c) {
    if(!w||!c||c->herbivores>c->capacity||c->carnivores>c->capacity-c->herbivores||c->plants>100000)return false;
    if(!EcosystemWorld_Init(w,c->capacity,c->plants+2+c->capacity,2,c->seed,c->step,c->day))return false;
    EcosystemSpecies herb=EcosystemSpecies_Default(1,1,ECO_PLANT,0,2);
    EcosystemSpecies carn=EcosystemSpecies_Default(2,2,ECO_MEAT,1,0);
    if(!strcmp(c->scenario,"generations")) {
        herb.speed=2.5f;carn.speed=2.25f;carn.damage=.2f;carn.attackCooldown=1.2f;
    }
    if(!EcosystemWorld_AddSpecies(w,herb)||!EcosystemWorld_AddSpecies(w,carn))goto failure;
    for(unsigned i=0;i<2;++i) {
        if(!EcosystemWorld_AddResource(w,(EcosystemResource){.category=ECO_WATER,.infinite=true,.accessible=true,
            .nutritionalValue=1},Vec3_Create(i?8:-8,0,0),1.3f))goto failure;
    }
    bool scarce=!strcmp(c->scenario,"scarce");
    for(unsigned i=0;i<c->plants;++i) {
        float x=-14+CreatureGenome_Random(&w->random)*28,z=-14+CreatureGenome_Random(&w->random)*28;
        float biomass=scarce?.15f:5;
        if(!EcosystemWorld_AddResource(w,(EcosystemResource){.category=ECO_PLANT,.accessible=true,
            .biomass=biomass,.maximumBiomass=biomass,.regenerationRate=scarce?.0001f:.018f,.nutritionalValue=.3f},
            Vec3_Create(x,0,z),.8f))goto failure;
    }
    for(unsigned species=1;species<=2;++species) {
        unsigned n=species==1?c->herbivores:c->carnivores;
        for(unsigned i=0;i<n;++i) {
            bool spawned=false;
            for(unsigned j=0;j<1000&&!spawned;++j) {
                Vector3 p=Vec3_Create(-12+CreatureGenome_Random(&w->random)*24,0,-12+CreatureGenome_Random(&w->random)*24);
                float energy=scarce?.3f:species==2?.48f:.85f;
                spawned=EcosystemWorld_Spawn(w,species,p,180+CreatureGenome_Random(&w->random)*180,energy,NULL)!=0;
            }
            if(!spawned)goto failure;
        }
    }
    return true;
failure:
    EcosystemWorld_Free(w);return false;
}
