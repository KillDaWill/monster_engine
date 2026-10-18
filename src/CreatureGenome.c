#include "CreatureGenome.h"
#include <math.h>
CreatureGenome CreatureGenome_Default(void) {return (CreatureGenome){1,1,1,1,.5f,1,1};}
float CreatureGenome_Random(uint32_t *r) {
    uint32_t x=*r?*r:1;x^=x<<13;x^=x>>17;x^=x<<5;*r=x;
    return (x>>8)/16777216.f;
}
static float trait(float a,float b,uint32_t *r,float p,float amp,float lo,float hi) {
    float t=CreatureGenome_Random(r),v=a*t+b*(1-t);
    if(CreatureGenome_Random(r)<p)v+=(CreatureGenome_Random(r)*2-1)*amp;
    return fmaxf(lo,fminf(hi,v));
}
CreatureGenome CreatureGenome_Inherit(const CreatureGenome *a,const CreatureGenome *b,uint32_t *r,float p,float amp) {
    if(!CreatureGenome_Validate(a)||!CreatureGenome_Validate(b)||!r)return CreatureGenome_Default();
    p=fmaxf(0,fminf(1,p));amp=fmaxf(0,fminf(1,amp));
    CreatureGenome g;
#define INHERIT(field,lo,hi) g.field=trait(a->field,b->field,r,p,amp,lo,hi)
    INHERIT(size,.65f,1.4f);INHERIT(speed,.7f,1.3f);INHERIT(efficiency,.7f,1.3f);
    INHERIT(perception,.7f,1.3f);INHERIT(pigment,0,1);INHERIT(morphology,.8f,1.2f);INHERIT(maturation,.8f,1.2f);
#undef INHERIT
    return g;
}
int CreatureGenome_Validate(const CreatureGenome *g) {
    return g&&isfinite(g->size)&&g->size>=.65f&&g->size<=1.4f&&
        isfinite(g->speed)&&g->speed>=.7f&&g->speed<=1.3f&&
        isfinite(g->efficiency)&&g->efficiency>=.7f&&g->efficiency<=1.3f&&
        isfinite(g->perception)&&g->perception>=.7f&&g->perception<=1.3f&&
        isfinite(g->pigment)&&g->pigment>=0&&g->pigment<=1&&
        isfinite(g->morphology)&&g->morphology>=.8f&&g->morphology<=1.2f&&
        isfinite(g->maturation)&&g->maturation>=.8f&&g->maturation<=1.2f;
}
