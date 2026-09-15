#include "LizardSurface.h"
#include <math.h>
static uint32_t Hash(uint32_t x) { x^=x>>16; x*=0x7feb352du; x^=x>>15; x*=0x846ca68bu; return x^(x>>16); }
static float Random(uint32_t* s) { *s=Hash(*s+0x9e3779b9u); return (float)(*s>>8)*(1.f/16777216.f); }
SurfacePhenotype LizardSurface_FromSeed(uint32_t seed,float maturity) {
    float t=isfinite(maturity)?fmaxf(0,fminf(1,maturity)):1;
    SurfacePhenotype p=SurfacePhenotype_Default(); uint32_t state=seed;
    float warm=Random(&state);
    p.pigment.baseColor=Color_Lerp(Color_FromRGB(92,120,48),Color_FromRGB(128,89,48),warm);
    p.pigment.secondaryColor=Color_Lerp(Color_FromRGB(36,57,27),Color_FromRGB(45,32,25),warm);
    p.pigment.ventralColor=Color_FromRGB(193,181,108);
    p.pigment.baseColor=Color_Lerp(Color_FromRGB(228,221,190),p.pigment.baseColor,.35f+.65f*t);
    p.pigment.dorsalDarkening=.15f; p.pigment.ventralLightening=.85f;
    p.pigment.patternStrength=(.2f+.3f*Random(&state))*(.3f+.7f*t);
    p.pigment.patternScale=.65f+.5f*Random(&state); p.pigment.bands=Random(&state);
    p.pigment.seed=Hash(seed^0x534b494eu);
    p.integument.type=INTEGUMENT_SCALES; p.integument.coverage=.25f+.75f*t;
    ScalePhenotype* s=&p.integument.scales;
    s->size=.10f+.035f*Random(&state); s->aspectRatio=.9f+.5f*Random(&state);
    s->roundness=.55f+.35f*Random(&state); s->irregularity=.2f+.35f*Random(&state);
    s->relief=.005f+.011f*t; s->keelStrength=(.15f+.3f*Random(&state))*t;
    s->roughness=.45f+.25f*Random(&state); s->seed=Hash(seed^0x5343414cu);
    p.regions[SURFACE_REGION_HEAD]=(SurfaceRegionProfile){1.45f,1,.65f,.2f,1,1.4f,.8f,0};
    p.regions[SURFACE_REGION_VENTRAL_TRUNK]=(SurfaceRegionProfile){1.6f,.65f,.35f,0,1.15f,.25f,1.1f,0};
    p.regions[SURFACE_REGION_FORELIMB].size=.7f; p.regions[SURFACE_REGION_HINDLIMB].size=.75f;
    p.regions[SURFACE_REGION_TAIL].aspect=1.7f; p.regions[SURFACE_REGION_TAIL].size=.85f;
    p.regions[SURFACE_REGION_DIGIT].size=.35f; p.regions[SURFACE_REGION_DIGIT].relief=.3f;
    SurfacePhenotype_Normalize(&p); return p;
}
SurfaceMapping LizardSurface_Mapping(const AnatomyGraph* graph,float totalScale) {
    SurfaceMapping map={.unitScale=totalScale};
    if(!graph)return map;
    for(size_t i=0;i<graph->nodeCount;++i) {
        AnatomyId id=graph->nodes[i].id; SurfaceRegion r=SURFACE_REGION_DORSAL_TRUNK;
        if(id==ANATOMY_ID_HEAD)r=SURFACE_REGION_HEAD;
        else if(id==ANATOMY_ID_NECK)r=SURFACE_REGION_NECK;
        else if(id>=ANATOMY_ID_DIGIT_BASE)r=SURFACE_REGION_DIGIT;
        else if(id>=ANATOMY_ID_HIND_LEFT_HIP)r=SURFACE_REGION_HINDLIMB;
        else if(id>=ANATOMY_ID_FORE_LEFT_SHOULDER)r=SURFACE_REGION_FORELIMB;
        else if(id>=ANATOMY_ID_TAIL_BASE)r=SURFACE_REGION_TAIL;
        map.tags[map.tagCount++]=(SurfaceRegionTag){id,r};
    }
    return map;
}
