#include "EcosystemResources.h"
#include <math.h>
bool EcosystemResource_Available(const EcosystemResource *r) {
    return r&&r->id&&r->accessible&&(r->infinite||r->biomass>1e-6f);
}
void EcosystemResource_Update(EcosystemResource *r,float dt) {
    if(!r||!r->id||!isfinite(dt)||dt<=0)return;
    if(r->category==ECO_MEAT) {
        if(r->remainingLifetime<=dt){r->remainingLifetime=0;r->biomass=0;}
        else {r->biomass*=fmaxf(0,1-dt/(float)r->remainingLifetime);r->remainingLifetime-=dt;}
    } else if(!r->infinite)r->biomass=fminf(r->maximumBiomass,fmaxf(0,r->biomass+r->regenerationRate*dt));
}
float EcosystemResource_Consume(EcosystemResource *r,float requested) {
    if(!EcosystemResource_Available(r)||!isfinite(requested)||requested<=0)return 0;
    float actual=r->infinite?requested:fminf(requested,r->biomass);
    if(!r->infinite)r->biomass=fmaxf(0,r->biomass-actual);
    return actual;
}
