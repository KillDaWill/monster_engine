/**
 * @file Creature.c
 * @brief Constructor y resolutor de apariencia y anatomía para criaturas modulares.
 * @author Monster Engine Team
 * @date 2026
 */

#include "Creature.h"
#include "AxialBody.h"
#include "HeadModule.h"
#include "Limb.h"
#include "Tail.h"
#include "Ornament.h"
#include "AttachmentPath.h"
#include "CreatureModuleInternal.h"
#include "CreatureRig.h"
#include "GaitPresets.h"
#include "MonsterAnimation.h"
#include "Monster.h"
#include "MathUtils.h"
#include <math.h>

bool Creature_ResolveAnatomy(const CreatureRecipe* recipe, const CreaturePhenotype* source, AnatomyGraph* graph) {
    if (!graph || !CreatureRecipe_Validate(recipe, source)) return false;
    CreaturePhenotype p = *source;
    CreaturePhenotype_Normalize(&p);
    AnatomyGraph_Init(graph);
    AttachmentSlotSet slots = {0};

    /* Primer pase: módulos axiales publican estaciones y ranuras anfitrionas */
    for (size_t i = 0; i < recipe->bodyPlan.moduleCount; ++i) {
        const CreatureModuleInstance* m = &recipe->bodyPlan.modules[i];
        if (m->kind == CREATURE_MODULE_AXIAL &&
            !AxialBody_Resolve(&p.axial, m->instanceId, graph, &slots)) {
            return false;
        }
    }

    /* Segundo pase: módulos periféricos resuelven anatomía consumiendo ranuras */
    for (size_t i = 0; i < recipe->bodyPlan.moduleCount; ++i) {
        const CreatureModuleInstance* m = &recipe->bodyPlan.modules[i];
        if (m->kind == CREATURE_MODULE_AXIAL) continue;

        const AttachmentSlot* slot = AttachmentSlotSet_FindRef(&slots, m->attachment);
        AttachmentSlot pathSlot = {0};
        if (m->pathAttached) {
            const AnatomyNode* a = AnatomyGraph_FindNode(graph, m->pathNodeA);
            const AnatomyNode* b = AnatomyGraph_FindNode(graph, m->pathNodeB);
            if (!a || !b) return false;
            pathSlot.hostNode = m->pathU < .5f ? a->id : b->id;
            pathSlot.up = m->pathNormal;
            pathSlot.forward = Vec3_Normalize(Vec3_Sub(b->center, a->center));
            pathSlot.side = Vec3_Normalize(Vec3_Cross(pathSlot.forward, pathSlot.up));
            pathSlot.up = Vec3_Normalize(Vec3_Cross(pathSlot.side, pathSlot.forward));
            float h = Math_Lerp(a->heightRadius, b->heightRadius, m->pathU);
            float w = Math_Lerp(a->widthRadius, b->widthRadius, m->pathU);
            float lateral = Math_Clamp(m->lateralOffset, -.95f, .95f);
            /* Parametrización continua de la sección elíptica: evita el salto
             * dorsal/lateral y orienta el ornamento según la normal superficial. */
            Vector3 radial = Vec3_Add(Vec3_Scale(pathSlot.up, sqrtf(1-lateral*lateral)),
                                     Vec3_Scale(pathSlot.side, lateral));
            Vector3 reference = fabsf(pathSlot.forward.y) > .94f ? Vec3_Create(0,0,1) : Vec3_Create(0,1,0);
            Vector3 transverse = Vec3_Normalize(Vec3_Cross(pathSlot.forward, reference));
            Vector3 vertical = Vec3_Normalize(Vec3_Cross(transverse, pathSlot.forward));
            float rx = Vec3_Dot(radial, transverse), ry = Vec3_Dot(radial, vertical);
            pathSlot.position = Vec3_Add(Vec3_Lerp(a->center, b->center, m->pathU),
                Vec3_Add(Vec3_Scale(transverse, rx*w), Vec3_Scale(vertical, ry*h)));
            pathSlot.up = Vec3_Normalize(Vec3_Add(Vec3_Scale(transverse, rx/w),
                                                  Vec3_Scale(vertical, ry/h)));
            pathSlot.side = Vec3_Normalize(Vec3_Cross(pathSlot.forward, pathSlot.up));
            pathSlot.hostRadii = Vec3_Create(w,h,w);
            pathSlot.scale = p.axial.totalScale;
            slot = &pathSlot;
        }
        if (!slot) return false;

        switch (m->kind) {
        case CREATURE_MODULE_HEAD:
            if (!HeadModule_Resolve(&p.head, &p.headEnvelope, p.development.cephalic, m->instanceId, slot, graph, &slots)) {
                return false;
            }
            break;
        case CREATURE_MODULE_LIMB: {
            LimbPhenotype limb = p.limbs[m->phenotypeIndex];
            limb.development *= p.development.appendages;
            if (!Limb_Resolve(&limb, m->instanceId, slot, graph)) return false;
            break;
        }
        case CREATURE_MODULE_TAIL:
            if (!Tail_Resolve(&p.tails[m->phenotypeIndex], m->instanceId, slot, graph)) return false;
            break;
        case CREATURE_MODULE_ORNAMENT:
            if (!Ornament_Resolve(&p.ornaments[m->phenotypeIndex], m->instanceId, slot, graph)) return false;
            if (m->membranePrevious && !Module_Edge(graph, m->instanceId, 4,
                Anatomy_MakeId(m->membranePrevious, ORNAMENT_NODE_MID),
                Anatomy_MakeId(m->instanceId, ORNAMENT_NODE_MID), BODY_CONNECTION_SUPPORT, 1)) return false;
            break;
        default:
            return false;
        }
    }

    return AnatomyGraph_Validate(graph);
}

bool Creature_ResolveAppearance(struct Monster* monster, const CreatureRecipe* recipe, const CreaturePhenotype* source) {
    if (!monster || !source || !recipe) return false;
    CreaturePhenotype p = *source;
    CreaturePhenotype_Normalize(&p);
    if (monster->bodyPartCount == 0) Monster_Init(monster);
    if (monster->bodyPartCount == 0) return false;

    Color dorsal = Color_Lerp(p.surface.pigment.baseColor, p.dorsalColor, p.development.pigmentation);
    Color ventral = Color_Lerp(p.unpigmentedVentralColor, p.ventralColor, p.development.pigmentation);
    monster->colorPalette = ColorPalette_CreateGradient(dorsal, ventral, 6);

    /* 1. Resolver el grafo anatómico como única fuente de verdad geométrica */
    if (!Creature_ResolveAnatomy(recipe, &p, &monster->anatomyGraph)) return false;
    monster->hasAnatomyGraph = true;
    monster->surface = p.surface;
    monster->hasSurface = true;
    monster->surfaceMapping = SurfaceMapping_FromAnatomy(&monster->anatomyGraph, p.axial.totalScale);
    monster->phenotype = p;
    monster->recipeId = recipe->id;
    monster->recipe = *recipe;
    monster->hasCreaturePhenotype = true;

    /* 2. Posicionar anfitrión visual cefálico a partir de la estación semántica HEAD */
    const AnatomyNode* headNode = AnatomyGraph_FindFirstRegion(&monster->anatomyGraph, ANATOMY_REGION_HEAD);
    if (headNode) {
        BodyPart* host = Monster_GetHead(monster);
        host->position = host->oldPosition = host->positionRender = headNode->center;
        host->width = host->widthRender = headNode->envelopeRadii.x * 2.0f;
        host->height = host->heightRender = headNode->envelopeRadii.y * 2.0f;
        host->length = host->lengthRender = headNode->envelopeRadii.z * 2.0f;
        host->color.index = 3;
        host->bellyColor.index = 0;
        host->bellyThreshold = 0.24f;

        Head head = Head_Create(p.head.archetype, 0,
            Vec3_Create(host->widthRender * 0.5f, host->heightRender * 0.5f, host->lengthRender * 0.5f));
        head.phenotype = p.head;
        head.development = p.development.cephalic;
        if (!Monster_SetHead(monster, head)) return false;
        if(monster->head.anatomy.surface.hasEars) {
            const HeadSurfaceRecipe* ear=&monster->head.anatomy.surface;
            SurfaceMapping* mapping=&monster->surfaceMapping;
            mapping->pinnaCount=2;
            mapping->pinnae[0]=(SurfacePinnaCoverage){
                .center=Vec3_Add(headNode->center,ear->leftEarCenter),
                .up=ear->earDirection,.side=ear->leftEarSide,.normal=ear->leftEarNormal,
                .halfWidth=ear->earShape.x,.height=ear->earShape.y,
                .depth=ear->earShape.z*(2.5f+ear->earConcavity*2.0f)};
            mapping->pinnae[1]=(SurfacePinnaCoverage){
                .center=Vec3_Add(headNode->center,ear->rightEarCenter),
                .up=ear->rightEarDirection,.side=ear->rightEarSide,.normal=ear->rightEarNormal,
                .halfWidth=ear->earShape.x,.height=ear->earShape.y,
                .depth=ear->earShape.z*(2.5f+ear->earConcavity*2.0f)};
        }

        for (size_t i = 0; i < monster->eyeCount; ++i) {
            monster->eyes[i].scleraColor = p.eyes.scleraColor;
            monster->eyes[i].irisColor = p.eyes.irisColor;
            monster->eyes[i].pupilColor = p.eyes.pupilColor;
            monster->eyes[i].irisScale = p.eyes.irisScale;
            monster->eyes[i].pupilScale = p.eyes.pupilScale;
            monster->eyes[i].pupilShape = p.eyes.pupilShape;
            monster->eyes[i].pupilAspect = (p.eyes.pupilShape == PUPIL_ROUND) ? 1.0f : p.eyes.pupilAspect;
            Eye* eye = &monster->eyes[i];
            const HeadAnatomy* anatomy = &monster->head.anatomy;
            Vector3 center = i == 0 ? anatomy->landmarks.leftEyeCenter : anatomy->landmarks.rightEyeCenter;
            float eyeFactor = p.head.eyeSize > 1e-4f ? p.eyes.size / p.head.eyeSize : 1.0f;
            /* El globo debe caber entre techo/base del cráneo y región temporal.
             * El tamaño solicitado está limitado por ese espacio orbital físico. */
            float orbitalLimit = Math_Min(anatomy->surface.craniumRadii.y * .55f,
                                          anatomy->surface.craniumRadii.z * .40f);
            float baseRadius = anatomy->eyeScale.x / .92f;
            float requestedRadius = baseRadius * eyeFactor;
            float resolvedRadius = Math_Min(requestedRadius, orbitalLimit);
            float factor = baseRadius > 1e-6f ? resolvedRadius / baseRadius : 0;
            eye->scale = Vec3_Scale(eye->scale, factor * p.development.cephalic);
            /* La anatomía fija el centro; la apariencia solo permite un ajuste
             * pequeño y relativo. La cobertura palpebral pertenece al párpado,
             * no debe hundir de nuevo el globo dentro del cráneo. */
            float offset = eye->scale.z * .10f * (Math_Clamp01(p.eyes.protrusion) - .5f);
            eye->offset = Vec3_Add(center, Vec3_Scale(eye->forward, offset));

        }
    } else {
        monster->hasHead = false;
        Monster_ClearMouths(monster);
        Monster_ClearEyes(monster);
    }

    return true;
}

bool Creature_BuildMonster(struct Monster* monster, const CreatureRecipe* recipe, const CreaturePhenotype* source) {
    if (!Creature_ResolveAppearance(monster, recipe, source)) return false;
    Rig rig;
    if (!CreatureRig_Build(monster, &rig)) return false;
    if (recipe->gait == CREATURE_GAIT_NONE) {
        if (monster->animation) {
            MonsterAnimation_Free(monster->animation);
            monster->animation = NULL;
        }
        return true;
    }
    return MonsterAnimation_Configure(monster, &rig,
        recipe->gait == CREATURE_GAIT_SPRAWLING_QUADRUPED ?
            GaitPreset_SprawlingQuadruped(monster->phenotype.axial.totalScale) : (GaitProfile){0});
}

float Creature_AgeFromScaleBetween(float scale, float initialScale, float finalScale) {
    float span = finalScale - initialScale;
    if (!isfinite(span) || fabsf(span) < 1e-6f) return 0.0f;
    float t = Math_Clamp01((scale - initialScale) / span);
    float a = t;
    for (int iter = 0; iter < 8; ++iter) {
        float f = a * a * (3.0f - 2.0f * a) - t;
        float df = 6.0f * a * (1.0f - a);
        if (fabsf(df) < 1e-6f) break;
        a = Math_Clamp01(a - f / df);
    }
    return a;
}

MorphologicalSignature Creature_MorphologicalSignature(const Monster* m) {
    MorphologicalSignature s = {0};
    if (!m || !m->hasAnatomyGraph) return s;
    const AnatomyGraph* g = &m->anatomyGraph;
    float width=0,height=0,minZ=1e6f,maxZ=-1e6f,fore=0,hind=0,foot=0,tail=0,tailBase=0,ornament=0,thickness=0;
    for (size_t i=0;i<g->nodeCount;++i) {
        const AnatomyNode* n=&g->nodes[i];
        if (n->region==ANATOMY_REGION_TRUNK || n->region==ANATOMY_REGION_PELVIS || n->region==ANATOMY_REGION_NECK) {
            width=fmaxf(width,2*n->widthRadius); height=fmaxf(height,2*n->heightRadius);
            minZ=fminf(minZ,n->center.z);maxZ=fmaxf(maxZ,n->center.z);
        }
        if ((n->region==ANATOMY_REGION_FORELIMB || n->region==ANATOMY_REGION_HINDLIMB) && n->localNodeId==LIMB_NODE_AUTOPOD)
            foot=fmaxf(foot,2*n->widthRadius);
        if (n->region==ANATOMY_REGION_FORELIMB && n->localNodeId==LIMB_NODE_ROOT) thickness=fmaxf(thickness,2*n->widthRadius);
        if (n->region==ANATOMY_REGION_TAIL && n->localNodeId==1) tailBase=fmaxf(tailBase,2*n->widthRadius);
    }
    for(size_t i=0;i<g->connectionCount;++i) {
        const AnatomyNode* a=AnatomyGraph_FindNode(g,g->connections[i].fromId),*b=AnatomyGraph_FindNode(g,g->connections[i].toId);
        if(!a||!b)continue;
        float distance=Vec3_Distance(a->center,b->center);
        if(a->region==ANATOMY_REGION_FORELIMB && b->region==ANATOMY_REGION_FORELIMB)fore+=distance*.5f;
        if(a->region==ANATOMY_REGION_HINDLIMB && b->region==ANATOMY_REGION_HINDLIMB)hind+=distance*.5f;
        if(a->region==ANATOMY_REGION_TAIL && b->region==ANATOMY_REGION_TAIL)tail+=distance;
        if(a->region==ANATOMY_REGION_ORNAMENT && b->region==ANATOMY_REGION_ORNAMENT)
            ornament=fmaxf(ornament,fabsf(b->center.y-a->center.y)*2);
    }
    float length=fmaxf(maxZ-minZ,.001f);width=fmaxf(width,.001f);height=fmaxf(height,.001f);
    s.bodyLengthWidthRatio=length/width;s.bodyHeightWidthRatio=height/width;
    s.forelimbBodyRatio=fore/length;s.hindlimbBodyRatio=hind/length;s.hindForeRatio=hind/fmaxf(fore,.001f);
    s.footBodyRatio=foot/width;s.tailBodyLengthRatio=tail/length;s.tailBaseBodyRatio=tailBase/width;
    s.maxOrnamentHeightBodyRatio=ornament/height;s.forelimbThicknessBodyRatio=thickness/width;
    if(m->hasHead) {
        Vector3 r=m->head.anatomy.surface.craniumRadii;
        s.headBodyScale=2*cbrtf(r.x*r.y*r.z)/cbrtf(length*width*height);
        s.headLengthWidthRatio=(m->head.anatomy.landmarks.muzzleTip.z-m->head.anatomy.landmarks.neckAttachment.z)/(2*r.x);
        if(m->eyeCount)s.eyeHeadRatio=m->eyes[0].scale.y/fmaxf(r.y,.001f);
    }
    for(size_t i=0;i<g->nodeCount;++i) {
        const AnatomyNode* n=&g->nodes[i];
        if(n->region!=ANATOMY_REGION_TAIL || n->localNodeId<3)continue;
        const AnatomyNode* a=AnatomyGraph_FindModuleNode(g,n->moduleInstanceId,n->localNodeId-2);
        const AnatomyNode* b=AnatomyGraph_FindModuleNode(g,n->moduleInstanceId,n->localNodeId-1);
        if(a&&b)s.distalTailCurvature+=acosf(Math_Clamp(Vec3_Dot(Vec3_Normalize(Vec3_Sub(b->center,a->center)),Vec3_Normalize(Vec3_Sub(n->center,b->center))),-1,1));
    }
    return s;
}
unsigned Creature_ValidateGeometry(const Monster* m) {
    if(!m || !AnatomyGraph_Validate(&m->anatomyGraph))return 1;
    unsigned errors=0;
    const AnatomyNode* neck=AnatomyGraph_FindFirstRegion(&m->anatomyGraph,ANATOMY_REGION_NECK);
    if(m->hasHead && neck) {
        Vector3 origin=m->bodyParts[m->head.anatomy.attachmentBodyPartIndex].positionRender;
        if(Vec3_Distance(Vec3_Add(origin,m->head.anatomy.landmarks.neckAttachment),neck->center)>neck->widthRadius*.1f)errors|=2;
        for(size_t i=0;i<m->eyeCount && i<2;++i) {
            Vector3 orbit=i?m->head.anatomy.landmarks.rightOrbit:m->head.anatomy.landmarks.leftOrbit;
            float displacement=Vec3_Distance(m->eyes[i].offset,orbit);
            if(displacement>m->eyes[i].scale.z*.90f+1e-5f)errors|=4;
        }
    }
    for(size_t i=0;i<m->anatomyGraph.connectionCount;++i) {
        const BodyConnection* e=&m->anatomyGraph.connections[i];
        const AnatomyNode* a=AnatomyGraph_FindNode(&m->anatomyGraph,e->fromId);
        const AnatomyNode* b=AnatomyGraph_FindNode(&m->anatomyGraph,e->toId);
        if(a->moduleInstanceId==b->moduleInstanceId)continue;
        if(b->region==ANATOMY_REGION_TAIL && Vec3_Distance(a->center,b->center)>a->widthRadius+b->widthRadius)errors|=8;
        if(b->role==ANATOMY_ROLE_JOINT && Vec3_Distance(a->center,b->center)>a->widthRadius+b->widthRadius)errors|=16;
    }
    return errors;
}
