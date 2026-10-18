/** @file CreatureMorph.c
 * @brief Interpolación de anatomía resuelta; no elige una especie a mitad del recorrido.
 */
#include "CreatureMorph.h"
#include "AxialBodyUprightTetrapod.h"
#include "MathUtils.h"
#include <math.h>
#include <string.h>

static void MixHead(const HeadAnatomy* a,const HeadAnatomy* b,float t,HeadAnatomy* out) {
    *out=*a;
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
    out->surface.eyelidCoverage=Math_Lerp(a->surface.eyelidCoverage,b->surface.eyelidCoverage,t);
    out->surface.noseCenter=Vec3_Lerp(a->surface.noseCenter,b->surface.noseCenter,t);
    out->surface.noseRadii=Vec3_Lerp(a->surface.noseRadii,b->surface.noseRadii,t);
    out->surface.leftNostrilCenter=Vec3_Lerp(a->surface.leftNostrilCenter,b->surface.leftNostrilCenter,t);
    out->surface.rightNostrilCenter=Vec3_Lerp(a->surface.rightNostrilCenter,b->surface.rightNostrilCenter,t);
    out->surface.nostrilRadii=Vec3_Lerp(a->surface.nostrilRadii,b->surface.nostrilRadii,t);
    out->surface.leftTympanumCenter=Vec3_Lerp(a->surface.leftTympanumCenter,b->surface.leftTympanumCenter,t);
    out->surface.rightTympanumCenter=Vec3_Lerp(a->surface.rightTympanumCenter,b->surface.rightTympanumCenter,t);
    out->surface.tympanumRadii=Vec3_Lerp(a->surface.tympanumRadii,b->surface.tympanumRadii,t);
    out->surface.tympanumDepth=Math_Lerp(a->surface.tympanumDepth,b->surface.tympanumDepth,t);
    out->surface.leftEarCenter=Vec3_Lerp(a->surface.leftEarCenter,b->surface.leftEarCenter,t);
    out->surface.rightEarCenter=Vec3_Lerp(a->surface.rightEarCenter,b->surface.rightEarCenter,t);
    out->surface.earRadii=Vec3_Lerp(a->surface.earRadii,b->surface.earRadii,t);
    out->surface.earShape=Vec3_Lerp(a->surface.earShape,b->surface.earShape,t);
    out->surface.earDirection=Vec3_Lerp(a->surface.earDirection,b->surface.earDirection,t);
    out->surface.rightEarDirection=Vec3_Lerp(a->surface.rightEarDirection,b->surface.rightEarDirection,t);
    out->surface.leftEarSide=Vec3_Lerp(a->surface.leftEarSide,b->surface.leftEarSide,t);
    out->surface.rightEarSide=Vec3_Lerp(a->surface.rightEarSide,b->surface.rightEarSide,t);
    out->surface.leftEarNormal=Vec3_Lerp(a->surface.leftEarNormal,b->surface.leftEarNormal,t);
    out->surface.rightEarNormal=Vec3_Lerp(a->surface.rightEarNormal,b->surface.rightEarNormal,t);
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
    out->surface.sweptSkull=a->surface.sweptSkull || b->surface.sweptSkull;
    out->surface.isBeak=a->surface.isBeak || b->surface.isBeak;
    out->surface.tympanumDevelopment=Math_Lerp(a->surface.hasTympana?a->surface.tympanumDevelopment:0,b->surface.hasTympana?b->surface.tympanumDevelopment:0,t);
    out->surface.hasTympana=a->surface.hasTympana || b->surface.hasTympana;
    out->surface.faceRounding=Math_Lerp(a->surface.faceRounding,b->surface.faceRounding,t);
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

static const BodyConnection* Incoming(const AnatomyGraph* g,AnatomyId id) {
    for(size_t i=0;i<g->connectionCount;++i)
        if(g->connections[i].toId==id)return &g->connections[i];
    return NULL;
}
static const BodyConnection* Edge(const AnatomyGraph* g,const BodyConnection* e) {
    for(size_t i=0;i<g->connectionCount;++i) {
        const BodyConnection* x=&g->connections[i];
        if(x->fromId==e->fromId && x->toId==e->toId && x->kind==e->kind)return x;
    }
    return NULL;
}
static AnatomyNode Counterpart(const AnatomyGraph* own,const AnatomyGraph* other,const AnatomyNode* n) {
    const AnatomyNode* match=AnatomyGraph_FindNode(other,n->id);
    if(match)return *match;
    AnatomyNode collapsed=*n;
    AnatomyId id=n->id;
    const AnatomyNode* root=n;
    for(size_t depth=0;depth<own->nodeCount;++depth) {
        const BodyConnection* parent=Incoming(own,id);
        if(!parent)break;
        id=parent->fromId;
        match=AnatomyGraph_FindNode(other,id);
        if(match){
            collapsed.center=match->center;
            if(n->region==ANATOMY_REGION_ORNAMENT) {
                const AnatomyNode* host=AnatomyGraph_FindNode(own,id);
                if(host) {
                    /* El brote nace en su ranura superficial, no en el centro
                     * del anfitrión. Todos sus nodos colapsan en la misma raíz. */
                    Vector3 offset=Vec3_Sub(root->center,host->center);
                    Vector3 from=host->envelopeRadii,to=match->envelopeRadii;
                    offset.x*=to.x/fmaxf(from.x,1e-6f);
                    offset.y*=to.y/fmaxf(from.y,1e-6f);
                    offset.z*=to.z/fmaxf(from.z,1e-6f);
                    collapsed.center=Vec3_Add(match->center,offset);
                }
            }
            break;
        }
        const AnatomyNode* ancestor=AnatomyGraph_FindNode(own,id);
        if(ancestor&&ancestor->region==ANATOMY_REGION_ORNAMENT)root=ancestor;
    }
    collapsed.widthRadius=collapsed.heightRadius=1e-6f;
    collapsed.envelopeRadii=Vec3_Zero();
    collapsed.development=0;
    return collapsed;
}

/* Los identificadores de módulo son locales a cada receta. Si dos recetas
 * reutilizan uno con significado distinto, se traslada todo el módulo del
 * segundo extremo a un espacio libre antes de formar el grafo unión. */
static bool Morph_ModuleUsed(const AnatomyGraph* a,const AnatomyGraph* b,uint32_t module) {
    for(size_t i=0;i<a->nodeCount;++i)if(a->nodes[i].moduleInstanceId==module)return true;
    for(size_t i=0;i<b->nodeCount;++i)if(b->nodes[i].moduleInstanceId==module)return true;
    for(size_t i=0;i<a->connectionCount;++i)if(a->connections[i].moduleInstanceId==module)return true;
    for(size_t i=0;i<b->connectionCount;++i)if(b->connections[i].moduleInstanceId==module)return true;
    return false;
}
static uint32_t Morph_RemapModule(uint32_t module,const uint32_t* oldModules,
    const uint32_t* newModules,size_t count) {
    for(size_t i=0;i<count;++i)if(oldModules[i]==module)return newModules[i];
    return module;
}
static AnatomyId Morph_RemapId(AnatomyId id,const uint32_t* oldModules,
    const uint32_t* newModules,size_t count) {
    uint32_t module=Morph_RemapModule(id>>16,oldModules,newModules,count);
    return Anatomy_MakeId(module,(uint16_t)id);
}
static bool Morph_RemapConflictingModules(const AnatomyGraph* a,const AnatomyGraph* b,
    AnatomyGraph* remapped) {
    *remapped=*b;
    uint32_t oldModules[ANATOMY_MAX_NODES+ANATOMY_MAX_CONNECTIONS];
    uint32_t newModules[ANATOMY_MAX_NODES+ANATOMY_MAX_CONNECTIONS];
    size_t count=0;
    for(size_t i=0;i<b->nodeCount;++i) {
        const AnatomyNode* source=&b->nodes[i];
        const AnatomyNode* peer=AnatomyGraph_FindNode(a,source->id);
        if(!peer||source->moduleInstanceId==0)continue;
        if(peer->role==source->role&&peer->region==source->region&&
           peer->side==source->side&&peer->localNodeId==source->localNodeId)continue;
        bool known=false;
        for(size_t j=0;j<count;++j)known|=oldModules[j]==source->moduleInstanceId;
        if(!known)oldModules[count++]=source->moduleInstanceId;
    }
    for(size_t i=0;i<b->connectionCount;++i) {
        const BodyConnection* source=&b->connections[i];
        if(source->moduleInstanceId==0)continue;
        bool conflict=false;
        for(size_t j=0;j<a->connectionCount;++j) {
            const BodyConnection* peer=&a->connections[j];
            if(peer->id!=source->id)continue;
            conflict=peer->fromId!=source->fromId||peer->toId!=source->toId||
                peer->kind!=source->kind;
            break;
        }
        if(!conflict)continue;
        bool known=false;
        for(size_t j=0;j<count;++j)known|=oldModules[j]==source->moduleInstanceId;
        if(!known)oldModules[count++]=source->moduleInstanceId;
    }
    for(size_t i=0;i<count;++i) {
        uint32_t candidate=1;
        for(;candidate<=65534;++candidate) {
            bool assigned=false;
            for(size_t j=0;j<i;++j)assigned|=newModules[j]==candidate;
            if(!assigned&&!Morph_ModuleUsed(a,b,candidate))break;
        }
        if(candidate>65534)return false;
        newModules[i]=candidate;
    }
    for(size_t i=0;i<remapped->nodeCount;++i) {
        AnatomyNode* node=&remapped->nodes[i];
        node->id=Morph_RemapId(node->id,oldModules,newModules,count);
        node->moduleInstanceId=Morph_RemapModule(node->moduleInstanceId,
            oldModules,newModules,count);
    }
    for(size_t i=0;i<remapped->connectionCount;++i) {
        BodyConnection* edge=&remapped->connections[i];
        edge->id=Morph_RemapId(edge->id,oldModules,newModules,count);
        edge->fromId=Morph_RemapId(edge->fromId,oldModules,newModules,count);
        edge->toId=Morph_RemapId(edge->toId,oldModules,newModules,count);
        edge->moduleInstanceId=Morph_RemapModule(edge->moduleInstanceId,
            oldModules,newModules,count);
    }
    return AnatomyGraph_Validate(remapped);
}
static bool Morph_ModuleTopologyDifferent(const AnatomyGraph* a,const AnatomyGraph* b,
    uint32_t module,CreatureMorphTopologyEvent* event) {
    memset(event,0,sizeof(*event));event->moduleInstanceId=module;
    bool different=false;
    for(size_t i=0;i<a->nodeCount;++i)if(a->nodes[i].moduleInstanceId==module) {
        const AnatomyNode* n=&a->nodes[i];
        const AnatomyNode* peer=AnatomyGraph_FindNode(b,n->id);
        event->nodesAtStart++;
        different|=!peer||peer->role!=n->role||peer->region!=n->region||
            peer->side!=n->side;
    }
    for(size_t i=0;i<b->nodeCount;++i)
        event->nodesAtEnd+=b->nodes[i].moduleInstanceId==module;
    different|=event->nodesAtStart!=event->nodesAtEnd;
    for(size_t i=0;i<a->connectionCount;++i)if(a->connections[i].moduleInstanceId==module) {
        const BodyConnection* edge=&a->connections[i];
        bool found=false;event->connectionsAtStart++;
        for(size_t j=0;j<b->connectionCount;++j) {
            const BodyConnection* peer=&b->connections[j];
            if(edge->id==peer->id&&edge->fromId==peer->fromId&&
               edge->toId==peer->toId&&edge->kind==peer->kind) {
                found=true;break;
            }
        }
        different|=!found;
    }
    for(size_t i=0;i<b->connectionCount;++i)
        event->connectionsAtEnd+=b->connections[i].moduleInstanceId==module;
    different|=event->connectionsAtStart!=event->connectionsAtEnd;
    return different;
}
size_t CreatureMorph_CollectTopologyEvents(const Monster* a,const Monster* b,
    CreatureMorphTopologyEvent* out,size_t capacity) {
    if(!a||!b||!a->hasAnatomyGraph||!b->hasAnatomyGraph||
       !AnatomyGraph_Validate(&a->anatomyGraph)||
       !AnatomyGraph_Validate(&b->anatomyGraph))return SIZE_MAX;
    AnatomyGraph remapped;
    if(!Morph_RemapConflictingModules(&a->anatomyGraph,&b->anatomyGraph,&remapped))
        return SIZE_MAX;
    uint32_t modules[ANATOMY_MAX_NODES*2+ANATOMY_MAX_CONNECTIONS*2];
    size_t count=0;
    const AnatomyGraph* graphs[2]={&a->anatomyGraph,&remapped};
    for(int graphIndex=0;graphIndex<2;++graphIndex) {
        const AnatomyGraph* graph=graphs[graphIndex];
        for(size_t pass=0;pass<2;++pass) {
            size_t length=pass?graph->connectionCount:graph->nodeCount;
            for(size_t i=0;i<length;++i) {
                uint32_t module=pass?graph->connections[i].moduleInstanceId:
                    graph->nodes[i].moduleInstanceId;
                if(!module)continue;
                size_t at=0;
                while(at<count&&modules[at]<module)at++;
                if(at<count&&modules[at]==module)continue;
                memmove(modules+at+1,modules+at,(count-at)*sizeof(*modules));
                modules[at]=module;count++;
            }
        }
    }
    size_t events=0;
    for(size_t i=0;i<count;++i) {
        CreatureMorphTopologyEvent event;
        if(!Morph_ModuleTopologyDifferent(&a->anatomyGraph,&remapped,
            modules[i],&event))continue;
        if(out&&events<capacity)out[events]=event;
        events++;
    }
    return events;
}
static bool MixGraph(const AnatomyGraph* a,const AnatomyGraph* b,float t,AnatomyGraph* out) {
    AnatomyGraph_Init(out);
    for(int pass=0;pass<2;++pass) {
        const AnatomyGraph* g=pass?b:a;
        for(size_t i=0;i<g->nodeCount;++i) {
            const AnatomyNode* n=&g->nodes[i];
            if(AnatomyGraph_FindNode(out,n->id))continue;
            AnatomyNode x=Counterpart(g,a,n),y=Counterpart(g,b,n),v=*n;
            if(x.role!=y.role || x.region!=y.region || x.side!=y.side)return false;
            v.center=Vec3_Lerp(x.center,y.center,t);
            v.envelopeRadii=Vec3_Lerp(x.envelopeRadii,y.envelopeRadii,t);
            v.widthRadius=Math_Lerp(x.widthRadius,y.widthRadius,t);
            v.heightRadius=Math_Lerp(x.heightRadius,y.heightRadius,t);
            v.development=Math_Lerp(x.development,y.development,t);
            if(!AnatomyGraph_AddNode(out,v))return false;
        }

    }
    for(int pass=0;pass<2;++pass) {
        const AnatomyGraph* g=pass?b:a;
        for(size_t i=0;i<g->connectionCount;++i) {
            const BodyConnection* e=&g->connections[i];
            if(Edge(out,e))continue;
            const BodyConnection* x=Edge(a,e),*y=Edge(b,e);
            BodyConnection v=*e;
            v.widthBulge=Math_Lerp(x?x->widthBulge:0,y?y->widthBulge:0,t);
            v.heightBulge=Math_Lerp(x?x->heightBulge:0,y?y->heightBulge:0,t);
            v.transverseAxis=Vec3_Lerp(x?x->transverseAxis:e->transverseAxis,y?y->transverseAxis:e->transverseAxis,t);
            v.development=Math_Lerp(x?x->development:0,y?y->development:0,t);
            if(!AnatomyGraph_Connect(out,v))return false;
            bool dormantA=!x || a->dormantConnections[x-a->connections];
            bool dormantB=!y || b->dormantConnections[y-b->connections];
            out->dormantConnections[out->connectionCount-1]=(dormantA && dormantB)||(t<=0 && dormantA)||(t>=1 && dormantB);
        }
    }
    /* Los dedos cambian de dirección mediante rotación, sin atravesar longitud cero. */
    for(size_t i=0;i<out->nodeCount;++i) {
        AnatomyNode* n=&out->nodes[i];
        if(n->role!=ANATOMY_ROLE_DIGIT)continue;
        const BodyConnection* edge=Incoming(out,n->id);
        if(!edge)continue;
        const AnatomyNode* xa=AnatomyGraph_FindNode(a,edge->fromId),*xb=AnatomyGraph_FindNode(b,edge->fromId);
        const AnatomyNode* ya=AnatomyGraph_FindNode(a,n->id),*yb=AnatomyGraph_FindNode(b,n->id);
        const AnatomyNode* parent=AnatomyGraph_FindNode(out,edge->fromId);
        if(!xa || !xb || !ya || !yb || !parent)continue;
        Vector3 va=Vec3_Sub(ya->center,xa->center),vb=Vec3_Sub(yb->center,xb->center);
        float la=Vec3_Length(va),lb=Vec3_Length(vb);
        if(la<1e-6f || lb<1e-6f)continue;
        Quaternion q=Quat_Slerp(Quat_Identity(),Quat_FromTo(va,vb),t);
        n->center=Vec3_Add(parent->center,Vec3_Scale(Quat_RotateVector(q,va),Math_Lerp(la,lb,t)/la));
    }
    return AnatomyGraph_Validate(out);
}

/* Resolver el mismo perfil axial que consume el SDF, antes de la interpolación. */
static int Profile(const Monster* m,SDFSweepStation* stations) {
    const AnatomyGraph* g=&m->anatomyGraph;
    if(g->axialProfileCount) {
        memcpy(stations,g->axialProfile,g->axialProfileCount*sizeof(*stations));
        return (int)g->axialProfileCount;
    }
    const AnatomyNode* start=NULL;
    for(size_t i=0;i<g->nodeCount;++i) {
        const AnatomyNode* n=&g->nodes[i];
        if(n->role!=ANATOMY_ROLE_AXIAL || n->region==ANATOMY_REGION_HEAD)continue;
        bool incoming=false;
        for(size_t j=0;j<g->connectionCount;++j) {
            const BodyConnection* e=&g->connections[j];
            const AnatomyNode* from=AnatomyGraph_FindNode(g,e->fromId);
            if(e->toId==n->id && e->kind==BODY_CONNECTION_AXIAL_LOFT && from && from->region!=ANATOMY_REGION_HEAD)incoming=true;
        }
        if(!incoming){if(start)return 0;start=n;}
    }
    if(!start)return 0;
    int count=0;
    const AnatomyNode* n=start;
    if(m->phenotype.axial.archetype==AXIAL_ARCHETYPE_UPRIGHT_TETRAPOD) {
        if(!AxialBodyUprightTetrapod_BuildSweepStations(&m->phenotype.axial,g,start->moduleInstanceId,stations,ANATOMY_MAX_SWEEP_STATIONS,&count))return 0;
        n=AnatomyGraph_FindModuleNode(g,start->moduleInstanceId,AXIAL_NODE_PELVIS);
    } else stations[count++]=(SDFSweepStation){.center=n->center,.width=n->widthRadius,.height=n->heightRadius};
    for(size_t step=0;n && step<g->nodeCount;++step) {
        const AnatomyNode* next=NULL;
        for(size_t i=0;i<g->connectionCount;++i) {
            const BodyConnection* e=&g->connections[i];
            if(e->fromId!=n->id || e->kind!=BODY_CONNECTION_AXIAL_LOFT)continue;
            const AnatomyNode* candidate=AnatomyGraph_FindNode(g,e->toId);
            if(candidate && candidate->region!=ANATOMY_REGION_HEAD){if(next)return 0;next=candidate;}
        }
        n=next;
        if(n){if(count==ANATOMY_MAX_SWEEP_STATIONS)return 0;stations[count++]=(SDFSweepStation){.center=n->center,.width=n->widthRadius,.height=n->heightRadius};}
    }
    return SDF_SweepResolveTangents(stations,count)?count:0;
}
static float Hermite(float a,float b,float da,float db,float u,float dz,float* slope) {
    float u2=u*u,u3=u2*u;
    *slope=((6*u2-6*u)*a+(-6*u2+6*u)*b)/dz+(3*u2-4*u+1)*da+(3*u2-2*u)*db;
    return (2*u3-3*u2+1)*a+(-2*u3+3*u2)*b+(u3-2*u2+u)*dz*da+(u3-u2)*dz*db;
}
static float ProfileCoordinate(float z,const SDFSweepStation* p,int count,float pelvis) {
    if(z>=pelvis)return (z-p[0].center.z)/(pelvis-p[0].center.z);
    return 1+(z-pelvis)/(p[count-1].center.z-pelvis);
}
static float ProfileZ(float u,const SDFSweepStation* p,int count,float pelvis) {
    return u<=1?Math_Lerp(p[0].center.z,pelvis,u):Math_Lerp(pelvis,p[count-1].center.z,u-1);
}
static SDFSweepStation SampleProfile(const SDFSweepStation* p,int count,float z) {
    int i=0;while(i<count-2 && z<p[i+1].center.z)++i;
    const SDFSweepStation* a=&p[i],*b=&p[i+1];
    float dz=b->center.z-a->center.z,t=(z-a->center.z)/dz;
    SDFSweepStation out={.center={Math_Lerp(a->center.x,b->center.x,t),0,z}};
    out.center.y=Hermite(a->center.y,b->center.y,a->centerSlope,b->centerSlope,t,dz,&out.centerSlope);
    out.width=Hermite(a->width,b->width,a->widthSlope,b->widthSlope,t,dz,&out.widthSlope);
    out.height=Hermite(a->height,b->height,a->heightSlope,b->heightSlope,t,dz,&out.heightSlope);
    return out;
}
static bool MixProfile(const Monster* a,const Monster* b,float t,AnatomyGraph* g) {
    SDFSweepStation x[ANATOMY_MAX_SWEEP_STATIONS],y[ANATOMY_MAX_SWEEP_STATIONS];
    int nx=Profile(a,x),ny=Profile(b,y);
    if(nx<2 || ny<2)return true; /* Las anatomías ramificadas conservan sus conexiones. */
    const AnatomyNode* pa=AnatomyGraph_FindFirstRegion(&a->anatomyGraph,ANATOMY_REGION_PELVIS);
    const AnatomyNode* pb=AnatomyGraph_FindFirstRegion(&b->anatomyGraph,ANATOMY_REGION_PELVIS);
    float pelvisX=pa?pa->center.z:(x[0].center.z+x[nx-1].center.z)*.5f;
    float pelvisY=pb?pb->center.z:(y[0].center.z+y[ny-1].center.z)*.5f;
    if(pelvisX>=x[0].center.z || pelvisX<=x[nx-1].center.z)pelvisX=(x[0].center.z+x[nx-1].center.z)*.5f;
    if(pelvisY>=y[0].center.z || pelvisY<=y[ny-1].center.z)pelvisY=(y[0].center.z+y[ny-1].center.z)*.5f;
    float knots[ANATOMY_MAX_SWEEP_STATIONS];int count=0;
    for(int pass=0;pass<2;++pass) {
        const SDFSweepStation* p=pass?y:x;int n=pass?ny:nx;
        for(int i=0;i<n;++i) {
            float u=ProfileCoordinate(p[i].center.z,p,n,pass?pelvisY:pelvisX);
            int j=0;while(j<count && knots[j]<u-1e-6f)++j;
            if(j<count && fabsf(knots[j]-u)<1e-6f)continue;
            if(count==ANATOMY_MAX_SWEEP_STATIONS)return false;
            memmove(knots+j+1,knots+j,(count-j)*sizeof(*knots));knots[j]=u;++count;
        }
    }
    for(int i=0;i<count;++i) {
        SDFSweepStation p=SampleProfile(x,nx,ProfileZ(knots[i],x,nx,pelvisX)),q=SampleProfile(y,ny,ProfileZ(knots[i],y,ny,pelvisY));
        float dx=knots[i]<=1?pelvisX-x[0].center.z:x[nx-1].center.z-pelvisX;
        float dy=knots[i]<=1?pelvisY-y[0].center.z:y[ny-1].center.z-pelvisY;
        float dz=Math_Lerp(dx,dy,t);
        g->axialProfile[i]=(SDFSweepStation){.center=Vec3_Lerp(p.center,q.center,t),
            .width=Math_Lerp(p.width,q.width,t),.height=Math_Lerp(p.height,q.height,t),
            .widthSlope=Math_Lerp(p.widthSlope*dx,q.widthSlope*dy,t)/dz,
            .heightSlope=Math_Lerp(p.heightSlope*dx,q.heightSlope*dy,t)/dz,
            .centerSlope=Math_Lerp(p.centerSlope*dx,q.centerSlope*dy,t)/dz};
    }
    g->axialProfileCount=(size_t)count;
    return true;
}

bool CreatureMorph_Interpolate(const Monster* a,const Monster* b,float age,Monster* out) {
    if(!a || !b || !out || !a->hasAnatomyGraph || !b->hasAnatomyGraph || out==a || out==b)return false;
    float t=isfinite(age)?Math_Clamp01(age):0;
    float u=t*t*(3-2*t);
    if(!Monster_CopyInto(out,t<.5f?a:b))return false;
    AnatomyGraph remapped;
    if(!Morph_RemapConflictingModules(&a->anatomyGraph,&b->anatomyGraph,&remapped)||
       !MixGraph(&a->anatomyGraph,&remapped,u,&out->anatomyGraph) ||
       !MixProfile(a,b,u,&out->anatomyGraph))return false;
    if(!AnatomyGraph_Validate(&out->anatomyGraph))return false;
    out->hasResolvedMorph=true;
    out->hasGrowthAge=true;out->growthAge=t;
    out->phenotype.axial.totalScale=Math_Lerp(a->phenotype.axial.totalScale,b->phenotype.axial.totalScale,u);
    out->surface=SurfacePhenotype_Interpolate(&a->surface,&b->surface,u);
    /* Tipos discretos de cobertura cambian cuando su contribución es nula. */
    if(a->surface.integument.type!=b->surface.integument.type) {
        if(t<.45f) {
            out->surface.integument=a->surface.integument;
            float k=Math_Clamp01(t/.45f);float blend=k*k*(3-2*k);
            out->surface.integument.coverage*=1-blend;
            out->surface.integument.fur.length=Math_Lerp(a->surface.integument.fur.length,0.002f,blend);
            out->surface.integument.fur.density=Math_Lerp(a->surface.integument.fur.density,0.05f,blend);
            out->surface.integument.fur.seed=a->surface.integument.fur.seed;
        } else if(t>.55f) {
            out->surface.integument=b->surface.integument;
            float k=Math_Clamp01((t-.55f)/.45f);float blend=k*k*(3-2*k);
            out->surface.integument.coverage*=blend;
            out->surface.integument.scales.relief*=blend;
            out->surface.integument.scales.seed=b->surface.integument.scales.seed;
        } else {
            out->surface.integument.type=INTEGUMENT_SMOOTH_SKIN;
            out->surface.integument.coverage=0;
        }
    }
    out->phenotype.surface=out->surface;
    out->surfaceMapping=SurfaceMapping_FromAnatomy(&out->anatomyGraph,out->phenotype.axial.totalScale);
    size_t colors=a->colorPalette.count<b->colorPalette.count?a->colorPalette.count:b->colorPalette.count;
    for(size_t i=0;i<colors;++i)out->colorPalette.colors[i]=Color_Lerp(a->colorPalette.colors[i],b->colorPalette.colors[i],u);
    if(a->hasHead && b->hasHead) {
        MixHead(&a->head.anatomy,&b->head.anatomy,u,&out->head.anatomy);
        const AnatomyNode* n=AnatomyGraph_FindFirstRegion(&out->anatomyGraph,ANATOMY_REGION_HEAD);
        if(n) {
            BodyPart* host=&out->bodyParts[out->head.anatomy.attachmentBodyPartIndex];
            host->position=host->positionRender=host->oldPosition=n->center;
            host->width=host->widthRender=2*n->envelopeRadii.x;
            host->height=host->heightRender=2*n->envelopeRadii.y;
            host->length=host->lengthRender=2*n->envelopeRadii.z;
        }
        if(out->mouthCount)out->mouths[0]=out->head.anatomy.oralSystem;
    }
    size_t eyes=a->eyeCount<b->eyeCount?a->eyeCount:b->eyeCount;
    for(size_t i=0;i<eyes;++i) {
        Eye* e=&out->eyes[i];const Eye* x=&a->eyes[i],*y=&b->eyes[i];
        e->offset=Vec3_Lerp(x->offset,y->offset,u);e->scale=Vec3_Lerp(x->scale,y->scale,u);
        e->forward=Vec3_Normalize(Vec3_Lerp(x->forward,y->forward,u));
        e->rotation=Vec3_Create(-asinf(Math_Clamp(e->forward.y,-1,1))*57.2957795f,atan2f(e->forward.x,e->forward.z)*57.2957795f,0);
        e->scleraColor=Color_Lerp(x->scleraColor,y->scleraColor,u);e->irisColor=Color_Lerp(x->irisColor,y->irisColor,u);e->pupilColor=Color_Lerp(x->pupilColor,y->pupilColor,u);
        e->irisScale=Math_Lerp(x->irisScale,y->irisScale,u);e->pupilScale=Math_Lerp(x->pupilScale,y->pupilScale,u);e->pupilAspect=Math_Lerp(x->pupilAspect,y->pupilAspect,u);
    }
    out->angle=Math_Lerp(a->angle,b->angle,u);out->updateSpeed=Math_Lerp(a->updateSpeed,b->updateSpeed,u);
    return true;
}
