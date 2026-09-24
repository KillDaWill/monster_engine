#include "Head.h"
#include "MathUtils.h"
#include <math.h>
#include <string.h>

bool Head_Supports(HeadArchetype a) {
    return a == HEAD_ARCHETYPE_LIZARD || a == HEAD_ARCHETYPE_CANID || a == HEAD_ARCHETYPE_AVIAN;
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
    if(p->archetype<HEAD_ARCHETYPE_LIZARD||p->archetype>HEAD_ARCHETYPE_AVIAN)
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
    if (p->cranialForm < HEAD_CRANIAL_STANDARD || p->cranialForm > HEAD_CRANIAL_WEDGE)
        p->cranialForm = HEAD_CRANIAL_STANDARD;
    if (p->muzzleForm < HEAD_MUZZLE_TAPERED || p->muzzleForm > HEAD_MUZZLE_WEDGE)
        p->muzzleForm = HEAD_MUZZLE_TAPERED;
    if (p->eyeLayout < HEAD_EYES_LATERAL || p->eyeLayout > HEAD_EYES_LOW_LATERAL)
        p->eyeLayout = HEAD_EYES_LATERAL;
}
HeadPhenotype HeadPhenotype_RandomValid(HeadArchetype archetype,uint32_t seed) {
    HeadPhenotype p=archetype==HEAD_ARCHETYPE_CANID?HeadPhenotype_CanidPreset():archetype==HEAD_ARCHETYPE_AVIAN?HeadPhenotype_AvianPreset():HeadPhenotype_LizardPreset();uint32_t s=seed?seed:1u;
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
    float brachy=p.cephalicIndex-.5f;
    skullR.x*=1.0f+brachy*.34f;skullR.z*=1.0f-brachy*.22f;
    if(p.archetype==HEAD_ARCHETYPE_LIZARD){skullR.x*=.78f;skullR.y*=.62f;skullR.z*=.72f;}
    Vector3 skull=Vec3_Create(0,0,-hz*.10f);float faceLength=(p.archetype==HEAD_ARCHETYPE_AVIAN?p.beakLength:p.muzzleLength)*hz*1.20f+hz*.30f;
    if(p.archetype==HEAD_ARCHETYPE_CANID)faceLength*=1.0f-brachy*.34f;
    if (p.muzzleForm == HEAD_MUZZLE_BLUNT) faceLength *= .82f;
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
    float eyeAspect = 1.0f - .42f*p.eyeCompression;
    float orbitRadius=Math_Min(skullR.y,skullR.z)*(.25f+.24f*p.eyeSize);
    float sideFactor=p.archetype==HEAD_ARCHETYPE_LIZARD?(.72f+.08f*p.eyeLaterality):(.70f+.18f*p.eyeLaterality);
    if (p.eyeLayout == HEAD_EYES_FORWARD) sideFactor *= .82f;
    else if (p.eyeLayout == HEAD_EYES_HIGH_LATERAL) sideFactor *= 1.04f;
    else if (p.eyeLayout == HEAD_EYES_LOW_LATERAL) sideFactor *= 1.02f;
    if (p.cranialForm == HEAD_CRANIAL_SHIELD) sideFactor *= .91f;
    if (p.archetype == HEAD_ARCHETYPE_CANID) sideFactor *= .88f;
    float side=skullR.x*sideFactor;
    float eyeZ=skull.z+skullR.z*(p.archetype==HEAD_ARCHETYPE_LIZARD?
        (-.12f+.24f*(p.eyeForwardness + (p.eyeLayout == HEAD_EYES_FORWARD ? .28f : 0.0f))):
        (.13f+.20f*p.eyeForwardness));
    float eyeY=skull.y+skullR.y*(.24f+.34f*p.eyeDorsality);
    if (p.eyeLayout == HEAD_EYES_HIGH_LATERAL) eyeY += skullR.y*.16f;
    else if (p.eyeLayout == HEAD_EYES_LOW_LATERAL) eyeY -= skullR.y*.14f;
    if (p.cranialForm == HEAD_CRANIAL_SHIELD) eyeY -= skullR.y*.06f;
    float eyeForward = p.eyeForwardness + (p.eyeLayout == HEAD_EYES_FORWARD ? .28f : 0.0f);
    Vector3 ln=Vec3_Normalize(Vec3_Create(1,.08f+.08f*p.eyeDorsality,.10f+(p.archetype==HEAD_ARCHETYPE_CANID?1.35f:.22f)*eyeForward)),rn=ln;rn.x*=-1;
    if (p.cranialForm == HEAD_CRANIAL_SHIELD) {
        ln = Vec3_Normalize(Vec3_Create(.82f, .45f, .35f)); rn = ln; rn.x *= -1;
    }
    float cornerX=rootWidth*(p.archetype==HEAD_ARCHETYPE_CANID?(.50f+.34f*p.mouthWidth):.88f);
    float cornerZ=root.z+faceLength*.18f;
    float jawSkullX=shapeSkullX;
    float jawSkullZ=shapeSkullZ;
    float hingeX=jawSkullX*(.72f+.08f*p.jawStrength);
    if (p.muzzleForm == HEAD_MUZZLE_WEDGE) hingeX *= .88f;
    float jawEyeZ=skull.z+jawSkullZ*(.10f+.16f*p.eyeForwardness);
    float hingeZ=jawEyeZ-jawSkullZ*(.30f+.12f*p.jawLength),hingeY=Math_Min(-skullR.y*(.30f+.18f*p.jawDepth),-faceDepth*.50f);
    float nt=Math_Clamp(p.nostrilPosition,.20f,.90f),nostrilZ=root.z+(tip.z-root.z)*nt;
    float localWidth=nt<=.52f?Math_Lerp(rootWidth,midWidth,nt/.52f):Math_Lerp(midWidth,tipWidth,(nt-.52f)/.48f);
    float rootHeight=faceDepth*.88f,midHeight=faceDepth*(.64f+.06f*(1-taper));
    float tipHeight=faceDepth*(.50f+.12f*p.snoutBluntness);
    float localHeight=nt<=.52f?Math_Lerp(rootHeight,midHeight,nt/.52f):Math_Lerp(midHeight,tipHeight,(nt-.52f)/.48f);
    float profileY=nt<=.52f?Math_Lerp(root.y,mid.y,nt/.52f):Math_Lerp(mid.y,tip.y,(nt-.52f)/.48f);
    float tymR=orbitRadius*(.24f+.22f*p.tympanumSize);
    float neckAttachY=p.archetype==HEAD_ARCHETYPE_CANID?-skullR.y*.28f:-skullR.y*.18f;
    HeadLandmarks* l=&a->landmarks;l->headId=0;l->skullCenter=skull;l->neckAttachment=Vec3_Create(0,neckAttachY,skull.z-skullR.z*.82f);
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
    l->leftMouthCorner=Vec3_Create(cornerX,-faceDepth*.42f,cornerZ);l->rightMouthCorner=l->leftMouthCorner;l->rightMouthCorner.x*=-1;
    l->leftJawHinge=Vec3_Create(hingeX,hingeY,hingeZ);l->rightJawHinge=l->leftJawHinge;l->rightJawHinge.x*=-1;
    l->muzzleRoot=root;l->muzzleTip=tip;l->noseTip=tip;l->leftNostril=Vec3_Create(localWidth*.74f,profileY+localHeight*.72f,nostrilZ);l->rightNostril=l->leftNostril;l->rightNostril.x*=-1;
    float tymY=eyeY-orbitRadius*.30f,tymZ=eyeZ-skullR.z*.28f;
    float tymNy=(tymY-skull.y)/skullR.y,tymNz=(tymZ-skull.z)/skullR.z;
    float tymSurfaceX=skullR.x*sqrtf(Math_Max(.05f,1.0f-tymNy*tymNy-tymNz*tymNz));
    l->leftTympanum=Vec3_Create(tymSurfaceX-tymR*.12f,tymY,tymZ);l->rightTympanum=l->leftTympanum;l->rightTympanum.x*=-1;
    l->mandibularSymphysis=Vec3_Create(0,tip.y-faceDepth*.62f,tip.z-faceLength*.04f);l->oralCavityCenter=Vec3_Create(0,root.y-faceDepth*.38f,root.z+faceLength*.44f);
    float earLateral=p.archetype==HEAD_ARCHETYPE_CANID?.60f+.08f*p.earBaseWidth:.58f;
    float earDorsal=p.archetype==HEAD_ARCHETYPE_CANID?.40f:.40f;
    float earRear=p.archetype==HEAD_ARCHETYPE_CANID?.20f:.18f;
    l->leftEarBase=Vec3_Create(skullR.x*earLateral,skullR.y*earDorsal,skull.z-skullR.z*earRear);l->rightEarBase=l->leftEarBase;l->rightEarBase.x*=-1;l->upperBeakAnchor=root;l->lowerBeakAnchor=Vec3_Create(0,-faceDepth*.42f,root.z+faceLength*.12f);
    HeadSurfaceRecipe* r=&a->surface;r->craniumCenter=skull;r->craniumRadii=skullR;
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
    r->cranialPostorbitalRadii=Vec3_Create(skullR.x*(p.archetype==HEAD_ARCHETYPE_CANID?.72f+.06f*p.zygomaticWidth:.98f),skullR.y*.92f,skullR.z);
    r->cranialTemporalRadii=Vec3_Create(skullR.x*(p.archetype==HEAD_ARCHETYPE_CANID?.90f+.08f*p.zygomaticWidth:.88f),skullR.y*.88f,skullR.z);
    r->cranialOccipitalRadii=Vec3_Create(skullR.x*(p.archetype==HEAD_ARCHETYPE_CANID?.68f+.08f*p.jawStrength:.50f),skullR.y*.62f,skullR.z);
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
    /* El reborde canino rodea el globo: no queda enterrado tras él. */
    float rimInset = p.archetype == HEAD_ARCHETYPE_CANID ? .10f : 1.10f;
    r->leftBrowCenter=Vec3_Add(Vec3_Sub(l->leftOrbit,Vec3_Scale(ln,orbitRadius*rimInset)),Vec3_Create(0,orbitRadius*eyeAspect*(.52f+.16f*p.browProminence),-orbitRadius*.08f));
    r->rightBrowCenter=Vec3_Add(Vec3_Sub(l->rightOrbit,Vec3_Scale(rn,orbitRadius*rimInset)),Vec3_Create(0,orbitRadius*eyeAspect*(.52f+.16f*p.browProminence),-orbitRadius*.08f));
    r->browRadii=Vec3_Create(orbitRadius*(.38f+.12f*p.browProminence),orbitRadius*(.23f+.15f*p.browProminence),orbitRadius*(.52f+.20f*p.browProminence));
    r->leftOrbitCenter=l->leftOrbit;r->rightOrbitCenter=l->rightOrbit;r->orbitRadii=Vec3_Create(orbitRadius,orbitRadius*.94f*eyeAspect,orbitRadius*.82f);r->leftOrbitRimCenter=Vec3_Sub(l->leftOrbit,Vec3_Scale(ln,orbitRadius*rimInset));r->rightOrbitRimCenter=Vec3_Sub(l->rightOrbit,Vec3_Scale(rn,orbitRadius*rimInset));r->orbitRimRadii=Vec3_Create(r->orbitRadii.x*1.15f,r->orbitRadii.y*1.15f,r->orbitRadii.z*1.12f);r->leftOrbitNormal=ln;r->rightOrbitNormal=rn;r->orbitSocketDepth=orbitRadius*(.52f+.16f*p.orbitDepth);r->eyelidCoverage=p.eyelidCoverage;
    /* La abertura palpebral puede ser menor que el globo que protege. */
    r->orbitRadii.y *= 1-.55f*p.eyelidCoverage;
    r->orbitRadii.z *= 1-.35f*p.eyelidCoverage;
    r->noseCenter=Vec3_Create(0,tip.y,tip.z-faceLength*.035f);r->noseRadii=Vec3_Create(tipWidth*(.40f+.18f*p.noseScale),faceDepth*.28f,tipWidth*.26f);r->leftNostrilCenter=l->leftNostril;r->rightNostrilCenter=l->rightNostril;r->nostrilRadii=Vec3_Create(localWidth*.15f,faceDepth*.17f,localWidth*.17f);
    if (p.archetype==HEAD_ARCHETYPE_CANID) {
        /* La trufa sobresale del premaxilar, pero no debe convertirse en un
         * segundo hocico bulboso al aumentar la anchura del perro. */
        r->noseCenter=Vec3_Add(tip,Vec3_Create(0,faceDepth*.08f,tipWidth*.38f));
        r->noseRadii=Vec3_Create(tipWidth*(.70f+.16f*p.noseScale),tipHeight*.56f,tipWidth*.38f);
        l->leftNostril=Vec3_Create(r->noseRadii.x*.70f,r->noseCenter.y+r->noseRadii.y*.10f,tip.z);
        l->rightNostril=l->leftNostril;l->rightNostril.x*=-1;
        r->leftNostrilCenter=l->leftNostril;r->rightNostrilCenter=l->rightNostril;
        r->nostrilRadii=Vec3_Create(r->noseRadii.x*.20f,r->noseRadii.y*.25f,r->noseRadii.z*.30f);
        r->nostrilRadii=Vec3_Scale(r->nostrilRadii,.25f+1.5f*p.noseScale);
    }
    r->leftTympanumCenter=l->leftTympanum;r->rightTympanumCenter=l->rightTympanum;r->tympanumRadii=Vec3_Create(tymR*.42f,tymR,tymR*.86f);r->tympanumDepth=tymR*.55f;
    float earWidthScale=p.archetype==HEAD_ARCHETYPE_CANID?.15f+.10f*p.earBaseWidth:.31f+.32f*p.earBaseWidth;
    float earHeightScale=p.archetype==HEAD_ARCHETYPE_CANID?.86f+1.22f*p.earSize:.60f+1.20f*p.earSize;
    r->earShape=Vec3_Create(skullR.x*earWidthScale,
        skullR.y*earHeightScale,skullR.z*(.045f+.07f*p.earThickness));
    r->earConcavity=p.earConcavity;
    r->earTipFraction=.06f+.18f*(1-p.earPointiness);
    r->earLongitudinalCurve=(p.earLongitudinalCurve-.5f)*r->earShape.z*1.4f;
    r->earRootRoll=p.earRootRoll;r->earTipRoundness=p.earTipRoundness;r->earFold=(p.earFold-.5f)*r->earShape.y*.62f;
    r->earRootFlare=p.earRootFlare;r->earMarginBow=p.earMarginBow;r->earMarginAsymmetry=p.earMarginAsymmetry;
    Vector3 leftUp=Vec3_Create(p.earOutward*.7f,1,p.earForward*.7f);
    Vector3 rightUp=Vec3_Create(-leftUp.x,leftUp.y,leftUp.z);
    if(!HeadSurfaceRecipe_SetEarPose(r,false,l->leftEarBase,leftUp,Vec3_Create(0,0,1))||
       !HeadSurfaceRecipe_SetEarPose(r,true,l->rightEarBase,rightUp,Vec3_Create(0,0,1)))return false;

    r->unionSmoothness=Math_Min(skullR.x,skullR.y)*.10f;r->headBodySmoothness=Math_Min(r->unionSmoothness,Math_Min(skullR.y,skullR.z)*.10f);r->hasNasalPad=p.archetype==HEAD_ARCHETYPE_CANID;r->hasEars=p.archetype==HEAD_ARCHETYPE_CANID;r->isBeak=p.archetype==HEAD_ARCHETYPE_AVIAN;r->sweptSkull=p.archetype!=HEAD_ARCHETYPE_AVIAN;r->hasTympana=p.archetype==HEAD_ARCHETYPE_LIZARD;
    float tymWeight=Math_Clamp01(p.tympanumSize/.20f);
    r->tympanumDevelopment=tymWeight*tymWeight*(3.0f-2.0f*tymWeight);
    float mouthWidth=cornerX*2,mouthHeight=faceDepth*(p.archetype==HEAD_ARCHETYPE_AVIAN?.70f:1.05f);Color oral=Color_FromRGB(45,8,12);a->oralSystem=Mouth_Create(attachment,Vec3_Zero(),Vec3_Create(mouthWidth,mouthHeight,faceLength*.78f),oral,oral);
    a->oralSystem.shape=p.archetype==HEAD_ARCHETYPE_AVIAN?MOUTH_SHAPE_LOWER_BEAK:MOUTH_SHAPE_TAPERED_MANDIBLE;a->oralSystem.offset=Vec3_Zero();a->oralSystem.jawPivot=Vec3_Create(0,hingeY,hingeZ);a->oralSystem.jawLength=l->mandibularSymphysis.z-hingeZ;a->oralSystem.jawWidth=hingeX*2;a->oralSystem.jawThickness=faceDepth*(p.archetype==HEAD_ARCHETYPE_LIZARD?(.62f+.38f*p.jawDepth):(p.archetype==HEAD_ARCHETYPE_CANID?(.66f+.35f*p.jawDepth):(.28f+.38f*p.jawDepth)));a->oralSystem.jawRearMass=faceDepth*(.30f+.38f*p.jawStrength);a->oralSystem.jawMuscle=faceDepth*(.24f+.38f*p.jawStrength);a->oralSystem.maxJawAngle=p.archetype==HEAD_ARCHETYPE_AVIAN?26:20;a->oralSystem.hingeRadius=faceDepth*(.13f+.14f*p.jawStrength);a->oralSystem.throatRadius=faceDepth*.20f;Mouth_Normalize(&a->oralSystem);a->eyeScale=Vec3_Create(eyeRadius*.92f,eyeRadius*eyeAspect,eyeRadius*(p.archetype==HEAD_ARCHETYPE_CANID?.68f:.86f));return HeadAnatomy_Validate(a)==HEAD_VALID;
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
    m.muzzleTipWidth=2*r->faceTipRadii.x;
    m.interEyeDistance=Vec3_Distance(a->landmarks.leftEyeCenter,a->landmarks.rightEyeCenter);
    m.mouthCornerSpan=Vec3_Distance(a->landmarks.leftMouthCorner,a->landmarks.rightMouthCorner);
    return m;
}

uint32_t HeadAnatomy_Validate(const HeadAnatomy* a){
    if(!a)return HEAD_INVALID_FINITE;
    const HeadLandmarks* l=&a->landmarks;const HeadSurfaceRecipe* r=&a->surface;uint32_t e=0;
    float t=Math_Max(r->craniumRadii.x,.1f)*.001f;
    const Vector3 v[]={l->neckAttachment,l->skullCenter,l->leftOrbit,l->rightOrbit,l->leftOrbitNormal,l->rightOrbitNormal,l->leftEyeCenter,l->rightEyeCenter,l->leftJawHinge,l->rightJawHinge,l->muzzleRoot,l->muzzleTip,l->leftNostril,l->rightNostril,l->leftTympanum,l->rightTympanum,l->leftMouthCorner,l->rightMouthCorner,r->craniumRadii,r->faceRoot,r->faceMid,r->faceTip,r->faceRootRadii,r->faceMidRadii,r->faceTipRadii,r->leftBrowCenter,r->rightBrowCenter};
    for(size_t i=0;i<sizeof(v)/sizeof(v[0]);++i)if(!Head_VectorFinite(v[i]))e|=HEAD_INVALID_FINITE;
    if(!Head_Symmetric(l->leftOrbit,l->rightOrbit,t)||!Head_Symmetric(l->leftOrbitNormal,l->rightOrbitNormal,t)||!Head_Symmetric(l->leftEyeCenter,l->rightEyeCenter,t)||!Head_Symmetric(l->leftJawHinge,l->rightJawHinge,t)||!Head_Symmetric(l->leftNostril,l->rightNostril,t)||!Head_Symmetric(l->leftTympanum,l->rightTympanum,t)||!Head_Symmetric(l->leftMouthCorner,l->rightMouthCorner,t)||!Head_Symmetric(r->leftBrowCenter,r->rightBrowCenter,t))e|=HEAD_INVALID_SYMMETRY;
    if(l->muzzleTip.z<=l->muzzleRoot.z||r->faceMid.z<=r->faceRoot.z||r->faceMid.z>=r->faceTip.z||l->leftJawHinge.z>=l->leftMouthCorner.z||l->leftJawHinge.y>l->leftMouthCorner.y||l->leftTympanum.z>=l->leftOrbit.z||a->oralSystem.maxJawAngle>45)e|=HEAD_INVALID_ORDER;
    float exposure=Vec3_Dot(Vec3_Sub(l->leftEyeCenter,l->leftOrbit),l->leftOrbitNormal);
    float globeDepth=Math_Max(a->eyeScale.x,Math_Max(a->eyeScale.y,a->eyeScale.z));
    float lateralFraction=fabsf(l->leftOrbit.x-r->craniumCenter.x)/Math_Max(r->craniumRadii.x,1e-4f);
    if(!Head_InEllipsoid(l->leftOrbit,r->craniumCenter,r->craniumRadii,1.16f)||!Head_InEllipsoid(l->rightOrbit,r->craniumCenter,r->craniumRadii,1.16f)||!Head_InEllipsoid(l->leftEyeCenter,r->craniumCenter,r->craniumRadii,1.18f)||exposure<=0||exposure>globeDepth*.25f||lateralFraction>.88f||r->orbitRadii.y<a->eyeScale.y*.55f)e|=HEAD_INVALID_ORBITS;
    float span=Math_Max(l->muzzleTip.z-l->muzzleRoot.z,1e-4f),nt=Math_Clamp01((l->leftNostril.z-l->muzzleRoot.z)/span);
    float nw=nt<=.52f?Math_Lerp(r->faceRootRadii.x,r->faceMidRadii.x,nt/.52f):Math_Lerp(r->faceMidRadii.x,r->faceTipRadii.x,(nt-.52f)/.48f);
    if(l->leftNostril.z<l->muzzleRoot.z||l->leftNostril.z>l->muzzleTip.z||fabsf(l->leftNostril.x)>nw*1.02f)e|=HEAD_INVALID_NOSTRILS;
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
Head Head_Create(HeadArchetype archetype,size_t attachment,Vector3 hostRadii){Head h;memset(&h,0,sizeof(h));h.development=1;h.phenotype=archetype==HEAD_ARCHETYPE_CANID?HeadPhenotype_CanidPreset():archetype==HEAD_ARCHETYPE_AVIAN?HeadPhenotype_AvianPreset():HeadPhenotype_LizardPreset();HeadAnatomy_Resolve(&h.phenotype,attachment,hostRadii,&h.anatomy);return h;}
void Head_SetOpenFactor(Head* head,float factor){if(head)Mouth_SetOpenFactor(&head->anatomy.oralSystem,factor);}
