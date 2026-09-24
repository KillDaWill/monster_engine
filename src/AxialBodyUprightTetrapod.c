/** @file AxialBodyUprightTetrapod.c
 * @brief Composición erecta independiente del tronco reptiliano.
 */
#include "AxialBodyUprightTetrapod.h"
#include "CreatureModuleInternal.h"
#include "AttachmentPath.h"
#include "MathUtils.h"
#include <math.h>

bool AxialBodyUprightTetrapod_Resolve(const AxialPhenotype* p, uint32_t module,
    AnatomyGraph* g, AttachmentSlotSet* slots) {
    if (!p || !g || !slots || p->archetype != AXIAL_ARCHETYPE_UPRIGHT_TETRAPOD) return false;
    float s = p->totalScale;
    float widths[] = {p->neckWidth*.38f,p->shoulderWidth*.5f,p->thoraxWidth*.5f,
        p->thoraxWidth*.46f,p->abdomenWidth*.5f,p->pelvicWidth*.5f};
    float heights[] = {p->neckWidth*.36f,p->thoraxHeight*.5f,p->thoraxHeight*.5f,
        p->thoraxHeight*.46f,p->abdomenHeight*.5f,p->pelvicHeight*.5f};
    float z[] = {p->neckLength*.85f,0,-p->trunkLength*.23f,
        -p->trunkLength*.48f,-p->trunkLength*.73f,-p->trunkLength};
    for (unsigned i=0;i<6;++i) {
        const AxialStationMorph* morph=&p->profile[i];
        float h=heights[i]*(morph->heightScale>0?morph->heightScale:1);
        float w=widths[i]*(morph->widthScale>0?morph->widthScale:1);
        /* El techo común conserva el dorso; la reducción de sección eleva el vientre. */
        float y=i==0?p->withersHeight+p->neckLength*.16f:p->withersHeight-h;
        Vector3 c={0,(y+morph->verticalOffset)*s,(z[i]+morph->longitudinalOffset)*s};
        AnatomyRegion region=i==0?ANATOMY_REGION_NECK:i==5?ANATOMY_REGION_PELVIS:ANATOMY_REGION_TRUNK;
        if (!Module_Node(g,module,(uint16_t)(i+1),c,w*s,h*s,2,ANATOMY_ROLE_AXIAL,
            region,ANATOMY_SIDE_CENTER,1)) return false;
        if (i && !Module_Edge(g,module,(uint16_t)i,Anatomy_MakeId(module,(uint16_t)i),
            Anatomy_MakeId(module,(uint16_t)(i+1)),BODY_CONNECTION_AXIAL_LOFT,1)) return false;
    }
    const AttachmentRole roles[]={ATTACHMENT_CERVICAL,ATTACHMENT_PECTORAL_LEFT,
        ATTACHMENT_PECTORAL_RIGHT,ATTACHMENT_PELVIC_LEFT,ATTACHMENT_PELVIC_RIGHT,
        ATTACHMENT_CAUDAL,ATTACHMENT_DORSAL,ATTACHMENT_VENTRAL};
    const uint16_t hosts[]={1,2,2,6,6,6,4,4};
    for (unsigned i=0;i<8;++i) {
        const AnatomyNode* n=AnatomyGraph_FindModuleNode(g,module,hosts[i]);
        AttachmentSlot slot={.id=i+1,.role=roles[i],.hostModuleInstanceId=module,
            .hostNode=n->id,.position=n->center,.forward={0,0,1},.up={0,1,0},.side={1,0,0},
            .hostRadii={n->widthRadius,n->heightRadius,n->widthRadius},.scale=s};
        if (i>=1 && i<=4) slot.position.x=(i%2?1:-1)*n->widthRadius*p->limbAttachmentLateral;
        if (i==5) { slot.position.y+=n->heightRadius*.55f; slot.position.z-=n->widthRadius*.65f; slot.forward=Vec3_Normalize(Vec3_Create(0,-.65f,-1)); slot.up=Vec3_Normalize(Vec3_Create(0,1,-.65f)); }
        if (i==6) slot.position.y+=n->heightRadius;
        if (i==7) slot.position.y-=n->heightRadius;
        if (slots->count>=ATTACHMENT_MAX_SLOTS) return false;
        slots->slots[slots->count++]=slot;
    }
    AttachmentPath path=AttachmentPath_FromAxialDorsal(g,module);
    AttachmentPath_PublishSlots(&path,16,100,module,slots);
    return true;
}

bool AxialBodyUprightTetrapod_BuildSweepStations(const AxialPhenotype* p,
    const AnatomyGraph* g, uint32_t module,
    SDFSweepStation* outStations, int maxStations, int* outCount) {
    if (!p || !g || !outStations || !outCount || maxStations < 9 || p->archetype != AXIAL_ARCHETYPE_UPRIGHT_TETRAPOD) return false;
    float s = p->totalScale;

    const AnatomyNode* neckNode = AnatomyGraph_FindModuleNode(g, module, AXIAL_NODE_NECK);
    const AnatomyNode* pectoralNode = AnatomyGraph_FindModuleNode(g, module, AXIAL_NODE_PECTORAL);
    const AnatomyNode* thoraxANode = AnatomyGraph_FindModuleNode(g, module, AXIAL_NODE_THORAX_ANTERIOR);
    const AnatomyNode* thoraxPNode = AnatomyGraph_FindModuleNode(g, module, AXIAL_NODE_THORAX_POSTERIOR);
    const AnatomyNode* abdomenNode = AnatomyGraph_FindModuleNode(g, module, AXIAL_NODE_ABDOMEN);
    const AnatomyNode* pelvisNode = AnatomyGraph_FindModuleNode(g, module, AXIAL_NODE_PELVIS);
    if (!neckNode || !pectoralNode || !thoraxANode || !thoraxPNode || !abdomenNode || !pelvisNode) return false;

    float trunkLen = p->trunkLength * s;
    float curvature = isfinite(p->cervicalCurvature) && p->cervicalCurvature > 0.0f ? p->cervicalCurvature : 0.45f;
    float dorsalMass = isfinite(p->cervicalDorsalMass) && p->cervicalDorsalMass > 0.0f ? p->cervicalDorsalMass : 1.0f;
    float midNarrow = isfinite(p->cervicalMidNarrowing) && p->cervicalMidNarrowing > 0.0f ? p->cervicalMidNarrowing : 0.82f;
    float withersElev = isfinite(p->withersElevation) && p->withersElevation >= 0.0f ? p->withersElevation : 0.08f;

    /* Las secciones cervicales derivan de los anclajes semánticos, incluidos
     * sus modificadores. Interpolar techo y garganta evita una masa nucal
     * aislada y una cintura artificial entre occipucio y cintura escapular. */
    float z0 = neckNode->center.z;
    float zW = pectoralNode->center.z;
    float deltaW = withersElev * p->withersHeight * s * .5f;
    float dorsal0 = neckNode->center.y + neckNode->heightRadius;
    float ventral0 = neckNode->center.y - neckNode->heightRadius;
    float dorsalW = pectoralNode->center.y + pectoralNode->heightRadius + deltaW;
    float ventralW = pectoralNode->center.y - pectoralNode->heightRadius;
    if (z0 <= zW) return false;
    int count = 0;
    for (unsigned i=0;i<4;++i) {
        float t=(float)i/3.0f;
        /* La masa dorsal redistribuye la transición sin crear un máximo
         * intermedio; el estrechamiento retrasa la expansión hacia el pecho. */
        float dorsalT=powf(t,Math_Clamp(dorsalMass,.5f,2.0f));
        float ventralT=powf(t,.8f+.4f*Math_Clamp01(curvature));
        float widthT=powf(t,1.0f/Math_Clamp(midNarrow,.5f,1.0f));
        float dorsal=Math_Lerp(dorsal0,dorsalW,dorsalT);
        float ventral=Math_Lerp(ventral0,ventralW,ventralT);
        outStations[count++]=(SDFSweepStation){
            .center={Math_Lerp(neckNode->center.x,pectoralNode->center.x,t),
                     (dorsal+ventral)*.5f,Math_Lerp(z0,zW,t)},
            .width=Math_Lerp(neckNode->widthRadius,pectoralNode->widthRadius,widthT),
            .height=(dorsal-ventral)*.5f};
    }

    /* 5. Post-Withers: Transición suave hacia el dorso nivelado del tórax */
    float zPW = -trunkLen * 0.10f;
    if (zPW <= thoraxANode->center.z + 0.05f * s) zPW = (zW + thoraxANode->center.z) * 0.5f;
    float wPW = (pectoralNode->widthRadius + thoraxANode->widthRadius) * .5f;
    float deltaPW = deltaW * .35f;
    float hPW = thoraxANode->heightRadius + deltaPW * .5f;
    float yPW = thoraxANode->center.y + deltaPW * .5f;
    outStations[count++] = (SDFSweepStation){.center = {0, yPW, zPW}, .width = wPW, .height = hPW};

    /* 6. Tórax Anterior */
    outStations[count++] = (SDFSweepStation){.center = thoraxANode->center, .width = thoraxANode->widthRadius, .height = thoraxANode->heightRadius};

    /* 7. Tórax Posterior */
    outStations[count++] = (SDFSweepStation){.center = thoraxPNode->center, .width = thoraxPNode->widthRadius, .height = thoraxPNode->heightRadius};

    /* 8. Abdomen */
    outStations[count++] = (SDFSweepStation){.center = abdomenNode->center, .width = abdomenNode->widthRadius, .height = abdomenNode->heightRadius};

    /* 9. Pelvis */
    outStations[count++] = (SDFSweepStation){.center = pelvisNode->center, .width = pelvisNode->widthRadius, .height = pelvisNode->heightRadius};

    *outCount = count;
    return true;
}
