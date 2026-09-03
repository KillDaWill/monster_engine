#include "Head.h"
#include "MathUtils.h"
#include <math.h>
#include <string.h>

static float Head_Random01(uint32_t* state) {
    *state = *state * 1664525u + 1013904223u;
    return (float)(*state >> 8) / 16777215.0f;
}

static float Head_Vary(uint32_t* state, float value, float amplitude) {
    return value + (Head_Random01(state) * 2.0f - 1.0f) * amplitude;
}

static HeadPhenotype HeadPhenotype_Base(HeadArchetype archetype) {
    HeadPhenotype p;
    memset(&p, 0, sizeof(p));
    p.archetype = archetype;
    p.skullWidth = 0.55f; p.skullHeight = 0.55f; p.skullLength = 0.55f;
    p.muzzleLength = 0.55f; p.muzzleWidth = 0.55f; p.muzzleTaper = 0.45f;
    p.eyeSize = 0.50f; p.eyeLaterality = 0.50f; p.eyeForwardness = 0.50f;
    p.jawLength = 0.55f; p.jawDepth = 0.50f; p.jawStrength = 0.50f;
    p.noseScale = 0.50f; p.earSize = 0.0f; p.earPointiness = 0.0f;
    p.cheekMass = 0.45f; p.beakLength = 0.50f; p.beakDepth = 0.45f;
    p.beakTaper = 0.65f; p.beakCurvature = 0.35f; p.nostrilPosition = 0.72f;
    return p;
}

HeadPhenotype HeadPhenotype_LizardPreset(void) {
    HeadPhenotype p = HeadPhenotype_Base(HEAD_ARCHETYPE_LIZARD);
    p.skullWidth=.62f; p.skullHeight=.28f; p.skullLength=.58f;
    p.muzzleLength=.72f; p.muzzleWidth=.68f; p.muzzleTaper=.30f;
    p.eyeSize=.46f; p.eyeLaterality=.86f; p.eyeForwardness=.18f;
    p.jawLength=.78f; p.jawDepth=.32f; p.jawStrength=.42f;
    p.noseScale=.25f; p.cheekMass=.28f; p.nostrilPosition=.86f;
    return p;
}

HeadPhenotype HeadPhenotype_CanidPreset(void) {
    HeadPhenotype p = HeadPhenotype_Base(HEAD_ARCHETYPE_CANID);
    p.skullWidth=.68f; p.skullHeight=.62f; p.skullLength=.58f;
    p.muzzleLength=.72f; p.muzzleWidth=.46f; p.muzzleTaper=.62f;
    p.eyeSize=.42f; p.eyeLaterality=.42f; p.eyeForwardness=.72f;
    p.jawLength=.70f; p.jawDepth=.58f; p.jawStrength=.76f;
    p.noseScale=.72f; p.earSize=.72f; p.earPointiness=.82f;
    p.cheekMass=.78f; p.nostrilPosition=.92f;
    return p;
}

HeadPhenotype HeadPhenotype_AvianPreset(void) {
    HeadPhenotype p = HeadPhenotype_Base(HEAD_ARCHETYPE_AVIAN);
    p.skullWidth=.54f; p.skullHeight=.68f; p.skullLength=.48f;
    p.muzzleLength=.25f; p.muzzleWidth=.32f; p.muzzleTaper=.82f;
    p.eyeSize=.68f; p.eyeLaterality=.48f; p.eyeForwardness=.58f;
    p.jawLength=.68f; p.jawDepth=.26f; p.jawStrength=.34f;
    p.noseScale=.18f; p.cheekMass=.20f;
    p.beakLength=.82f; p.beakDepth=.48f; p.beakTaper=.88f;
    p.beakCurvature=.52f; p.nostrilPosition=.43f;
    return p;
}

void HeadPhenotype_Normalize(HeadPhenotype* p) {
    if (!p) return;
    if (p->archetype < HEAD_ARCHETYPE_LIZARD || p->archetype > HEAD_ARCHETYPE_AVIAN) p->archetype = HEAD_ARCHETYPE_LIZARD;
#define CLAMP_FIELD(name) p->name = Math_Clamp01(isfinite(p->name) ? p->name : 0.5f)
    CLAMP_FIELD(skullWidth); CLAMP_FIELD(skullHeight); CLAMP_FIELD(skullLength);
    CLAMP_FIELD(muzzleLength); CLAMP_FIELD(muzzleWidth); CLAMP_FIELD(muzzleTaper);
    CLAMP_FIELD(eyeSize); CLAMP_FIELD(eyeLaterality); CLAMP_FIELD(eyeForwardness);
    CLAMP_FIELD(jawLength); CLAMP_FIELD(jawDepth); CLAMP_FIELD(jawStrength);
    CLAMP_FIELD(noseScale); CLAMP_FIELD(earSize); CLAMP_FIELD(earPointiness);
    CLAMP_FIELD(cheekMass); CLAMP_FIELD(beakLength); CLAMP_FIELD(beakDepth);
    CLAMP_FIELD(beakTaper); CLAMP_FIELD(beakCurvature); CLAMP_FIELD(nostrilPosition);
#undef CLAMP_FIELD
}

HeadPhenotype HeadPhenotype_RandomValid(HeadArchetype archetype, uint32_t seed) {
    HeadPhenotype p = archetype == HEAD_ARCHETYPE_CANID ? HeadPhenotype_CanidPreset() :
                      archetype == HEAD_ARCHETYPE_AVIAN ? HeadPhenotype_AvianPreset() : HeadPhenotype_LizardPreset();
    uint32_t s = seed ? seed : 1u;
#define VARY(name, amount) p.name = Head_Vary(&s, p.name, amount)
    VARY(skullWidth,.20f); VARY(skullHeight,.18f); VARY(skullLength,.16f);
    VARY(muzzleLength,.18f); VARY(muzzleWidth,.16f); VARY(muzzleTaper,.18f);
    VARY(eyeSize,.16f); VARY(eyeLaterality,.14f); VARY(eyeForwardness,.14f);
    VARY(jawLength,.16f); VARY(jawDepth,.14f); VARY(jawStrength,.18f);
    VARY(noseScale,.16f); VARY(earSize,.18f); VARY(earPointiness,.16f);
    VARY(cheekMass,.18f); VARY(beakLength,.18f); VARY(beakDepth,.16f);
    VARY(beakTaper,.14f); VARY(beakCurvature,.16f); VARY(nostrilPosition,.12f);
#undef VARY
    HeadPhenotype_Normalize(&p);
    return p;
}

static bool Head_VectorFinite(Vector3 v) { return isfinite(v.x) && isfinite(v.y) && isfinite(v.z); }
static bool Head_Symmetric(Vector3 a, Vector3 b, float tolerance) {
    return fabsf(a.x + b.x) <= tolerance && fabsf(a.y - b.y) <= tolerance && fabsf(a.z - b.z) <= tolerance;
}
static bool Head_InEllipsoid(Vector3 point, Vector3 center, Vector3 radii, float margin) {
    Vector3 d = Vec3_Sub(point, center);
    float x=d.x/Math_Max(radii.x,1e-4f), y=d.y/Math_Max(radii.y,1e-4f), z=d.z/Math_Max(radii.z,1e-4f);
    return x*x+y*y+z*z <= margin*margin;
}

bool HeadAnatomy_Resolve(const HeadPhenotype* source, size_t attachment, Vector3 hostRadii, HeadAnatomy* a) {
    if (!source || !a) return false;
    HeadPhenotype p=*source; HeadPhenotype_Normalize(&p); memset(a,0,sizeof(*a));
    a->archetype=p.archetype; a->attachmentBodyPartIndex=attachment;
    float hx=Math_Max(fabsf(hostRadii.x),.08f), hy=Math_Max(fabsf(hostRadii.y),.08f), hz=Math_Max(fabsf(hostRadii.z),.08f);
    Vector3 skullR=Vec3_Create(hx*(.72f+.56f*p.skullWidth),hy*(.62f+.58f*p.skullHeight),hz*(.72f+.48f*p.skullLength));
    if(p.archetype==HEAD_ARCHETYPE_LIZARD) skullR.y*=.72f;
    Vector3 skull=Vec3_Create(0,0,-hz*.05f);
    float faceLength=(p.archetype==HEAD_ARCHETYPE_AVIAN?p.beakLength:p.muzzleLength)*hz*1.45f+hz*.28f;
    float faceWidth=skullR.x*(.30f+.55f*p.muzzleWidth);
    float faceDepth=skullR.y*(p.archetype==HEAD_ARCHETYPE_AVIAN?(.18f+.45f*p.beakDepth):(.34f+.38f*p.jawDepth));
    float taper=p.archetype==HEAD_ARCHETYPE_AVIAN?p.beakTaper:p.muzzleTaper;
    Vector3 root=Vec3_Create(0,-skullR.y*.08f,skull.z+skullR.z*.54f);
    Vector3 tip=Vec3_Create(0,root.y-(p.archetype==HEAD_ARCHETYPE_AVIAN?p.beakCurvature*faceDepth*.28f:0),root.z+faceLength);
    float side=skullR.x*(.48f+.20f*p.eyeLaterality);
    float eyeZ=skull.z+skullR.z*(.16f+.24f*p.eyeForwardness);
    float eyeY=skull.y+skullR.y*(.18f+.16f*p.eyeSize);
    float orbitRadius=Math_Min(skullR.x,skullR.y)*(.16f+.20f*p.eyeSize);
    float cornerX=faceWidth*(.78f-.12f*taper), cornerZ=root.z+faceLength*.28f;
    float hingeX=skullR.x*(.62f+.08f*p.jawStrength), hingeZ=cornerZ-faceLength*(.20f+.12f*p.jawLength);
    float hingeY=-skullR.y*(.28f+.18f*p.jawDepth);
    float nostrilT=Math_Clamp(p.nostrilPosition,.18f,.92f);
    float nostrilZ=root.z+(tip.z-root.z)*nostrilT;
    float nostrilX=faceWidth*(.24f+.22f*(1.0f-taper));
    a->landmarks.headId=1u; a->landmarks.skullCenter=skull;
    a->landmarks.neckAttachment=Vec3_Create(0,-skullR.y*.42f,skull.z-skullR.z*.82f);
    a->landmarks.leftOrbit=Vec3_Create(side,eyeY,eyeZ); a->landmarks.rightOrbit=Vec3_Create(-side,eyeY,eyeZ);
    a->landmarks.leftMouthCorner=Vec3_Create(cornerX,-faceDepth*.24f,cornerZ); a->landmarks.rightMouthCorner=Vec3_Create(-cornerX,-faceDepth*.24f,cornerZ);
    a->landmarks.leftJawHinge=Vec3_Create(hingeX,hingeY,hingeZ); a->landmarks.rightJawHinge=Vec3_Create(-hingeX,hingeY,hingeZ);
    a->landmarks.muzzleRoot=root; a->landmarks.muzzleTip=tip; a->landmarks.noseTip=tip;
    a->landmarks.leftNostril=Vec3_Create(nostrilX,tip.y+faceDepth*.18f,nostrilZ); a->landmarks.rightNostril=Vec3_Create(-nostrilX,tip.y+faceDepth*.18f,nostrilZ);
    float earX=skullR.x*.62f, earY=skullR.y*.72f, earZ=skull.z-skullR.z*.18f;
    a->landmarks.leftEarBase=Vec3_Create(earX,earY,earZ); a->landmarks.rightEarBase=Vec3_Create(-earX,earY,earZ);
    a->landmarks.upperBeakAnchor=root; a->landmarks.lowerBeakAnchor=Vec3_Create(0,-faceDepth*.42f,root.z+faceLength*.12f);
    HeadSurfaceRecipe* r=&a->surface; r->craniumCenter=skull; r->craniumRadii=skullR; r->faceRoot=root; r->faceTip=tip;
    r->faceRootRadii=Vec3_Create(faceWidth,faceDepth,faceWidth*.82f);
    r->faceTipRadii=Vec3_Create(faceWidth*(.22f+.52f*(1-taper)),faceDepth*(.24f+.48f*(1-taper)),faceWidth*.30f);
    r->leftCheekCenter=Vec3_Create(skullR.x*.56f,-skullR.y*.13f,skull.z+skullR.z*.08f); r->rightCheekCenter=r->leftCheekCenter; r->rightCheekCenter.x*=-1;
    r->cheekRadii=Vec3_Create(skullR.x*(.28f+.22f*p.cheekMass),skullR.y*(.28f+.26f*p.cheekMass),skullR.z*.38f);
    r->browCenter=Vec3_Create(0,eyeY+orbitRadius*.62f,eyeZ-orbitRadius*.10f); r->browRadii=Vec3_Create(side+orbitRadius*.78f,orbitRadius*.48f,orbitRadius*.58f);
    r->leftOrbitCenter=a->landmarks.leftOrbit; r->rightOrbitCenter=a->landmarks.rightOrbit; r->orbitRadii=Vec3_Create(orbitRadius,orbitRadius*.88f,orbitRadius*.72f);
    r->leftOrbitRimCenter=r->leftOrbitCenter; r->rightOrbitRimCenter=r->rightOrbitCenter; r->orbitRimRadii=Vec3_Scale(r->orbitRadii,1.24f);
    r->noseCenter=Vec3_Create(0,tip.y,tip.z-faceLength*.04f); r->noseRadii=Vec3_Create(faceWidth*(.26f+.26f*p.noseScale),faceDepth*(.28f+.30f*p.noseScale),faceWidth*(.18f+.24f*p.noseScale));
    r->leftNostrilCenter=a->landmarks.leftNostril; r->rightNostrilCenter=a->landmarks.rightNostril; r->nostrilRadii=Vec3_Create(faceWidth*.10f,faceDepth*.12f,faceWidth*.14f);
    r->leftEarCenter=Vec3_Add(a->landmarks.leftEarBase,Vec3_Create(0,skullR.y*(.18f+.36f*p.earSize),0)); r->rightEarCenter=r->leftEarCenter; r->rightEarCenter.x*=-1;
    r->earRadii=Vec3_Create(skullR.x*(.12f+.13f*p.earSize),skullR.y*(.20f+.40f*p.earSize),skullR.z*(.10f+.10f*(1-p.earPointiness)));
    r->unionSmoothness=Math_Min(skullR.x,skullR.y)*.14f; r->hasNasalPad=p.archetype==HEAD_ARCHETYPE_CANID; r->hasEars=p.archetype==HEAD_ARCHETYPE_CANID; r->isBeak=p.archetype==HEAD_ARCHETYPE_AVIAN;
    float mouthWidth=cornerX*2.0f, mouthHeight=faceDepth*(p.archetype==HEAD_ARCHETYPE_AVIAN?.70f:1.05f), mouthDepth=faceLength*.78f;
    Color oralColor=Color_FromRGB(45,8,12);
    a->oralSystem=Mouth_Create(attachment,Vec3_Zero(),Vec3_Create(mouthWidth,mouthHeight,mouthDepth),oralColor,oralColor);
    a->oralSystem.shape=p.archetype==HEAD_ARCHETYPE_AVIAN?MOUTH_SHAPE_LOWER_BEAK:MOUTH_SHAPE_MANDIBLE;
    a->oralSystem.offset=Vec3_Create(0,-faceDepth*.30f,root.z+faceLength*.12f);
    a->oralSystem.jawPivot=Vec3_Create(0,hingeY-a->oralSystem.offset.y,hingeZ-a->oralSystem.offset.z);
    a->oralSystem.jawLength=faceLength*(.76f+.20f*p.jawLength); a->oralSystem.jawWidth=mouthWidth*(.82f+.14f*p.jawStrength);
    a->oralSystem.jawThickness=faceDepth*(.30f+.42f*p.jawDepth); a->oralSystem.jawRearMass=faceDepth*(.34f+.42f*p.jawStrength);
    a->oralSystem.jawMuscle=faceDepth*(.28f+.44f*p.jawStrength); a->oralSystem.maxJawAngle=p.archetype==HEAD_ARCHETYPE_AVIAN?28.0f:38.0f;
    a->oralSystem.hingeRadius=faceDepth*(.16f+.18f*p.jawStrength); a->oralSystem.throatRadius=faceDepth*.24f; Mouth_Normalize(&a->oralSystem);
    a->eyeScale=Vec3_Create(orbitRadius*.72f,orbitRadius*.72f,orbitRadius*.60f);
    return HeadAnatomy_Validate(a)==HEAD_VALID;
}

uint32_t HeadAnatomy_Validate(const HeadAnatomy* a) {
    if(!a) return HEAD_INVALID_FINITE;
    const HeadLandmarks* l=&a->landmarks; const HeadSurfaceRecipe* r=&a->surface; uint32_t e=0; float tol=Math_Max(r->craniumRadii.x,.1f)*.001f;
    const Vector3 values[]={l->neckAttachment,l->skullCenter,l->leftOrbit,l->rightOrbit,l->leftJawHinge,l->rightJawHinge,l->muzzleRoot,l->muzzleTip,l->leftNostril,l->rightNostril,l->leftMouthCorner,l->rightMouthCorner,r->craniumRadii,r->faceRootRadii,r->faceTipRadii};
    for(size_t i=0;i<sizeof(values)/sizeof(values[0]);++i) if(!Head_VectorFinite(values[i])) e|=HEAD_INVALID_FINITE;
    if(!Head_Symmetric(l->leftOrbit,l->rightOrbit,tol)||!Head_Symmetric(l->leftJawHinge,l->rightJawHinge,tol)||!Head_Symmetric(l->leftNostril,l->rightNostril,tol)||!Head_Symmetric(l->leftMouthCorner,l->rightMouthCorner,tol)) e|=HEAD_INVALID_SYMMETRY;
    if(l->muzzleTip.z<=l->muzzleRoot.z||l->leftJawHinge.z>=l->leftMouthCorner.z||l->leftJawHinge.y>l->leftMouthCorner.y||a->oralSystem.maxJawAngle>45.0f) e|=HEAD_INVALID_ORDER;
    if(!Head_InEllipsoid(l->leftOrbit,r->craniumCenter,r->craniumRadii,1.04f)||!Head_InEllipsoid(l->rightOrbit,r->craniumCenter,r->craniumRadii,1.04f)) e|=HEAD_INVALID_ORBITS;
    float faceSpan=Math_Max(l->muzzleTip.z-l->muzzleRoot.z,1e-4f);
    float nt=Math_Clamp01((l->leftNostril.z-l->muzzleRoot.z)/faceSpan);
    Vector3 nasalAxis=Vec3_Lerp(r->faceRoot,r->faceTip,nt), nasalRadii=Vec3_Lerp(r->faceRootRadii,r->faceTipRadii,nt);
    if(l->leftNostril.z<l->muzzleRoot.z||l->leftNostril.z>l->muzzleTip.z||
       !Head_InEllipsoid(l->leftNostril,nasalAxis,nasalRadii,1.02f)||
       !Head_InEllipsoid(l->rightNostril,nasalAxis,nasalRadii,1.02f)) e|=HEAD_INVALID_NOSTRILS;
    if(l->neckAttachment.z>=l->skullCenter.z||l->neckAttachment.y>l->skullCenter.y) e|=HEAD_INVALID_NECK;
    return e;
}

Head Head_Create(HeadArchetype archetype,size_t attachment,Vector3 hostRadii) {
    Head h; memset(&h,0,sizeof(h)); h.phenotype=archetype==HEAD_ARCHETYPE_CANID?HeadPhenotype_CanidPreset():archetype==HEAD_ARCHETYPE_AVIAN?HeadPhenotype_AvianPreset():HeadPhenotype_LizardPreset();
    HeadAnatomy_Resolve(&h.phenotype,attachment,hostRadii,&h.anatomy); return h;
}

void Head_SetOpenFactor(Head* head,float factor) { if(head) Mouth_SetOpenFactor(&head->anatomy.oralSystem,factor); }
