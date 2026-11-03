#include "Surface.h"
#include <math.h>
#include <string.h>
#include <stdbool.h>
static float Limit(float x,float lo,float hi,float fallback) {
    return isfinite(x)?fmaxf(lo,fminf(hi,x)):fallback;
}
static float RegionCoverage(const PigmentLayer* l,const SurfaceCoordinate* c) {
        bool ta=(int)c->region==3||(int)c->region==4,tb=(int)c->secondaryRegion==3||(int)c->secondaryRegion==4;
        float a=PigmentPattern_RegionWeight(l,(unsigned)c->region,(unsigned)c->region,0);
        float b=PigmentPattern_RegionWeight(l,(unsigned)c->secondaryRegion,(unsigned)c->secondaryRegion,0);
        float belly=PigmentPattern_RegionWeight(l,SURFACE_REGION_VENTRAL_TRUNK,SURFACE_REGION_VENTRAL_TRUNK,0);
        return (1-c->blend)*a*(ta?1-c->ventral:1)+c->blend*b*(tb?1-c->ventral:1)+
            c->ventral*((ta?1-c->blend:0)+(tb?c->blend:0))*belly;
}
static SurfaceLocalMaterial LocalMaterialBank(const PigmentPhenotype* p,const SurfaceCoordinate* c,float roughness) {
    SurfaceLocalMaterial result={1,roughness,0};
    for(size_t i=0;i<p->layerCount&&i<SURFACE_MAX_PIGMENT_LAYERS;++i) {
        const PigmentLayer* l=&p->layers[i];const PigmentDomain* d=&l->domain;
        if(d->radii.x<=0||d->radii.y<=0||d->radii.z<=0)continue;
        float region=RegionCoverage(l,c);
        float value=PigmentPattern_SampleSurface(l->pattern,c->position,l->scale,l->sharpness,l->direction,p->seed^(l->seed&0xffffu)^(uint32_t)(i*101));
        float weight=Limit(value*l->strength*region*PigmentDomain_Weight(d,c->position,l->seed),0,1,0)*d->materialStrength;
        result.furCoverage+=(d->furCoverage-result.furCoverage)*weight;
        result.roughness+=(d->roughness-result.roughness)*weight;
        result.skinWeight=1-(1-result.skinWeight)*(1-weight);
    }
    return result;
}
static float DomainMaskBank(const PigmentPhenotype* p,const SurfaceCoordinate* c) {
    float mask=0;
    for(size_t i=0;i<p->layerCount&&i<SURFACE_MAX_PIGMENT_LAYERS;++i) {
        const PigmentLayer* l=&p->layers[i];const PigmentDomain* d=&l->domain;
        if(d->radii.x>0&&d->radii.y>0&&d->radii.z>0)
            mask=fmaxf(mask,RegionCoverage(l,c)*PigmentDomain_Weight(d,c->position,l->seed));
    }
    return mask;
}
float Surface_EvaluatePigmentDomainMask(const SurfacePhenotype* p,const SurfaceCoordinate* c) {
    if(!p||!c)return 0;
    if(!p->transition.active)return DomainMaskBank(&p->pigment,c);
    float a=DomainMaskBank(&p->transition.from,c),b=DomainMaskBank(&p->transition.to,c);
    return a+(b-a)*p->transition.weight;
}
SurfaceLocalMaterial Surface_EvaluateLocalMaterial(const SurfacePhenotype* p,const SurfaceCoordinate* c,float roughness) {
    if(!p||!c)return (SurfaceLocalMaterial){1,roughness,0};
    if(!p->transition.active)return LocalMaterialBank(&p->pigment,c,roughness);
    SurfaceLocalMaterial a=LocalMaterialBank(&p->transition.from,c,roughness),b=LocalMaterialBank(&p->transition.to,c,roughness);
    float t=p->transition.weight;
    return (SurfaceLocalMaterial){a.furCoverage+(b.furCoverage-a.furCoverage)*t,a.roughness+(b.roughness-a.roughness)*t,a.skinWeight+(b.skinWeight-a.skinWeight)*t};
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
    if(p->tuftCount>SURFACE_MAX_FUR_TUFTS)p->tuftCount=SURFACE_MAX_FUR_TUFTS;
    if(p->longHairCount>LONG_HAIR_MAX_GROUPS)p->longHairCount=LONG_HAIR_MAX_GROUPS;
    for(size_t i=0;i<p->longHairCount;++i)LongHairGroup_Normalize(&p->longHair[i]);
    for(size_t i=0;i<p->tuftCount;++i) {
        FurTuftPhenotype* f=&p->tufts[i];
        if(f->region<0||f->region>=SURFACE_REGION_COUNT)f->region=SURFACE_REGION_UNKNOWN;
        float* coordinates[]={&f->center.x,&f->center.y,&f->center.z,&f->direction.x,&f->direction.y,&f->direction.z,&f->normal.x,&f->normal.y,&f->normal.z};
        for(unsigned j=0;j<9;++j)*coordinates[j]=Limit(*coordinates[j],-100,100,0);
        f->halfExtents.x=Limit(f->halfExtents.x,.001f,100,.1f);
        f->halfExtents.y=Limit(f->halfExtents.y,.001f,100,.1f);
        f->halfExtents.z=Limit(f->halfExtents.z,.001f,100,.1f);
        if(Vec3_LengthSq(f->direction)<.0001f)f->direction=Vec3_Create(0,-1,0);
        f->direction=Vec3_Normalize(f->direction);
        if(Vec3_LengthSq(f->normal)<.0001f)f->normal=Vec3_Create(0,1,0);
        f->normal=Vec3_Normalize(f->normal);
        f->length=Limit(f->length,0,4,0);f->density=Limit(f->density,0,4,0);
        f->width=Limit(f->width,.0005f,.02f,.003f);f->minNormalDot=Limit(f->minNormalDot,-1,1,-1);
    }
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
        CLAMP(furRegions[i].lengthMultiplier,.02f,8,1);CLAMP(furRegions[i].densityMultiplier,0,4,1);
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
        l->regionMask&=(1u<<SURFACE_REGION_COUNT)-1;
        l->strength=Limit(l->strength,0,1,1);
        l->scale=Limit(l->scale,.01f,50,1);
        l->sharpness=Limit(l->sharpness,0,1,.5f);
        PigmentDomain* d=&l->domain;
        float* center[]={&d->center.x,&d->center.y,&d->center.z};
        float* radii[]={&d->radii.x,&d->radii.y,&d->radii.z};
        for(unsigned k=0;k<3;++k){*center[k]=Limit(*center[k],-100,100,0);*radii[k]=Limit(*radii[k],0,100,0);}
        d->softness=Limit(d->softness,.001f,1,.3f);d->irregularity=Limit(d->irregularity,0,1,0);
        d->materialStrength=Limit(d->materialStrength,0,1,0);
        d->furCoverage=Limit(d->furCoverage,0,1,0);d->roughness=Limit(d->roughness,.05f,1,.5f);
    }
#undef CLAMP
}
SurfacePhenotype SurfacePhenotype_Interpolate(const SurfacePhenotype* a,const SurfacePhenotype* b,float t) {
    SurfacePhenotype p=a?*a:SurfacePhenotype_Default(),q=b?*b:SurfacePhenotype_Default();
    SurfacePhenotype_Normalize(&p); SurfacePhenotype_Normalize(&q); t=Limit(t,0,1,0);
    if(t<=0)return p;
    if(t>=1)return q;
    PigmentPhenotype sourcePigment=p.pigment;
    float sourceCoverage=(p.integument.type==INTEGUMENT_SCALES||p.integument.type==INTEGUMENT_PLATES)?p.integument.coverage:0;
    float targetCoverage=(q.integument.type==INTEGUMENT_SCALES||q.integument.type==INTEGUMENT_PLATES)?q.integument.coverage:0;
    p.transition.from=sourcePigment;p.transition.to=q.pigment;
    p.transition.weight=t;p.transition.active=true;
    p.transition.plateWeight=(p.integument.type==INTEGUMENT_PLATES?1-t:0)+(q.integument.type==INTEGUMENT_PLATES?t:0);
    p.transition.scaleCoverage=sourceCoverage+(targetCoverage-sourceCoverage)*t;
    p.transition.targetScaleSeed=q.integument.scales.seed;
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
    if(p.tuftCount==q.tuftCount)for(size_t i=0;i<p.tuftCount;++i) {
        MIX(tufts[i].center.x);MIX(tufts[i].center.y);MIX(tufts[i].center.z);
        MIX(tufts[i].halfExtents.x);MIX(tufts[i].halfExtents.y);MIX(tufts[i].halfExtents.z);
        MIX(tufts[i].direction.x);MIX(tufts[i].direction.y);MIX(tufts[i].direction.z);
        MIX(tufts[i].normal.x);MIX(tufts[i].normal.y);MIX(tufts[i].normal.z);
        MIX(tufts[i].length);MIX(tufts[i].density);MIX(tufts[i].width);MIX(tufts[i].minNormalDot);
        p.tufts[i].color=Color_Lerp(p.tufts[i].color,q.tufts[i].color,t);
    }
    else {
        /* Sin capacidad para ambos bancos, retirada e introducción continuas. */
        if(t>=.5f){p.tuftCount=q.tuftCount;memcpy(p.tufts,q.tufts,sizeof(p.tufts));}
        for(size_t i=0;i<p.tuftCount;++i)p.tufts[i].density*=t<.5f?1-2*t:2*t-1;
    }
    bool sameHair=p.longHairCount==q.longHairCount;
    for(size_t i=0;i<p.longHairCount&&sameHair;++i){const LongHairDomain *a=&p.longHair[i].domain,*b=&q.longHair[i].domain;
        sameHair=p.longHair[i].id==q.longHair[i].id&&p.longHair[i].seed==q.longHair[i].seed&&a->kind==b->kind&&
            a->hostModuleId==b->hostModuleId&&a->region==b->region&&a->anchorCount==b->anchorCount&&
            p.longHair[i].guideCount==q.longHair[i].guideCount&&p.longHair[i].childrenPerGuide==q.longHair[i].childrenPerGuide&&
            Vec3_Distance(a->normal,b->normal)<.00001f&&Vec3_Dot(p.longHair[i].direction,q.longHair[i].direction)>-.95f;
        for(size_t j=0;j<a->anchorCount&&sameHair;++j)sameHair=a->anchors[j]==b->anchors[j];}
    if(sameHair)for(size_t i=0;i<p.longHairCount;++i){
        MIX(longHair[i].length);MIX(longHair[i].lengthVariation);MIX(longHair[i].width);MIX(longHair[i].rootSpread);
        MIX(longHair[i].clumping);MIX(longHair[i].stiffness);MIX(longHair[i].gravityInfluence);MIX(longHair[i].density);
        MIX(longHair[i].direction.x);MIX(longHair[i].direction.y);MIX(longHair[i].direction.z);
        MIX(longHair[i].domain.offset.x);MIX(longHair[i].domain.offset.y);MIX(longHair[i].domain.offset.z);
        MIX(longHair[i].domain.angle);MIX(longHair[i].domain.angularSpread);MIX(longHair[i].domain.start);MIX(longHair[i].domain.end);MIX(longHair[i].domain.patchRadius);
        p.longHair[i].rootColor=Color_Lerp(p.longHair[i].rootColor,q.longHair[i].rootColor,t);
        p.longHair[i].tipColor=Color_Lerp(p.longHair[i].tipColor,q.longHair[i].tipColor,t);
    }else{
        if(t>=.5f){p.longHairCount=q.longHairCount;memcpy(p.longHair,q.longHair,sizeof(p.longHair));}
        for(size_t i=0;i<p.longHairCount;++i)p.longHair[i].density*=t<.5f?1-2*t:2*t-1;
    }
#undef MIX
    /* Los patrones discretos no se interpolan como números: se mezclan sus
     * contribuciones. Las semillas de cada capa permanecen inmutables. */
    PigmentPhenotype origin=p.pigment;
    PigmentLayer layers[2*SURFACE_MAX_PIGMENT_LAYERS];size_t count=0;
    bool used[SURFACE_MAX_PIGMENT_LAYERS]={0};
    for(size_t i=0;i<origin.layerCount;++i) {
        PigmentLayer layer=origin.layers[i];size_t match=q.pigment.layerCount;
        for(size_t j=0;j<q.pigment.layerCount;++j)
            if(!used[j]&&layer.pattern==q.pigment.layers[j].pattern&&layer.seed==q.pigment.layers[j].seed&&layer.regionMask==q.pigment.layers[j].regionMask){match=j;break;}
        if(match<q.pigment.layerCount) {
            PigmentLayer dest=q.pigment.layers[match];used[match]=true;
            layer.color=Color_Lerp(layer.color,dest.color,t);
            layer.strength+=(dest.strength-layer.strength)*t;
            layer.scale+=(dest.scale-layer.scale)*t;layer.sharpness+=(dest.sharpness-layer.sharpness)*t;
            layer.direction=Vec3_Lerp(layer.direction,dest.direction,t);
        } else layer.strength*=1-t;
        layers[count++]=layer;
    }
    for(size_t j=0;j<q.pigment.layerCount;++j)if(!used[j]) {
        PigmentLayer layer=q.pigment.layers[j];layer.strength*=t;layers[count++]=layer;
    }
    if(t>=1) {
        p.pigment.layerCount=q.pigment.layerCount;
        memcpy(p.pigment.layers,q.pigment.layers,q.pigment.layerCount*sizeof(PigmentLayer));
    } else if(t>0 && count<=SURFACE_MAX_PIGMENT_LAYERS) {
        p.pigment.layerCount=count;memcpy(p.pigment.layers,layers,count*sizeof(PigmentLayer));
    } else if(t>0) {
        /* Capacidad fija: retirar una familia antes de introducir la otra.
         * En el punto de cambio ambas contribuciones valen exactamente cero. */
        bool target=t>=.5f;
        p.pigment.layerCount=target?q.pigment.layerCount:origin.layerCount;
        float strength=target?2*t-1:1-2*t;
        for(size_t i=0;i<p.pigment.layerCount;++i) {
            p.pigment.layers[i]=target?q.pigment.layers[i]:origin.layers[i];
            p.pigment.layers[i].strength*=strength;
        }
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
void SurfacePhenotype_CopyAnchoredDomains(SurfacePhenotype* out,const SurfacePhenotype* source) {
    if(!out||!source)return;
    PigmentPhenotype* dst[]={&out->pigment,&out->transition.from,&out->transition.to};
    const PigmentPhenotype* src[]={&source->pigment,&source->transition.from,&source->transition.to};
    for(unsigned bank=0;bank<3;++bank)for(size_t i=0;i<dst[bank]->layerCount&&i<SURFACE_MAX_PIGMENT_LAYERS;++i)
        if(i<src[bank]->layerCount&&src[bank]->layers[i].domain.anchorNode&&
           dst[bank]->layers[i].pattern==src[bank]->layers[i].pattern&&dst[bank]->layers[i].seed==src[bank]->layers[i].seed)
            dst[bank]->layers[i].domain=src[bank]->layers[i].domain;
}
/* Cada banco conserva el índice original de capa, que participa en su ruido.
 * Separar bancos evita cambiar semilla al desaparecer una capa del origen. */
static void PackPigmentBank(float (*rows)[4],const PigmentPhenotype* p) {
    PackColor(rows[0],p->baseColor);PackColor(rows[1],p->secondaryColor);PackColor(rows[2],p->ventralColor);
    float params[4]={p->dorsalDarkening,p->ventralLightening,p->patternStrength,p->patternScale};memcpy(rows[3],params,sizeof(params));
    rows[4][0]=p->bands;rows[4][1]=(float)p->layerCount;
    rows[4][2]=(float)(p->seed&0xffffu);rows[4][3]=(float)(p->seed>>16);
    for(size_t i=0;i<p->layerCount&&i<SURFACE_MAX_PIGMENT_LAYERS;++i) {
        const PigmentLayer* l=&p->layers[i];size_t row=5+3*i;
        PackColor(rows[row],l->color);rows[row][3]=l->strength;
        rows[row+1][0]=(float)l->pattern;rows[row+1][1]=l->scale;rows[row+1][2]=l->sharpness;rows[row+1][3]=(float)(l->seed&0xffffu);
        rows[row+2][0]=l->direction.x;rows[row+2][1]=l->direction.y;rows[row+2][2]=l->direction.z;rows[row+2][3]=(float)l->regionMask;
    }
}
static void PackDomains(float (*rows)[4],const PigmentPhenotype* p) {
    for(size_t i=0;i<p->layerCount&&i<SURFACE_MAX_PIGMENT_LAYERS;++i) {
        const PigmentDomain* d=&p->layers[i].domain;
        float data[3][4]={{d->center.x,d->center.y,d->center.z,d->softness},
            {d->radii.x,d->radii.y,d->radii.z,d->irregularity},
            {d->materialStrength,d->furCoverage,d->roughness,(float)(p->layers[i].seed&1023u)}};
        memcpy(rows+3*i,data,sizeof(data));
    }
}
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
    if(p.transition.active) {
        r.data[7][1]=p.transition.plateWeight;
        r.data[6][2]=p.transition.scaleCoverage;
        size_t row=SURFACE_RECIPE_TRANSITION_BASE;
        r.data[row][0]=1;r.data[row][1]=p.transition.weight;
        r.data[row+1][0]=(float)(p.transition.targetScaleSeed&0xffffu);
        r.data[row+1][1]=(float)(p.transition.targetScaleSeed>>16);
        PackPigmentBank(r.data+row+2,&p.transition.from);
        PackPigmentBank(r.data+row+2+SURFACE_RECIPE_PIGMENT_BANK_ROWS,&p.transition.to);
    }

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
        r.data[baseIdx+2][3]=(float)l->regionMask;
    }
    for(size_t i=0;i<p.tuftCount;++i) {
        const FurTuftPhenotype* f=&p.tufts[i];size_t row=SURFACE_RECIPE_TUFT_BASE+5*i;
        float data[4][4]={{f->center.x,f->center.y,f->center.z,f->length},
            {f->halfExtents.x,f->halfExtents.y,f->halfExtents.z,f->density},
            {f->direction.x,f->direction.y,f->direction.z,f->width},
            {f->normal.x,f->normal.y,f->normal.z,f->minNormalDot}};
        memcpy(r.data[row],data,sizeof(data));PackColor(r.data[row+4],f->color);
        r.data[row+4][3]=(float)f->region;
    }
    r.data[SURFACE_RECIPE_DOMAIN_BASE-1][0]=(float)p.tuftCount;
    PackDomains(r.data+SURFACE_RECIPE_DOMAIN_BASE,&p.pigment);
    if(p.transition.active) {
        PackDomains(r.data+SURFACE_RECIPE_DOMAIN_BASE+SURFACE_RECIPE_DOMAIN_ROWS,&p.transition.from);
        PackDomains(r.data+SURFACE_RECIPE_DOMAIN_BASE+2*SURFACE_RECIPE_DOMAIN_ROWS,&p.transition.to);
    }
    r.pigmentSeed=p.pigment.seed; r.scaleSeed=p.integument.scales.seed; r.furSeed=p.integument.fur.seed; return r;
}

SurfaceRecipe SurfaceRecipe_CompileScaled(const SurfacePhenotype* surface,float unitScale) {
    SurfaceRecipe r=SurfaceRecipe_Compile(surface);
    r.data[11][2]=isfinite(unitScale)&&unitScale>1e-6f?unitScale:1;
    return r;
}
