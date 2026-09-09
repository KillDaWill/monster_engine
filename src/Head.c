#include "Head.h"
#include "MathUtils.h"
#include <math.h>
#include <string.h>

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
    p.noseScale=.50f;p.earSize=0;p.earPointiness=0;p.cheekMass=.45f;
    p.beakLength=.50f;p.beakDepth=.45f;p.beakTaper=.65f;p.beakCurvature=.35f;
    p.nostrilPosition=.72f;p.orbitDepth=.42f;p.tympanumSize=0;
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
    p.skullWidth=.68f;p.skullHeight=.62f;p.skullLength=.58f;p.muzzleLength=.72f;p.muzzleWidth=.46f;p.muzzleTaper=.62f;
    p.rostrumDepth=.58f;p.temporalWidth=.58f;p.temporalDepth=.62f;p.eyeSize=.42f;p.eyeLaterality=.42f;p.eyeForwardness=.72f;
    p.eyeDorsality=.55f;p.eyeExposure=.38f;p.browProminence=.48f;p.snoutBluntness=.58f;
    p.jawLength=.70f;p.jawDepth=.58f;p.jawStrength=.76f;p.noseScale=.72f;p.earSize=.72f;p.earPointiness=.82f;p.cheekMass=.78f;p.nostrilPosition=.92f;p.orbitDepth=.55f;return p;
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
    C(eyeSize);C(eyeLaterality);C(eyeForwardness);C(eyeDorsality);C(eyeExposure);
    C(browProminence);C(snoutBluntness);C(jawLength);C(jawDepth);C(jawStrength);
    C(noseScale);C(earSize);C(earPointiness);C(cheekMass);C(beakLength);C(beakDepth);
    C(beakTaper);C(beakCurvature);C(nostrilPosition);C(orbitDepth);C(tympanumSize);
#undef C
}
HeadPhenotype HeadPhenotype_RandomValid(HeadArchetype archetype,uint32_t seed) {
    HeadPhenotype p=archetype==HEAD_ARCHETYPE_CANID?HeadPhenotype_CanidPreset():archetype==HEAD_ARCHETYPE_AVIAN?HeadPhenotype_AvianPreset():HeadPhenotype_LizardPreset();uint32_t s=seed?seed:1u;
#define V(name,amount) p.name=Head_Vary(&s,p.name,amount)
    V(skullWidth,.18f);V(skullHeight,.16f);V(skullLength,.16f);V(muzzleLength,.16f);V(muzzleWidth,.14f);V(muzzleTaper,.16f);
    V(rostrumDepth,.16f);V(rostrumDorsalSlope,.12f);V(temporalWidth,.15f);V(temporalDepth,.15f);
    V(eyeSize,.14f);V(eyeLaterality,.10f);V(eyeForwardness,.10f);V(eyeDorsality,.12f);V(eyeExposure,.10f);
    V(browProminence,.14f);V(snoutBluntness,.12f);V(jawLength,.14f);V(jawDepth,.13f);V(jawStrength,.16f);
    V(noseScale,.14f);V(earSize,.16f);V(earPointiness,.14f);V(cheekMass,.16f);V(beakLength,.16f);V(beakDepth,.14f);
    V(beakTaper,.12f);V(beakCurvature,.14f);V(nostrilPosition,.10f);V(orbitDepth,.10f);V(tympanumSize,.12f);
#undef V
    HeadPhenotype_Normalize(&p);return p;
}

static bool Head_VectorFinite(Vector3 v){return isfinite(v.x)&&isfinite(v.y)&&isfinite(v.z);}
static bool Head_Symmetric(Vector3 a,Vector3 b,float t){return fabsf(a.x+b.x)<=t&&fabsf(a.y-b.y)<=t&&fabsf(a.z-b.z)<=t;}
static bool Head_InEllipsoid(Vector3 p,Vector3 c,Vector3 r,float margin){Vector3 d=Vec3_Sub(p,c);float x=d.x/Math_Max(r.x,1e-4f),y=d.y/Math_Max(r.y,1e-4f),z=d.z/Math_Max(r.z,1e-4f);return x*x+y*y+z*z<=margin*margin;}

bool HeadAnatomy_Resolve(const HeadPhenotype* source,size_t attachment,Vector3 hostRadii,HeadAnatomy* a){
    if(!source||!a)return false;
    HeadPhenotype p=*source;HeadPhenotype_Normalize(&p);memset(a,0,sizeof(*a));a->archetype=p.archetype;a->attachmentBodyPartIndex=attachment;
    float hx=Math_Max(fabsf(hostRadii.x),.08f),hy=Math_Max(fabsf(hostRadii.y),.08f),hz=Math_Max(fabsf(hostRadii.z),.08f);
    Vector3 skullR=Vec3_Create(hx*(.74f+.48f*p.skullWidth),hy*(.55f+.48f*p.skullHeight),hz*(.68f+.44f*p.skullLength));
    if(p.archetype==HEAD_ARCHETYPE_LIZARD){skullR.x*=.78f;skullR.y*=.62f;skullR.z*=.72f;}
    Vector3 skull=Vec3_Create(0,0,-hz*.10f);float faceLength=(p.archetype==HEAD_ARCHETYPE_AVIAN?p.beakLength:p.muzzleLength)*hz*1.20f+hz*.30f;
    float shapeSkullX=skullR.x;
    float shapeSkullZ=skullR.z;
    float rootWidth=shapeSkullX*(p.archetype==HEAD_ARCHETYPE_LIZARD?(.72f+.18f*p.muzzleWidth):(.30f+.52f*p.muzzleWidth));
    float faceDepth=skullR.y*(p.archetype==HEAD_ARCHETYPE_AVIAN?(.22f+.46f*p.beakDepth):(p.archetype==HEAD_ARCHETYPE_LIZARD?(.76f+.24f*p.rostrumDepth):(.42f+.48f*p.rostrumDepth)));
    float taper=p.archetype==HEAD_ARCHETYPE_AVIAN?p.beakTaper:p.muzzleTaper;
    float tipWidth=rootWidth*(.34f+.22f*(1-taper)+.08f*p.snoutBluntness);
    Vector3 root=Vec3_Create(0,-skullR.y*.08f,skull.z+(p.archetype==HEAD_ARCHETYPE_LIZARD?shapeSkullZ*.24f:skullR.z*.38f));
    float profileDelta=(p.rostrumDorsalSlope-.5f)*faceDepth*1.10f;
    if(p.archetype==HEAD_ARCHETYPE_AVIAN)profileDelta-=p.beakCurvature*faceDepth*.24f;
    Vector3 tip=Vec3_Create(0,root.y+profileDelta,root.z+faceLength);
    Vector3 mid=Vec3_Lerp(root,tip,.52f);
    mid.y+=faceDepth*(.06f+.08f*(1-taper));
    float midWidth=Math_Lerp(rootWidth,tipWidth,.52f)*(.94f-.06f*taper);
    float orbitRadius=Math_Min(skullR.y,skullR.z)*(.25f+.24f*p.eyeSize);
    float sideFactor=p.archetype==HEAD_ARCHETYPE_LIZARD?(.72f+.08f*p.eyeLaterality):(.70f+.18f*p.eyeLaterality);
    float side=skullR.x*sideFactor;
    float eyeZ=skull.z+skullR.z*(p.archetype==HEAD_ARCHETYPE_LIZARD?(-.12f+.24f*p.eyeForwardness):(.13f+.20f*p.eyeForwardness));
    float eyeY=skull.y+skullR.y*(.24f+.34f*p.eyeDorsality);
    Vector3 ln=Vec3_Normalize(Vec3_Create(1,.08f+.08f*p.eyeDorsality,.10f+.22f*p.eyeForwardness)),rn=ln;rn.x*=-1;
    float cornerX=rootWidth*.88f,cornerZ=root.z+faceLength*.18f;
    float jawSkullX=shapeSkullX;
    float jawSkullZ=shapeSkullZ;
    float hingeX=jawSkullX*(.72f+.08f*p.jawStrength);
    float jawEyeZ=skull.z+jawSkullZ*(.10f+.16f*p.eyeForwardness);
    float hingeZ=jawEyeZ-jawSkullZ*(.30f+.12f*p.jawLength),hingeY=Math_Min(-skullR.y*(.30f+.18f*p.jawDepth),-faceDepth*.50f);
    float nt=Math_Clamp(p.nostrilPosition,.20f,.90f),nostrilZ=root.z+(tip.z-root.z)*nt;
    float localWidth=nt<=.52f?Math_Lerp(rootWidth,midWidth,nt/.52f):Math_Lerp(midWidth,tipWidth,(nt-.52f)/.48f);
    float rootHeight=faceDepth*.88f,midHeight=faceDepth*(.64f+.06f*(1-taper));
    float tipHeight=faceDepth*(.50f+.12f*p.snoutBluntness);
    float localHeight=nt<=.52f?Math_Lerp(rootHeight,midHeight,nt/.52f):Math_Lerp(midHeight,tipHeight,(nt-.52f)/.48f);
    float profileY=nt<=.52f?Math_Lerp(root.y,mid.y,nt/.52f):Math_Lerp(mid.y,tip.y,(nt-.52f)/.48f);
    float tymR=orbitRadius*(.24f+.22f*p.tympanumSize);
    HeadLandmarks* l=&a->landmarks;l->headId=ANATOMY_ID_HEAD;l->skullCenter=skull;l->neckAttachment=Vec3_Create(0,-skullR.y*.18f,skull.z-skullR.z*.82f);
    l->leftOrbit=Vec3_Create(side,eyeY,eyeZ);l->rightOrbit=Vec3_Create(-side,eyeY,eyeZ);l->leftOrbitNormal=ln;l->rightOrbitNormal=rn;
    float eyeRadius=orbitRadius*(.74f+.08f*p.eyeSize);
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
    l->leftEarBase=Vec3_Create(skullR.x*.62f,skullR.y*.72f,skull.z-skullR.z*.18f);l->rightEarBase=l->leftEarBase;l->rightEarBase.x*=-1;l->upperBeakAnchor=root;l->lowerBeakAnchor=Vec3_Create(0,-faceDepth*.42f,root.z+faceLength*.12f);
    HeadSurfaceRecipe* r=&a->surface;r->craniumCenter=skull;r->craniumRadii=skullR;
    r->faceRoot=root;r->faceMid=mid;r->faceTip=tip;
    r->faceRootRadii=Vec3_Create(rootWidth,rootHeight,Math_Min(faceLength*.16f,rootWidth*.34f));
    r->faceMidRadii=Vec3_Create(midWidth,midHeight,Math_Min(faceLength*.14f,midWidth*.38f));
    r->faceTipRadii=Vec3_Create(tipWidth,tipHeight,tipWidth*(.55f+.20f*p.snoutBluntness));
    r->faceRounding=Math_Min(faceDepth,midWidth)*(.14f+.10f*p.snoutBluntness);
    r->leftTemporalCenter=Vec3_Create(skullR.x*.43f,-skullR.y*.04f,skull.z-skullR.z*.20f);r->rightTemporalCenter=r->leftTemporalCenter;r->rightTemporalCenter.x*=-1;r->temporalRadii=Vec3_Create(skullR.x*(.25f+.13f*p.temporalWidth),skullR.y*(.36f+.16f*p.temporalDepth),skullR.z*.36f);
    r->leftCheekCenter=Vec3_Create(skullR.x*.50f,-skullR.y*.27f,skull.z+skullR.z*.10f);r->rightCheekCenter=r->leftCheekCenter;r->rightCheekCenter.x*=-1;r->cheekRadii=Vec3_Create(skullR.x*(.19f+.12f*p.cheekMass),skullR.y*(.22f+.16f*p.cheekMass),skullR.z*.28f);
    r->leftMaxillaryCenter=Vec3_Create(rootWidth*.55f,root.y-faceDepth*.30f,root.z+faceLength*.27f);r->rightMaxillaryCenter=r->leftMaxillaryCenter;r->rightMaxillaryCenter.x*=-1;r->maxillaryRadii=Vec3_Create(rootWidth*.28f,faceDepth*.30f,faceLength*.28f);
    r->leftBrowCenter=Vec3_Add(Vec3_Sub(l->leftOrbit,Vec3_Scale(ln,orbitRadius*1.10f)),Vec3_Create(0,orbitRadius*(.52f+.16f*p.browProminence),-orbitRadius*.08f));
    r->rightBrowCenter=Vec3_Add(Vec3_Sub(l->rightOrbit,Vec3_Scale(rn,orbitRadius*1.10f)),Vec3_Create(0,orbitRadius*(.52f+.16f*p.browProminence),-orbitRadius*.08f));
    r->browRadii=Vec3_Create(orbitRadius*(.38f+.12f*p.browProminence),orbitRadius*(.23f+.15f*p.browProminence),orbitRadius*(.52f+.20f*p.browProminence));
    r->leftOrbitCenter=l->leftOrbit;r->rightOrbitCenter=l->rightOrbit;r->orbitRadii=Vec3_Create(orbitRadius,orbitRadius*.94f,orbitRadius*.82f);r->leftOrbitRimCenter=Vec3_Sub(l->leftOrbit,Vec3_Scale(ln,orbitRadius*1.10f));r->rightOrbitRimCenter=Vec3_Sub(l->rightOrbit,Vec3_Scale(rn,orbitRadius*1.10f));r->orbitRimRadii=Vec3_Create(r->orbitRadii.x*1.15f,r->orbitRadii.y*1.15f,r->orbitRadii.z*1.12f);r->leftOrbitNormal=ln;r->rightOrbitNormal=rn;r->orbitSocketDepth=orbitRadius*(.66f+.24f*p.orbitDepth);
    r->noseCenter=Vec3_Create(0,tip.y,tip.z-faceLength*.035f);r->noseRadii=Vec3_Create(tipWidth*(.40f+.18f*p.noseScale),faceDepth*.28f,tipWidth*.26f);r->leftNostrilCenter=l->leftNostril;r->rightNostrilCenter=l->rightNostril;r->nostrilRadii=Vec3_Create(localWidth*.15f,faceDepth*.17f,localWidth*.17f);
    r->leftTympanumCenter=l->leftTympanum;r->rightTympanumCenter=l->rightTympanum;r->tympanumRadii=Vec3_Create(tymR*.42f,tymR,tymR*.86f);r->tympanumDepth=tymR*.55f;
    r->leftEarCenter=Vec3_Add(l->leftEarBase,Vec3_Create(0,skullR.y*(.18f+.36f*p.earSize),0));r->rightEarCenter=r->leftEarCenter;r->rightEarCenter.x*=-1;r->earRadii=Vec3_Create(skullR.x*(.12f+.13f*p.earSize),skullR.y*(.20f+.40f*p.earSize),skullR.z*(.10f+.10f*(1-p.earPointiness)));
    r->unionSmoothness=Math_Min(skullR.x,skullR.y)*.10f;r->headBodySmoothness=Math_Min(r->unionSmoothness,Math_Min(skullR.y,skullR.z)*.10f);r->hasNasalPad=p.archetype==HEAD_ARCHETYPE_CANID;r->hasEars=p.archetype==HEAD_ARCHETYPE_CANID;r->isBeak=p.archetype==HEAD_ARCHETYPE_AVIAN;r->hasTympana=p.archetype==HEAD_ARCHETYPE_LIZARD&&p.tympanumSize>.02f;
    float mouthWidth=cornerX*2,mouthHeight=faceDepth*(p.archetype==HEAD_ARCHETYPE_AVIAN?.70f:1.05f);Color oral=Color_FromRGB(45,8,12);a->oralSystem=Mouth_Create(attachment,Vec3_Zero(),Vec3_Create(mouthWidth,mouthHeight,faceLength*.78f),oral,oral);
    a->oralSystem.shape=p.archetype==HEAD_ARCHETYPE_AVIAN?MOUTH_SHAPE_LOWER_BEAK:MOUTH_SHAPE_TAPERED_MANDIBLE;a->oralSystem.offset=Vec3_Zero();a->oralSystem.jawPivot=Vec3_Create(0,hingeY,hingeZ);a->oralSystem.jawLength=l->mandibularSymphysis.z-hingeZ;a->oralSystem.jawWidth=hingeX*2;a->oralSystem.jawThickness=faceDepth*(p.archetype==HEAD_ARCHETYPE_LIZARD?(.62f+.38f*p.jawDepth):(.28f+.38f*p.jawDepth));a->oralSystem.jawRearMass=faceDepth*(.30f+.38f*p.jawStrength);a->oralSystem.jawMuscle=faceDepth*(.24f+.38f*p.jawStrength);a->oralSystem.maxJawAngle=p.archetype==HEAD_ARCHETYPE_AVIAN?26:20;a->oralSystem.hingeRadius=faceDepth*(.13f+.14f*p.jawStrength);a->oralSystem.throatRadius=faceDepth*.20f;Mouth_Normalize(&a->oralSystem);a->eyeScale=Vec3_Create(eyeRadius*.92f,eyeRadius,eyeRadius*.86f);return HeadAnatomy_Validate(a)==HEAD_VALID;
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
    if(!Head_InEllipsoid(l->leftOrbit,r->craniumCenter,r->craniumRadii,1.16f)||!Head_InEllipsoid(l->rightOrbit,r->craniumCenter,r->craniumRadii,1.16f)||!Head_InEllipsoid(l->leftEyeCenter,r->craniumCenter,r->craniumRadii,1.18f)||exposure<=0||exposure>globeDepth*.25f||lateralFraction>.88f||r->orbitRadii.y<a->eyeScale.y*1.12f)e|=HEAD_INVALID_ORBITS;
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
Head Head_Create(HeadArchetype archetype,size_t attachment,Vector3 hostRadii){Head h;memset(&h,0,sizeof(h));h.phenotype=archetype==HEAD_ARCHETYPE_CANID?HeadPhenotype_CanidPreset():archetype==HEAD_ARCHETYPE_AVIAN?HeadPhenotype_AvianPreset():HeadPhenotype_LizardPreset();HeadAnatomy_Resolve(&h.phenotype,attachment,hostRadii,&h.anatomy);return h;}
void Head_SetOpenFactor(Head* head,float factor){if(head)Mouth_SetOpenFactor(&head->anatomy.oralSystem,factor);}
