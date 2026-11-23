#include "Head.h"
#include "MathUtils.h"
#include "Quaternion.h"
#include "SDFPrimitives.h"
#include "SDFOperations.h"
#include <math.h>

static void Morph_HeadCranialStations(const HeadSurfaceRecipe* s,Vector3 centers[6],Vector3 radii[6]) {
    if(s->hasCranialProfile) {
        for(int i=0;i<6;++i){centers[i]=s->cranialCenters[i];radii[i]=s->cranialRadii[i];}
        return;
    }
    centers[0]=s->faceTip;                  radii[0]=s->faceTipRadii;
    centers[1]=s->faceMid;                  radii[1]=s->faceMidRadii;
    centers[2]=s->faceRoot;                 radii[2]=s->faceRootRadii;
    centers[3]=s->cranialPostorbitalCenter; radii[3]=s->cranialPostorbitalRadii;
    centers[4]=s->cranialTemporalCenter;    radii[4]=s->cranialTemporalRadii;
    centers[5]=s->cranialOccipitalCenter;   radii[5]=s->cranialOccipitalRadii;
}

static void Morph_HeadJawStations(const HeadAnatomy* h,Vector3 centers[6],Vector3 radii[6]) {
    const HeadSurfaceRecipe* s=&h->surface;
    if(s->hasMandibularProfile) {
        for(int i=0;i<6;++i){centers[i]=s->mandibularCenters[i];radii[i]=s->mandibularRadii[i];}
        return;
    }
    Vector3 headC[6],headR[6];
    Morph_HeadCranialStations(s,headC,headR);
    Mouth m=h->oralSystem;
    Mouth_Normalize(&m);
    float slitThickness=m.slitThickness;
    float cutHalfHeight=slitThickness*.5f;
    float front=s->faceTip.z+s->faceTipRadii.z+0.04f;
    float rear=h->landmarks.leftMouthCorner.z;
    float entrZ=(front+rear)*.5f;
    float entrY=h->landmarks.leftMouthCorner.y+s->oralSlope*(entrZ-h->landmarks.leftMouthCorner.z);
    float lip=entrY-cutHalfHeight;
    float hh=Math_Max(m.hingeRadius,Math_Max(m.throatRadius,cutHalfHeight*2.0f));
    if(hh<1e-4f)hh=cutHalfHeight*2.0f+0.02f;
    float ry=Math_Max(m.jawThickness*.5f,Math_Max(hh*.55f,cutHalfHeight*1.70f));
    ry=Math_Max(ry,m.throatRadius*.60f);
    float flat=Math_Clamp01(fabsf(s->oralSlope)*4.0f);
    bool equine=h->archetype==HEAD_ARCHETYPE_EQUINE;
    for(int j=0;j<6;++j) {
        Vector3 lc=Vec3_Sub(headC[j],m.offset);
        Vector3 lr=headR[j];
        if(equine) {
            float jawHalf=ry*(.95f+.20f*flat);
            lc.y=lip+s->oralSlope*(lc.z-entrZ)-jawHalf+ry*.20f;
            lr.y=jawHalf;
            lr.x*=(j<2?1.0f-.24f*flat:1.0f-.10f*flat);
        } else {
            lc.y=lip+s->oralSlope*(lc.z-entrZ)-ry*(1.0f-.6f*flat);
            lr.y=ry*(1.0f+.4f*flat);
            lr.x*=1.0f-.18f*flat;
        }
        centers[j]=Vec3_Add(lc,m.offset);
        radii[j]=lr;
    }
}

/* Cuando el progenitor aún usa una mandíbula implícita, se le asigna un
 * perfil propio antes de recombinar sus proporciones. La mandíbula termina
 * en el cóndilo y la sínfisis, en vez de copiar las estaciones de la bóveda. */
static void RegionalJawStations(const HeadAnatomy* head,Vector3 centers[6],Vector3 radii[6])
{
    Morph_HeadJawStations(head,centers,radii);
    if(head->surface.hasMandibularProfile || !head->surface.sweptSkull)return;

    const HeadLandmarks* l=&head->landmarks;
    const HeadSurfaceRecipe* s=&head->surface;
    float hingeZ=l->leftJawHinge.z;
    float span=fmaxf(l->mandibularSymphysis.z-hingeZ,s->faceTipRadii.z);
    float end=fminf(hingeZ+span,s->faceTip.z-span*.025f);
    float halfDepth=radii[2].y;
    float rearWidth=fmaxf(l->leftJawHinge.x*.78f,s->faceTipRadii.x*.40f);
    float tipWidth=s->faceTipRadii.x*.71f;
    const float positions[]={1,.82f,.61f,.38f,.16f,0};
    const float depths[]={.62f,.83f,.98f,1.07f,1.01f,.72f};
    const float widths[]={.76f,.96f,1,1,.91f,.71f};
    for(unsigned i=0;i<6;++i) {
        float t=positions[i],z=Math_Lerp(hingeZ,end,t);
        float h=halfDepth*depths[i];
        float lip=l->leftMouthCorner.y+s->oralSlope*(z-l->leftMouthCorner.z)-head->oralSystem.slitThickness*.5f;
        float y=Math_Lerp(l->leftJawHinge.y,lip-h*.60f,Math_Clamp01(t/.38f));
        centers[i]=Vec3_Create(0,y,z);
        radii[i]=Vec3_Create(Math_Lerp(rearWidth,tipWidth,t)*widths[i],h,0);
    }
}

/* Interpolar un marco ortonormal. Dos ejes opuestos no se pueden mezclar
 * componente a componente: la sección se vuelve casi invariante al desplazar
 * el punto en el eje que se anula, extendiendo la lámina fuera de sus cotas. */
static void Morph_Pinna(HeadSurfaceRecipe* out, const HeadSurfaceRecipe* a, const HeadSurfaceRecipe* b,
    bool right, Vector3 root, float t) {
    Vector3 upA=right?a->rightEarDirection:a->earDirection;
    Vector3 upB=right?b->rightEarDirection:b->earDirection;
    Vector3 normalA=right?a->rightEarNormal:a->leftEarNormal;
    Vector3 normalB=right?b->rightEarNormal:b->leftEarNormal;
    Quaternion align=Quat_FromTo(normalA,normalB);
    Quaternion transported=Quat_Slerp(Quat_Identity(),align,t);
    Vector3 normal=Quat_RotateVector(transported,normalA);
    Vector3 finalUp=Quat_RotateVector(align,upA);
    float roll=atan2f(Vec3_Dot(Vec3_Cross(finalUp,upB),normalB),Vec3_Dot(finalUp,upB));
    Vector3 up=Quat_RotateVector(Quat_FromAxisAngle(normal,roll*t),Quat_RotateVector(transported,upA));
    HeadSurfaceRecipe_SetEarPose(out,right,root,up,normal);
}

void HeadAnatomy_Blend(const HeadAnatomy* a,const HeadAnatomy* b,float t,HeadAnatomy* out) {
    if(t<=0.0f){*out=*a;return;}
    if(t>=1.0f){*out=*b;return;}
    *out=t<0.5f?*a:*b;
    out->landmarks.neckAttachment=Vec3_Lerp(a->landmarks.neckAttachment,b->landmarks.neckAttachment,t);
    out->landmarks.skullCenter=Vec3_Lerp(a->landmarks.skullCenter,b->landmarks.skullCenter,t);
    out->landmarks.leftOrbit=Vec3_Lerp(a->landmarks.leftOrbit,b->landmarks.leftOrbit,t);
    out->landmarks.rightOrbit=Vec3_Lerp(a->landmarks.rightOrbit,b->landmarks.rightOrbit,t);
    out->landmarks.leftOrbitNormal=Vec3_Lerp(a->landmarks.leftOrbitNormal,b->landmarks.leftOrbitNormal,t);
    out->landmarks.rightOrbitNormal=Vec3_Lerp(a->landmarks.rightOrbitNormal,b->landmarks.rightOrbitNormal,t);
    out->landmarks.leftEyeCenter=Vec3_Lerp(a->landmarks.leftEyeCenter,b->landmarks.leftEyeCenter,t);
    out->landmarks.rightEyeCenter=Vec3_Lerp(a->landmarks.rightEyeCenter,b->landmarks.rightEyeCenter,t);
    out->landmarks.leftJawHinge=Vec3_Lerp(a->landmarks.leftJawHinge,b->landmarks.leftJawHinge,t);
    out->landmarks.rightJawHinge=Vec3_Lerp(a->landmarks.rightJawHinge,b->landmarks.rightJawHinge,t);
    out->landmarks.muzzleRoot=Vec3_Lerp(a->landmarks.muzzleRoot,b->landmarks.muzzleRoot,t);
    out->landmarks.muzzleTip=Vec3_Lerp(a->landmarks.muzzleTip,b->landmarks.muzzleTip,t);
    out->landmarks.noseTip=Vec3_Lerp(a->landmarks.noseTip,b->landmarks.noseTip,t);
    out->landmarks.leftNostril=Vec3_Lerp(a->landmarks.leftNostril,b->landmarks.leftNostril,t);
    out->landmarks.rightNostril=Vec3_Lerp(a->landmarks.rightNostril,b->landmarks.rightNostril,t);
    out->landmarks.leftTympanum=Vec3_Lerp(a->landmarks.leftTympanum,b->landmarks.leftTympanum,t);
    out->landmarks.rightTympanum=Vec3_Lerp(a->landmarks.rightTympanum,b->landmarks.rightTympanum,t);
    out->landmarks.oralContactPoint=Vec3_Lerp(a->landmarks.oralContactPoint,b->landmarks.oralContactPoint,t);
    out->landmarks.mandibularSymphysis=Vec3_Lerp(a->landmarks.mandibularSymphysis,b->landmarks.mandibularSymphysis,t);
    out->landmarks.oralCavityCenter=Vec3_Lerp(a->landmarks.oralCavityCenter,b->landmarks.oralCavityCenter,t);
    out->landmarks.leftEarBase=Vec3_Lerp(a->landmarks.leftEarBase,b->landmarks.leftEarBase,t);
    out->landmarks.rightEarBase=Vec3_Lerp(a->landmarks.rightEarBase,b->landmarks.rightEarBase,t);
    out->landmarks.upperBeakAnchor=Vec3_Lerp(a->landmarks.upperBeakAnchor,b->landmarks.upperBeakAnchor,t);
    out->landmarks.lowerBeakAnchor=Vec3_Lerp(a->landmarks.lowerBeakAnchor,b->landmarks.lowerBeakAnchor,t);
    out->landmarks.leftMouthCorner=Vec3_Lerp(a->landmarks.leftMouthCorner,b->landmarks.leftMouthCorner,t);
    out->landmarks.rightMouthCorner=Vec3_Lerp(a->landmarks.rightMouthCorner,b->landmarks.rightMouthCorner,t);
    out->surface.craniumCenter=Vec3_Lerp(a->surface.craniumCenter,b->surface.craniumCenter,t);
    out->surface.craniumRadii=Vec3_Lerp(a->surface.craniumRadii,b->surface.craniumRadii,t);
    out->surface.neckCollarContinuity=Math_Lerp(a->surface.neckCollarContinuity,b->surface.neckCollarContinuity,t);
    out->surface.neckCollarPenetration=Math_Lerp(a->surface.neckCollarPenetration,b->surface.neckCollarPenetration,t);
    out->surface.neckCollarRoot=Vec3_Lerp(a->surface.neckCollarRoot,b->surface.neckCollarRoot,t);
    out->surface.neckCollarRadii=Vec3_Lerp(a->surface.neckCollarRadii,b->surface.neckCollarRadii,t);
    out->surface.cranialPostorbitalCenter=Vec3_Lerp(a->surface.cranialPostorbitalCenter,b->surface.cranialPostorbitalCenter,t);
    out->surface.cranialTemporalCenter=Vec3_Lerp(a->surface.cranialTemporalCenter,b->surface.cranialTemporalCenter,t);
    out->surface.cranialOccipitalCenter=Vec3_Lerp(a->surface.cranialOccipitalCenter,b->surface.cranialOccipitalCenter,t);
    out->surface.faceRoot=Vec3_Lerp(a->surface.faceRoot,b->surface.faceRoot,t);
    out->surface.faceMid=Vec3_Lerp(a->surface.faceMid,b->surface.faceMid,t);
    out->surface.faceTip=Vec3_Lerp(a->surface.faceTip,b->surface.faceTip,t);
    out->surface.faceRootRadii=Vec3_Lerp(a->surface.faceRootRadii,b->surface.faceRootRadii,t);
    out->surface.faceMidRadii=Vec3_Lerp(a->surface.faceMidRadii,b->surface.faceMidRadii,t);
    out->surface.faceTipRadii=Vec3_Lerp(a->surface.faceTipRadii,b->surface.faceTipRadii,t);
    out->surface.leftCheekCenter=Vec3_Lerp(a->surface.leftCheekCenter,b->surface.leftCheekCenter,t);
    out->surface.rightCheekCenter=Vec3_Lerp(a->surface.rightCheekCenter,b->surface.rightCheekCenter,t);
    out->surface.cheekRadii=Vec3_Lerp(a->surface.cheekRadii,b->surface.cheekRadii,t);
    out->surface.leftTemporalCenter=Vec3_Lerp(a->surface.leftTemporalCenter,b->surface.leftTemporalCenter,t);
    out->surface.rightTemporalCenter=Vec3_Lerp(a->surface.rightTemporalCenter,b->surface.rightTemporalCenter,t);
    out->surface.temporalRadii=Vec3_Lerp(a->surface.temporalRadii,b->surface.temporalRadii,t);
    out->surface.leftMaxillaryCenter=Vec3_Lerp(a->surface.leftMaxillaryCenter,b->surface.leftMaxillaryCenter,t);
    out->surface.rightMaxillaryCenter=Vec3_Lerp(a->surface.rightMaxillaryCenter,b->surface.rightMaxillaryCenter,t);
    out->surface.maxillaryRadii=Vec3_Lerp(a->surface.maxillaryRadii,b->surface.maxillaryRadii,t);
    out->surface.leftBrowCenter=Vec3_Lerp(a->surface.leftBrowCenter,b->surface.leftBrowCenter,t);
    out->surface.rightBrowCenter=Vec3_Lerp(a->surface.rightBrowCenter,b->surface.rightBrowCenter,t);
    out->surface.browRadii=Vec3_Lerp(a->surface.browRadii,b->surface.browRadii,t);
    out->surface.leftOrbitCenter=Vec3_Lerp(a->surface.leftOrbitCenter,b->surface.leftOrbitCenter,t);
    out->surface.rightOrbitCenter=Vec3_Lerp(a->surface.rightOrbitCenter,b->surface.rightOrbitCenter,t);
    out->surface.orbitRadii=Vec3_Lerp(a->surface.orbitRadii,b->surface.orbitRadii,t);
    out->surface.leftOrbitRimCenter=Vec3_Lerp(a->surface.leftOrbitRimCenter,b->surface.leftOrbitRimCenter,t);
    out->surface.rightOrbitRimCenter=Vec3_Lerp(a->surface.rightOrbitRimCenter,b->surface.rightOrbitRimCenter,t);
    out->surface.orbitRimRadii=Vec3_Lerp(a->surface.orbitRimRadii,b->surface.orbitRimRadii,t);
    out->surface.leftOrbitNormal=Vec3_Lerp(a->surface.leftOrbitNormal,b->surface.leftOrbitNormal,t);
    out->surface.rightOrbitNormal=Vec3_Lerp(a->surface.rightOrbitNormal,b->surface.rightOrbitNormal,t);
    out->surface.orbitSocketDepth=Math_Lerp(a->surface.orbitSocketDepth,b->surface.orbitSocketDepth,t);
    out->surface.eyelidProfile=Vec3_Lerp(a->surface.eyelidProfile,b->surface.eyelidProfile,t);
    out->surface.eyelidCoverage=Math_Lerp(a->surface.eyelidCoverage,b->surface.eyelidCoverage,t);
    out->surface.noseCenter=Vec3_Lerp(a->surface.noseCenter,b->surface.noseCenter,t);
    out->surface.noseRadii=Vec3_Lerp(a->surface.noseRadii,b->surface.noseRadii,t);
    out->surface.nasalColor=Color_Lerp(a->surface.nasalColor,b->surface.nasalColor,t);
    out->surface.leftNostrilCenter=Vec3_Lerp(a->surface.leftNostrilCenter,b->surface.leftNostrilCenter,t);
    out->surface.rightNostrilCenter=Vec3_Lerp(a->surface.rightNostrilCenter,b->surface.rightNostrilCenter,t);
    out->surface.nostrilRadii=Vec3_Lerp(a->surface.nostrilRadii,b->surface.nostrilRadii,t);
    out->surface.leftTympanumCenter=Vec3_Lerp(a->surface.leftTympanumCenter,b->surface.leftTympanumCenter,t);
    out->surface.rightTympanumCenter=Vec3_Lerp(a->surface.rightTympanumCenter,b->surface.rightTympanumCenter,t);
    out->surface.tympanumRadii=Vec3_Lerp(a->surface.tympanumRadii,b->surface.tympanumRadii,t);
    out->surface.tympanumDepth=Math_Lerp(a->surface.tympanumDepth,b->surface.tympanumDepth,t);
    out->surface.earShape=Vec3_Lerp(a->surface.earShape,b->surface.earShape,t);
    out->surface.earConcavity=Math_Lerp(a->surface.earConcavity,b->surface.earConcavity,t);
    out->surface.earTipFraction=Math_Lerp(a->surface.earTipFraction,b->surface.earTipFraction,t);
    out->surface.earLongitudinalCurve=Math_Lerp(a->surface.earLongitudinalCurve,b->surface.earLongitudinalCurve,t);
    out->surface.earRootRoll=Math_Lerp(a->surface.earRootRoll,b->surface.earRootRoll,t);
    out->surface.earTipRoundness=Math_Lerp(a->surface.earTipRoundness,b->surface.earTipRoundness,t);
    out->surface.earFold=Math_Lerp(a->surface.earFold,b->surface.earFold,t);
    out->surface.earRootFlare=Math_Lerp(a->surface.earRootFlare,b->surface.earRootFlare,t);
    out->surface.earMarginBow=Math_Lerp(a->surface.earMarginBow,b->surface.earMarginBow,t);
    out->surface.earMarginAsymmetry=Math_Lerp(a->surface.earMarginAsymmetry,b->surface.earMarginAsymmetry,t);
    out->surface.cranialPostorbitalRadii=Vec3_Lerp(a->surface.cranialPostorbitalRadii,b->surface.cranialPostorbitalRadii,t);
    out->surface.cranialTemporalRadii=Vec3_Lerp(a->surface.cranialTemporalRadii,b->surface.cranialTemporalRadii,t);
    out->surface.cranialOccipitalRadii=Vec3_Lerp(a->surface.cranialOccipitalRadii,b->surface.cranialOccipitalRadii,t);
    out->surface.unionSmoothness=Math_Lerp(a->surface.unionSmoothness,b->surface.unionSmoothness,t);
    out->surface.headBodySmoothness=Math_Lerp(a->surface.headBodySmoothness,b->surface.headBodySmoothness,t);
    out->surface.hasNasalPad=a->surface.hasNasalPad || b->surface.hasNasalPad;
    out->surface.hasEars=a->surface.hasEars || b->surface.hasEars;
    out->surface.sweptSkull=t<0.5f?a->surface.sweptSkull:b->surface.sweptSkull;
    out->surface.isBeak=t<0.5f?a->surface.isBeak:b->surface.isBeak;
    out->surface.isBill=t<0.5f?a->surface.isBill:b->surface.isBill;
    out->surface.tympanumDevelopment=Math_Lerp(a->surface.hasTympana?a->surface.tympanumDevelopment:0,b->surface.hasTympana?b->surface.tympanumDevelopment:0,t);
    out->surface.hasTympana=(a->surface.hasTympana && t<1.0f)||(b->surface.hasTympana && t>0.0f);
    out->surface.faceCapWeight=Math_Lerp(a->surface.faceCapWeight,b->surface.faceCapWeight,t);
    out->surface.nasalDiscWeight=Math_Lerp(a->surface.nasalDiscWeight,b->surface.nasalDiscWeight,t);
    out->surface.faceCapDepth=Math_Lerp(a->surface.faceCapDepth,b->surface.faceCapDepth,t);
    out->surface.jawRearClipWeight=Math_Lerp(a->surface.jawRearClipWeight,b->surface.jawRearClipWeight,t);
    out->surface.oralArch=Math_Lerp(a->surface.oralArch,b->surface.oralArch,t);
    out->surface.oralSlope=Math_Lerp(a->surface.oralSlope,b->surface.oralSlope,t);
    out->surface.nostrilCurve=Math_Lerp(a->surface.nostrilCurve,b->surface.nostrilCurve,t);
    out->surface.faceRounding=Math_Lerp(a->surface.faceRounding,b->surface.faceRounding,t);
    out->surface.equineSculptWeight=Math_Lerp(a->surface.equineSculptWeight,b->surface.equineSculptWeight,t);
    if(a->surface.equineSculptWeight>0.0f && b->surface.equineSculptWeight>0.0f) {
        out->surface.equineNasalRoot=Vec3_Lerp(a->surface.equineNasalRoot,b->surface.equineNasalRoot,t);
        out->surface.equineNasalTip=Vec3_Lerp(a->surface.equineNasalTip,b->surface.equineNasalTip,t);
        out->surface.equineNasalRoof=Vec3_Lerp(a->surface.equineNasalRoof,b->surface.equineNasalRoof,t);
        out->surface.equineCrestStart=Vec3_Lerp(a->surface.equineCrestStart,b->surface.equineCrestStart,t);
        out->surface.equineCrestEnd=Vec3_Lerp(a->surface.equineCrestEnd,b->surface.equineCrestEnd,t);
        out->surface.equineCrestRadii=Vec3_Lerp(a->surface.equineCrestRadii,b->surface.equineCrestRadii,t);
    } else if(a->surface.equineSculptWeight>0.0f || b->surface.equineSculptWeight>0.0f) {
        const HeadSurfaceRecipe* eq=a->surface.equineSculptWeight>0.0f?&a->surface:&b->surface;
        out->surface.equineNasalRoot=eq->equineNasalRoot;
        out->surface.equineNasalTip=eq->equineNasalTip;
        out->surface.equineNasalRoof=eq->equineNasalRoof;
        out->surface.equineCrestStart=eq->equineCrestStart;
        out->surface.equineCrestEnd=eq->equineCrestEnd;
        out->surface.equineCrestRadii=eq->equineCrestRadii;
    }
    out->surface.beakWeight=Math_Lerp(a->surface.beakWeight,b->surface.beakWeight,t);
    if(a->surface.beakWeight>0.0f && b->surface.beakWeight>0.0f) {
        out->surface.beakBase=Vec3_Lerp(a->surface.beakBase,b->surface.beakBase,t);
        out->surface.beakMid=Vec3_Lerp(a->surface.beakMid,b->surface.beakMid,t);
        out->surface.beakTip=Vec3_Lerp(a->surface.beakTip,b->surface.beakTip,t);
        out->surface.beakBaseRadii=Vec3_Lerp(a->surface.beakBaseRadii,b->surface.beakBaseRadii,t);
        out->surface.beakShape=Vec3_Lerp(a->surface.beakShape,b->surface.beakShape,t);
        out->surface.beakTipShape=Vec3_Lerp(a->surface.beakTipShape,b->surface.beakTipShape,t);
        out->surface.beakLowerTip=Vec3_Lerp(a->surface.beakLowerTip,b->surface.beakLowerTip,t);
        out->surface.beakLowerRadii=Vec3_Lerp(a->surface.beakLowerRadii,b->surface.beakLowerRadii,t);
    } else if(a->surface.beakWeight>0.0f || b->surface.beakWeight>0.0f) {
        const HeadSurfaceRecipe* bk=a->surface.beakWeight>0.0f?&a->surface:&b->surface;
        out->surface.beakBase=bk->beakBase;
        out->surface.beakMid=bk->beakMid;
        out->surface.beakTip=bk->beakTip;
        out->surface.beakBaseRadii=bk->beakBaseRadii;
        out->surface.beakShape=bk->beakShape;
        out->surface.beakTipShape=bk->beakTipShape;
        out->surface.beakLowerTip=bk->beakLowerTip;
        out->surface.beakLowerRadii=bk->beakLowerRadii;
    }
    if(a->surface.hasCranialProfile || b->surface.hasCranialProfile) {
        Vector3 ca[6],cb[6],ra[6],rb[6];
        Morph_HeadCranialStations(&a->surface,ca,ra);
        Morph_HeadCranialStations(&b->surface,cb,rb);
        for(int i=0;i<6;++i) {
            out->surface.cranialCenters[i]=Vec3_Lerp(ca[i],cb[i],t);
            out->surface.cranialRadii[i]=Vec3_Lerp(ra[i],rb[i],t);
        }
        out->surface.hasCranialProfile=true;
    } else {
        out->surface.hasCranialProfile=false;
    }
    if(a->surface.hasMandibularProfile || b->surface.hasMandibularProfile) {
        Vector3 ja[6],jb[6],ra[6],rb[6];
        Morph_HeadJawStations(a,ja,ra);
        Morph_HeadJawStations(b,jb,rb);
        for(int i=0;i<6;++i) {
            out->surface.mandibularCenters[i]=Vec3_Lerp(ja[i],jb[i],t);
            out->surface.mandibularRadii[i]=Vec3_Lerp(ra[i],rb[i],t);
        }
        out->surface.hasMandibularProfile=true;
    } else {
        out->surface.hasMandibularProfile=false;
    }
    out->oralSystem.offset=Vec3_Lerp(a->oralSystem.offset,b->oralSystem.offset,t);
    out->oralSystem.rotation=Vec3_Lerp(a->oralSystem.rotation,b->oralSystem.rotation,t);
    out->oralSystem.scale=Vec3_Lerp(a->oralSystem.scale,b->oralSystem.scale,t);
    out->oralSystem.insideColor=Color_Lerp(a->oralSystem.insideColor,b->oralSystem.insideColor,t);
    out->oralSystem.lipColor=Color_Lerp(a->oralSystem.lipColor,b->oralSystem.lipColor,t);
    out->oralSystem.openFactor=Math_Lerp(a->oralSystem.openFactor,b->oralSystem.openFactor,t);
    out->oralSystem.slitThickness=Math_Lerp(a->oralSystem.slitThickness,b->oralSystem.slitThickness,t);
    out->oralSystem.slitSoftness=Math_Lerp(a->oralSystem.slitSoftness,b->oralSystem.slitSoftness,t);
    out->oralSystem.cornerRadius=Math_Lerp(a->oralSystem.cornerRadius,b->oralSystem.cornerRadius,t);
    out->oralSystem.jawPivot=Vec3_Lerp(a->oralSystem.jawPivot,b->oralSystem.jawPivot,t);
    out->oralSystem.jawLength=Math_Lerp(a->oralSystem.jawLength,b->oralSystem.jawLength,t);
    out->oralSystem.jawWidth=Math_Lerp(a->oralSystem.jawWidth,b->oralSystem.jawWidth,t);
    out->oralSystem.jawThickness=Math_Lerp(a->oralSystem.jawThickness,b->oralSystem.jawThickness,t);
    out->oralSystem.jawRearMass=Math_Lerp(a->oralSystem.jawRearMass,b->oralSystem.jawRearMass,t);
    out->oralSystem.jawMuscle=Math_Lerp(a->oralSystem.jawMuscle,b->oralSystem.jawMuscle,t);
    out->oralSystem.maxJawAngle=Math_Lerp(a->oralSystem.maxJawAngle,b->oralSystem.maxJawAngle,t);
    out->oralSystem.hingeRadius=Math_Lerp(a->oralSystem.hingeRadius,b->oralSystem.hingeRadius,t);
    out->oralSystem.throatRadius=Math_Lerp(a->oralSystem.throatRadius,b->oralSystem.throatRadius,t);
    out->oralSystem.cranium=Vec3_Lerp(a->oralSystem.cranium,b->oralSystem.cranium,t);
    out->oralSystem.snout=Vec3_Lerp(a->oralSystem.snout,b->oralSystem.snout,t);
    out->oralSystem.cheeks=Vec3_Lerp(a->oralSystem.cheeks,b->oralSystem.cheeks,t);
    out->oralSystem.brows=Vec3_Lerp(a->oralSystem.brows,b->oralSystem.brows,t);
    out->eyeScale=Vec3_Lerp(a->eyeScale,b->eyeScale,t);
    if(a->surface.hasEars && b->surface.hasEars) {
        /* La concavidad es una cavidad de concha. Al invertir una pinna y
         * convertirla en lámina, el cuenco desaparece antes de afinarla. */
        if(Vec3_Dot(a->surface.earDirection,b->surface.earDirection)<0) {
            bool sheetA=a->surface.earShape.x>9*a->surface.earShape.z;
            bool sheetB=b->surface.earShape.x>9*b->surface.earShape.z;
            if(sheetA||sheetB) {
                float roll=sinf(t*3.14159265f);
                float tuck=roll*roll;
                /* Durante el giro se pliega hacia la inserción. La lámina
                 * se despliega al recuperar una orientación de reposo. */
                out->surface.earShape.x*=1-.48f*tuck;
                out->surface.earShape.y*=1-.24f*tuck;
                out->surface.earShape.z*=1+.20f*tuck;
                out->surface.earFold*=1-.72f*tuck;
                out->surface.earLongitudinalCurve*=1-.55f*tuck;
                float inset=out->surface.craniumRadii.x*.15f*tuck;
                out->landmarks.leftEarBase.x-=inset;
                out->landmarks.rightEarBase.x+=inset;
                if(sheetA!=sheetB) {
                    float sheet=Math_Clamp01((sheetA?1-t:t)*4.f);
                    out->surface.earConcavity*=1-sheet*sheet*(3-2*sheet);
                }
            }
        }
        Morph_Pinna(&out->surface,&a->surface,&b->surface,false,out->landmarks.leftEarBase,t);
        Morph_Pinna(&out->surface,&a->surface,&b->surface,true,out->landmarks.rightEarBase,t);
    }
    /* La pinna ausente conserva el marco de su homóloga y se contrae en su inserción. */
    const HeadSurfaceRecipe* ear=a->surface.hasEars?&a->surface:&b->surface;
    if (a->surface.hasEars != b->surface.hasEars) {
        float weight=a->surface.hasEars?1-t:t;
        Vector3 scale={out->surface.craniumRadii.x/ear->craniumRadii.x,
                       out->surface.craniumRadii.y/ear->craniumRadii.y,
                       out->surface.craniumRadii.z/ear->craniumRadii.z};
        Vector3 left=Vec3_Sub(Vec3_Sub(ear->leftEarCenter,Vec3_Scale(ear->earDirection,ear->earShape.y*.5f)),ear->craniumCenter);
        Vector3 right=Vec3_Sub(Vec3_Sub(ear->rightEarCenter,Vec3_Scale(ear->rightEarDirection,ear->earShape.y*.5f)),ear->craniumCenter);
        out->surface.leftEarCenter=Vec3_Add(out->surface.craniumCenter,Vec3_Create(left.x*scale.x,left.y*scale.y,left.z*scale.z));
        out->surface.rightEarCenter=Vec3_Add(out->surface.craniumCenter,Vec3_Create(right.x*scale.x,right.y*scale.y,right.z*scale.z));
        out->surface.earLongitudinalCurve=ear->earLongitudinalCurve*weight;
        out->surface.earFold=ear->earFold*weight;
        out->surface.earConcavity=ear->earConcavity;
        out->surface.earTipFraction=ear->earTipFraction;
        out->surface.earRootRoll=ear->earRootRoll;
        out->surface.earTipRoundness=ear->earTipRoundness;
        out->surface.earRootFlare=ear->earRootFlare;
        out->surface.earMarginBow=ear->earMarginBow;
        out->surface.earMarginAsymmetry=ear->earMarginAsymmetry;
        out->landmarks.leftEarBase=out->surface.leftEarCenter;
        out->landmarks.rightEarBase=out->surface.rightEarCenter;
        out->surface.leftEarCenter=Vec3_Add(out->surface.leftEarCenter,Vec3_Scale(ear->earDirection,ear->earShape.y*weight*.5f));
        out->surface.rightEarCenter=Vec3_Add(out->surface.rightEarCenter,Vec3_Scale(ear->rightEarDirection,ear->earShape.y*weight*.5f));
        out->surface.earShape=Vec3_Scale(ear->earShape,weight);
        out->surface.earRadii=Vec3_Scale(ear->earRadii,weight);
        out->surface.earDirection=ear->earDirection;
        out->surface.rightEarDirection=ear->rightEarDirection;
        out->surface.leftEarSide=ear->leftEarSide;
        out->surface.rightEarSide=ear->rightEarSide;
        out->surface.leftEarNormal=ear->leftEarNormal;
        out->surface.rightEarNormal=ear->rightEarNormal;
        out->surface.hasEars=weight>0;
    }
    if (!a->surface.hasNasalPad) out->surface.noseRadii=Vec3_Scale(b->surface.noseRadii,t);
    if (!b->surface.hasNasalPad) out->surface.noseRadii=Vec3_Scale(a->surface.noseRadii,1-t);
    out->surface.hasNasalPad=(a->surface.hasNasalPad && t<1)||(b->surface.hasNasalPad && t>0);
}


bool HeadHybridProfile_Validate(const HeadHybridProfile *profile)
{
    if (!profile) return true;
    if (!profile->active) return !profile->hasRegionalProfile;
    const Vector3 radii[] = {profile->primaryRadii,profile->secondaryRadii};
    for (size_t i=0; i<2; ++i) {
        Vector3 r = radii[i];
        if (!isfinite(r.x) || !isfinite(r.y) || !isfinite(r.z) ||
            r.x <= 0 || r.y <= 0 || r.z <= 0 || r.x > 64 || r.y > 64 || r.z > 64) return false;
    }
    if (profile->hasRegionalProfile && !HeadAnatomy_ValidateProfile(&profile->regional))
        return false;
    return Head_Supports(profile->secondary.archetype) && isfinite(profile->blend) &&
        profile->blend >= 0 && profile->blend <= 1;
}

Vector3 HeadHybridProfile_EnvelopeRatio(const HeadHybridProfile *profile)
{
    return Vec3_Lerp(profile->primaryRadii,profile->secondaryRadii,profile->blend);
}

static void FitRostralMaxilla(HeadSurfaceRecipe* surface);

bool HeadAnatomy_ResolveHybrid(const HeadPhenotype *primary, const HeadHybridProfile *hybrid,
    size_t attachment, Vector3 envelope, HeadAnatomy *out)
{
    if (!hybrid || !hybrid->active) return HeadAnatomy_Resolve(primary,attachment,envelope,out);
    if (!HeadHybridProfile_Validate(hybrid)) return false;
    if (hybrid->hasRegionalProfile) {
        *out = hybrid->regional;
        HeadAnatomy_EditProfile(out,primary,&hybrid->referenceControls);
        HeadAnatomy_ScaleProfile(out,envelope);
        out->archetype = primary->archetype;
        HeadSurfaceRecipe *s=&out->surface;
        if(s->nasalDiscWeight>0 && s->equineSculptWeight>0 && s->hasNasalPad) {
            Vector3 tip=s->cranialCenters[0], section=s->cranialRadii[0];
            float cap=fminf(s->faceTip.z+s->faceCapDepth-tip.z,section.y*.55f);
            s->noseRadii.x=fminf(s->noseRadii.x,section.x*.91f);
            s->noseRadii.y=fminf(s->noseRadii.y,section.y*.68f);
            s->noseCenter.y=Math_Clamp(s->noseCenter.y,tip.y-section.y*.20f,tip.y+section.y*.20f);
            s->noseCenter.z=fminf(s->noseCenter.z,tip.z+cap-s->noseRadii.z*.72f);
            FitRostralMaxilla(s);
        }
        out->surface.nasalColor = primary->nasalColor;
        out->attachmentBodyPartIndex = out->oralSystem.bodyPartIndex = attachment;
        return true;
    }
    Vector3 a = hybrid->primaryRadii, b = hybrid->secondaryRadii;
    HeadAnatomy first, second;
    if (!HeadAnatomy_Resolve(primary,attachment,
            Vec3_Create(a.x*envelope.x,a.y*envelope.y,a.z*envelope.z),&first) ||
        !HeadAnatomy_Resolve(&hybrid->secondary,attachment,
            Vec3_Create(b.x*envelope.x,b.y*envelope.y,b.z*envelope.z),&second)) return false;
    HeadAnatomy_Blend(&first,&second,hybrid->blend,out);

    /* Las ranuras de la cabeza y la clase de articulación siguen el plano,
     * aunque cráneo, músculos y mandíbula tengan una geometría intermedia. */
    out->archetype = first.archetype;
    out->surface.isBeak = first.surface.isBeak;
    out->surface.isBill = first.surface.isBill;
    out->oralSystem.shape = first.oralSystem.shape;
    out->landmarks.leftOrbitNormal = Vec3_Normalize(out->landmarks.leftOrbitNormal);
    out->landmarks.rightOrbitNormal = Vec3_Normalize(out->landmarks.rightOrbitNormal);
    out->surface.leftOrbitNormal = out->landmarks.leftOrbitNormal;
    out->surface.rightOrbitNormal = out->landmarks.rightOrbitNormal;
    HeadSurfaceRecipe *surface = &out->surface;
    Vector3 nose = surface->noseRadii;
    if (surface->hasNasalPad && nose.x>1e-5f && nose.y>1e-5f && nose.z>1e-5f) {
        /* Si una base carece de planum, el morph puede dejar la fosa detrás
         * de su nueva almohadilla. Proyecta el cutter al volumen receptor. */
        Vector3 offset = Vec3_Sub(out->landmarks.leftNostril,surface->noseCenter);
        float extent = Vec3_Length(Vec3_Create(offset.x/nose.x,offset.y/nose.y,offset.z/nose.z));
        if (extent>1.02f) {
            out->landmarks.leftNostril = Vec3_Add(surface->noseCenter,Vec3_Scale(offset,.97f/extent));
            out->landmarks.rightNostril = out->landmarks.leftNostril;
            out->landmarks.rightNostril.x *= -1;
            surface->leftNostrilCenter = out->landmarks.leftNostril;
            surface->rightNostrilCenter = out->landmarks.rightNostril;
        }
    }
    HeadLandmarks *landmarks = &out->landmarks;
    float oralLine = landmarks->leftMouthCorner.y + surface->oralSlope*
        (landmarks->leftJawHinge.z-landmarks->leftMouthCorner.z);
    float excess = landmarks->leftJawHinge.y-oralLine;
    if (excess>0 && excess<surface->craniumRadii.y*.38f) {
        /* La pendiente y la longitud se mezclan por separado. Su producto
         * puede dejar el cóndilo arriba del corte, aunque ambas bases sean válidas. */
        float drop = excess+surface->craniumRadii.y*.002f;
        landmarks->leftJawHinge.y -= drop;
        landmarks->rightJawHinge.y -= drop;
        out->oralSystem.jawPivot.y -= drop;
        if (surface->hasMandibularProfile) {
            surface->mandibularCenters[4].y -= drop*.55f;
            surface->mandibularCenters[5].y -= drop;
        }
    }
    uint32_t invalid = HeadAnatomy_Validate(out);
    return !(invalid & (HEAD_INVALID_FINITE | HEAD_INVALID_SYMMETRY | HEAD_INVALID_ORDER |
        HEAD_INVALID_NECK));
}

HeadPhenotype HeadPhenotype_Interpolate(const HeadPhenotype *a, const HeadPhenotype *b, float weight)
{
    HeadPhenotype p = weight < .5f ? *a : *b;
#define BLEND(field) p.field = Math_Lerp(a->field,b->field,weight)
    BLEND(skullWidth); BLEND(skullHeight); BLEND(skullLength);
    BLEND(muzzleLength); BLEND(muzzleWidth); BLEND(muzzleTaper);
    BLEND(rostrumDepth); BLEND(rostrumDorsalSlope); BLEND(temporalWidth);
    BLEND(temporalDepth); BLEND(eyeSize); BLEND(eyeLaterality);
    BLEND(eyeForwardness); BLEND(eyeDorsality); BLEND(eyelidCoverage);
    BLEND(eyeCompression); BLEND(eyeExposure); BLEND(browProminence);
    BLEND(snoutBluntness); BLEND(jawLength); BLEND(jawDepth);
    BLEND(jawStrength); BLEND(noseScale); BLEND(earSize);
    BLEND(earBaseWidth); BLEND(earWidthRatio); BLEND(earHeightRatio);
    BLEND(earThickness); BLEND(earConcavity); BLEND(earOutward);
    BLEND(earForward); BLEND(earLongitudinalCurve); BLEND(earRootRoll);
    BLEND(earTipRoundness); BLEND(earFold); BLEND(earPointiness);
    BLEND(cheekMass); BLEND(maxillaryWidth); BLEND(mouthWidth);
    BLEND(cephalicIndex); BLEND(stopProminence); BLEND(zygomaticWidth);
    BLEND(masseterMass); BLEND(earRootFlare); BLEND(earMarginBow);
    BLEND(earMarginAsymmetry); BLEND(beakLength); BLEND(beakDepth);
    BLEND(beakTaper); BLEND(beakCurvature); BLEND(nostrilPosition);
    BLEND(orbitDepth); BLEND(tympanumSize);
#undef BLEND
    p.nasalColor = Color_Lerp(a->nasalColor,b->nasalColor,weight);
    return p;
}

static Vector3 RelativeToSkull(Vector3 point, const HeadSurfaceRecipe *from,
    const HeadSurfaceRecipe *to)
{
    Vector3 p = Vec3_Sub(point,from->craniumCenter);
    return Vec3_Add(to->craniumCenter,Vec3_Create(
        p.x*to->craniumRadii.x/from->craniumRadii.x,
        p.y*to->craniumRadii.y/from->craniumRadii.y,
        p.z*to->craniumRadii.z/from->craniumRadii.z));
}

static Vector3 Mirror(Vector3 v)
{
    return Vec3_Create(-v.x,v.y,v.z);
}

static Vector3 FacialPoint(Vector3 p, const HeadSurfaceRecipe *from,
    const HeadSurfaceRecipe *to)
{
    float t = Math_Clamp01((p.z-from->faceRoot.z)/(from->faceTip.z-from->faceRoot.z));
    Vector3 old = Vec3_Lerp(from->faceRoot,from->faceTip,t);
    Vector3 next = Vec3_Lerp(to->faceRoot,to->faceTip,t);
    return Vec3_Add(next,Vec3_Sub(p,old));
}

static void RegionalEars(const HeadAnatomy *a, const HeadAnatomy *b,
    const HeadRegionalInheritance *g, HeadAnatomy *out)
{
    HeadSurfaceRecipe *s = &out->surface;
    s->hasEars = g->inheritEars && (a->surface.hasEars || b->surface.hasEars);
    if (!s->hasEars) {
        s->earShape = s->earRadii = Vec3_Zero();
        return;
    }
    float t = !a->surface.hasEars ? 1 : !b->surface.hasEars ? 0 : g->ear;
    HeadAnatomy source;
    HeadAnatomy_Blend(a,b,t,&source);
    const HeadSurfaceRecipe *ear = &source.surface;
    const HeadSurfaceRecipe *sa = a->surface.hasEars ? &a->surface : ear;
    const HeadSurfaceRecipe *sb = b->surface.hasEars ? &b->surface : ear;
    /* La altura heredada es relativa a la bóveda. Una oreja grande sigue
     * siéndolo sobre un cráneo pequeño sin conservar el tamaño absoluto. */
    float earScaleA = sqrtf(sa->craniumRadii.x*sa->craniumRadii.y);
    float earScaleB = sqrtf(sb->craniumRadii.x*sb->craniumRadii.y);
    float earScale = sqrtf(s->craniumRadii.x*s->craniumRadii.y);
    float sourceScale = sqrtf(ear->craniumRadii.x*ear->craniumRadii.y);
    float fold = 1;
    if (Vec3_Dot(sa->earDirection,sb->earDirection)<0)
        fold -= .50f*t*(1-t);
    s->earShape = Vec3_Create(
        Math_Lerp(sa->earShape.x/earScaleA,sb->earShape.x/earScaleB,g->earWidth)*g->earWidthGain*earScale*fold,
        Math_Lerp(sa->earShape.y/earScaleA,sb->earShape.y/earScaleB,g->earHeight)*g->earHeightGain*earScale*fold,
        ear->earShape.z/sourceScale*g->earThicknessGain*earScale);
#define COPY(field) s->field = ear->field
    COPY(earConcavity); COPY(earTipFraction); COPY(earRootRoll); COPY(earTipRoundness);
    COPY(earRootFlare); COPY(earMarginBow); COPY(earMarginAsymmetry);
#undef COPY
    s->earFold = Math_Lerp(sa->earFold/earScaleA,sb->earFold/earScaleB,g->earCurve)
        *g->earCurveGain*earScale*fold;
    s->earLongitudinalCurve = Math_Lerp(sa->earLongitudinalCurve/earScaleA,
        sb->earLongitudinalCurve/earScaleB,g->earCurve)*g->earCurveGain*earScale*fold;
    Vector3 root = RelativeToSkull(source.landmarks.leftEarBase,ear,s);
    Vector3 radial = Vec3_Sub(root,s->craniumCenter);
    float distance = Vec3_Length(Vec3_Create(radial.x/s->craniumRadii.x,
        radial.y/s->craniumRadii.y,radial.z/s->craniumRadii.z));
    float tall = Math_Clamp01((s->earShape.y/s->craniumRadii.y-1.8f)*.40f);
    float mismatch = fabsf(g->ear-g->cranium);
    float inset = tall*mismatch*.12f;
    float seat = .96f-inset;
    if (distance>seat) root=Vec3_Add(s->craniumCenter,Vec3_Scale(radial,seat/distance));
    s->earRootFlare = Math_Clamp01(s->earRootFlare+inset);
    /* Se conserva el asiento local de la pinna; la inserción acompaña la bóveda. */
    out->landmarks.leftEarBase = root;
    out->landmarks.rightEarBase = Mirror(root);
    Quaternion angle = Quat_FromAxisAngle(Vec3_Create(0,0,1),g->earAngle);
    Vector3 up = Quat_RotateVector(angle,ear->earDirection);
    Vector3 normal = Quat_RotateVector(angle,ear->leftEarNormal);
    HeadSurfaceRecipe_SetEarPose(s,false,root,up,normal);
    HeadSurfaceRecipe_SetEarPose(s,true,Mirror(root),Mirror(up),Mirror(normal));
}

static float OrbitSupport(const HeadSurfaceRecipe *s, const SDFSweepStation sections[6], Vector3 p)
{
    float d=s->sweptSkull ? SDF_EllipticalSweepZ(p,sections,6) :
        SDF_Ellipsoid(Vec3_Sub(p,s->craniumCenter),s->craniumRadii);
    float k=fmaxf(s->unionSmoothness,.005f);
    d=SDF_SmoothUnion(d,SDF_Ellipsoid(Vec3_Sub(p,s->leftTemporalCenter),s->temporalRadii),k*.38f);
    d=SDF_SmoothUnion(d,SDF_Ellipsoid(Vec3_Sub(p,s->leftMaxillaryCenter),s->maxillaryRadii),k*.34f);
    return SDF_SmoothUnion(d,SDF_Ellipsoid(Vec3_Sub(p,s->leftCheekCenter),s->cheekRadii),k*.42f);
}

static float CoreFace(const HeadSurfaceRecipe *s, const SDFSweepStation sections[6],
    bool swept, Vector3 point)
{
    if (swept) return SDF_EllipticalSweepZ(point,sections,6);
    float cranium = SDF_Ellipsoid(Vec3_Sub(point,s->craniumCenter),s->craniumRadii);
    float rostrum = SDF_ThreeSectionEllipticalLoftApprox(point,s->faceRoot,s->faceMid,s->faceTip,
        s->faceRootRadii,s->faceMidRadii,s->faceTipRadii,s->faceRounding);
    return SDF_SmoothUnion(cranium,rostrum,fmaxf(s->unionSmoothness,.005f));
}

static Vector3 SeatFacialVolume(const HeadSurfaceRecipe *s, Vector3 center, Vector3 radii,
    const SDFSweepStation sections[6], bool swept, float fraction)
{
    float depth = -fraction*fminf(radii.x,radii.y);
    if (CoreFace(s,sections,swept,center) <= depth) return center;
    Vector3 axis = s->cranialCenters[5];
    for (unsigned i=1; i<6; ++i) {
        Vector3 a = s->cranialCenters[i-1], b = s->cranialCenters[i];
        if (center.z < b.z) continue;
        axis = Vec3_Lerp(a,b,Math_Clamp01((a.z-center.z)/(a.z-b.z)));
        break;
    }
    if (CoreFace(s,sections,swept,axis) > depth) return center;
    Vector3 outside = center, inside = axis;
    for (unsigned i=0; i<9; ++i) {
        Vector3 point = Vec3_Lerp(outside,inside,.5f);
        if (CoreFace(s,sections,swept,point) > depth) outside = point;
        else inside = point;
    }
    return inside;
}

static void FitRostralMaxilla(HeadSurfaceRecipe *s)
{
    SDFSweepStation sections[6];
    for (unsigned i=0; i<6; ++i)
        sections[i]=(SDFSweepStation){.center=s->cranialCenters[i],
            .width=s->cranialRadii[i].x,.height=s->cranialRadii[i].y};
    if (!s->sweptSkull || !SDF_SweepResolveTangents(sections,6)) return;

    /* El centro puede quedar dentro de un puente inclinado mientras el
     * extremo anterodorsal del maxilar sobresale por encima del morro. */
    Vector3 radii=s->maxillaryRadii;
    Vector3 center=SeatFacialVolume(s,s->leftMaxillaryCenter,radii,sections,true,.72f);
    const Vector3 offsets[]={{0,radii.y,0},{0,.866f*radii.y,.5f*radii.z},
        {0,.5f*radii.y,.866f*radii.z}};
    float overhang=0;
    for (unsigned i=0; i<sizeof(offsets)/sizeof(*offsets); ++i) {
        Vector3 probe=Vec3_Add(center,offsets[i]);
        overhang=fmaxf(overhang,SDF_EllipticalSweepZ(probe,sections,6));
    }
    float correction=Math_Clamp01(overhang/fmaxf(radii.y*.65f,.001f));
    s->leftMaxillaryCenter=Vec3_Sub(center,Vec3_Scale(Vec3_Create(0,radii.y*.42f,radii.z*.37f),correction));
    s->rightMaxillaryCenter=Mirror(s->leftMaxillaryCenter);
    s->maxillaryRadii.y*=1-.42f*correction;
    s->maxillaryRadii.z*=1-.30f*correction;
}

static Vector3 OrbitSeat(const HeadSurfaceRecipe *s, Vector3 orbit, Vector3 normal, float eyeRadius)
{
    SDFSweepStation sections[6];
    for (unsigned i=0; i<6; ++i)
        sections[i]=(SDFSweepStation){.center=s->cranialCenters[i],
            .width=s->cranialRadii[i].x,.height=s->cranialRadii[i].y};
    if (s->sweptSkull && !SDF_SweepResolveTangents(sections,6)) return orbit;
    float distance=OrbitSupport(s,sections,orbit);
    float depth=-eyeRadius*.12f;
    if (distance>=-eyeRadius*.27f && distance<=depth) return orbit;
    /* Los volúmenes temporal y maxilar también pueden cubrir el globo.
     * Proyectar en la normal conserva la posición alrededor de la órbita. */
    float step=eyeRadius*.40f*(distance>depth ? -1 : 1);
    Vector3 start=orbit,end=orbit;
    for (unsigned i=0; i<9; ++i) {
        start=end;
        end=Vec3_Add(end,Vec3_Scale(normal,step));
        float probe=OrbitSupport(s,sections,end);
        if ((probe>depth)==(distance>depth)) continue;
        for (unsigned j=0; j<8; ++j) {
            Vector3 middle=Vec3_Lerp(start,end,.5f);
            if ((OrbitSupport(s,sections,middle)>depth)==(distance>depth)) start=middle;
            else end=middle;
        }
        return end;
    }
    return orbit;
}

void HeadAnatomy_BlendRegional(const HeadAnatomy *a, const HeadAnatomy *b,
    const HeadRegionalInheritance *g, HeadAnatomy *out)
{
    HeadAnatomy_Blend(a,b,g->cranium,out);
    HeadAnatomy_ScaleProfile(out,Vec3_Create(g->skullWidthGain,1,1));
    HeadSurfaceRecipe *s = &out->surface;
    HeadLandmarks *l = &out->landmarks;
    HeadAnatomy face, jaw, orbit, cheek, neck, nose;
    HeadAnatomy_Blend(a,b,g->rostrum,&face);
    HeadAnatomy_Blend(a,b,g->jaw,&jaw);
    HeadAnatomy_Blend(a,b,g->orbit,&orbit);
    HeadAnatomy_Blend(a,b,g->cheek,&cheek);
    HeadAnatomy_Blend(a,b,g->neck,&neck);
    HeadAnatomy_Blend(a,b,g->nose,&nose);

    /* Las secciones atraviesan gradualmente la transición frente-hocico. */
    Vector3 ca[6], cb[6], ra[6], rb[6];
    Morph_HeadCranialStations(&a->surface,ca,ra);
    Morph_HeadCranialStations(&b->surface,cb,rb);
    const float facialInfluence[] = {1,1,.32f,.15f,0,0};
    s->hasCranialProfile = true;
    for (size_t i=0; i<6; ++i) {
        float weight = Math_Lerp(g->cranium,g->rostrum,facialInfluence[i]);
        s->cranialCenters[i] = Vec3_Lerp(ca[i],cb[i],weight);
        s->cranialRadii[i] = Vec3_Lerp(ra[i],rb[i],weight);
        s->cranialRadii[i].x *= Math_Lerp(g->skullWidthGain,1,facialInfluence[i]);
    }
    float span = Math_Lerp((ca[0].z-ca[2].z)/a->surface.craniumRadii.z,
        (cb[0].z-cb[2].z)/b->surface.craniumRadii.z,g->rostrum)*s->craniumRadii.z*g->muzzleLengthGain;
    float mid = Math_Clamp((face.surface.faceMid.z-face.surface.faceRoot.z) /
        (face.surface.faceTip.z-face.surface.faceRoot.z),.16f,.84f);
    for (unsigned i=0; i<2; ++i) {
        s->cranialCenters[i].z = s->cranialCenters[2].z+span*(i ? mid : 1);
        float width = Math_Lerp(ra[i].x/a->surface.craniumRadii.x,
            rb[i].x/b->surface.craniumRadii.x,g->rostrum);
        s->cranialRadii[i].x = width*s->craniumRadii.x;
        float depth = Math_Lerp(ra[i].y/a->surface.craniumRadii.y,
            rb[i].y/b->surface.craniumRadii.y,g->rostrum);
        s->cranialRadii[i].y = depth*s->craniumRadii.y;
    }
    float gap = s->craniumRadii.z*.012f;
    for (size_t i=5; i>0; --i)
        s->cranialCenters[i-1].z = fmaxf(s->cranialCenters[i-1].z,s->cranialCenters[i].z+gap);
    s->faceRoot = s->cranialCenters[2]; s->faceRootRadii = s->cranialRadii[2];
    s->faceMid = s->cranialCenters[1]; s->faceMidRadii = s->cranialRadii[1];
    s->faceTip = s->cranialCenters[0]; s->faceTipRadii = s->cranialRadii[0];
    s->faceRootRadii.z = face.surface.faceRootRadii.z;
    s->faceMidRadii.z = face.surface.faceMidRadii.z;
    s->faceTipRadii.z = face.surface.faceTipRadii.z;
    l->muzzleRoot = FacialPoint(face.landmarks.muzzleRoot,&face.surface,s);
    l->muzzleTip = FacialPoint(face.landmarks.muzzleTip,&face.surface,s);
    s->faceCapWeight = face.surface.faceCapWeight;
    s->faceCapDepth = face.surface.faceCapDepth;
    s->faceRounding = face.surface.faceRounding;
    s->equineSculptWeight = face.surface.equineSculptWeight;
    s->equineNasalRoot = FacialPoint(face.surface.equineNasalRoot,&face.surface,s);
    s->equineNasalTip = FacialPoint(face.surface.equineNasalTip,&face.surface,s);
    s->equineCrestStart = FacialPoint(face.surface.equineCrestStart,&face.surface,s);
    s->equineCrestEnd = FacialPoint(face.surface.equineCrestEnd,&face.surface,s);
    s->equineNasalRoof = face.surface.equineNasalRoof;
    s->equineNasalRoof.z += s->faceMid.z-face.surface.faceMid.z;
    s->equineCrestRadii = face.surface.equineCrestRadii;
    s->beakWeight = face.surface.beakWeight;
    s->beakBase = FacialPoint(face.surface.beakBase,&face.surface,s);
    s->beakMid = FacialPoint(face.surface.beakMid,&face.surface,s);
    s->beakTip = FacialPoint(face.surface.beakTip,&face.surface,s);
    s->beakLowerTip = FacialPoint(face.surface.beakLowerTip,&face.surface,s);
    s->beakBaseRadii = face.surface.beakBaseRadii;
    s->beakShape = face.surface.beakShape;
    s->beakTipShape = face.surface.beakTipShape;
    s->beakLowerRadii = face.surface.beakLowerRadii;

#define SKULL(field, sample) s->field = RelativeToSkull(sample.surface.field,&sample.surface,s)
    SKULL(leftTemporalCenter,cheek); SKULL(rightTemporalCenter,cheek);
    SKULL(leftCheekCenter,cheek); SKULL(rightCheekCenter,cheek);
    Vector3 maxilla = RelativeToSkull(cheek.surface.leftMaxillaryCenter,&cheek.surface,s);
    Vector3 rostralMaxilla = FacialPoint(cheek.surface.leftMaxillaryCenter,&cheek.surface,s);
    /* El pilar anterior sigue el rostro, conservando su relación caudal
     * con el arco cigomático. El cambio es nulo si ambos marcos coinciden. */
    float dependency = Math_Clamp01(fabsf(g->rostrum-g->cheek)*2);
    s->leftMaxillaryCenter = Vec3_Lerp(maxilla,rostralMaxilla,dependency*.72f);
    s->rightMaxillaryCenter = Mirror(s->leftMaxillaryCenter);
    Vector3 upperCheek = Vec3_Lerp(s->leftTemporalCenter,s->leftMaxillaryCenter,.40f);
    s->leftCheekCenter = Vec3_Lerp(s->leftCheekCenter,upperCheek,dependency*.26f);
    s->rightCheekCenter = Mirror(s->leftCheekCenter);
    s->temporalRadii = cheek.surface.temporalRadii;
    s->cheekRadii = cheek.surface.cheekRadii;
    s->maxillaryRadii = cheek.surface.maxillaryRadii;
    /* La prominencia conserva sus radios heredados y se asienta en el
     * volumen receptor, antes de proyectar el reborde y el globo ocular. */
    if (g->facialSeatStrength>0) {
        SDFSweepStation sections[6];
        for (unsigned i=0; i<6; ++i)
            sections[i]=(SDFSweepStation){.center=s->cranialCenters[i],
                .width=s->cranialRadii[i].x,.height=s->cranialRadii[i].y};
        bool swept = s->sweptSkull && SDF_SweepResolveTangents(sections,6);
        Vector3 maxillaSeat = SeatFacialVolume(s,s->leftMaxillaryCenter,s->maxillaryRadii,
            sections,swept,.52f);
        Vector3 cheekSeat = SeatFacialVolume(s,s->leftCheekCenter,s->cheekRadii,sections,swept,.28f);
        s->leftMaxillaryCenter = Vec3_Lerp(s->leftMaxillaryCenter,maxillaSeat,g->facialSeatStrength);
        s->leftCheekCenter = Vec3_Lerp(s->leftCheekCenter,cheekSeat,g->facialSeatStrength);
    }
    s->rightMaxillaryCenter = Mirror(s->leftMaxillaryCenter);
    s->rightCheekCenter = Mirror(s->leftCheekCenter);
    SKULL(neckCollarRoot,neck);
    s->neckCollarRadii = neck.surface.neckCollarRadii;
    s->neckCollarContinuity = neck.surface.neckCollarContinuity;
    s->neckCollarPenetration = neck.surface.neckCollarPenetration;
    s->headBodySmoothness = neck.surface.headBodySmoothness;

    SKULL(leftOrbitRimCenter,orbit); SKULL(rightOrbitRimCenter,orbit);
    SKULL(leftBrowCenter,orbit); SKULL(rightBrowCenter,orbit);
#undef SKULL
    l->leftOrbit = RelativeToSkull(orbit.landmarks.leftOrbit,&orbit.surface,s);
    Vector3 orbitalNormal=Vec3_Normalize(orbit.landmarks.leftOrbitNormal);
    Vector3 seat=OrbitSeat(s,l->leftOrbit,orbitalNormal,orbit.eyeScale.x*g->eyeSizeGain);
    Vector3 shift=Vec3_Sub(seat,l->leftOrbit);
    l->leftOrbit=seat;
    s->leftOrbitRimCenter=Vec3_Add(s->leftOrbitRimCenter,shift);
    s->rightOrbitRimCenter=Mirror(s->leftOrbitRimCenter);
    s->leftBrowCenter=Vec3_Add(s->leftBrowCenter,shift);
    s->rightBrowCenter=Mirror(s->leftBrowCenter);
    l->rightOrbit = Mirror(l->leftOrbit);
    s->leftOrbitCenter = l->leftOrbit; s->rightOrbitCenter = l->rightOrbit;
    l->leftOrbitNormal = s->leftOrbitNormal = orbitalNormal;
    l->rightOrbitNormal = s->rightOrbitNormal = Mirror(l->leftOrbitNormal);
    out->eyeScale = Vec3_Scale(orbit.eyeScale,g->eyeSizeGain);
    float exposure = Vec3_Dot(Vec3_Sub(orbit.landmarks.leftEyeCenter,orbit.landmarks.leftOrbit),
        l->leftOrbitNormal);
    l->leftEyeCenter = Vec3_Add(l->leftOrbit,Vec3_Scale(l->leftOrbitNormal,
        Math_Clamp(exposure,out->eyeScale.x*.025f,out->eyeScale.x*.18f)));
    l->rightEyeCenter = Mirror(l->leftEyeCenter);
    s->orbitRadii = Vec3_Scale(orbit.surface.orbitRadii,g->eyeSizeGain);
    s->orbitRimRadii = Vec3_Scale(orbit.surface.orbitRimRadii,g->eyeSizeGain);
    s->browRadii = orbit.surface.browRadii;
    s->orbitSocketDepth = orbit.surface.orbitSocketDepth*g->eyeSizeGain;
    s->eyelidCoverage = orbit.surface.eyelidCoverage;
    s->eyelidProfile = orbit.surface.eyelidProfile;
    l->leftTympanum = RelativeToSkull(orbit.landmarks.leftTympanum,&orbit.surface,s);
    l->leftTympanum.z = fminf(l->leftTympanum.z,l->leftOrbit.z-gap);
    l->rightTympanum = Mirror(l->leftTympanum);
    s->leftTympanumCenter = l->leftTympanum; s->rightTympanumCenter = l->rightTympanum;
    s->hasTympana = orbit.surface.hasTympana;
    s->tympanumDevelopment = orbit.surface.tympanumDevelopment;
    s->tympanumRadii = orbit.surface.tympanumRadii;
    s->tympanumDepth = orbit.surface.tympanumDepth;

#define FACE(field) l->field = FacialPoint(jaw.landmarks.field,&jaw.surface,s)
    FACE(leftMouthCorner); FACE(rightMouthCorner); FACE(oralContactPoint); FACE(oralCavityCenter);
    FACE(mandibularSymphysis); FACE(upperBeakAnchor); FACE(lowerBeakAnchor);
#undef FACE
    l->leftJawHinge = RelativeToSkull(jaw.landmarks.leftJawHinge,&jaw.surface,s);
    l->leftJawHinge.z = fminf(l->leftJawHinge.z,l->leftMouthCorner.z-gap);
    s->oralSlope = jaw.surface.oralSlope;
    float line = l->leftMouthCorner.y+s->oralSlope*(l->leftJawHinge.z-l->leftMouthCorner.z);
    l->leftJawHinge.y = fminf(l->leftJawHinge.y,line-gap*.1f);
    l->rightJawHinge = Mirror(l->leftJawHinge);
    out->oralSystem = jaw.oralSystem;
    out->oralSystem.openFactor = 0;
    out->oralSystem.jawPivot = Vec3_Lerp(l->leftJawHinge,l->rightJawHinge,.5f);
    out->oralSystem.jawLength = fmaxf(gap,l->mandibularSymphysis.z-l->leftJawHinge.z);
    out->oralSystem.jawWidth = fabsf(l->leftJawHinge.x)*2;
    out->oralSystem.jawThickness *= g->jawDepthGain;
    s->jawRearClipWeight = jaw.surface.jawRearClipWeight;

    RegionalJawStations(a,ca,ra);
    RegionalJawStations(b,cb,rb);
    s->hasMandibularProfile = true;
    for (size_t i=0; i<6; ++i) {
        Vector3 point = Vec3_Lerp(ca[i],cb[i],g->jaw);
        s->mandibularCenters[i] = FacialPoint(point,&jaw.surface,s);
        s->mandibularRadii[i] = Vec3_Lerp(ra[i],rb[i],g->jaw);
        s->mandibularRadii[i].y *= g->jawDepthGain;
        /* La rama caudal sigue el cóndilo; el extremo sigue el corte oral. */
        if (i>=3) {
            Vector3 shift = Vec3_Sub(l->leftJawHinge,
                FacialPoint(jaw.landmarks.leftJawHinge,&jaw.surface,s));
            shift.x = 0;
            s->mandibularCenters[i] = Vec3_Add(s->mandibularCenters[i],Vec3_Scale(shift,(i-2)/3.0f));
        }
    }
    for (size_t i=5; i>0; --i)
        s->mandibularCenters[i-1].z = fmaxf(s->mandibularCenters[i-1].z,s->mandibularCenters[i].z+gap);
    /* El maxilar y la mandíbula comparten la línea labial. Se conserva
     * el espesor heredado al proyectar las secciones sobre ambos labios. */
    float slit = .5f*out->oralSystem.slitThickness;
    float oralCorner=l->leftMouthCorner.z, oralFront=s->faceTip.z+s->faceTipRadii.z;
    s->oralArch=Math_Lerp(s->oralArch,s->faceTipRadii.y*.14f,g->facialSeatStrength*.68f);
    for (size_t i=0; i<3; ++i) {
        Vector3 *upper=&s->cranialCenters[i], *lower=&s->mandibularCenters[i];
        Vector3 *u=&s->cranialRadii[i], *v=&s->mandibularRadii[i];
        float upperLip=l->leftMouthCorner.y+s->oralSlope*(upper->z-oralCorner)+slit+
            Head_OralBend(upper->z,oralCorner,oralFront,s->oralArch);
        float lowerLip=l->leftMouthCorner.y+s->oralSlope*(lower->z-oralCorner)-slit+
            Head_OralBend(lower->z,oralCorner,oralFront,s->oralArch);
        float dropped=fmaxf(0,upper->y-u->y-upperLip+u->y*.12f);
        upper->y-=dropped*.5f;
        u->y+=dropped*.5f;
        if (i==0) {
            lower->z=fminf(lower->z,upper->z+u->z*.55f);
            v->x=fminf(v->x,u->x*.91f);
        }
        float desired=lowerLip-v->y*(i==0 ? .56f : .62f);
        float strength=i==0 ? .86f : i==1 ? .80f : .62f;
        lower->y=Math_Lerp(lower->y,desired,strength);
    }
    s->mandibularCenters[0].z=fmaxf(s->mandibularCenters[0].z,s->mandibularCenters[1].z+gap);
    /* Corrige sólo saltos grandes entre secciones internas del cuerpo
     * mandibular. Los extremos y el asiento sobre el labio se conservan. */
    for (size_t i=1; i<4; ++i) {
        Vector3 *section = &s->mandibularRadii[i];
        float adjacent = sqrtf(s->mandibularRadii[i-1].x*s->mandibularRadii[i+1].x);
        section->x = Math_Clamp(section->x,adjacent*.66f,adjacent*1.5f);
    }
    l->noseTip = FacialPoint(nose.landmarks.noseTip,&nose.surface,s);
    l->leftNostril = FacialPoint(nose.landmarks.leftNostril,&nose.surface,s);
    s->noseCenter = FacialPoint(nose.surface.noseCenter,&nose.surface,s);
    s->noseRadii = nose.surface.noseRadii;
    s->nostrilRadii = nose.surface.nostrilRadii;
    s->nostrilCurve = nose.surface.nostrilCurve;
    s->nasalDiscWeight = s->sweptSkull ? nose.surface.nasalDiscWeight : 0;
    s->faceCapWeight = Math_Lerp(s->faceCapWeight,1,s->nasalDiscWeight);
    s->faceCapDepth = Math_Lerp(s->faceCapDepth,s->cranialRadii[0].y*.40f,s->nasalDiscWeight);
    s->nasalColor = nose.surface.nasalColor;
    s->hasNasalPad = nose.surface.hasNasalPad;
    if (s->hasNasalPad && s->noseRadii.x>0 && s->noseRadii.y>0 && s->noseRadii.z>0) {
        /* Una trufa corta puede proceder de un perfil más alto que el nuevo
         * hocico. Se asienta sobre la sección distal, con sus narinas. */
        Vector3 offset=Vec3_Sub(s->noseCenter,s->faceTip);
        Vector3 support=s->faceTipRadii;
        float extentFromTip=Vec3_Length(Vec3_Create(offset.x/support.x,offset.y/support.y,
            offset.z/fmaxf(support.z,s->craniumRadii.z*.06f)));
        if (extentFromTip>.88f) {
            Vector3 shift=Vec3_Scale(offset,.88f/extentFromTip-1);
            s->noseCenter=Vec3_Add(s->noseCenter,shift);
            l->noseTip=Vec3_Add(l->noseTip,shift);
            l->leftNostril=Vec3_Add(l->leftNostril,shift);
        }
        Vector3 delta = Vec3_Sub(l->leftNostril,s->noseCenter);
        float extent = Vec3_Length(Vec3_Create(delta.x/s->noseRadii.x,delta.y/s->noseRadii.y,
            delta.z/s->noseRadii.z));
        if (extent>1) l->leftNostril = Vec3_Add(s->noseCenter,Vec3_Scale(delta,.97f/extent));
    } else {
        l->leftNostril.z = Math_Clamp(l->leftNostril.z,l->muzzleRoot.z,l->muzzleTip.z);
        l->leftNostril.x = fminf(l->leftNostril.x,
            fminf(s->faceRootRadii.x,fminf(s->faceMidRadii.x,s->faceTipRadii.x)));
    }
    l->rightNostril = Mirror(l->leftNostril);
    s->leftNostrilCenter = l->leftNostril; s->rightNostrilCenter = l->rightNostril;
    RegionalEars(a,b,g,out);
}
