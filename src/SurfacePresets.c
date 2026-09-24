#include "SurfacePresets.h"
#include <math.h>
static uint32_t Hash(uint32_t x) { x^=x>>16; x*=0x7feb352du; x^=x>>15; x*=0x846ca68bu; return x^(x>>16); }
static float Random(uint32_t* s) { *s=Hash(*s+0x9e3779b9u); return (float)(*s>>8)*(1.f/16777216.f); }
SurfacePhenotype SurfacePreset_ScaledReptile(uint32_t seed,float maturity) {
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
SurfacePhenotype SurfacePreset_CanidShortDoubleCoat(uint32_t seed,float maturity) {
    float t=isfinite(maturity)?fmaxf(0,fminf(1,maturity)):1;
    SurfacePhenotype p=SurfacePhenotype_Default(); uint32_t state=seed;
    float warm=Random(&state);
    p.pigment.baseColor=Color_Lerp(Color_FromRGB(145,110,75),Color_FromRGB(165,125,85),warm);
    p.pigment.secondaryColor=Color_FromRGB(48,36,26);
    p.pigment.ventralColor=Color_FromRGB(225,215,195);
    p.pigment.dorsalDarkening=.35f; p.pigment.ventralLightening=.65f;
    p.pigment.patternStrength=.35f; p.pigment.patternScale=.85f;
    p.pigment.seed=Hash(seed^0x444f4750u);
    p.integument.type=INTEGUMENT_FUR; p.integument.coverage=.3f+.7f*t;
    FurPhenotype* f=&p.integument.fur;
    f->length=.055f+.02f*t;
    f->density=1.0f+.2f*Random(&state);
    f->thickness=.72f;
    f->lay=.75f;
    f->stiffness=.70f;
    f->clumping=.30f+.10f*Random(&state);
    f->irregularity=.28f;
    f->undercoat=.85f+.12f*t;
    f->undercoatLength=.62f;
    f->guardDensity=.72f;
    f->roughness=.63f;
    f->sheen=.20f;
    f->rootDarkening=.30f;
    f->tipLightening=.12f;
    f->seed=Hash(seed^0x46555231u);
    /* Perfiles explícitos: longitud, densidad, grosor, inclinación, subpelo, cobertura, mechón, variación. */
    p.furRegions[SURFACE_REGION_HEAD]=(FurRegionProfile){.16f,1,.65f,.8f,.65f,.45f,.4f,.6f};
    p.furRegions[SURFACE_REGION_NECK]=(FurRegionProfile){1.2f,1.1f,1,.9f,1,1,1,1};
    p.furRegions[SURFACE_REGION_VENTRAL_TRUNK]=(FurRegionProfile){.75f,1,.8f,1,1,.65f,.8f,1};
    p.furRegions[SURFACE_REGION_FORELIMB]=(FurRegionProfile){.45f,.9f,.75f,1,1,.7f,.7f,.7f};
    p.furRegions[SURFACE_REGION_HINDLIMB]=(FurRegionProfile){.6f,1,.85f,1,1,.8f,.8f,.8f};
    p.furRegions[SURFACE_REGION_TAIL]=(FurRegionProfile){1.3f,1.1f,1,1,1,1,1,1};
    p.furRegions[SURFACE_REGION_DIGIT]=(FurRegionProfile){.16f,.8f,.55f,1,.6f,.3f,.4f,.6f};
    SurfacePhenotype_Normalize(&p); return p;
}
