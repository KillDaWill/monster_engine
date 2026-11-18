#include "Head.h"
#include "MathUtils.h"
#include <math.h>
#include <string.h>

bool Head_Supports(HeadArchetype a) {
    return a == HEAD_ARCHETYPE_LIZARD || a == HEAD_ARCHETYPE_CANID || a == HEAD_ARCHETYPE_AVIAN || a == HEAD_ARCHETYPE_BOVID || a == HEAD_ARCHETYPE_EQUINE || a == HEAD_ARCHETYPE_SUID || a == HEAD_ARCHETYPE_PROBOSCIDEAN;
}

static float Head_Random01(uint32_t* state) { *state=*state*1664525u+1013904223u; return (float)(*state>>8)/16777215.0f; }
static float Head_Vary(uint32_t* state,float value,float amplitude) { return value+(Head_Random01(state)*2.0f-1.0f)*amplitude; }

static HeadPhenotype HeadPhenotype_Base(HeadArchetype archetype) {
    HeadPhenotype p; memset(&p,0,sizeof(p)); p.archetype=archetype;
    p.skullWidth=.55f;p.skullHeight=.55f;p.skullLength=.55f;
    p.muzzleLength=.55f;p.muzzleWidth=.55f;p.muzzleTaper=.45f;
    p.rostrumDepth=.52f;p.rostrumDorsalSlope=.35f;p.temporalWidth=.45f;p.temporalDepth=.45f;
    p.eyeSize=.50f;p.eyeLaterality=.50f;p.eyeForwardness=.50f;
    p.eyeDorsality=.55f;p.eyeExposure=.25f;p.browProminence=.50f;p.snoutBluntness=.50f;
    p.jawLength=.55f;p.jawDepth=.50f;p.jawStrength=.50f;
    p.earConcavity=.65f;p.earBaseWidth=.55f;p.earThickness=.3f;p.earOutward=.15f;p.earForward=.10f;
    p.earLongitudinalCurve=.5f;p.earRootRoll=.42f;p.earTipRoundness=.3f;p.earFold=.5f;
    p.cephalicIndex=.5f;p.stopProminence=.45f;p.zygomaticWidth=.5f;p.masseterMass=.4f;
    p.noseScale=.50f;p.earSize=0;p.earPointiness=0;p.cheekMass=.45f;
    p.maxillaryWidth=.5f;p.mouthWidth=.5f;
    p.earRootFlare=.35f;p.earMarginBow=.4f;p.earMarginAsymmetry=.5f;
    p.beakLength=.50f;p.beakDepth=.45f;p.beakTaper=.65f;p.beakCurvature=.35f;
    p.nostrilPosition=.72f;p.orbitDepth=.42f;p.tympanumSize=0;
    p.cranialForm=HEAD_CRANIAL_STANDARD;p.muzzleForm=HEAD_MUZZLE_TAPERED;p.eyeLayout=HEAD_EYES_LATERAL;
    return p;
}
HeadPhenotype HeadPhenotype_LizardPreset(void) {
    HeadPhenotype p=HeadPhenotype_Base(HEAD_ARCHETYPE_LIZARD);
    p.skullWidth=.70f;p.skullHeight=.38f;p.skullLength=.62f;
    p.muzzleLength=.72f;p.muzzleWidth=.64f;p.muzzleTaper=.48f;
    p.rostrumDepth=.62f;p.rostrumDorsalSlope=.28f;p.temporalWidth=.72f;p.temporalDepth=.68f;
    p.eyeSize=.50f;p.eyeLaterality=.90f;p.eyeForwardness=.20f;
    p.eyeDorsality=.72f;p.eyeExposure=.18f;p.browProminence=.58f;p.snoutBluntness=.62f;
    p.jawLength=.78f;p.jawDepth=.38f;p.jawStrength=.54f;p.noseScale=.22f;p.cheekMass=.42f;
    p.nostrilPosition=.82f;p.orbitDepth=.34f;p.tympanumSize=.52f;return p;
}
HeadPhenotype HeadPhenotype_CanidPreset(void) {
    HeadPhenotype p=HeadPhenotype_Base(HEAD_ARCHETYPE_CANID);
    /* Cánido mesocéfalo: caja temporal moderada, hocico convergente y
     * pabellones erectos con altura independiente de anchura y espesor. */
    p.skullWidth=.42f;p.skullHeight=.62f;p.skullLength=.63f;
    p.muzzleLength=.54f;p.muzzleWidth=.50f;p.muzzleTaper=.47f;
    p.rostrumDepth=.66f;p.rostrumDorsalSlope=.38f;p.temporalWidth=.65f;p.temporalDepth=.58f;
    p.eyeSize=.44f;p.eyeLaterality=.32f;p.eyeForwardness=.62f;p.eyeDorsality=.34f;
    p.eyelidCoverage=.60f;p.eyeCompression=.68f;p.eyeExposure=.08f;p.browProminence=.52f;
    p.snoutBluntness=.32f;p.jawLength=.68f;p.jawDepth=.44f;p.jawStrength=.50f;
    p.noseScale=.52f;p.earSize=.54f;p.earBaseWidth=.80f;p.earThickness=.20f;
    p.earConcavity=.72f;p.earOutward=.25f;p.earForward=.15f;p.earPointiness=.50f;
    p.earLongitudinalCurve=.62f;p.earRootRoll=.55f;p.earTipRoundness=.68f;
    p.earRootFlare=.55f;p.earMarginBow=.80f;p.earMarginAsymmetry=.72f;
    p.cheekMass=.34f;p.maxillaryWidth=.36f;p.mouthWidth=.32f;
    p.zygomaticWidth=.60f;p.masseterMass=.48f;p.stopProminence=.70f;
    p.cephalicIndex=.50f;p.nostrilPosition=.86f;p.orbitDepth=.52f;
    p.eyeLayout=HEAD_EYES_FORWARD;
    p.cranialForm=HEAD_CRANIAL_STANDARD;p.muzzleForm=HEAD_MUZZLE_TAPERED;
    return p;
}
HeadPhenotype HeadPhenotype_BovinePreset(void) {
    HeadPhenotype p=HeadPhenotype_Base(HEAD_ARCHETYPE_BOVID);
    p.skullWidth=.80f;p.skullHeight=.72f;p.skullLength=.52f;
    p.muzzleLength=.64f;p.muzzleWidth=.80f;p.muzzleTaper=.16f;
    p.rostrumDepth=.78f;p.rostrumDorsalSlope=.48f;p.temporalWidth=.78f;p.temporalDepth=.72f;
    p.eyeSize=.42f;p.eyeLaterality=.52f;p.eyeForwardness=.34f;p.eyeDorsality=.46f;
    p.eyelidCoverage=.62f;p.eyeCompression=.60f;p.eyeExposure=.12f;p.browProminence=.48f;
    p.snoutBluntness=.94f;p.jawLength=.62f;p.jawDepth=.78f;p.jawStrength=.86f;
    p.noseScale=.72f;p.earSize=.34f;p.earBaseWidth=.76f;p.earThickness=.26f;
    p.earWidthRatio=1.7f;p.earHeightRatio=.50f;
    p.earConcavity=.62f;p.earOutward=.44f;p.earForward=.12f;p.earPointiness=.04f;
    p.earLongitudinalCurve=.34f;p.earRootRoll=.48f;p.earTipRoundness=.94f;
    p.earRootFlare=.70f;p.earMarginBow=.54f;p.earMarginAsymmetry=.50f;
    p.cheekMass=.72f;p.maxillaryWidth=.80f;p.mouthWidth=.76f;p.zygomaticWidth=.82f;
    p.masseterMass=.92f;p.stopProminence=.34f;p.cephalicIndex=.60f;p.nostrilPosition=.84f;
    p.orbitDepth=.46f;p.eyeLayout=HEAD_EYES_LATERAL;
    p.cranialForm=HEAD_CRANIAL_BLOCK;p.muzzleForm=HEAD_MUZZLE_BLUNT;
    return p;
}
HeadPhenotype HeadPhenotype_EquinePreset(void) {
    HeadPhenotype p=HeadPhenotype_Base(HEAD_ARCHETYPE_EQUINE);
    /* El rostro largo conserva un puente estrecho y vuelve a ensancharse
     * hacia labios y narinas; no deriva de una trufa de carnívoro escalada. */
    p.skullWidth=.42f;p.skullHeight=.72f;p.skullLength=.70f;
    p.muzzleLength=.86f;p.muzzleWidth=.72f;p.muzzleTaper=.25f;
    p.rostrumDepth=.74f;p.rostrumDorsalSlope=.70f;
    p.temporalWidth=.62f;p.temporalDepth=.68f;
    p.eyeSize=.36f;p.eyeLaterality=.56f;p.eyeForwardness=.12f;p.eyeDorsality=.50f;
    p.eyelidCoverage=.52f;p.eyeCompression=.48f;p.eyeExposure=.12f;p.browProminence=.42f;
    p.snoutBluntness=.82f;p.jawLength=.88f;p.jawDepth=.68f;p.jawStrength=.72f;
    p.noseScale=.68f;p.nostrilPosition=.86f;p.orbitDepth=.46f;
    p.earSize=.42f;p.earBaseWidth=.72f;p.earWidthRatio=1.25f;p.earHeightRatio=.75f;
    p.earThickness=.22f;p.earConcavity=.70f;p.earOutward=.18f;p.earForward=.16f;
    p.earPointiness=.66f;p.earTipRoundness=.40f;
    p.cheekMass=.70f;p.maxillaryWidth=.42f;p.mouthWidth=.70f;
    p.cranialForm=HEAD_CRANIAL_NARROW;p.muzzleForm=HEAD_MUZZLE_LONG;
    return p;
}
HeadPhenotype HeadPhenotype_SuidPreset(void) {
    HeadPhenotype p=HeadPhenotype_Base(HEAD_ARCHETYPE_SUID);
    p.skullWidth=.66f;p.skullHeight=.60f;p.skullLength=.53f;
    p.muzzleLength=.60f;p.muzzleWidth=.65f;p.muzzleTaper=.32f;
    p.rostrumDepth=.62f;p.rostrumDorsalSlope=.42f;
    p.eyeSize=.12f;p.eyeLaterality=.78f;p.eyeForwardness=.28f;
    p.eyeDorsality=.42f;p.eyelidCoverage=.40f;p.eyeCompression=.25f;
    p.snoutBluntness=.90f;p.noseScale=.70f;
    p.earSize=.65f;p.earWidthRatio=1.22f;p.earHeightRatio=1.30f;
    p.earOutward=.28f;p.earForward=.55f;p.earConcavity=.62f;
    p.cheekMass=.68f;p.maxillaryWidth=.68f;
    p.cranialForm=HEAD_CRANIAL_WEDGE;p.muzzleForm=HEAD_MUZZLE_BLUNT;
    p.nasalColor=Color_FromRGB(198,131,127);
    return p;
}
static HeadPhenotype HeadPhenotype_ProboscideanPreset(void) {
    HeadPhenotype p = HeadPhenotype_Base(HEAD_ARCHETYPE_PROBOSCIDEAN);
    p.skullWidth=.72f; p.skullHeight=.85f; p.skullLength=.55f;
    p.muzzleLength=.42f; p.muzzleWidth=.76f; p.maxillaryWidth=.75f;
    p.jawDepth=.56f; p.jawStrength=.80f; p.cheekMass=.75f;
    p.eyeSize=.13f; p.eyeDorsality=.26f; p.eyeForwardness=.40f;
    p.eyeExposure=.34f; p.eyelidCoverage=.46f; p.eyeCompression=.50f;
    p.earSize=.78f; p.earBaseWidth=.80f; p.earOutward=.48f;
    p.earHeightRatio=1; p.earWidthRatio=1; p.earThickness=.16f;
    p.earConcavity=.25f; p.earFold=.43f; p.earTipRoundness=.50f;
    return p;
}

HeadPhenotype HeadPhenotype_FelinePreset(void) {
    HeadPhenotype p=HeadPhenotype_Base(HEAD_ARCHETYPE_CANID);
    /* Cráneo felino equilibrado: bóveda ancha, cigomáticos firmes,
     * hocico corto/ancho de pantera y orejas triangulares compactas. */
    p.skullWidth=.55f;p.skullHeight=.58f;p.skullLength=.58f;
    p.muzzleLength=.27f;p.muzzleWidth=.56f;p.muzzleTaper=.36f;
    p.rostrumDepth=.62f;p.rostrumDorsalSlope=.34f;p.temporalWidth=.50f;p.temporalDepth=.54f;
    p.eyeSize=.29f;p.eyeLaterality=.06f;p.eyeForwardness=.92f;p.eyeDorsality=.32f;
    p.eyelidCoverage=.0f;p.eyeCompression=.10f;p.eyeExposure=.86f;p.browProminence=.30f;
    p.snoutBluntness=.82f;p.jawLength=.62f;p.jawDepth=.46f;p.jawStrength=.70f;
    p.noseScale=.28f;p.earSize=.29f;p.earBaseWidth=.62f;p.earWidthRatio=.88f;p.earHeightRatio=.76f;
    p.earThickness=.46f;p.earConcavity=0.0f;p.earOutward=.28f;p.earForward=.14f;
    p.earPointiness=.54f;p.earLongitudinalCurve=.20f;p.earRootRoll=.22f;p.earTipRoundness=.56f;
    p.earRootFlare=.42f;p.earMarginBow=.44f;p.earMarginAsymmetry=.52f;
    p.cheekMass=.50f;p.maxillaryWidth=.54f;p.mouthWidth=.40f;p.zygomaticWidth=.56f;
    p.masseterMass=.50f;p.stopProminence=.44f;p.cephalicIndex=.60f;p.nostrilPosition=.86f;
    p.orbitDepth=.28f;p.eyeLayout=HEAD_EYES_FORWARD;
    p.cranialForm=HEAD_CRANIAL_STANDARD;p.muzzleForm=HEAD_MUZZLE_BLUNT;
    return p;
}
HeadPhenotype HeadPhenotype_AvianPreset(void) {
    HeadPhenotype p=HeadPhenotype_Base(HEAD_ARCHETYPE_AVIAN);
    p.skullWidth=.54f;p.skullHeight=.68f;p.skullLength=.48f;p.muzzleLength=.25f;p.muzzleWidth=.32f;p.muzzleTaper=.82f;
    p.rostrumDepth=.45f;p.temporalWidth=.38f;p.temporalDepth=.42f;p.eyeSize=.68f;p.eyeLaterality=.48f;p.eyeForwardness=.58f;
    p.eyeDorsality=.64f;p.eyeExposure=.28f;p.browProminence=.32f;p.snoutBluntness=.28f;
    p.jawLength=.68f;p.jawDepth=.26f;p.jawStrength=.34f;p.noseScale=.18f;p.cheekMass=.20f;
    p.beakLength=.82f;p.beakDepth=.48f;p.beakTaper=.88f;p.beakCurvature=.52f;p.nostrilPosition=.43f;p.orbitDepth=.48f;return p;
}

void HeadPhenotype_Normalize(HeadPhenotype* p) {
    if(!p)return;
    if(!Head_Supports(p->archetype))
        p->archetype=HEAD_ARCHETYPE_LIZARD;
#define C(name) p->name=Math_Clamp01(isfinite(p->name)?p->name:.5f)
    C(skullWidth);C(skullHeight);C(skullLength);C(muzzleLength);C(muzzleWidth);C(muzzleTaper);
    C(rostrumDepth);C(rostrumDorsalSlope);C(temporalWidth);C(temporalDepth);
    C(eyelidCoverage);C(eyeSize);C(eyeLaterality);C(eyeForwardness);C(eyeDorsality);C(eyeExposure);C(eyeCompression);
    C(browProminence);C(snoutBluntness);C(jawLength);C(jawDepth);C(jawStrength);
    C(earConcavity);C(earBaseWidth);C(earThickness);C(earOutward);C(earForward);
    C(earLongitudinalCurve);C(earRootRoll);C(earTipRoundness);C(earFold);
    C(cephalicIndex);C(stopProminence);C(zygomaticWidth);C(masseterMass);
    C(maxillaryWidth);C(mouthWidth);C(earRootFlare);C(earMarginBow);C(earMarginAsymmetry);
    C(noseScale);C(earSize);C(earPointiness);C(cheekMass);C(beakLength);C(beakDepth);
    C(beakTaper);C(beakCurvature);C(nostrilPosition);C(orbitDepth);C(tympanumSize);
#undef C
    p->earWidthRatio=isfinite(p->earWidthRatio)&&p->earWidthRatio>0?Math_Clamp(p->earWidthRatio,.5f,3):1;
    p->earHeightRatio=isfinite(p->earHeightRatio)&&p->earHeightRatio>0?Math_Clamp(p->earHeightRatio,.3f,2):1;
    if (p->cranialForm < HEAD_CRANIAL_STANDARD || p->cranialForm > HEAD_CRANIAL_WEDGE)
        p->cranialForm = HEAD_CRANIAL_STANDARD;
    if (p->muzzleForm < HEAD_MUZZLE_TAPERED || p->muzzleForm > HEAD_MUZZLE_WEDGE)
        p->muzzleForm = HEAD_MUZZLE_TAPERED;
    if (p->eyeLayout < HEAD_EYES_LATERAL || p->eyeLayout > HEAD_EYES_LOW_LATERAL)
        p->eyeLayout = HEAD_EYES_LATERAL;
}
HeadPhenotype HeadPhenotype_RandomValid(HeadArchetype archetype,uint32_t seed) {
    HeadPhenotype p=archetype==HEAD_ARCHETYPE_PROBOSCIDEAN?HeadPhenotype_ProboscideanPreset():archetype==HEAD_ARCHETYPE_SUID?HeadPhenotype_SuidPreset():archetype==HEAD_ARCHETYPE_EQUINE?HeadPhenotype_EquinePreset():archetype==HEAD_ARCHETYPE_BOVID?HeadPhenotype_BovinePreset():archetype==HEAD_ARCHETYPE_CANID?HeadPhenotype_CanidPreset():archetype==HEAD_ARCHETYPE_AVIAN?HeadPhenotype_AvianPreset():HeadPhenotype_LizardPreset();uint32_t s=seed?seed:1u;
#define V(name,amount) p.name=Head_Vary(&s,p.name,amount)
    V(skullWidth,.18f);V(skullHeight,.16f);V(skullLength,.16f);V(muzzleLength,.16f);V(muzzleWidth,.14f);V(muzzleTaper,.16f);
    V(rostrumDepth,.16f);V(rostrumDorsalSlope,.12f);V(temporalWidth,.15f);V(temporalDepth,.15f);
    V(eyeSize,.14f);V(eyeLaterality,.10f);V(eyeForwardness,.10f);V(eyeDorsality,.12f);V(eyeExposure,.10f);V(eyeCompression,.12f);
    V(browProminence,.14f);V(snoutBluntness,.12f);V(jawLength,.14f);V(jawDepth,.13f);V(jawStrength,.16f);
    V(cephalicIndex,.14f);V(stopProminence,.12f);V(zygomaticWidth,.14f);V(masseterMass,.14f);
    V(maxillaryWidth,.14f);V(mouthWidth,.14f);V(earRootFlare,.14f);V(earMarginBow,.14f);V(earMarginAsymmetry,.12f);
    V(noseScale,.14f);V(earSize,.16f);V(earPointiness,.14f);V(cheekMass,.16f);V(beakLength,.16f);V(beakDepth,.14f);
    V(beakTaper,.12f);V(beakCurvature,.14f);V(nostrilPosition,.10f);V(orbitDepth,.10f);V(tympanumSize,.12f);
#undef V
    HeadPhenotype_Normalize(&p);return p;
}

static bool Head_VectorFinite(Vector3 v){return isfinite(v.x)&&isfinite(v.y)&&isfinite(v.z);}
static bool Head_Symmetric(Vector3 a,Vector3 b,float t){return fabsf(a.x+b.x)<=t&&fabsf(a.y-b.y)<=t&&fabsf(a.z-b.z)<=t;}
static bool Head_InEllipsoid(Vector3 p,Vector3 c,Vector3 r,float margin){Vector3 d=Vec3_Sub(p,c);float x=d.x/Math_Max(r.x,1e-4f),y=d.y/Math_Max(r.y,1e-4f),z=d.z/Math_Max(r.z,1e-4f);return x*x+y*y+z*z<=margin*margin;}

/* Gram-Schmidt conserva la abertura cuando el eje longitudinal apunta
 * también hacia delante o atrás; cada pabellón posee su propio marco. */
bool HeadPinna_BuildFrame(Vector3 direction,Vector3 opening,Vector3* up,Vector3* side,Vector3* normal) {
    if(!up||!side||!normal||!Head_VectorFinite(direction)||!Head_VectorFinite(opening))return false;
    *up=Vec3_Normalize(direction);
    if(Vec3_LengthSq(*up)<1e-6f)return false;
    Vector3 projected=Vec3_Sub(opening,Vec3_Scale(*up,Vec3_Dot(opening,*up)));
    if(Vec3_LengthSq(projected)<1e-6f) {
        Vector3 fallback=fabsf(up->z)<.8f?Vec3_Create(0,0,1):Vec3_Create(1,0,0);
        projected=Vec3_Sub(fallback,Vec3_Scale(*up,Vec3_Dot(fallback,*up)));
    }
    *normal=Vec3_Normalize(projected);
    *side=Vec3_Normalize(Vec3_Cross(*up,*normal));
    return true;
}

static void Head_PinnaBounds(HeadSurfaceRecipe* r) {
    /* Máximos analíticos de flare, curvatura, cuenco y tapa redondeada. */
    float w=r->earShape.x,th=r->earShape.z;
    float boundWidth=w*(1.10f+.135f*r->earRootFlare)*(1+.32f*fabsf(r->earMarginAsymmetry-.5f))+
                     w*.69f*fabsf(r->earMarginBow)+th*.08f;
    float boundHeight=r->earShape.y*.5f+th*.08f;
    float boundDepth=fabsf(r->earLongitudinalCurve)+fabsf(r->earFold)+
                     th*(1.9f*r->earConcavity+1.22f+.32f*r->earRootRoll);
    Vector3 left=Vec3_Create(fabsf(r->leftEarSide.x)*boundWidth+fabsf(r->earDirection.x)*boundHeight+fabsf(r->leftEarNormal.x)*boundDepth,
        fabsf(r->leftEarSide.y)*boundWidth+fabsf(r->earDirection.y)*boundHeight+fabsf(r->leftEarNormal.y)*boundDepth,
        fabsf(r->leftEarSide.z)*boundWidth+fabsf(r->earDirection.z)*boundHeight+fabsf(r->leftEarNormal.z)*boundDepth);
    Vector3 right=Vec3_Create(fabsf(r->rightEarSide.x)*boundWidth+fabsf(r->rightEarDirection.x)*boundHeight+fabsf(r->rightEarNormal.x)*boundDepth,
        fabsf(r->rightEarSide.y)*boundWidth+fabsf(r->rightEarDirection.y)*boundHeight+fabsf(r->rightEarNormal.y)*boundDepth,
        fabsf(r->rightEarSide.z)*boundWidth+fabsf(r->rightEarDirection.z)*boundHeight+fabsf(r->rightEarNormal.z)*boundDepth);
    r->earRadii=Vec3_Create(Math_Max(left.x,right.x),Math_Max(left.y,right.y),Math_Max(left.z,right.z));
}

bool HeadSurfaceRecipe_SetEarPose(HeadSurfaceRecipe* r,bool right,Vector3 root,
                                  Vector3 direction,Vector3 opening) {
    if(!r||!Head_VectorFinite(root)||r->earShape.y<=0)return false;
    Vector3* up=right?&r->rightEarDirection:&r->earDirection;
    Vector3* side=right?&r->rightEarSide:&r->leftEarSide;
    Vector3* normal=right?&r->rightEarNormal:&r->leftEarNormal;
    if(!HeadPinna_BuildFrame(direction,opening,up,side,normal))return false;
    *(right?&r->rightEarCenter:&r->leftEarCenter)=Vec3_Add(root,Vec3_Scale(*up,r->earShape.y*.5f));
    Head_PinnaBounds(r);
    return true;
}

bool HeadAnatomy_Resolve(const HeadPhenotype* source,size_t attachment,Vector3 hostRadii,HeadAnatomy* a){
    if(!source||!a)return false;
    HeadPhenotype p=*source;HeadPhenotype_Normalize(&p);memset(a,0,sizeof(*a));a->archetype=p.archetype;a->attachmentBodyPartIndex=attachment;
    bool bovid=p.archetype==HEAD_ARCHETYPE_BOVID;
    bool equine=p.archetype==HEAD_ARCHETYPE_EQUINE;
    bool suid=p.archetype==HEAD_ARCHETYPE_SUID;
    bool proboscidean=p.archetype==HEAD_ARCHETYPE_PROBOSCIDEAN;
    bool feline=(p.archetype==HEAD_ARCHETYPE_CANID&&p.muzzleForm==HEAD_MUZZLE_BLUNT);
    bool grazing=bovid||equine;
    bool mammal=grazing||suid||proboscidean||p.archetype==HEAD_ARCHETYPE_CANID;
    float hx=Math_Max(fabsf(hostRadii.x),.08f),hy=Math_Max(fabsf(hostRadii.y),.08f),hz=Math_Max(fabsf(hostRadii.z),.08f);
    Vector3 skullR=Vec3_Create(hx*(.74f+.48f*p.skullWidth),hy*(.55f+.48f*p.skullHeight),hz*(.68f+.44f*p.skullLength));
    /* El perfil craneal modifica la relación entre caja posterior, altura y
     * longitud; después se aplican los controles continuos del fenotipo. */
    switch (p.cranialForm) {
    case HEAD_CRANIAL_SHIELD: skullR.x *= 1.04f; skullR.y *= 1.14f; skullR.z *= .92f; break;
    case HEAD_CRANIAL_BLOCK:  skullR.x *= 1.08f; skullR.y *= 1.18f; skullR.z *= .96f; break;
    case HEAD_CRANIAL_NARROW: skullR.x *= .82f; skullR.y *= .96f; skullR.z *= 1.10f; break;
    case HEAD_CRANIAL_WEDGE:  skullR.x *= 1.06f; skullR.y *= .90f; skullR.z *= .98f; break;
    default: break;
    }
    if(bovid)skullR.y*=.88f;
    float brachy=p.cephalicIndex-.5f;
    skullR.x*=1.0f+brachy*.34f;skullR.z*=1.0f-brachy*.22f;
    if(p.archetype==HEAD_ARCHETYPE_LIZARD){skullR.x*=.78f;skullR.y*=.62f;skullR.z*=.72f;}
    Vector3 skull=Vec3_Create(0,0,-hz*.10f);float faceLength=(p.archetype==HEAD_ARCHETYPE_AVIAN?p.beakLength:p.muzzleLength)*hz*1.20f+hz*.30f;
    if(p.archetype==HEAD_ARCHETYPE_CANID)faceLength*=1.0f-brachy*.34f;
    if(bovid)faceLength=hz*(.35f+1.25f*p.muzzleLength);
    else if (p.muzzleForm == HEAD_MUZZLE_BLUNT) faceLength *= .82f;
    else if (p.muzzleForm == HEAD_MUZZLE_LONG) faceLength *= 1.32f;
    else if (p.muzzleForm == HEAD_MUZZLE_WEDGE) faceLength *= .92f;
    float shapeSkullX=skullR.x;
    float shapeSkullZ=skullR.z;
    float rootWidth=shapeSkullX*(p.archetype==HEAD_ARCHETYPE_LIZARD?(.72f+.18f*p.muzzleWidth):(.30f+.52f*p.muzzleWidth));
    float faceDepth=skullR.y*(p.archetype==HEAD_ARCHETYPE_AVIAN?(.22f+.46f*p.beakDepth):(p.archetype==HEAD_ARCHETYPE_LIZARD?(.76f+.24f*p.rostrumDepth):(.42f+.48f*p.rostrumDepth)));
    if(p.archetype==HEAD_ARCHETYPE_CANID){rootWidth*=1.0f+brachy*.18f;faceDepth*=1.0f+brachy*.12f;}
    /* Una cuña facial estrecha la transición completa, no solo el extremo:
     * así el volumen posterior sigue siendo ancho y la cara cae hacia un
     * hocico fino sin conservar una sección rectangular. */
    if (p.muzzleForm == HEAD_MUZZLE_WEDGE) {
        rootWidth *= .78f;
        faceDepth *= .92f;
    }
    float taper=p.archetype==HEAD_ARCHETYPE_AVIAN?p.beakTaper:p.muzzleTaper;
    if (p.muzzleForm == HEAD_MUZZLE_BLUNT) taper *= .72f;
    else if (p.muzzleForm == HEAD_MUZZLE_LONG) taper = Math_Clamp01(taper * 1.25f);
    else if (p.muzzleForm == HEAD_MUZZLE_WEDGE) taper = Math_Clamp01(taper * 1.12f);
    float tipWidth=rootWidth*(.34f+.22f*(1-taper)+.08f*p.snoutBluntness);
    /* Perfiles distales distintos: el hocico romo conserva anchura hasta
     * la premaxila; el largo estrecha también su extremo nasal. */
    if (p.muzzleForm == HEAD_MUZZLE_BLUNT)
        tipWidth = rootWidth*(.66f+.16f*p.snoutBluntness);
    else if (p.muzzleForm == HEAD_MUZZLE_LONG)
        tipWidth = rootWidth*(.22f+.16f*(1-taper));
    Vector3 root=Vec3_Create(0,-skullR.y*.08f,skull.z+(p.archetype==HEAD_ARCHETYPE_LIZARD?shapeSkullZ*(p.cranialForm == HEAD_CRANIAL_SHIELD ? .50f : .24f):skullR.z*.38f));
    float profileDelta=(p.rostrumDorsalSlope-.5f)*faceDepth*1.10f;
    if(p.archetype==HEAD_ARCHETYPE_AVIAN)profileDelta-=p.beakCurvature*faceDepth*.24f;
    /* La cuña estrecha el hocico en planta sin hundir su techo hasta la
     * línea oral; el extremo conserva espesor dorsoventral propio. */
    if (p.muzzleForm == HEAD_MUZZLE_WEDGE) profileDelta = profileDelta*.45f - faceDepth*.02f;
    Vector3 tip=Vec3_Create(0,root.y+profileDelta,root.z+faceLength);
    Vector3 mid=Vec3_Lerp(root,tip,.52f);
    mid.y+=faceDepth*(.06f+.08f*(1-taper)+(p.archetype==HEAD_ARCHETYPE_CANID?.22f*(p.stopProminence-.5f):0.0f));
    float midWidth=Math_Lerp(rootWidth,tipWidth,.52f)*(.94f-.06f*taper);
    if (p.muzzleForm == HEAD_MUZZLE_BLUNT) midWidth = Math_Lerp(rootWidth,tipWidth,.30f);
    /* El puente y el planum son secciones independientes en rumiantes:
     * un único ahusamiento canino no puede ensanchar de nuevo el morro. */
    if(bovid) {
        rootWidth=skullR.x*(.56f+.18f*p.muzzleWidth+.08f*p.maxillaryWidth);
        faceDepth=skullR.y*(.78f+.24f*p.rostrumDepth);
        tipWidth=rootWidth*(.80f+.12f*p.snoutBluntness);
        midWidth=rootWidth*(.68f+.06f*(1-p.muzzleTaper));
        root=Vec3_Create(0,skullR.y*.18f,skull.z+skullR.z*.44f);
        tip=Vec3_Create(0,root.y-faceLength*(.35f+.65f*p.rostrumDorsalSlope),root.z+faceLength);
        mid=Vec3_Lerp(root,tip,.52f);mid.y+=faceDepth*.10f;
    }
    if(equine) {
        /* Cráneo y rostro equinos: frente ancha en órbitas respecto al rostro fino,
         * puente nasal estrecho y recto, cresta facial continua y morro equilibrado. */
        faceLength=hz*(.42f+.76f*p.muzzleLength);
        rootWidth=skullR.x*(.52f+.07f*p.muzzleWidth);
        faceDepth=skullR.y*(1.16f+.22f*p.rostrumDepth);
        tipWidth=rootWidth*(.59f+.07f*p.snoutBluntness);
        midWidth=tipWidth*(.88f+.04f*(1-p.muzzleTaper));
        root=Vec3_Create(0,skullR.y*.13f,skull.z+skullR.z*.38f);
        float drop=faceLength*(.74f+.36f*p.rostrumDorsalSlope);
        tip=Vec3_Create(0,root.y-drop,root.z+faceLength);
        mid=Vec3_Lerp(root,tip,.56f);
        mid.y+=faceDepth*((p.stopProminence-.5f)*.018f);
    }
    if(suid) {
        /* Cuña maxilar continua: frente alta, puente ligeramente cóncavo y
         * cartílago rostral ancho sin el ensanchamiento distal del bóvido. */
        faceLength=hz*(.50f+1.05f*p.muzzleLength);
        rootWidth=skullR.x*(.60f+.16f*p.maxillaryWidth);
        faceDepth=skullR.y*(.68f+.26f*p.rostrumDepth);
        tipWidth=rootWidth*(.62f+.16f*p.snoutBluntness);
        midWidth=Math_Lerp(rootWidth,tipWidth,.62f);
        root=Vec3_Create(0,skullR.y*.06f,skull.z+skullR.z*.36f);
        tip=Vec3_Create(0,root.y-faceLength*(.14f+.28f*p.rostrumDorsalSlope),root.z+faceLength);
        mid=Vec3_Lerp(root,tip,.54f);
        mid.y-=faceDepth*(.10f+.12f*p.stopProminence);
    }
    if (proboscidean) {
        /* Bóveda alta, pómulos anchos y maxilar corto bajo la raíz nasal. */
        faceLength=skullR.z*(.46f+.34f*p.muzzleLength);
        root=Vec3_Create(0,-skullR.y*.11f,skull.z+skullR.z*.36f);
        tip=Vec3_Add(root,Vec3_Create(0,-skullR.y*.36f,faceLength));
        mid=Vec3_Lerp(root,tip,.62f);
        rootWidth=skullR.x*(.74f+.15f*p.maxillaryWidth);
        midWidth=rootWidth*.87f;
        tipWidth=rootWidth*.63f;
        faceDepth=skullR.y*.66f;
    }
    float eyeAspect = 1.0f - .42f*p.eyeCompression;
    float orbitRadius=Math_Min(skullR.y,skullR.z)*(.25f+.24f*p.eyeSize);
    if(grazing)orbitRadius*=equine?.66f:.78f;
    if(suid)orbitRadius*=.67f;
    if(proboscidean)orbitRadius*=.37f;
    float sideFactor=p.archetype==HEAD_ARCHETYPE_LIZARD?(.72f+.08f*p.eyeLaterality):(.70f+.18f*p.eyeLaterality);
    if (p.eyeLayout == HEAD_EYES_FORWARD) sideFactor *= .82f;
    else if (p.eyeLayout == HEAD_EYES_HIGH_LATERAL) sideFactor *= 1.04f;
    else if (p.eyeLayout == HEAD_EYES_LOW_LATERAL) sideFactor *= 1.02f;
    if (p.cranialForm == HEAD_CRANIAL_SHIELD) sideFactor *= .91f;
    if (p.archetype == HEAD_ARCHETYPE_CANID) sideFactor *= .88f;
    if (feline) sideFactor *= .96f;
    if(bovid)sideFactor=.70f+.08f*p.eyeLaterality;
    if(equine)sideFactor=.505f+.040f*p.eyeLaterality;
    if(suid)sideFactor=.705f+.035f*(p.eyeLaterality-.5f);
    if(proboscidean)sideFactor=.81f;
    float side=skullR.x*sideFactor;
    float eyeZ=skull.z+skullR.z*(p.archetype==HEAD_ARCHETYPE_LIZARD?
        (-.12f+.24f*(p.eyeForwardness + (p.eyeLayout == HEAD_EYES_FORWARD ? .28f : 0.0f))):
        (.13f+.20f*p.eyeForwardness));
    float eyeY=skull.y+skullR.y*(.24f+.34f*p.eyeDorsality);
    if(equine){eyeZ=skull.z+skullR.z*(.22f+.14f*p.eyeForwardness);eyeY=skull.y+skullR.y*(.09f+.10f*p.eyeDorsality);}
    if(suid) {
        eyeZ=skull.z+skullR.z*(.04f+.18f*p.eyeForwardness);
        eyeY=skull.y+skullR.y*(.19f+.21f*p.eyeDorsality);
    }
    if (proboscidean) {
        eyeZ=skull.z+skullR.z*(.38f+.13f*p.eyeForwardness);
        eyeY=skull.y+skullR.y*(.05f+.18f*p.eyeDorsality);
    }
    if (p.eyeLayout == HEAD_EYES_HIGH_LATERAL) eyeY += skullR.y*.16f;
    else if (p.eyeLayout == HEAD_EYES_LOW_LATERAL) eyeY -= skullR.y*.14f;
    if (!equine && p.cranialForm == HEAD_CRANIAL_SHIELD) eyeY -= skullR.y*.06f;
    float eyeForward = p.eyeForwardness + (p.eyeLayout == HEAD_EYES_FORWARD ? .28f : 0.0f);
    Vector3 ln=Vec3_Normalize(Vec3_Create(1,.08f+.08f*p.eyeDorsality,.10f+(p.archetype==HEAD_ARCHETYPE_CANID?1.35f:.22f)*eyeForward)),rn=ln;rn.x*=-1;
    if(equine){ln=Vec3_Normalize(Vec3_Create(.76f,.06f+.05f*p.eyeDorsality,.64f+.12f*eyeForward));rn=ln;rn.x*=-1;}
    if (!equine && p.cranialForm == HEAD_CRANIAL_SHIELD) {
        ln = Vec3_Normalize(Vec3_Create(.82f, .45f, .35f)); rn = ln; rn.x *= -1;
    }
    float cornerX=rootWidth*(p.archetype==HEAD_ARCHETYPE_CANID?(.50f+.34f*p.mouthWidth):.88f);
    if(feline)cornerX=Math_Min(cornerX,tipWidth*.48f);
    float cornerZ=root.z+faceLength*(grazing?.74f:.18f);
    if(equine)cornerZ=root.z+faceLength*.78f;
    if(grazing){float muzzleSpan=equine?tipWidth:Math_Min(midWidth,bovid?tipWidth*.78f:tipWidth);cornerX=muzzleSpan*((bovid?.42f:(equine?.80f:.56f))+.06f*p.mouthWidth);}
    if(equine)cornerX=Math_Min(cornerX,rootWidth*.72f);
    if(suid){cornerZ=root.z+faceLength*.42f;cornerX=midWidth*.72f;}
    float oralSlope=grazing?(tip.y-faceDepth*.46f-root.y+faceDepth*.60f)/faceLength:0;
    if(suid)oralSlope=(tip.y-root.y)/faceLength;
    if(equine) {
        float lipRootY=root.y-faceDepth*.58f;
        float lipTipY=Math_Min(tip.y-faceDepth*(.27f+.03f*p.snoutBluntness),lipRootY-.12f*faceLength);
        oralSlope=(lipTipY-lipRootY)/faceLength;
    }
    if(proboscidean) {
        cornerZ=root.z+faceLength*.27f;
        cornerX=rootWidth*.68f;
        oralSlope=-.28f;
    }
    float jawSkullX=shapeSkullX;
    float jawSkullZ=shapeSkullZ;
    float hingeX=jawSkullX*(equine?(.54f+.06f*p.jawStrength):(.72f+.08f*p.jawStrength));
    if (!equine && p.muzzleForm == HEAD_MUZZLE_WEDGE) hingeX *= .88f;
    float jawEyeZ=skull.z+jawSkullZ*(.10f+.16f*p.eyeForwardness);
    float hingeZ=jawEyeZ-jawSkullZ*(.30f+.12f*p.jawLength),hingeY=Math_Min(-skullR.y*(.30f+.18f*p.jawDepth),-faceDepth*.50f);
    if(equine)hingeY=Math_Min(-skullR.y*(.20f+.08f*p.jawDepth),-faceDepth*.29f);
    if(proboscidean)hingeY=-skullR.y*.53f;
    float nt=Math_Clamp(p.nostrilPosition,.20f,.95f),nostrilZ=root.z+(tip.z-root.z)*nt;
    float localWidth=nt<=.52f?Math_Lerp(rootWidth,midWidth,nt/.52f):Math_Lerp(midWidth,tipWidth,(nt-.52f)/.48f);
    float rootHeight=faceDepth*.88f,midHeight=faceDepth*(.64f+.06f*(1-taper));
    float tipHeight=faceDepth*(.50f+.12f*p.snoutBluntness);
    if(grazing){rootHeight=faceDepth;midHeight=faceDepth*.98f;tipHeight=faceDepth*.72f;}
    if(equine){rootHeight=faceDepth*.92f;midHeight=faceDepth*.68f;tipHeight=faceDepth*.34f;}
    float localHeight=nt<=.52f?Math_Lerp(rootHeight,midHeight,nt/.52f):Math_Lerp(midHeight,tipHeight,(nt-.52f)/.48f);
    float profileY=nt<=.52f?Math_Lerp(root.y,mid.y,nt/.52f):Math_Lerp(mid.y,tip.y,(nt-.52f)/.48f);
    float tymR=orbitRadius*(.24f+.22f*p.tympanumSize);
    float neckAttachY=equine?-skullR.y*.16f:(mammal?-skullR.y*.28f:-skullR.y*.18f);
    HeadLandmarks* l=&a->landmarks;l->headId=0;l->skullCenter=skull;l->neckAttachment=Vec3_Create(0,neckAttachY,skull.z-skullR.z*(equine?.50f:.82f));
    l->leftOrbit=Vec3_Create(side,eyeY,eyeZ);l->rightOrbit=Vec3_Create(-side,eyeY,eyeZ);l->leftOrbitNormal=ln;l->rightOrbitNormal=rn;
    float eyeRadius=orbitRadius*(.74f+.08f*p.eyeSize);
    /* La disposición orbital también compone el tamaño aparente del globo:
     * una mirada frontal compacta, ojos altos más expuestos y ojos bajos
     * protegidos por el reborde. Se mantiene dentro de la copa orbital para
     * que cualquier combinación siga siendo una superficie estable. */
    if (p.eyeLayout == HEAD_EYES_FORWARD) eyeRadius *= .88f;
    else if (p.eyeLayout == HEAD_EYES_HIGH_LATERAL) eyeRadius *= 1.04f;
    else if (p.eyeLayout == HEAD_EYES_LOW_LATERAL) eyeRadius *= .92f;
    float eyeOffset=eyeRadius*(.03f+.15f*p.eyeExposure);
    l->leftEyeCenter=Vec3_Add(l->leftOrbit,Vec3_Scale(ln,eyeOffset));l->rightEyeCenter=Vec3_Add(l->rightOrbit,Vec3_Scale(rn,eyeOffset));
    l->leftMouthCorner=Vec3_Create(cornerX,grazing?root.y-faceDepth*(equine?.66f:.60f)+oralSlope*(cornerZ-root.z):-faceDepth*.42f,cornerZ);l->rightMouthCorner=l->leftMouthCorner;l->rightMouthCorner.x*=-1;
    if(suid){l->leftMouthCorner.y=root.y-faceDepth*.55f+oralSlope*(cornerZ-root.z);l->rightMouthCorner.y=l->leftMouthCorner.y;}
    if(proboscidean) {
        l->leftMouthCorner.y=-skullR.y*.55f;
        l->rightMouthCorner.y=l->leftMouthCorner.y;
        hingeY=-skullR.y*.59f;
    }
    l->leftJawHinge=Vec3_Create(hingeX,hingeY,hingeZ);l->rightJawHinge=l->leftJawHinge;l->rightJawHinge.x*=-1;
    l->muzzleRoot=root;l->muzzleTip=tip;l->noseTip=tip;l->leftNostril=Vec3_Create(localWidth*.74f,profileY+localHeight*.72f,nostrilZ);l->rightNostril=l->leftNostril;l->rightNostril.x*=-1;
    float tymY=eyeY-orbitRadius*.30f,tymZ=eyeZ-skullR.z*.28f;
    float tymNy=(tymY-skull.y)/skullR.y,tymNz=(tymZ-skull.z)/skullR.z;
    float tymSurfaceX=skullR.x*sqrtf(Math_Max(.05f,1.0f-tymNy*tymNy-tymNz*tymNz));
    l->leftTympanum=Vec3_Create(tymSurfaceX-tymR*.12f,tymY,tymZ);l->rightTympanum=l->leftTympanum;l->rightTympanum.x*=-1;
    l->mandibularSymphysis=Vec3_Create(0,tip.y-faceDepth*.62f,tip.z-faceLength*.04f);l->oralCavityCenter=Vec3_Create(0,root.y-faceDepth*.38f,root.z+faceLength*.44f);
    if(proboscidean) {
        l->mandibularSymphysis=Vec3_Create(0,-skullR.y*.76f,root.z+faceLength*.48f);
        l->oralCavityCenter=Vec3_Create(0,-skullR.y*.61f,root.z+faceLength*.20f);
    }
    float earLateral=bovid?.90f:feline?.50f+.06f*p.earBaseWidth:p.archetype==HEAD_ARCHETYPE_CANID?.60f+.08f*p.earBaseWidth:.58f;
    float earDorsal=bovid?.15f:feline?.56f:.40f;
    float earRear=bovid?.35f:feline?.22f:p.archetype==HEAD_ARCHETYPE_CANID?.20f:.18f;
    if(equine){earLateral=.24f;earDorsal=.23f;earRear=.18f;}
    if(proboscidean){earLateral=.82f;earDorsal=.58f;earRear=.16f;}
    l->leftEarBase=Vec3_Create(skullR.x*earLateral,skullR.y*earDorsal,skull.z-skullR.z*earRear);l->rightEarBase=l->leftEarBase;l->rightEarBase.x*=-1;l->upperBeakAnchor=root;l->lowerBeakAnchor=Vec3_Create(0,-faceDepth*.42f,root.z+faceLength*.12f);
    HeadSurfaceRecipe* r=&a->surface;r->oralSlope=oralSlope;r->nostrilCurve=bovid?.85f:0;r->craniumCenter=skull;r->craniumRadii=skullR;
    r->nasalColor=p.nasalColor.a?p.nasalColor:Color_FromRGB(30,27,24);
    r->faceRoot=root;r->faceMid=mid;r->faceTip=tip;
    r->faceRootRadii=Vec3_Create(rootWidth,rootHeight,Math_Min(faceLength*.16f,rootWidth*.34f));
    r->faceMidRadii=Vec3_Create(midWidth,midHeight,Math_Min(faceLength*.14f,midWidth*.38f));
    /* El espesor longitudinal de la punta no puede reutilizar el ancho
     * transversal: hacerlo convertía el hocico corto en un bloque grueso.
     * La profundidad queda ligada a la longitud local y al perfil elegido. */
    float tipDepth=Math_Min(faceLength*(.11f+.04f*p.snoutBluntness),
                            tipWidth*(.28f+.08f*p.snoutBluntness));
    if (p.muzzleForm == HEAD_MUZZLE_BLUNT) tipDepth *= 1.10f;
    else if (p.muzzleForm == HEAD_MUZZLE_LONG) tipDepth *= .90f;
    else if (p.muzzleForm == HEAD_MUZZLE_WEDGE) tipDepth *= .88f;
    r->faceTipRadii=Vec3_Create(tipWidth,tipHeight,tipDepth);
    r->faceRounding=Math_Min(faceDepth,midWidth)*(.14f+.10f*p.snoutBluntness);
    r->leftTemporalCenter=Vec3_Create(skullR.x*.43f,-skullR.y*.04f,skull.z-skullR.z*.20f);r->rightTemporalCenter=r->leftTemporalCenter;r->rightTemporalCenter.x*=-1;r->temporalRadii=Vec3_Create(skullR.x*(.25f+.13f*p.temporalWidth),skullR.y*(.36f+.16f*p.temporalDepth),skullR.z*.36f);
    float cheekLateral=p.archetype==HEAD_ARCHETYPE_CANID?.38f+.10f*p.zygomaticWidth:.50f;
    float cheekZ=p.archetype==HEAD_ARCHETYPE_CANID?skull.z-skullR.z*.08f:skull.z+skullR.z*.10f;
    r->leftCheekCenter=Vec3_Create(skullR.x*cheekLateral,-skullR.y*(p.archetype==HEAD_ARCHETYPE_CANID?(.22f+.10f*p.masseterMass):.27f),cheekZ);r->rightCheekCenter=r->leftCheekCenter;r->rightCheekCenter.x*=-1;
    if(p.archetype==HEAD_ARCHETYPE_CANID)r->cheekRadii=Vec3_Create(skullR.x*(.11f+.08f*p.cheekMass+.06f*p.zygomaticWidth),skullR.y*(.20f+.15f*p.cheekMass+.12f*p.masseterMass),skullR.z*(.19f+.04f*p.masseterMass));
    else r->cheekRadii=Vec3_Create(skullR.x*(.19f+.12f*p.cheekMass),skullR.y*(.22f+.16f*p.cheekMass),skullR.z*.28f);
    /* En cabezas barridas, el apoyo maxilar debe quedar sobre la línea oral;
     * situarlo debajo deja islotes al recortar la hendidura de la boca. */
    float maxillaryOffset=p.archetype==HEAD_ARCHETYPE_CANID?.34f+.22f*p.maxillaryWidth:.55f;
    float maxillaryHalf=p.archetype==HEAD_ARCHETYPE_CANID?.18f+.12f*p.maxillaryWidth:.28f;
    r->leftMaxillaryCenter=Vec3_Create(rootWidth*maxillaryOffset,root.y+faceDepth*(p.archetype!=HEAD_ARCHETYPE_AVIAN?.10f:-.30f),root.z+faceLength*.22f);r->rightMaxillaryCenter=r->leftMaxillaryCenter;r->rightMaxillaryCenter.x*=-1;r->maxillaryRadii=Vec3_Create(rootWidth*maxillaryHalf,faceDepth*.30f,faceLength*.22f);
    r->cranialPostorbitalRadii=Vec3_Create(skullR.x*(p.archetype==HEAD_ARCHETYPE_CANID?.74f+.06f*p.zygomaticWidth:.98f),skullR.y*.92f,skullR.z);
    r->cranialTemporalRadii=Vec3_Create(skullR.x*(p.archetype==HEAD_ARCHETYPE_CANID?.84f+.06f*p.zygomaticWidth:.88f),skullR.y*.88f,skullR.z);
    r->cranialOccipitalRadii=Vec3_Create(skullR.x*(p.archetype==HEAD_ARCHETYPE_CANID?.60f+.06f*p.jawStrength:.50f),skullR.y*.62f,skullR.z);
    if (p.cranialForm == HEAD_CRANIAL_SHIELD) {
        /* Los apoyos temporales y maxilares quedan dentro de la envolvente
         * superior: no deben dibujar pétalos bajo el barrido mandibular. */
        r->leftTemporalCenter.y = skullR.y*.22f;
        r->rightTemporalCenter.y = r->leftTemporalCenter.y;
        r->leftCheekCenter = Vec3_Create(skullR.x*.38f, skullR.y*.10f, skull.z);
        r->rightCheekCenter = r->leftCheekCenter; r->rightCheekCenter.x *= -1;
        r->cheekRadii.x *= .80f;
        r->leftMaxillaryCenter.x = rootWidth*.40f;
        r->leftMaxillaryCenter.y = root.y + faceDepth*.10f;
        r->rightMaxillaryCenter = r->leftMaxillaryCenter; r->rightMaxillaryCenter.x *= -1;
        r->maxillaryRadii.x = rootWidth*.22f;
    }
    /* El reborde orbital mamífero se integra al cráneo sin formar aros saltones. */
    float rimInset = bovid ? .28f : equine ? .22f : p.archetype == HEAD_ARCHETYPE_CANID ? .26f : suid ? .60f : mammal ? .10f : 1.10f;
    r->leftBrowCenter=Vec3_Add(Vec3_Sub(l->leftOrbit,Vec3_Scale(ln,orbitRadius*rimInset)),Vec3_Create(0,orbitRadius*eyeAspect*(.52f+.16f*p.browProminence),-orbitRadius*.08f));
    r->rightBrowCenter=Vec3_Add(Vec3_Sub(l->rightOrbit,Vec3_Scale(rn,orbitRadius*rimInset)),Vec3_Create(0,orbitRadius*eyeAspect*(.52f+.16f*p.browProminence),-orbitRadius*.08f));
    r->browRadii=Vec3_Create(orbitRadius*(.38f+.12f*p.browProminence),orbitRadius*(.23f+.15f*p.browProminence),orbitRadius*(.52f+.20f*p.browProminence));
    r->leftOrbitCenter=l->leftOrbit;r->rightOrbitCenter=l->rightOrbit;r->orbitRadii=Vec3_Create(orbitRadius,orbitRadius*.94f*eyeAspect,orbitRadius*.82f);r->leftOrbitRimCenter=Vec3_Sub(l->leftOrbit,Vec3_Scale(ln,orbitRadius*rimInset));r->rightOrbitRimCenter=Vec3_Sub(l->rightOrbit,Vec3_Scale(rn,orbitRadius*rimInset));r->orbitRimRadii=Vec3_Create(r->orbitRadii.x*1.15f,r->orbitRadii.y*1.15f,r->orbitRadii.z*1.12f);r->leftOrbitNormal=ln;r->rightOrbitNormal=rn;r->orbitSocketDepth=orbitRadius*(.52f+.16f*p.orbitDepth);r->eyelidCoverage=p.eyelidCoverage;r->eyelidProfile=bovid?Vec3_Create(1.10f,-1.0f,.44f):Vec3_Create(.33f,-.19f,.44f);
    /* La abertura palpebral puede ser menor que el globo que protege. */
    r->orbitRadii.y *= 1-.55f*p.eyelidCoverage;
    r->orbitRadii.z *= 1-.35f*p.eyelidCoverage;
    r->noseCenter=Vec3_Create(0,tip.y,tip.z-faceLength*.035f);r->noseRadii=Vec3_Create(tipWidth*(.40f+.18f*p.noseScale),faceDepth*.28f,tipWidth*.26f);r->leftNostrilCenter=l->leftNostril;r->rightNostrilCenter=l->rightNostril;r->nostrilRadii=Vec3_Create(localWidth*.15f,faceDepth*.17f,localWidth*.17f);
    if(proboscidean) {
        r->noseRadii=Vec3_Zero();
        r->nostrilRadii=Vec3_Zero();
        r->leftMaxillaryCenter=Vec3_Create(skullR.x*.57f,-skullR.y*.43f,root.z+faceLength*.42f);
        r->rightMaxillaryCenter=r->leftMaxillaryCenter; r->rightMaxillaryCenter.x*=-1;
        r->maxillaryRadii=Vec3_Create(skullR.x*.30f,skullR.y*.37f,skullR.z*.45f);
        r->leftCheekCenter=Vec3_Create(skullR.x*.52f,-skullR.y*.34f,skull.z+skullR.z*.09f);
        r->rightCheekCenter=r->leftCheekCenter; r->rightCheekCenter.x*=-1;
        r->cheekRadii=Vec3_Create(skullR.x*.32f,skullR.y*.43f,skullR.z*.59f);
    }
    if (feline) {
        /* Trufa felina compacta enrasada en la premaxila: más estrecha que el
         * cojinete labial y sin sobresalir delante del perfil nasal. */
        float noseW=tipWidth*(.28f+.10f*p.noseScale);
        float noseH=tipHeight*.30f;
        float noseZ=Math_Min(tipDepth*.75f,tipWidth*.18f);
        r->noseCenter=Vec3_Create(0,tip.y+faceDepth*.03f,tip.z+tipDepth*.48f);
        r->noseRadii=Vec3_Create(noseW,noseH,noseZ);
        l->leftNostril=Vec3_Create(noseW*.62f,r->noseCenter.y-noseH*.14f,r->noseCenter.z+noseZ*.42f);
        l->rightNostril=l->leftNostril;l->rightNostril.x*=-1;
        r->leftNostrilCenter=l->leftNostril;r->rightNostrilCenter=l->rightNostril;
        r->nostrilRadii=Vec3_Create(noseW*.24f,noseH*.32f,noseZ*.42f);
    } else if (p.archetype==HEAD_ARCHETYPE_CANID) {
        /* La trufa sobresale del premaxilar, pero no debe convertirse en un
         * segundo hocico bulboso al aumentar la anchura del perro. */
        float broad=Math_Clamp01((p.snoutBluntness-.85f)/.15f);
        r->noseCenter=Vec3_Add(tip,Vec3_Create(0,faceDepth*.08f,tipWidth*Math_Lerp(.38f,.08f,broad)));
        r->noseRadii=Vec3_Create(tipWidth*(.70f+.16f*p.noseScale),tipHeight*.56f,tipWidth*Math_Lerp(.38f,.18f,broad));
        /* Un morro plano sigue apoyado sobre la premaxila: reducir su
         * proyección sin moverlo al borde enterraría la almohadilla nasal. */
        float capDepth=Math_Min(r->faceTipRadii.x,r->faceTipRadii.y);
        r->noseCenter.z=Math_Lerp(r->noseCenter.z,tip.z+capDepth-r->noseRadii.z*.45f,broad);
        l->leftNostril=Vec3_Create(r->noseRadii.x*.70f,r->noseCenter.y+r->noseRadii.y*.10f,tip.z);
        l->rightNostril=l->leftNostril;l->rightNostril.x*=-1;
        l->leftNostril.z=Math_Lerp(l->leftNostril.z,r->noseCenter.z+r->noseRadii.z*.60f,broad);
        l->rightNostril.z=l->leftNostril.z;
        r->leftNostrilCenter=l->leftNostril;r->rightNostrilCenter=l->rightNostril;
        r->nostrilRadii=Vec3_Create(r->noseRadii.x*.20f,r->noseRadii.y*.25f,r->noseRadii.z*.30f);
        r->nostrilRadii=Vec3_Scale(r->nostrilRadii,.25f+1.5f*p.noseScale);
    }
    if(suid) {
        /* Disco integrado en el campo compartido; los cutters perforan la cara
         * anterior, no el puente dorsal. No hay malla decorativa separada. */
        r->faceCapWeight=1;r->faceCapDepth=tipDepth*.35f;
        r->faceTipRadii.y*=.82f;
        float depth=tipWidth*(.18f+.06f*p.noseScale);
        r->noseRadii=Vec3_Create(tipWidth*1.06f,tipHeight*.94f,depth);
        r->noseCenter=Vec3_Create(0,tip.y,tip.z+depth*.40f);
        l->leftNostril=Vec3_Add(r->noseCenter,Vec3_Create(r->noseRadii.x*.46f,
            r->noseRadii.y*.08f,depth*.86f));
        l->rightNostril=l->leftNostril;l->rightNostril.x*=-1;
        r->leftNostrilCenter=l->leftNostril;r->rightNostrilCenter=l->rightNostril;
        r->nostrilRadii=Vec3_Create(r->noseRadii.x*.18f,r->noseRadii.y*.29f,depth*.72f);
        /* El masetero ocupa el ángulo de la mandíbula detrás de la comisura. */
        r->leftCheekCenter=Vec3_Create(skullR.x*.42f,-skullR.y*.40f,skull.z+skullR.z*.21f);
        r->rightCheekCenter=r->leftCheekCenter;r->rightCheekCenter.x*=-1;
        r->cheekRadii=Vec3_Create(skullR.x*(.26f+.06f*p.cheekMass),
            skullR.y*(.38f+.10f*p.masseterMass),skullR.z*.57f);
    }
    if(bovid) {
        r->faceCapWeight=1;r->faceCapDepth=tipDepth*.65f;
        r->jawRearClipWeight=0;
        r->orbitSocketDepth=orbitRadius*.66f;
        r->orbitRimRadii.x=orbitRadius*.95f;
        r->orbitRimRadii.y*=1.18f;r->orbitRimRadii.z*=1.18f;
        /* Planum continuo con el premaxilar, no un disco adelantado. El radio
         * longitudinal solapa el labio y las narinas siguen su cara curva. */
        /* La premaxila queda dentro del planum; dos contornos del mismo ancho
         * cortados en el extremo dejaban un anillo aun sin pigmentación. */
        r->faceTipRadii.x*=.78f;r->faceTipRadii.y*=.70f;
        float lipTipY=tip.y-faceDepth*.46f;
        float noseH=tipHeight*(.54f+.08f*p.noseScale);
        float noseZ=Math_Min(tipWidth*(.26f+.05f*p.noseScale),Math_Max(tipDepth*1.20f,faceLength*.14f));
        r->noseRadii=Vec3_Create(tipWidth*(.98f+.08f*p.noseScale),noseH,noseZ);
        r->noseCenter=Vec3_Create(0,lipTipY+noseH*1.00f,tip.z+noseZ*.28f);
        /* El control de posición recorre el planum curvo manteniendo ambas
         * narinas sobre su superficie, en vez de perderse al resolver la trufa. */
        float nasalPosition=Math_Clamp01(p.nostrilPosition);
        l->leftNostril=Vec3_Add(r->noseCenter,Vec3_Create(r->noseRadii.x*(.76f-.05f*nasalPosition),
            r->noseRadii.y*.22f,r->noseRadii.z*(.54f+.10f*nasalPosition)));
        l->rightNostril=l->leftNostril;l->rightNostril.x*=-1;
        r->leftNostrilCenter=l->leftNostril;r->rightNostrilCenter=l->rightNostril;
        r->nostrilRadii=Vec3_Create(r->noseRadii.x*.22f,r->noseRadii.y*.42f,r->noseRadii.z*.68f);
        r->cranialPostorbitalRadii.x=skullR.x*.90f;
        r->cranialTemporalRadii.x=skullR.x*.96f;
        r->cranialOccipitalRadii.x=skullR.x*.74f;
        /* Ganacha y masetero caudales: sus controles no deben quedar ocultos
         * por el mismo radio genérico usado en la mejilla de un reptil. */
        r->leftCheekCenter=Vec3_Create(skullR.x*(.46f+.08f*p.zygomaticWidth),
            -skullR.y*.32f,skull.z-skullR.z*.04f);
        r->rightCheekCenter=r->leftCheekCenter;r->rightCheekCenter.x*=-1;
        r->cheekRadii=Vec3_Create(skullR.x*(.19f+.12f*p.cheekMass),
            skullR.y*(.26f+.18f*p.masseterMass),skullR.z*(.35f+.12f*p.jawStrength));
        r->maxillaryRadii.y=faceDepth*.24f;
        r->maxillaryRadii.x=rootWidth*(.20f+.06f*p.maxillaryWidth);
        r->leftMaxillaryCenter.x=rootWidth*(.36f+.08f*p.maxillaryWidth);r->rightMaxillaryCenter.x=-r->leftMaxillaryCenter.x;
        l->mandibularSymphysis.y=l->leftMouthCorner.y+oralSlope*(tip.z-cornerZ)-faceDepth*.24f;
        l->oralCavityCenter.y=l->leftMouthCorner.y+oralSlope*(l->oralCavityCenter.z-cornerZ);
    }
    if(equine) {
        r->faceTipRadii.z=Math_Max(r->faceTipRadii.z,Math_Min(tipWidth*.32f,r->faceMidRadii.y*.42f));
        /* Hocico distal romo y continuo: transición suave hacia alas nasales y
         * labio superior pleno hasta el plano oral sin hueco ni mordida dorsal. */
        float eqLipTipY=l->leftMouthCorner.y+oralSlope*(tip.z-cornerZ);
        float eqZ3=eyeZ+skullR.z*.04f;
        float eqTop3=skull.y+Math_Max(skullR.y*.52f,faceDepth*.34f);
        float eqBot3=l->leftMouthCorner.y+oralSlope*(eqZ3-cornerZ)-faceDepth*.32f;
        float eqW3=skullR.x*(.545f+.035f*p.zygomaticWidth);
        Vector3 eqPos2=Vec3_Lerp(root,tip,.38f);
        float eqW2=Math_Clamp(rootWidth*(.84f+.06f*p.maxillaryWidth),tipWidth*1.04f,eqW3*.88f);
        Vector3 eqPos1=Vec3_Lerp(root,tip,.68f);
        float eqW1=Math_Clamp(midWidth*(1.16f+.04f*(1-p.muzzleTaper)),tipWidth*.96f,eqW2*.90f);
        float eqEndTop=Math_Clamp(tip.y-faceDepth*.005f,eqLipTipY+faceDepth*.275f,eqLipTipY+faceDepth*.335f);
        float eqBot0=eqLipTipY-faceDepth*.045f;
        float eqTipY=Math_Max((eqEndTop-eqBot0)*.5f,faceDepth*.160f);
        float eqZ0=tip.z;
        float eqSpanZ=Math_Max(eqZ0-eqZ3,.05f);
        float eqU1=Math_Clamp01((eqPos1.z-eqZ3)/eqSpanZ);
        float eqTop1=Math_Lerp(eqTop3,eqEndTop,eqU1)+faceDepth*.006f;
        float eqLip1Y=l->leftMouthCorner.y+oralSlope*(eqPos1.z-cornerZ);
        float eqH1=Math_Max((eqTop1-(eqLip1Y+faceDepth*.002f))*.5f,faceDepth*.205f);
        float eqCy1=eqTop1-eqH1;
        float eqCy0=eqEndTop-eqTipY;
        float eqW0=Math_Clamp(tipWidth*.88f,eqW1*.94f,eqW2*.84f);
        float eqTipCap=Math_Min(eqW0,eqTipY);
        r->faceCapWeight=.18f;
        r->faceCapDepth=eqTipCap*.42f;
        r->nostrilCurve=.84f;
        r->cranialPostorbitalRadii.x=eqW3;
        r->cranialTemporalRadii.x=skullR.x*(.560f+.025f*p.cheekMass);
        r->cranialOccipitalRadii.x=skullR.x*.485f;
        /* Fosa temporal contenida y masetero caudal amplio y muscular sin bolas sueltas. */
        r->leftTemporalCenter=Vec3_Create(skullR.x*.18f,skullR.y*.06f,skull.z-skullR.z*.12f);
        r->rightTemporalCenter=r->leftTemporalCenter;r->rightTemporalCenter.x*=-1;
        r->temporalRadii=Vec3_Create(skullR.x*.16f,skullR.y*.20f,skullR.z*.26f);
        r->leftCheekCenter=Vec3_Create(skullR.x*(.28f+.03f*p.cheekMass),eyeY-skullR.y*.42f,eyeZ-skullR.z*.28f);
        r->rightCheekCenter=r->leftCheekCenter;r->rightCheekCenter.x*=-1;
        r->cheekRadii=Vec3_Create(skullR.x*(.22f+.03f*p.cheekMass),skullR.y*(.36f+.05f*p.masseterMass),skullR.z*.46f);
        float eqLip2Y=l->leftMouthCorner.y+oralSlope*(eqPos2.z-cornerZ);
        r->leftMaxillaryCenter=Vec3_Create(eqW2*.24f,eqLip2Y+faceDepth*.18f,eqPos2.z);
        r->rightMaxillaryCenter=r->leftMaxillaryCenter;r->rightMaxillaryCenter.x*=-1;
        r->maxillaryRadii=Vec3_Create(eqW2*.12f,faceDepth*(.11f+.02f*p.rostrumDepth),faceLength*.22f);
        /* Parámetros paramétricos de sección transversal equina (bisel dorsal nasal suave,
         * transición cigomática-maxilar continua sin depresiones laterales ni escalón bajo el ojo). */
        float bridgeEndZ=Math_Lerp(root.z,tip.z,.82f);
        float crestStartZ=eqZ3-skullR.z*.45f;
        float crestTuberosityZ=Math_Lerp(root.z,tip.z,.72f);
        float bevelAmp=.108f+.012f*(1-p.muzzleTaper);
        float cheekTuckAmp=0.0f;
        float crestAmp=.022f+.006f*p.maxillaryWidth+.006f*p.zygomaticWidth;
        float hollowAmp=.002f;
        r->equineSculptWeight=1.0f;
        r->equineNasalRoot=Vec3_Create(0,eqTop3,eqZ3);
        r->equineNasalTip=Vec3_Create(0,eqEndTop,eqZ0);
        r->equineNasalRoof=Vec3_Create(bevelAmp,cheekTuckAmp,bridgeEndZ);
        r->equineCrestStart=Vec3_Create(crestAmp,eqBot3,crestStartZ);
        r->equineCrestEnd=Vec3_Create(hollowAmp,eqBot0,crestTuberosityZ);
        r->equineCrestRadii=Vec3_Create(.11f,.80f,.72f);
        /* Ollares en coma oblicua abiertos limpiamente sobre el ala nasal (sin techo cutáneo encima). */
        r->nostrilRadii=Vec3_Create(tipWidth*(.20f+.03f*p.noseScale),eqH1*(.24f+.03f*p.noseScale),Math_Max(tipWidth*(.20f+.03f*p.noseScale),faceLength*.055f));
        float nosFrac=Math_Clamp(.58f+.10f*(p.nostrilPosition-.5f),.38f,.74f);
        float nosZ=Math_Lerp(eqPos1.z,eqZ0,nosFrac);
        float nosCy=Math_Lerp(eqCy1,eqCy0,nosFrac);
        float nosRx=Math_Lerp(eqW1,eqW0,nosFrac);
        float nosRy=Math_Lerp(eqH1,eqTipY,nosFrac);
        float relY=.24f;
        float surfaceX=nosRx*sqrtf(Math_Max(.05f,1.0f-relY*relY));
        l->leftNostril.x=surfaceX*.82f;
        l->leftNostril.y=nosCy+nosRy*relY;
        l->leftNostril.z=nosZ;
        l->rightNostril=l->leftNostril;l->rightNostril.x*=-1;
        r->leftNostrilCenter=l->leftNostril;r->rightNostrilCenter=l->rightNostril;
        /* Órbita encastrada con párpados ceñidos al globo y arcada supraorbital suave. */
        float eqEyeR=orbitRadius*(.75f+.08f*p.eyeSize);
        r->orbitRadii=Vec3_Create(eqEyeR*.92f,eqEyeR*eyeAspect*(.92f-.28f*p.eyelidCoverage),eqEyeR*(.88f-.12f*p.eyelidCoverage));
        r->leftOrbitRimCenter=Vec3_Sub(l->leftOrbit,Vec3_Scale(ln,orbitRadius*.22f));
        r->rightOrbitRimCenter=Vec3_Sub(l->rightOrbit,Vec3_Scale(rn,orbitRadius*.22f));
        r->orbitRimRadii=Vec3_Create(eqEyeR*.96f,eqEyeR*eyeAspect*1.02f,eqEyeR*1.02f);
        r->orbitSocketDepth=eqEyeR*(.38f+.10f*p.orbitDepth);
        r->leftBrowCenter=Vec3_Add(Vec3_Sub(l->leftOrbit,Vec3_Scale(ln,eqEyeR*.30f)),Vec3_Create(-eqEyeR*.22f,eqEyeR*eyeAspect*(.32f+.06f*p.browProminence),-eqEyeR*.04f));
        r->rightBrowCenter=r->leftBrowCenter;r->rightBrowCenter.x*=-1;
        r->browRadii=Vec3_Create(eqEyeR*(.56f+.05f*p.browProminence),eqEyeR*(.22f+.04f*p.browProminence),eqEyeR*(.72f+.06f*p.browProminence));
        r->eyelidProfile=Vec3_Create(.62f,-.42f,.22f);
        l->mandibularSymphysis.y=eqLipTipY-faceDepth*.12f;
        l->oralCavityCenter.z=root.z+faceLength*.74f;
        l->oralCavityCenter.y=l->leftMouthCorner.y+oralSlope*(l->oralCavityCenter.z-cornerZ);
    }
    r->leftTympanumCenter=l->leftTympanum;r->rightTympanumCenter=l->rightTympanum;r->tympanumRadii=Vec3_Create(tymR*.42f,tymR,tymR*.86f);r->tympanumDepth=tymR*.55f;
    float earWidthScale=mammal?.15f+.10f*p.earBaseWidth:.31f+.32f*p.earBaseWidth;
    earWidthScale*=p.earWidthRatio;
    float earHeightScale=mammal?.86f+1.22f*p.earSize:.60f+1.20f*p.earSize;
    earHeightScale*=p.earHeightRatio;
    r->earShape=Vec3_Create(skullR.x*earWidthScale,
        skullR.y*earHeightScale,skullR.z*(.045f+.07f*p.earThickness));
    r->earConcavity=p.earConcavity;
    r->earTipFraction=.06f+.18f*(1-p.earPointiness);
    r->earLongitudinalCurve=(p.earLongitudinalCurve-.5f)*r->earShape.z*1.4f;
    r->earRootRoll=p.earRootRoll;r->earTipRoundness=p.earTipRoundness;r->earFold=(p.earFold-.5f)*r->earShape.y*.62f;
    r->earRootFlare=p.earRootFlare;r->earMarginBow=p.earMarginBow;r->earMarginAsymmetry=p.earMarginAsymmetry;
    /* El extremo del control permite pabellones laterales casi horizontales;
     * el intervalo habitual de orejas erectas conserva su orientación. */
    float lateralEar=Math_Clamp01((p.earOutward-.6f)/.4f);
    lateralEar=lateralEar*lateralEar*(3-2*lateralEar);
    Vector3 leftUp=Vec3_Create(p.earOutward*.7f+lateralEar*1.1f,1-lateralEar*.75f,p.earForward*.7f);
    Vector3 rightUp=Vec3_Create(-leftUp.x,leftUp.y,leftUp.z);
    if(equine) {
        /* Ligera divergencia sagital y espesor mínimo robusto del pabellón
         * para evitar perforaciones o puntas flotantes al voxelizar/mutar. */
        leftUp.z+=.08f;rightUp.z+=.03f;
        r->earShape.y+=skullR.y*.16f;
        r->earShape.z=Math_Max(r->earShape.z*1.75f,Math_Max(r->earShape.x*.62f,r->earShape.y*.105f));
        r->earConcavity=0.0f;
        r->earRootRoll=Math_Max(r->earRootRoll,.95f);
        r->earLongitudinalCurve+=r->earShape.y*.045f;
    }
    Vector3 earOpeningLeft=Vec3_Create(0,0,1),earOpeningRight=earOpeningLeft;
    if(proboscidean) {
        /* Lámina pendular: eje oblicuo hacia abajo, con el borde libre
         * desplegado lateralmente y hacia la espalda. */
        r->earShape=Vec3_Create(skullR.x*(.66f+.24f*p.earBaseWidth)*p.earWidthRatio,
            skullR.y*(1.38f+1.03f*p.earSize)*p.earHeightRatio,skullR.z*(.039f+.055f*p.earThickness));
        r->earFold=-r->earShape.y*(.07f+.19f*(1-p.earFold));
        r->earLongitudinalCurve=r->earShape.y*(.045f+.09f*p.earLongitudinalCurve);
        r->earTipFraction=.13f;
        r->earMarginBow=.10f;
        r->earConcavity=0;
        leftUp=Vec3_Create(.30f+.40f*p.earOutward,-1,-.27f);
        rightUp=Vec3_Create(-leftUp.x,leftUp.y,leftUp.z);
        earOpeningLeft=Vec3_Create(.70f+.45f*(1-p.earOutward),0,1);
        earOpeningRight=Vec3_Create(-earOpeningLeft.x,0,1);
    }
    if(!HeadSurfaceRecipe_SetEarPose(r,false,l->leftEarBase,leftUp,earOpeningLeft)||
       !HeadSurfaceRecipe_SetEarPose(r,true,l->rightEarBase,rightUp,earOpeningRight))return false;

    bool canid=mammal;
    r->neckCollarPenetration=equine?.40f:(canid?.35f:.25f);
    r->neckCollarRoot=Vec3_Add(r->craniumCenter,Vec3_Create(0,-skullR.y*(equine?.06f:(canid?.10f:.08f)),-skullR.z*(equine?.32f:(canid?.28f:.35f))));
    float collarX=skullR.x*(equine?.48f:(canid?.72f:.70f)),collarY=skullR.y*(equine?.62f:(canid?.76f:.74f));
    r->neckCollarRadii=Vec3_Create(collarX,collarY,Math_Min(collarX,collarY));
    r->cranialPostorbitalCenter=Vec3_Add(r->craniumCenter,Vec3_Create(0,0,skullR.z*.05f));
    r->cranialTemporalCenter=Vec3_Add(r->craniumCenter,Vec3_Create(0,0,-skullR.z*.45f));
    r->cranialOccipitalCenter=Vec3_Add(r->craniumCenter,Vec3_Create(0,-skullR.y*.08f,-skullR.z*.85f));
    if(suid) {
        /* El apoyo maxilar llega al plano labial: un barrido demasiado poco
         * profundo dejaba aire entre el hocico y la mandíbula cerrada. */
        r->hasCranialProfile=true;
        const Vector3 centers[]={tip,mid,root,r->cranialPostorbitalCenter,
            r->cranialTemporalCenter,r->cranialOccipitalCenter};
        const float widths[]={tipWidth,midWidth,rootWidth,skullR.x*.90f,
            skullR.x*.88f,skullR.x*.56f};
        const float tops[]={tip.y+faceDepth*.54f,mid.y+faceDepth*.72f,
            skull.y+skullR.y*.78f,skull.y+skullR.y*.92f,
            skull.y+skullR.y*.86f,skull.y+skullR.y*.54f};
        const float throat[]={.22f,.36f,.50f,.47f,.38f,.20f};
        for(int i=0;i<6;++i) {
            float bottom=l->leftMouthCorner.y+oralSlope*(centers[i].z-cornerZ)-faceDepth*throat[i];
            r->cranialCenters[i]=Vec3_Create(0,(tops[i]+bottom)*.5f,centers[i].z);
            r->cranialRadii[i]=Vec3_Create(widths[i],(tops[i]-bottom)*.5f,0);
        }
    }
    if(equine) {
        /* Secciones cefálicas C² continuas: puente nasal proporcionado, frente
         * orbital definida, diastema contenido y nuca alineada sin escalón cervical. */
        float eqLipTipY=l->leftMouthCorner.y+oralSlope*(tip.z-cornerZ);
        float eqZ3=eyeZ+skullR.z*.04f;
        float eqTop3=skull.y+Math_Max(skullR.y*.52f,faceDepth*.34f);
        float eqBot3=l->leftMouthCorner.y+oralSlope*(eqZ3-cornerZ)-faceDepth*.32f;
        float eqH3=Math_Max((eqTop3-eqBot3)*.5f,faceDepth*.44f);
        float eqW3=skullR.x*(.545f+.035f*p.zygomaticWidth);
        Vector3 eqPos2=Vec3_Lerp(root,tip,.38f);
        float eqW2=Math_Clamp(rootWidth*(.84f+.06f*p.maxillaryWidth),tipWidth*1.04f,eqW3*.88f);
        Vector3 eqPos1=Vec3_Lerp(root,tip,.68f);
        float eqW1=Math_Clamp(midWidth*(1.16f+.04f*(1-p.muzzleTaper)),tipWidth*.96f,eqW2*.90f);
        float eqEndTop=Math_Clamp(tip.y-faceDepth*.005f,eqLipTipY+faceDepth*.275f,eqLipTipY+faceDepth*.335f);
        float eqBot0=eqLipTipY-faceDepth*.045f;
        float eqTipY=Math_Max((eqEndTop-eqBot0)*.5f,faceDepth*.160f);
        float eqZ0=tip.z;
        float eqSpanZ=Math_Max(eqZ0-eqZ3,.05f);
        float eqU1=Math_Clamp01((eqPos1.z-eqZ3)/eqSpanZ);
        float eqTop1=Math_Lerp(eqTop3,eqEndTop,eqU1)+faceDepth*.006f;
        float eqLip1Y=l->leftMouthCorner.y+oralSlope*(eqPos1.z-cornerZ);
        float eqH1=Math_Max((eqTop1-(eqLip1Y+faceDepth*.002f))*.5f,faceDepth*.205f);
        float eqU2=Math_Clamp01((eqPos2.z-eqZ3)/eqSpanZ);
        float eqTop2=Math_Lerp(eqTop3,eqEndTop,eqU2)+faceDepth*((p.stopProminence-.5f)*.012f);
        float eqLip2Y=l->leftMouthCorner.y+oralSlope*(eqPos2.z-cornerZ);
        float eqH2=Math_Max((eqTop2-(eqLip2Y-faceDepth*.17f))*.5f,faceDepth*.36f);
        float eqW0=Math_Clamp(tipWidth*.88f,eqW1*.94f,eqW2*.84f);
        float eqTop4=skull.y+Math_Max(skullR.y*.52f,faceDepth*.35f);
        float eqBot4=skull.y-skullR.y*.52f;
        float eqH4=(eqTop4-eqBot4)*.5f;
        float eqTop5=skull.y+skullR.y*.42f;
        float eqBot5=skull.y-skullR.y*.44f;
        float eqH5=(eqTop5-eqBot5)*.5f;
        r->hasCranialProfile=true;
        r->cranialCenters[0]=Vec3_Create(0,eqEndTop-eqTipY,eqZ0);
        r->cranialRadii[0]=Vec3_Create(eqW0,eqTipY,0);
        r->cranialCenters[1]=Vec3_Create(0,eqTop1-eqH1,eqPos1.z);
        r->cranialRadii[1]=Vec3_Create(eqW1,eqH1,0);
        r->cranialCenters[2]=Vec3_Create(0,eqTop2-eqH2,eqPos2.z);
        r->cranialRadii[2]=Vec3_Create(eqW2,eqH2,0);
        r->cranialCenters[3]=Vec3_Create(0,eqTop3-eqH3,eqZ3);
        r->cranialRadii[3]=Vec3_Create(eqW3,eqH3,0);
        r->cranialCenters[4]=Vec3_Create(0,eqTop4-eqH4,skull.z-skullR.z*.12f);
        r->cranialRadii[4]=Vec3_Create(skullR.x*(.560f+.025f*p.cheekMass),eqH4,0);
        r->cranialCenters[5]=Vec3_Create(0,eqTop5-eqH5,skull.z-skullR.z*.38f);
        r->cranialRadii[5]=Vec3_Create(skullR.x*.485f,eqH5,0);
    }
    if(bovid) {
        /* La ganacha pertenece al contorno craneal continuo, no a una bola
         * pegada por fuera de la mejilla. El masetero profundiza el tercio caudal. */
        r->hasCranialProfile=true;
        r->cranialCenters[0]=tip;r->cranialRadii[0]=r->faceTipRadii;
        r->cranialCenters[1]=mid;r->cranialCenters[1].y-=faceDepth*.07f;
        r->cranialRadii[1]=Vec3_Create(r->faceMidRadii.x,r->faceMidRadii.y+faceDepth*.07f,0);
        r->cranialCenters[2]=root;r->cranialCenters[2].y-=faceDepth*.06f;
        r->cranialRadii[2]=Vec3_Create(r->faceRootRadii.x,r->faceRootRadii.y+faceDepth*.06f,0);
        r->cranialCenters[3]=Vec3_Add(r->cranialPostorbitalCenter,Vec3_Create(0,-skullR.y*.08f,0));
        r->cranialRadii[3]=Vec3_Create(skullR.x*(.90f+.04f*p.zygomaticWidth+.06f*p.cheekMass),skullR.y*(.92f+.18f*p.masseterMass),0);
        r->cranialCenters[4]=Vec3_Add(r->cranialTemporalCenter,Vec3_Create(0,-skullR.y*.04f,0));
        r->cranialRadii[4]=Vec3_Create(skullR.x*.96f,skullR.y,0);
        r->cranialCenters[5]=r->cranialOccipitalCenter;
        r->cranialRadii[5]=Vec3_Create(skullR.x*.74f,skullR.y*.70f,0);
    }
    if(feline) {
        /* Cráneo felino estilizado: órbitas recogidas bajo ceja firme,
         * hocico felino sellado con trufa integrada y transición suave al cuello. */
        r->orbitSocketDepth=orbitRadius*.42f;
        r->leftOrbitRimCenter=Vec3_Sub(l->leftOrbit,Vec3_Scale(ln,orbitRadius*.32f));
        r->rightOrbitRimCenter=Vec3_Sub(l->rightOrbit,Vec3_Scale(rn,orbitRadius*.32f));
        r->orbitRimRadii=Vec3_Create(r->orbitRadii.x*1.04f,r->orbitRadii.y*1.05f,r->orbitRadii.z*1.04f);
        r->browRadii=Vec3_Create(orbitRadius*.38f,orbitRadius*.20f,orbitRadius*.52f);
        r->faceTipRadii.x*=.88f;
        r->faceTipRadii.z=Math_Max(r->faceTipRadii.z*1.08f,r->faceTipRadii.x*.26f);
        r->faceMidRadii.x*=.94f;
        r->faceRounding=Math_Min(faceDepth,midWidth)*.24f;
        Vector3 domeTip=Vec3_Create(0,tip.y-faceDepth*.07f,tip.z+r->faceTipRadii.z*.16f);
        {
            float noseW=r->faceTipRadii.x*(.20f+.04f*p.noseScale);
            float noseH=r->faceTipRadii.y*.11f;
            float noseZ=Math_Min(r->faceTipRadii.z*.11f,noseW*.32f);
            r->noseCenter=Vec3_Create(0,tip.y+faceDepth*.02f,tip.z+r->faceTipRadii.z*.04f);
            r->noseRadii=Vec3_Create(noseW,noseH,noseZ);
            l->leftNostril=Vec3_Create(noseW*.60f,r->noseCenter.y-noseH*.14f,r->noseCenter.z+noseZ*.42f);
            l->rightNostril=l->leftNostril;l->rightNostril.x*=-1;
            r->leftNostrilCenter=l->leftNostril;r->rightNostrilCenter=l->rightNostril;
            r->nostrilRadii=Vec3_Create(noseW*.24f,noseH*.32f,noseZ*.42f);
        }
        r->neckCollarPenetration=.52f;
        r->neckCollarRoot=Vec3_Add(r->craniumCenter,Vec3_Create(0,-skullR.y*.05f,-skullR.z*.38f));
        r->neckCollarRadii=Vec3_Create(skullR.x*.70f,skullR.y*.72f,Math_Min(skullR.x*.70f,skullR.y*.72f));
        r->hasCranialProfile=true;
        r->cranialCenters[0]=domeTip;
        r->cranialRadii[0]=Vec3_Create(r->faceTipRadii.x*.58f,r->faceTipRadii.y*.62f,0);
        r->cranialCenters[1]=Vec3_Lerp(root,tip,.76f);r->cranialCenters[1].y-=faceDepth*.06f;
        r->cranialRadii[1]=Vec3_Create(r->faceTipRadii.x*.96f,r->faceMidRadii.y*.92f,0);
        r->cranialCenters[2]=Vec3_Lerp(root,tip,.34f);r->cranialCenters[2].y-=faceDepth*.02f;
        r->cranialRadii[2]=Vec3_Create(r->faceRootRadii.x*.95f,r->faceRootRadii.y*.94f,0);
        r->cranialCenters[3]=Vec3_Add(r->cranialPostorbitalCenter,Vec3_Create(0,-skullR.y*.03f,0));
        r->cranialRadii[3]=Vec3_Create(skullR.x*(.82f+.06f*p.zygomaticWidth),skullR.y*.88f,0);
        r->cranialCenters[4]=Vec3_Add(r->cranialTemporalCenter,Vec3_Create(0,-skullR.y*.02f,skullR.z*.03f));
        r->cranialRadii[4]=Vec3_Create(skullR.x*(.84f+.04f*p.zygomaticWidth),skullR.y*.86f,0);
        r->cranialCenters[5]=r->cranialOccipitalCenter;
        r->cranialRadii[5]=Vec3_Create(skullR.x*.74f,skullR.y*.74f,0);
    }
    if(!equine && p.cranialForm==HEAD_CRANIAL_SHIELD) {
        r->cranialPostorbitalCenter.z=r->craniumCenter.z-skullR.z*.18f;
        r->cranialTemporalCenter.z=r->craniumCenter.z-skullR.z*.55f;
        r->cranialTemporalRadii.x=skullR.x*.94f;
    }
    r->unionSmoothness=Math_Min(skullR.x,skullR.y)*(suid?.18f:.10f);r->headBodySmoothness=(feline||suid||proboscidean)?Math_Min(skullR.y,skullR.z)*.32f:equine?Math_Min(skullR.y,skullR.z)*.28f:Math_Min(r->unionSmoothness,Math_Min(skullR.y,skullR.z)*.10f);r->hasNasalPad=mammal&&!equine&&!proboscidean;r->hasEars=mammal;r->isBeak=p.archetype==HEAD_ARCHETYPE_AVIAN;r->sweptSkull=p.archetype!=HEAD_ARCHETYPE_AVIAN;r->hasTympana=p.archetype==HEAD_ARCHETYPE_LIZARD;
    float tymWeight=Math_Clamp01(p.tympanumSize/.20f);
    r->tympanumDevelopment=tymWeight*tymWeight*(3.0f-2.0f*tymWeight);
    float mouthWidth=cornerX*2,mouthHeight=faceDepth*(p.archetype==HEAD_ARCHETYPE_AVIAN?.70f:1.05f);Color oral=Color_FromRGB(45,8,12);a->oralSystem=Mouth_Create(attachment,Vec3_Zero(),Vec3_Create(mouthWidth,mouthHeight,faceLength*(feline?.72f:.78f)),oral,oral);
    a->oralSystem.shape=p.archetype==HEAD_ARCHETYPE_AVIAN?MOUTH_SHAPE_LOWER_BEAK:MOUTH_SHAPE_TAPERED_MANDIBLE;a->oralSystem.offset=Vec3_Zero();a->oralSystem.jawPivot=Vec3_Create(0,hingeY,hingeZ);a->oralSystem.jawLength=l->mandibularSymphysis.z-hingeZ;a->oralSystem.jawWidth=hingeX*2;a->oralSystem.jawThickness=faceDepth*(p.archetype==HEAD_ARCHETYPE_LIZARD?(.62f+.38f*p.jawDepth):(mammal?(.66f+.35f*p.jawDepth):(.28f+.38f*p.jawDepth)));a->oralSystem.jawRearMass=faceDepth*(.30f+.38f*p.jawStrength);a->oralSystem.jawMuscle=faceDepth*(.24f+.38f*p.jawStrength);a->oralSystem.maxJawAngle=feline?42:(p.archetype==HEAD_ARCHETYPE_CANID?36:(p.archetype==HEAD_ARCHETYPE_AVIAN?26:20));a->oralSystem.hingeRadius=faceDepth*(.13f+.14f*p.jawStrength);a->oralSystem.throatRadius=faceDepth*.20f;if(bovid){a->oralSystem.jawThickness=faceDepth*(.38f+.20f*p.jawDepth);a->oralSystem.slitThickness=faceDepth*.014f;}if(p.archetype==HEAD_ARCHETYPE_CANID){a->oralSystem.openFactor=0;if(feline){a->oralSystem.jawThickness=faceDepth*(.36f+.18f*p.jawDepth);a->oralSystem.slitThickness=faceDepth*.014f;}}Mouth_Normalize(&a->oralSystem);
    if(feline) {
        /* Mandíbula felina compacta sellada bajo el labio superior. */
        r->hasMandibularProfile=true;
        const float positions[]={1.00f,.76f,.52f,.28f,.12f,0};
        const float widths[]={.60f,.80f,.86f,.86f,.74f,.56f};
        const float depths[]={.42f,.44f,.46f,.50f,.50f,.34f};
        float length=l->mandibularSymphysis.z-hingeZ;
        float symphysisWidth=r->faceTipRadii.x;
        for(int i=0;i<6;++i) {
            float z=hingeZ+positions[i]*length;
            if(i==0)z=tip.z+r->faceTipRadii.z*.06f;
            float baseH=faceDepth*depths[i]*(.85f+.30f*p.jawDepth);
            float centerOffset=i<4?.42f:.64f;
            float top=l->leftMouthCorner.y+(i<2?faceDepth*(i==0?.08f:.06f):0.0f);
            float posterior=Math_Clamp01((.30f-positions[i])/.30f);
            top=Math_Lerp(top,hingeY+faceDepth*.28f,posterior);
            r->mandibularCenters[i]=Vec3_Create(0,top-baseH*centerOffset,z);
            float w=Math_Lerp(symphysisWidth,hingeX,Math_Clamp01((1-positions[i])/.76f));
            r->mandibularRadii[i]=Vec3_Create(w*widths[i],baseH,0);
        }
    }
    if(suid) {
        a->oralSystem.openFactor=0;
        a->oralSystem.slitThickness=faceDepth*.012f;
        a->oralSystem.jawThickness=faceDepth*.40f;
        Mouth_Normalize(&a->oralSystem);
        r->hasMandibularProfile=true;
        /* Sínfisis redonda, cuerpo ascendente y ángulo que se hunde en el masetero. */
        const float positions[]={.93f,.80f,.58f,.35f,.15f,0};
        const float depths[]={.155f,.22f,.31f,.38f,.39f,.31f};
        const float widths[]={.48f,.57f,.66f,.76f,.81f,.76f};
        for(int i=0;i<6;++i) {
            float z=Math_Lerp(hingeZ,tip.z,positions[i]);
            float h=faceDepth*depths[i];
            float top=l->leftMouthCorner.y+oralSlope*(z-cornerZ);
            float width=tipWidth*widths[i];
            r->mandibularCenters[i]=Vec3_Create(0,top-h*(i<2?1.02f:.82f),z);
            r->mandibularRadii[i]=Vec3_Create(width,h,0);
        }
    }
    if(grazing) {
        a->oralSystem.jawThickness=faceDepth*(.36f+.20f*p.jawDepth);
        a->oralSystem.slitThickness=faceDepth*(equine?.006f:.014f);
        a->oralSystem.openFactor=0;
        if(equine) {
            a->oralSystem.jawThickness=faceDepth*(.48f+.26f*p.jawDepth);
            a->oralSystem.hingeRadius=faceDepth*(.07f+.06f*p.jawStrength);
            a->oralSystem.throatRadius=faceDepth*.09f;
        }
        Mouth_Normalize(&a->oralSystem);
        /* Perfil mandibular propio: el ramus nace junto al cóndilo y desciende
         * hacia un músculo masetero / ganacha amplio, profundo y cuadrado, con
         * cuerpo mandibular firme hasta una sínfisis ancha bajo el labio inferior. */
        r->hasMandibularProfile=true;
        const float positions[]={1.035f,.82f,.51f,.24f,.08f,0};
        const float eqPositions[]={1.00f,.80f,.52f,.25f,.11f,0};
        const float eqWidths[]={.72f,.58f,.74f,.80f,.70f,.52f};
        const float eqDepths[]={.145f,.190f,.315f,.410f,.340f,.195f};
        const float eqOffsets[]={.35f,.32f,.42f,.50f,.48f,.44f};
        const float bovidWidths[]={.72f,.70f,.76f,.80f,.68f,.48f};
        const float bovidDepths[]={.18f,.24f,.36f,.58f,.70f,.44f};
        float length=l->mandibularSymphysis.z-hingeZ;
        float symphysisWidth=bovid?r->faceTipRadii.x:tipWidth;
        for(int i=0;i<6;++i) {
            float pos=bovid?positions[i]:eqPositions[i];
            float z=hingeZ+pos*length;
            if(i==0)z=bovid?tip.z:(tip.z-faceDepth*.025f);
            float baseH=faceDepth*(bovid?bovidDepths[i]*(.82f+.38f*p.jawDepth):eqDepths[i]*(.82f+.38f*p.jawDepth));
            float centerOffset=bovid?(i<4?.38f:.65f):eqOffsets[i];
            float height=bovid?(i<4?baseH*(1.65f/1.38f):baseH):(i==0?baseH*1.06f:baseH*1.16f);
            float top=l->leftMouthCorner.y+oralSlope*(z-cornerZ)+(equine&&i<4?(i==0?faceDepth*.015f:(i==1?faceDepth*.03f:(i==2?faceDepth*.12f:faceDepth*.15f))):0.0f);
            /* El techo caudal sigue el cóndilo, no la prolongación del plano
             * incisivo, que ascendía por encima de la región temporal. */
            float posterior=Math_Clamp01(((bovid?.30f:.25f)-pos)/(bovid?.30f:.25f));
            top=Math_Lerp(top,hingeY+faceDepth*(bovid?.48f:.30f),posterior);
            r->mandibularCenters[i]=Vec3_Create(0,top-height*centerOffset,z);
            float w=Math_Lerp(symphysisWidth,hingeX,Math_Clamp01((1-pos)/.76f));
            r->mandibularRadii[i]=Vec3_Create(w*(bovid?bovidWidths[i]:eqWidths[i]),height,0);
        }
        if(equine) {
            float jawCapR0=Math_Min(r->mandibularRadii[0].x,r->mandibularRadii[0].y);
            r->faceCapDepth=Math_Min(r->faceCapDepth,jawCapR0*.90f);
        }
    }
    if(proboscidean) {
        a->oralSystem.openFactor=0;
        a->oralSystem.jawThickness=faceDepth*.37f;
        a->oralSystem.slitThickness=faceDepth*.01f;
        Mouth_Normalize(&a->oralSystem);
        r->hasMandibularProfile=true;
        const float widths[]={.38f,.53f,.68f,.82f,.88f,.69f};
        const float depths[]={.13f,.19f,.23f,.25f,.27f,.20f};
        for (unsigned i=0;i<6;++i) {
            float z=Math_Lerp(l->mandibularSymphysis.z,hingeZ,(float)i/5);
            r->mandibularCenters[i]=Vec3_Create(0,-skullR.y*(.71f+.03f*(1-(float)i/5)),z);
            r->mandibularRadii[i]=Vec3_Create(hingeX*widths[i],faceDepth*depths[i],0);
        }
    }
    l->oralContactPoint=l->mandibularSymphysis;
    if(grazing) {
        l->oralContactPoint.z=tip.z+tipDepth*.5f;
        l->oralContactPoint.y=l->leftMouthCorner.y+oralSlope*(l->oralContactPoint.z-cornerZ);
    }
    a->eyeScale=Vec3_Create(eyeRadius*.92f,eyeRadius*eyeAspect,eyeRadius*(equine?.86f:(mammal?.68f:.86f)));return HeadAnatomy_Validate(a)==HEAD_VALID;
}

HeadResolvedMeasurements HeadAnatomy_Measure(const HeadAnatomy* a) {
    HeadResolvedMeasurements m={0};
    if(!a)return m;
    const HeadSurfaceRecipe* r=&a->surface;
    m.actualHeadLength=(r->hasNasalPad?r->noseCenter.z+r->noseRadii.z:r->faceTip.z+r->faceTipRadii.z)-
                       (r->craniumCenter.z-r->craniumRadii.z);
    float temporal=Math_Max(r->cranialTemporalRadii.x,r->leftTemporalCenter.x+r->temporalRadii.x);
    float zygomatic=r->leftCheekCenter.x+r->cheekRadii.x;
    m.actualMaxZygomaticWidth=2*Math_Max(temporal,zygomatic);
    m.actualWidthLengthRatio=m.actualMaxZygomaticWidth/Math_Max(m.actualHeadLength,.001f);
    m.muzzleRootWidth=2*r->faceRootRadii.x;
    m.muzzleTipWidth=2*(r->hasNasalPad?Math_Max(r->faceTipRadii.x,r->noseRadii.x):r->faceTipRadii.x);
    m.interEyeDistance=Vec3_Distance(a->landmarks.leftEyeCenter,a->landmarks.rightEyeCenter);
    m.mouthCornerSpan=Vec3_Distance(a->landmarks.leftMouthCorner,a->landmarks.rightMouthCorner);
    return m;
}

uint32_t HeadAnatomy_Validate(const HeadAnatomy* a){
    if(!a)return HEAD_INVALID_FINITE;
    const HeadLandmarks* l=&a->landmarks;const HeadSurfaceRecipe* r=&a->surface;uint32_t e=0;
    float t=Math_Max(r->craniumRadii.x,.1f)*.001f;
    const Vector3 v[]={l->oralContactPoint,r->eyelidProfile,l->neckAttachment,l->skullCenter,l->leftOrbit,l->rightOrbit,l->leftOrbitNormal,l->rightOrbitNormal,l->leftEyeCenter,l->rightEyeCenter,l->leftJawHinge,l->rightJawHinge,l->muzzleRoot,l->muzzleTip,l->leftNostril,l->rightNostril,l->leftTympanum,l->rightTympanum,l->leftMouthCorner,l->rightMouthCorner,r->craniumRadii,r->faceRoot,r->faceMid,r->faceTip,r->faceRootRadii,r->faceMidRadii,r->faceTipRadii,r->leftBrowCenter,r->rightBrowCenter};
    for(size_t i=0;i<sizeof(v)/sizeof(v[0]);++i)if(!Head_VectorFinite(v[i]))e|=HEAD_INVALID_FINITE;
    if(!Head_Symmetric(l->leftOrbit,l->rightOrbit,t)||!Head_Symmetric(l->leftOrbitNormal,l->rightOrbitNormal,t)||!Head_Symmetric(l->leftEyeCenter,l->rightEyeCenter,t)||!Head_Symmetric(l->leftJawHinge,l->rightJawHinge,t)||!Head_Symmetric(l->leftNostril,l->rightNostril,t)||!Head_Symmetric(l->leftTympanum,l->rightTympanum,t)||!Head_Symmetric(l->leftMouthCorner,l->rightMouthCorner,t)||!Head_Symmetric(r->leftBrowCenter,r->rightBrowCenter,t))e|=HEAD_INVALID_SYMMETRY;
    if(l->muzzleTip.z<=l->muzzleRoot.z||r->faceMid.z<=r->faceRoot.z||r->faceMid.z>=r->faceTip.z||l->leftJawHinge.z>=l->leftMouthCorner.z||l->leftJawHinge.y>l->leftMouthCorner.y+r->oralSlope*(l->leftJawHinge.z-l->leftMouthCorner.z)||l->leftTympanum.z>=l->leftOrbit.z||a->oralSystem.maxJawAngle>45)e|=HEAD_INVALID_ORDER;
    float exposure=Vec3_Dot(Vec3_Sub(l->leftEyeCenter,l->leftOrbit),l->leftOrbitNormal);
    float globeDepth=Math_Max(a->eyeScale.x,Math_Max(a->eyeScale.y,a->eyeScale.z));
    float lateralFraction=fabsf(l->leftOrbit.x-r->craniumCenter.x)/Math_Max(r->craniumRadii.x,1e-4f);
    if(!Head_InEllipsoid(l->leftOrbit,r->craniumCenter,r->craniumRadii,1.16f)||!Head_InEllipsoid(l->rightOrbit,r->craniumCenter,r->craniumRadii,1.16f)||!Head_InEllipsoid(l->leftEyeCenter,r->craniumCenter,r->craniumRadii,1.18f)||exposure<=0||exposure>globeDepth*.25f||lateralFraction>.88f||r->orbitRadii.y<a->eyeScale.y*.55f)e|=HEAD_INVALID_ORBITS;
    float span=Math_Max(l->muzzleTip.z-l->muzzleRoot.z,1e-4f),nt=Math_Clamp01((l->leftNostril.z-l->muzzleRoot.z)/span);
    float nw=nt<=.52f?Math_Lerp(r->faceRootRadii.x,r->faceMidRadii.x,nt/.52f):Math_Lerp(r->faceMidRadii.x,r->faceTipRadii.x,(nt-.52f)/.48f);
    bool nasalPadNostril=r->hasNasalPad&&Head_InEllipsoid(l->leftNostril,r->noseCenter,r->noseRadii,1.02f)&&
        Head_InEllipsoid(l->rightNostril,r->noseCenter,r->noseRadii,1.02f);
    if(a->archetype!=HEAD_ARCHETYPE_PROBOSCIDEAN&&!nasalPadNostril&&(l->leftNostril.z<l->muzzleRoot.z||l->leftNostril.z>l->muzzleTip.z||fabsf(l->leftNostril.x)>nw*1.02f))e|=HEAD_INVALID_NOSTRILS;
    if(l->neckAttachment.z>=l->skullCenter.z||l->neckAttachment.y>l->skullCenter.y)e|=HEAD_INVALID_NECK;
    return e;
}

float HeadAnatomy_RecommendedVoxelSize(const HeadAnatomy* a){
    if(!a)return .05f;
    float rim=Math_Max(a->surface.orbitRadii.y-a->eyeScale.y,.006f);
    float nostril=Math_Min(a->surface.nostrilRadii.x,a->surface.nostrilRadii.y);
    float tympanum=a->surface.hasTympana?Math_Min(a->surface.tympanumRadii.y,a->surface.tympanumRadii.z):1e6f;
    float slit=Math_Max(a->oralSystem.slitThickness,.006f);
    float feature=Math_Min(Math_Min(rim,nostril),Math_Min(tympanum,slit));
    /* El diámetro de la característica ocupa al menos cuatro celdas; el suelo
     * de 9 mm evita que las narinas juveniles caigan en una sola celda útil. */
    return Math_Clamp(feature*.50f,.009f,.055f);
}
Head Head_Create(HeadArchetype archetype,size_t attachment,Vector3 hostRadii){Head h;memset(&h,0,sizeof(h));h.development=1;h.phenotype=archetype==HEAD_ARCHETYPE_PROBOSCIDEAN?HeadPhenotype_ProboscideanPreset():archetype==HEAD_ARCHETYPE_SUID?HeadPhenotype_SuidPreset():archetype==HEAD_ARCHETYPE_EQUINE?HeadPhenotype_EquinePreset():archetype==HEAD_ARCHETYPE_BOVID?HeadPhenotype_BovinePreset():archetype==HEAD_ARCHETYPE_CANID?HeadPhenotype_CanidPreset():archetype==HEAD_ARCHETYPE_AVIAN?HeadPhenotype_AvianPreset():HeadPhenotype_LizardPreset();HeadAnatomy_Resolve(&h.phenotype,attachment,hostRadii,&h.anatomy);return h;}
void Head_SetOpenFactor(Head* head,float factor){if(head)Mouth_SetOpenFactor(&head->anatomy.oralSystem,factor);}
