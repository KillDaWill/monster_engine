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
SurfacePhenotype SurfacePreset_BovineShaggyCoat(uint32_t seed,float maturity) {
    float t=isfinite(maturity)?fmaxf(0,fminf(1,maturity)):1;
    SurfacePhenotype p=SurfacePhenotype_Default(); uint32_t state=seed;
    float warm=Random(&state);
    p.pigment.baseColor=Color_Lerp(Color_FromRGB(108,96,68),Color_FromRGB(130,114,82),warm);
    p.pigment.secondaryColor=Color_Lerp(Color_FromRGB(150,130,92),Color_FromRGB(181,161,120),warm);
    p.pigment.ventralColor=Color_FromRGB(209,191,149);
    p.pigment.dorsalDarkening=.42f;p.pigment.ventralLightening=.62f;p.pigment.patternStrength=.24f;
    p.pigment.patternScale=.72f;p.pigment.bands=.18f;p.pigment.seed=Hash(seed^0x42564e31u);
    p.integument.type=INTEGUMENT_FUR;p.integument.coverage=.25f+.75f*t;
    FurPhenotype* f=&p.integument.fur;
    f->length=.095f+.075f*t;f->density=.92f+.18f*Random(&state);f->thickness=.86f;
    f->lay=.58f;f->stiffness=.56f;f->clumping=.46f+.16f*Random(&state);f->irregularity=.52f;
    f->undercoat=.92f+.08f*t;f->undercoatLength=.66f;f->guardDensity=.86f;
    f->roughness=.82f;f->sheen=.10f;f->rootDarkening=.38f;f->tipLightening=.10f;
    f->seed=Hash(seed^0x46555242u);
    p.furRegions[SURFACE_REGION_HEAD]=(FurRegionProfile){.22f,.95f,.80f,.90f,.72f,.62f,.48f,.72f};
    p.furRegions[SURFACE_REGION_NECK]=(FurRegionProfile){1.45f,1.10f,1.05f,.82f,1.08f,1.12f,1.18f,1.05f};
    p.furRegions[SURFACE_REGION_DORSAL_TRUNK]=(FurRegionProfile){1.18f,1.02f,1.05f,.88f,1.04f,1.05f,1.08f,1.04f};
    p.furRegions[SURFACE_REGION_VENTRAL_TRUNK]=(FurRegionProfile){.90f,1.02f,.92f,1.05f,1.08f,.84f,.92f,1.02f};
    p.furRegions[SURFACE_REGION_FORELIMB]=(FurRegionProfile){.68f,.96f,.92f,1,1,.86f,.82f,.82f};
    p.furRegions[SURFACE_REGION_HINDLIMB]=(FurRegionProfile){.78f,1,.98f,1,1,.92f,.90f,.88f};
    p.furRegions[SURFACE_REGION_TAIL]=(FurRegionProfile){1.18f,1.05f,1.02f,.94f,1,.98f,1.04f,1};
    p.furRegions[SURFACE_REGION_DIGIT]=(FurRegionProfile){.18f,.80f,.60f,1,.55f,.36f,.42f,.62f};
    SurfacePhenotype_Normalize(&p); return p;
}
SurfacePhenotype SurfacePreset_FelineSleekCoat(uint32_t seed,float maturity) {
    float t=isfinite(maturity)?fmaxf(0,fminf(1,maturity)):1;
    SurfacePhenotype p=SurfacePhenotype_Default(); uint32_t state=seed;
    float warm=Random(&state);
    p.pigment.baseColor=Color_Lerp(Color_FromRGB(48,53,56),Color_FromRGB(65,59,55),warm);
    p.pigment.secondaryColor=Color_FromRGB(102,86,76);p.pigment.ventralColor=Color_FromRGB(210,197,171);
    p.pigment.dorsalDarkening=.30f;p.pigment.ventralLightening=.50f;p.pigment.patternStrength=.16f;
    p.pigment.patternScale=.85f;p.pigment.bands=.32f;p.pigment.seed=Hash(seed^0x46454c31u);
    p.integument.type=INTEGUMENT_FUR;p.integument.coverage=.30f+.70f*t;
    FurPhenotype* f=&p.integument.fur;
    f->length=.022f+.014f*t;f->density=1.12f+.18f*Random(&state);f->thickness=.46f;
    f->lay=.88f;f->stiffness=.88f;f->clumping=.10f+.06f*Random(&state);f->irregularity=.16f;
    f->undercoat=.42f+.16f*t;f->undercoatLength=.48f;f->guardDensity=.96f;
    f->roughness=.28f;f->sheen=.48f;f->rootDarkening=.22f;f->tipLightening=.08f;
    f->seed=Hash(seed^0x46555246u);
    p.furRegions[SURFACE_REGION_HEAD]=(FurRegionProfile){.20f,1.04f,.72f,.92f,.46f,.86f,.16f,.52f};
    p.furRegions[SURFACE_REGION_NECK]=(FurRegionProfile){.82f,1.06f,.82f,.96f,.66f,1.02f,.24f,.84f};
    p.furRegions[SURFACE_REGION_DORSAL_TRUNK]=(FurRegionProfile){1.0f,1.08f,.88f,.96f,.58f,1.04f,.18f,.82f};
    p.furRegions[SURFACE_REGION_VENTRAL_TRUNK]=(FurRegionProfile){.72f,1.02f,.78f,1.02f,.52f,.82f,.14f,.76f};
    p.furRegions[SURFACE_REGION_FORELIMB]=(FurRegionProfile){.62f,1.04f,.76f,1,.48f,.90f,.14f,.76f};
    p.furRegions[SURFACE_REGION_HINDLIMB]=(FurRegionProfile){.68f,1.08f,.80f,1,.50f,.96f,.16f,.78f};
    p.furRegions[SURFACE_REGION_TAIL]=(FurRegionProfile){.92f,1.10f,.82f,.98f,.54f,1.10f,.16f,.86f};
    p.furRegions[SURFACE_REGION_DIGIT]=(FurRegionProfile){.14f,.72f,.52f,1,.32f,.36f,.10f,.56f};
    SurfacePhenotype_Normalize(&p); return p;
}
