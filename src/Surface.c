#include "Surface.h"
#include <math.h>
#include <string.h>
static float Limit(float x,float lo,float hi,float fallback) {
    return isfinite(x)?fmaxf(lo,fminf(hi,x)):fallback;
}
SurfacePhenotype SurfacePhenotype_Default(void) {
    SurfacePhenotype p={0};
    p.pigment=(PigmentPhenotype){.baseColor={150,150,140,255},.secondaryColor={90,90,80,255},
        .ventralColor={200,195,175,255},.ventralLightening=.5f,.patternScale=1,.seed=1};
    p.integument.type=INTEGUMENT_SMOOTH_SKIN; p.integument.coverage=1;
    p.integument.scales=(ScalePhenotype){.size=.12f,.aspectRatio=1,.roundness=.65f,.irregularity=.3f,
        .relief=.012f,.edgeDepth=.15f,.edgeWidth=.075f,.roughness=.55f,.microColorVariation=.08f,.seed=2};
    for(int i=0;i<SURFACE_REGION_COUNT;++i)p.regions[i]=(SurfaceRegionProfile){1,1,1,1,1,1,1,0};
    return p;
}
void SurfacePhenotype_Normalize(SurfacePhenotype* p) {
    if(!p)return;
    if(p->integument.type<INTEGUMENT_SMOOTH_SKIN || p->integument.type>INTEGUMENT_PLATES)p->integument.type=INTEGUMENT_SMOOTH_SKIN;
#define CLAMP(path,lo,hi,def) p->path=Limit(p->path,lo,hi,def)
    CLAMP(integument.coverage,0,1,1);
    CLAMP(pigment.dorsalDarkening,0,.8f,0); CLAMP(pigment.ventralLightening,0,1,.5f);
    CLAMP(pigment.patternStrength,0,1,0); CLAMP(pigment.patternScale,.05f,20,1); CLAMP(pigment.bands,0,1,0);
    CLAMP(integument.scales.size,.012f,1,.12f); CLAMP(integument.scales.aspectRatio,.3f,3,1);
    CLAMP(integument.scales.roundness,0,1,.65f); CLAMP(integument.scales.irregularity,0,1,.3f);
    CLAMP(integument.scales.relief,0,.06f,.012f); CLAMP(integument.scales.edgeDepth,0,1,.15f);
    CLAMP(integument.scales.edgeWidth,.015f,.3f,.075f); CLAMP(integument.scales.keelStrength,0,1,0);
    CLAMP(integument.scales.roughness,.12f,1,.55f); CLAMP(integument.scales.microColorVariation,0,.35f,.08f);
    for(int i=0;i<SURFACE_REGION_COUNT;++i) {
        CLAMP(regions[i].size,.2f,4,1); CLAMP(regions[i].aspect,.4f,2.5f,1);
        CLAMP(regions[i].relief,0,2,1); CLAMP(regions[i].keel,0,2,1);
        CLAMP(regions[i].roughness,.3f,2,1); CLAMP(regions[i].irregularity,0,2,1);
        CLAMP(regions[i].roundness,0,2,1); p->regions[i].reserved=0;
    }
#undef CLAMP
}
SurfacePhenotype SurfacePhenotype_Interpolate(const SurfacePhenotype* a,const SurfacePhenotype* b,float t) {
    SurfacePhenotype p=a?*a:SurfacePhenotype_Default(),q=b?*b:SurfacePhenotype_Default();
    SurfacePhenotype_Normalize(&p); SurfacePhenotype_Normalize(&q); t=Limit(t,0,1,0);
#define MIX(x) p.x+=(q.x-p.x)*t
    p.pigment.baseColor=Color_Lerp(p.pigment.baseColor,q.pigment.baseColor,t);
    p.pigment.secondaryColor=Color_Lerp(p.pigment.secondaryColor,q.pigment.secondaryColor,t);
    p.pigment.ventralColor=Color_Lerp(p.pigment.ventralColor,q.pigment.ventralColor,t);
    MIX(pigment.dorsalDarkening); MIX(pigment.ventralLightening); MIX(pigment.patternStrength);
    MIX(pigment.patternScale); MIX(pigment.bands); MIX(integument.coverage);
    MIX(integument.scales.size); MIX(integument.scales.aspectRatio); MIX(integument.scales.roundness);
    MIX(integument.scales.irregularity); MIX(integument.scales.relief); MIX(integument.scales.edgeDepth);
    MIX(integument.scales.edgeWidth); MIX(integument.scales.keelStrength); MIX(integument.scales.roughness);
    MIX(integument.scales.microColorVariation);
    for(int i=0;i<SURFACE_REGION_COUNT;++i) {
        MIX(regions[i].size); MIX(regions[i].aspect); MIX(regions[i].relief); MIX(regions[i].keel);
        MIX(regions[i].roughness); MIX(regions[i].irregularity); MIX(regions[i].roundness);
    }
#undef MIX
    /* Piel y escamas comparten backend: transición continua por cobertura. */
    if(p.integument.type!=q.integument.type &&
       p.integument.type<=INTEGUMENT_SCALES && q.integument.type<=INTEGUMENT_SCALES) {
        float ca=a && a->integument.type==INTEGUMENT_SCALES?a->integument.coverage:0;
        float cb=q.integument.type==INTEGUMENT_SCALES?q.integument.coverage:0;
        p.integument.type=INTEGUMENT_SCALES; p.integument.coverage=ca+(cb-ca)*t;
    }
    if(t>=1) { p.integument.type=q.integument.type; p.pigment.seed=q.pigment.seed; p.integument.scales.seed=q.integument.scales.seed; }
    return p;
}
static void PackColor(float* out,Color c) { out[0]=c.r/255.f; out[1]=c.g/255.f; out[2]=c.b/255.f; out[3]=c.a/255.f; }
SurfaceRecipe SurfaceRecipe_Compile(const SurfacePhenotype* source) {
    SurfacePhenotype p=source?*source:SurfacePhenotype_Default(); SurfacePhenotype_Normalize(&p);
    SurfaceRecipe r; memset(&r,0,sizeof(r));
    PackColor(r.data[0],p.pigment.baseColor); PackColor(r.data[1],p.pigment.secondaryColor); PackColor(r.data[2],p.pigment.ventralColor);
    float rows[5][4]={
        {p.pigment.dorsalDarkening,p.pigment.ventralLightening,p.pigment.patternStrength,p.pigment.patternScale},
        {p.integument.scales.size,p.integument.scales.aspectRatio,p.integument.scales.roundness,p.integument.scales.irregularity},
        {p.integument.scales.relief,p.integument.scales.edgeDepth,p.integument.scales.edgeWidth,p.integument.scales.keelStrength},
        {p.integument.scales.roughness,p.integument.scales.microColorVariation,p.integument.type==INTEGUMENT_SCALES?p.integument.coverage:0,p.pigment.bands},
        {(float)p.integument.type,0,0,0}};
    memcpy(r.data[3],rows,sizeof(rows));
    for(int i=0;i<SURFACE_REGION_COUNT;++i) {
        SurfaceRegionProfile s=p.regions[i];
        float rowsRegion[2][4]={{s.size,s.aspect,s.relief,s.keel},{s.roughness,s.irregularity,s.roundness,0}};
        memcpy(r.data[8+2*i],rowsRegion,sizeof(rowsRegion));
    }
    r.pigmentSeed=p.pigment.seed; r.scaleSeed=p.integument.scales.seed; return r;
}
