#include "Head.h"
#include "MathUtils.h"
#include "Quaternion.h"
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
    out->surface.tympanumDevelopment=Math_Lerp(a->surface.hasTympana?a->surface.tympanumDevelopment:0,b->surface.hasTympana?b->surface.tympanumDevelopment:0,t);
    out->surface.hasTympana=(a->surface.hasTympana && t<1.0f)||(b->surface.hasTympana && t>0.0f);
    out->surface.faceCapWeight=Math_Lerp(a->surface.faceCapWeight,b->surface.faceCapWeight,t);
    out->surface.faceCapDepth=Math_Lerp(a->surface.faceCapDepth,b->surface.faceCapDepth,t);
    out->surface.jawRearClipWeight=Math_Lerp(a->surface.jawRearClipWeight,b->surface.jawRearClipWeight,t);
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
    if (!profile || !profile->active) return true;
    const Vector3 radii[] = {profile->primaryRadii,profile->secondaryRadii};
    for (size_t i=0; i<2; ++i) {
        Vector3 r = radii[i];
        if (!isfinite(r.x) || !isfinite(r.y) || !isfinite(r.z) ||
            r.x <= 0 || r.y <= 0 || r.z <= 0 || r.x > 64 || r.y > 64 || r.z > 64) return false;
    }
    return Head_Supports(profile->secondary.archetype) && isfinite(profile->blend) &&
        profile->blend >= 0 && profile->blend <= 1;
}

Vector3 HeadHybridProfile_EnvelopeRatio(const HeadHybridProfile *profile)
{
    return Vec3_Lerp(profile->primaryRadii,profile->secondaryRadii,profile->blend);
}

bool HeadAnatomy_ResolveHybrid(const HeadPhenotype *primary, const HeadHybridProfile *hybrid,
    size_t attachment, Vector3 envelope, HeadAnatomy *out)
{
    if (!hybrid || !hybrid->active) return HeadAnatomy_Resolve(primary,attachment,envelope,out);
    if (!HeadHybridProfile_Validate(hybrid)) return false;
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
