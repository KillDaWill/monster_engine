#include "CreaturePhysiology.h"
#include <math.h>
static float unit(float x) {return fmaxf(0,fminf(1,x));}
PhysiologyProfile CreaturePhysiology_Default(void) {
    return (PhysiologyProfile){.basalCost=.0022f,.hydrationCost=.003f,.movementCost=.0007f,
        .starvationDamage=.018f,.dehydrationDamage=.025f,.recoveryRate=.002f,
        .fatigueRate=.0015f,.restRate=.04f,.maturityAge=120,.oldAge=1600,.maximumAge=2200};
}
CreaturePhysiology CreaturePhysiology_Create(double age,float energy) {
    return (CreaturePhysiology){.energy=unit(energy),.hydration=.85f,.health=1,.age=fmax(0,age)};
}
void CreaturePhysiology_Update(CreaturePhysiology *p,const PhysiologyProfile *c,float dt,
    float speed,float efficiency,bool resting) {
    if(!p||!c||p->cause||!isfinite(dt)||dt<=0||!isfinite(speed)||!isfinite(efficiency)||efficiency<=0)return;
    p->age+=dt;
    p->stage=p->age<c->maturityAge?LIFE_JUVENILE:p->age<c->oldAge?LIFE_ADULT:LIFE_OLD;
    p->energy=unit(p->energy-(c->basalCost+c->movementCost*fabsf(speed))*dt/efficiency);
    p->hydration=unit(p->hydration-c->hydrationCost*dt);
    p->fatigue=unit(p->fatigue+(resting?-c->restRate:c->fatigueRate*(1+fabsf(speed)))*dt);
    if(p->energy<=0)p->health-=c->starvationDamage*dt;
    if(p->hydration<=0)p->health-=c->dehydrationDamage*dt;
    if(p->energy>.5f&&p->hydration>.5f)p->health+=c->recoveryRate*dt;
    p->health=unit(p->health);
    if(p->age>=c->maximumAge)p->cause=DEATH_AGE;
    else if(p->health<=0)p->cause=p->hydration<=0?DEATH_DEHYDRATION:DEATH_STARVATION;
    if(p->cause)p->stage=LIFE_DEAD;
}
void CreaturePhysiology_Feed(CreaturePhysiology *p,float n) {
    if(p&&!p->cause&&isfinite(n)&&n>0)p->energy=unit(p->energy+n);
}
void CreaturePhysiology_Drink(CreaturePhysiology *p,float n) {
    if(p&&!p->cause&&isfinite(n)&&n>0)p->hydration=unit(p->hydration+n);
}

bool CreaturePhysiology_ValidateProfile(const PhysiologyProfile *p) {
    if(!p)return false;
    const float rates[]={p->basalCost,p->hydrationCost,p->movementCost,p->starvationDamage,
        p->dehydrationDamage,p->recoveryRate,p->fatigueRate,p->restRate};
    for(unsigned i=0;i<sizeof(rates)/sizeof(*rates);++i)if(!isfinite(rates[i])||rates[i]<0)return false;
    return isfinite(p->maturityAge)&&p->maturityAge>0&&isfinite(p->oldAge)&&p->oldAge>=p->maturityAge&&
        isfinite(p->maximumAge)&&p->maximumAge>=p->oldAge;
}
