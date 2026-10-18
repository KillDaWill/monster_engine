#include "CreatureReproduction.h"
#include <math.h>
ReproductionProfile CreatureReproduction_Default(void) {
    return (ReproductionProfile){.cooldown=100,.gestation=45,.maximumAge=1400,.offspring=1,
        .minimumEnergy=.65f,.minimumHealth=.8f,.energyCost=.14f,
        .mutationProbability=.15f,.mutationAmplitude=.08f};
}
bool CreatureReproduction_Ready(const CreatureReproduction *r,const ReproductionProfile *p,
    const CreaturePhysiology *b,double now) {
    return !b->cause&&b->stage==LIFE_ADULT&&!r->gestating&&!r->partner&&
        now>=r->cooldownUntil&&b->age<=p->maximumAge&&b->energy>=fmaxf(p->minimumEnergy,p->energyCost)&&
        b->hydration>=.4f&&b->health>=p->minimumHealth&&b->fatigue<.7f;
}

bool CreatureReproduction_ValidateProfile(const ReproductionProfile *p) {
    return p&&isfinite(p->cooldown)&&p->cooldown>0&&isfinite(p->gestation)&&p->gestation>0&&
        isfinite(p->maximumAge)&&p->maximumAge>0&&p->offspring>0&&p->offspring<=32&&
        isfinite(p->minimumEnergy)&&p->minimumEnergy>=0&&p->minimumEnergy<=1&&
        isfinite(p->minimumHealth)&&p->minimumHealth>=0&&p->minimumHealth<=1&&
        isfinite(p->energyCost)&&p->energyCost>=0&&p->energyCost<=1&&
        isfinite(p->mutationProbability)&&p->mutationProbability>=0&&p->mutationProbability<=1&&
        isfinite(p->mutationAmplitude)&&p->mutationAmplitude>=0&&p->mutationAmplitude<=1;
}
