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
    p.integument.fur=(FurPhenotype){.length=.14f,.density=1.0f,.thickness=.85f,.lay=.75f,
        .stiffness=.70f,.clumping=.25f,.irregularity=.20f,.undercoat=.80f,.undercoatLength=.55f,
        .guardDensity=.65f,.roughness=.55f,.sheen=.35f,.rootDarkening=.25f,.tipLightening=.15f,.seed=3};
    for(int i=0;i<SURFACE_REGION_COUNT;++i)p.regions[i]=(SurfaceRegionProfile){1,1,1,1,1,1,1,0};
    for(int i=0;i<SURFACE_REGION_COUNT;++i)p.furRegions[i]=(FurRegionProfile){1,1,1,1,1,1,1,1};
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
    CLAMP(integument.fur.length,.005f,2.0f,.14f); CLAMP(integument.fur.density,.05f,4.0f,1.0f);
    CLAMP(integument.fur.thickness,.05f,3.0f,.85f); CLAMP(integument.fur.lay,0.0f,1.0f,.75f);
    CLAMP(integument.fur.stiffness,0.0f,1.0f,.70f); CLAMP(integument.fur.clumping,0.0f,1.0f,.25f);
    CLAMP(integument.fur.irregularity,0.0f,1.0f,.20f); CLAMP(integument.fur.undercoat,0.0f,1.0f,.80f);
    CLAMP(integument.fur.undercoatLength,.10f,1.0f,.55f); CLAMP(integument.fur.guardDensity,0.0f,1.0f,.65f);
    CLAMP(integument.fur.roughness,.05f,1.0f,.55f); CLAMP(integument.fur.sheen,0.0f,1.0f,.35f);
    CLAMP(integument.fur.rootDarkening,0.0f,1.0f,.25f); CLAMP(integument.fur.tipLightening,0.0f,1.0f,.15f);
    for(int i=0;i<SURFACE_REGION_COUNT;++i) {
        CLAMP(furRegions[i].lengthMultiplier,.02f,4,1);CLAMP(furRegions[i].densityMultiplier,0,4,1);
        CLAMP(furRegions[i].thicknessMultiplier,.05f,3,1);CLAMP(furRegions[i].layMultiplier,0,2,1);
        CLAMP(furRegions[i].undercoatMultiplier,0,2,1);CLAMP(furRegions[i].guardMultiplier,0,2,1);
        CLAMP(furRegions[i].clumpMultiplier,0,2,1);CLAMP(furRegions[i].variationMultiplier,0,2,1);
        CLAMP(regions[i].size,.2f,4,1); CLAMP(regions[i].aspect,.4f,2.5f,1);
        CLAMP(regions[i].relief,0,2,1); CLAMP(regions[i].keel,0,2,1);
        CLAMP(regions[i].roughness,.3f,2,1); CLAMP(regions[i].irregularity,0,2,1);
        CLAMP(regions[i].roundness,0,2,1); p->regions[i].reserved=0;
    }
    CLAMP(integument.plates.size,.02f,2.0f,.25f);
    CLAMP(integument.plates.aspect,.2f,4.0f,1.0f);
    CLAMP(integument.plates.thickness,0.0f,0.2f,0.04f);
    CLAMP(integument.plates.bevel,0.0f,1.0f,0.35f);
    CLAMP(integument.plates.overlap,0.0f,1.0f,0.25f);
    CLAMP(integument.plates.irregularity,0.0f,1.0f,0.25f);
    CLAMP(integument.plates.relief,0.0f,0.1f,0.035f);
    if(p->pigment.layerCount>SURFACE_MAX_PIGMENT_LAYERS)p->pigment.layerCount=SURFACE_MAX_PIGMENT_LAYERS;
    for(size_t i=0;i<p->pigment.layerCount;++i) {
        PigmentLayer* l=&p->pigment.layers[i];
        if(l->pattern>PIGMENT_PATTERN_GRADIENT)l->pattern=PIGMENT_PATTERN_SOLID;
        l->strength=Limit(l->strength,0,1,1);
        l->scale=Limit(l->scale,.01f,50,1);
        l->sharpness=Limit(l->sharpness,0,1,.5f);
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
    MIX(integument.plates.size); MIX(integument.plates.aspect); MIX(integument.plates.thickness);
    MIX(integument.plates.bevel); MIX(integument.plates.overlap); MIX(integument.plates.irregularity);
    MIX(integument.plates.relief);
    MIX(integument.fur.length); MIX(integument.fur.density); MIX(integument.fur.thickness);
    MIX(integument.fur.lay); MIX(integument.fur.stiffness); MIX(integument.fur.clumping);
    MIX(integument.fur.irregularity); MIX(integument.fur.undercoat); MIX(integument.fur.undercoatLength);
    MIX(integument.fur.guardDensity); MIX(integument.fur.roughness); MIX(integument.fur.sheen);
    MIX(integument.fur.rootDarkening); MIX(integument.fur.tipLightening);
    for(int i=0;i<SURFACE_REGION_COUNT;++i) {
        MIX(furRegions[i].lengthMultiplier);MIX(furRegions[i].densityMultiplier);MIX(furRegions[i].thicknessMultiplier);MIX(furRegions[i].layMultiplier);
        MIX(furRegions[i].undercoatMultiplier);MIX(furRegions[i].guardMultiplier);MIX(furRegions[i].clumpMultiplier);MIX(furRegions[i].variationMultiplier);
        MIX(regions[i].size); MIX(regions[i].aspect); MIX(regions[i].relief); MIX(regions[i].keel);
        MIX(regions[i].roughness); MIX(regions[i].irregularity); MIX(regions[i].roundness);
    }
#undef MIX
    if(t>=.5f) {
        p.pigment.layerCount=q.pigment.layerCount;
        for(size_t i=0;i<q.pigment.layerCount;++i)p.pigment.layers[i]=q.pigment.layers[i];
    }
    /* Piel, escamas, pelaje y placas comparten backend */
    if(p.integument.type!=q.integument.type &&
       p.integument.type<=INTEGUMENT_PLATES && q.integument.type<=INTEGUMENT_PLATES) {
        float ca=a && a->integument.type!=INTEGUMENT_SMOOTH_SKIN?a->integument.coverage:0;
        float cb=q.integument.type!=INTEGUMENT_SMOOTH_SKIN?q.integument.coverage:0;
        p.integument.coverage=ca+(cb-ca)*t;
    }
    if(t>=1) { p.integument.type=q.integument.type; p.pigment.seed=q.pigment.seed; p.integument.scales.seed=q.integument.scales.seed; p.integument.fur.seed=q.integument.fur.seed; }
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
        {p.integument.scales.roughness,p.integument.scales.microColorVariation,(p.integument.type==INTEGUMENT_SCALES||p.integument.type==INTEGUMENT_PLATES)?p.integument.coverage:0,p.pigment.bands},
        {(float)p.integument.type,p.integument.type==INTEGUMENT_PLATES?1.0f:0.0f,(float)p.pigment.layerCount,p.integument.plates.relief}};
    memcpy(r.data[3],rows,sizeof(rows));

    /* Filas 8..11: Parámetros del pelaje */
    float furRows[4][4]={
        {p.integument.fur.length,p.integument.fur.density,p.integument.fur.thickness,p.integument.fur.lay},
        {p.integument.fur.stiffness,p.integument.fur.clumping,p.integument.fur.irregularity,p.integument.fur.undercoat},
        {p.integument.fur.undercoatLength,p.integument.fur.guardDensity,p.integument.fur.roughness,p.integument.fur.sheen},
        {p.integument.fur.rootDarkening,p.integument.fur.tipLightening,1.0f,p.integument.type==INTEGUMENT_FUR?p.integument.coverage:0.0f}};
    memcpy(r.data[SURFACE_RECIPE_GLOBAL_ROWS],furRows,sizeof(furRows));

    /* Filas 12..33: Perfiles regionales */
    for(int i=0;i<SURFACE_REGION_COUNT;++i) {
        SurfaceRegionProfile s=p.regions[i];
        float rowsRegion[2][4]={{s.size,s.aspect,s.relief,s.keel},{s.roughness,s.irregularity,s.roundness,s.reserved}};
        if(p.integument.type==INTEGUMENT_FUR) {
            FurRegionProfile f=p.furRegions[i];
            float furRegion[2][4]={{f.lengthMultiplier,f.densityMultiplier,f.thicknessMultiplier,f.layMultiplier},
                {f.undercoatMultiplier,f.guardMultiplier,f.clumpMultiplier,f.variationMultiplier}};
            memcpy(rowsRegion,furRegion,sizeof(furRegion));
        }
        memcpy(r.data[SURFACE_RECIPE_GLOBAL_ROWS+SURFACE_RECIPE_FUR_ROWS+2*i],rowsRegion,sizeof(rowsRegion));
    }
    /* Filas 34..45: Capas de pigmento */
    size_t pigmentBase=SURFACE_RECIPE_GLOBAL_ROWS+SURFACE_RECIPE_FUR_ROWS+SURFACE_RECIPE_REGION_ROWS;
    for(size_t i=0;i<p.pigment.layerCount && i<SURFACE_MAX_PIGMENT_LAYERS;++i) {
        const PigmentLayer* l=&p.pigment.layers[i];
        size_t baseIdx=pigmentBase+3*i;
        PackColor(r.data[baseIdx],l->color);
        r.data[baseIdx][3]=l->strength;
        r.data[baseIdx+1][0]=(float)l->pattern;
        r.data[baseIdx+1][1]=l->scale;
        r.data[baseIdx+1][2]=l->sharpness;
        r.data[baseIdx+1][3]=(float)(l->seed&0xffff);
        r.data[baseIdx+2][0]=l->direction.x;
        r.data[baseIdx+2][1]=l->direction.y;
        r.data[baseIdx+2][2]=l->direction.z;
        r.data[baseIdx+2][3]=0.0f;
    }
    r.pigmentSeed=p.pigment.seed; r.scaleSeed=p.integument.scales.seed; r.furSeed=p.integument.fur.seed; return r;
}

SurfaceRecipe SurfaceRecipe_CompileScaled(const SurfacePhenotype* surface,float unitScale) {
    SurfaceRecipe r=SurfaceRecipe_Compile(surface);
    r.data[11][2]=isfinite(unitScale)&&unitScale>1e-6f?unitScale:1;
    return r;
}
