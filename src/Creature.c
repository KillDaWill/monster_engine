/**
 * @file Creature.c
 * @brief Constructor y resolutor de apariencia y anatomía para criaturas modulares.
 * @author Monster Engine Team
 * @date 2026
 */

#include "Creature.h"
#include "AxialBody.h"
#include "HeadModule.h"
#include "FlexibleAppendage.h"
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

    size_t order[CREATURE_MAX_MODULES];
    if (!CreatureRecipe_ResolutionOrder(recipe, order)) return false;
    /* Cada módulo consume únicamente anfitriones ya resueltos. */
    for (size_t i = 0; i < recipe->bodyPlan.moduleCount; ++i) {
        const CreatureModuleInstance* m = &recipe->bodyPlan.modules[order[i]];
        if (m->kind == CREATURE_MODULE_AXIAL) {
            if (!AxialBody_Resolve(&p.axial, m->instanceId, graph, &slots)) return false;
            continue;
        }

        const AttachmentSlot* slot = AttachmentSlotSet_FindRef(&slots, m->attachment);
        AttachmentSlot pathSlot = {0};
        if (m->pathAttached) {
            if (!AttachmentPath_ResolveNodes(graph, m, p.axial.totalScale, &pathSlot)) return false;
            slot = &pathSlot;
        }
        if (!slot) return false;

        switch (m->kind) {
        case CREATURE_MODULE_HEAD: {
            unsigned fittedSites = 0;
            for (size_t j=0; j<recipe->bodyPlan.moduleCount; ++j) {
                const CreatureModuleInstance *child = &recipe->bodyPlan.modules[j];
                AttachmentSlotId id = child->attachment.slotId;
                if (child->pathAttached || child->attachment.hostModuleInstanceId != m->instanceId) continue;
                if (id >= HEAD_SLOT_FITTED_PROBOSCIS && id <= HEAD_SLOT_FITTED_NASAL_POSTERIOR)
                    fittedSites |= 1u << (id-HEAD_SLOT_FITTED_PROBOSCIS);
                if (id == HEAD_SLOT_PROBOSCIS && child->kind == CREATURE_MODULE_FLEXIBLE_APPENDAGE)
                    fittedSites |= HEAD_SITE_LEGACY_PROBOSCIS;
            }
            if (!HeadModule_ResolveProfileWithSites(&p.head,&p.headEnvelope,&p.headHybrid,
                    p.development.cephalic,m->instanceId,slot,graph,&slots,fittedSites)) {
                return false;
            }
            break;
        }
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
        case CREATURE_MODULE_FLEXIBLE_APPENDAGE: {
            FlexibleAppendagePhenotype flexible=p.flexibleAppendages[m->phenotypeIndex];
            if(recipe->offspringVersion && HeadModule_IsNasalExtension(slot->id))
                flexible.length=fmaxf(flexible.length,
                    FlexibleAppendage_MinNasalLength(&flexible,slot->hostRadii.y/slot->scale));
            if (!FlexibleAppendage_Resolve(&flexible,m->instanceId,slot,graph)) return false;
            break;
        }
        default:
            return false;
        }
    }

    return AnatomyGraph_FindModuleNode(graph, recipe->bodyPlan.root.moduleInstanceId,
        recipe->bodyPlan.root.localNodeId) && AnatomyGraph_Validate(graph);
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
    monster->hasResolvedMorph = false;
    monster->surface = p.surface;
    monster->hasSurface = true;
    monster->surfaceMapping = SurfaceMapping_FromAnatomy(&monster->anatomyGraph, p.axial.totalScale);
    SurfaceMapper_ResolvePigmentDomains(&monster->surface,&monster->anatomyGraph,&monster->surfaceMapping);
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
        head.hybrid = p.headHybrid;
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
            if (p.head.archetype == HEAD_ARCHETYPE_ARTHROPOD) {
                /* Ojo compuesto de artrópodo: sin aro limbal ni pupila vertebrada contrastada. */
                monster->eyes[i].limbalRingStrength = 0.0f;
                monster->eyes[i].irisRadialNoise = 0.15f;
                monster->eyes[i].irisColorVariation = 0.22f;
                monster->eyes[i].scleraPigmentation = 0.0f;
            }
            Eye* eye = &monster->eyes[i];
            const HeadAnatomy* anatomy = &monster->head.anatomy;
            Vector3 center = i == 0 ? anatomy->landmarks.leftEyeCenter : anatomy->landmarks.rightEyeCenter;
            float nominalEye = p.headHybrid.active && !p.headHybrid.hasRegionalProfile ?
                Math_Lerp(p.head.eyeSize,p.headHybrid.secondary.eyeSize,p.headHybrid.blend) : p.head.eyeSize;
            float eyeFactor = nominalEye > 1e-4f ? p.eyes.size / nominalEye : 1.0f;
            /* El globo debe caber entre techo/base del cráneo y región temporal.
             * En artrópodos los ojos compuestos envuelven la cápsula cefálica. */
            float orbitalLimit = p.head.archetype == HEAD_ARCHETYPE_ARTHROPOD
                ? Math_Min(anatomy->surface.craniumRadii.y * 0.94f, anatomy->surface.craniumRadii.z * 0.88f)
                : Math_Min(anatomy->surface.craniumRadii.y * .55f, anatomy->surface.craniumRadii.z * .40f);
            float baseRadius = anatomy->eyeScale.x / .92f;
            float requestedRadius = baseRadius * eyeFactor;
            float resolvedRadius = Math_Min(requestedRadius, orbitalLimit);
            float factor = baseRadius > 1e-6f ? resolvedRadius / baseRadius : 0;
            eye->scale = Vec3_Scale(eye->scale, factor * p.development.cephalic);
            /* La anatomía fija el centro; la apariencia solo permite un ajuste
             * pequeño y relativo. La cobertura palpebral pertenece al párpado,
             * no debe hundir de nuevo el globo dentro del cráneo. */
            float offset = eye->scale.z * .10f * (Math_Clamp01(p.eyes.protrusion) - .5f);
            Vector3 orbit = i == 0 ? anatomy->landmarks.leftOrbit : anatomy->landmarks.rightOrbit;
            Vector3 seat = Vec3_Sub(center, orbit);
            float exposure = Vec3_Length(seat);
            /* El globo emergente puede ser mucho menor que el globo del
             * perfil. Limita su exposición respecto a la órbita resuelta. */
            float maxExposure = eye->scale.z * .80f;
            if (p.headHybrid.hasRegionalProfile && exposure > maxExposure)
                center = Vec3_Add(orbit,Vec3_Scale(seat,maxExposure/exposure));
            eye->offset = Vec3_Add(center,Vec3_Scale(eye->forward,offset));

        }
    } else {
        monster->hasHead = false;
        Monster_ClearMouths(monster);
        Monster_ClearEyes(monster);
    }

    return true;
}

bool Creature_ConfigureAnimation(struct Monster* monster) {
    Rig rig;
    if (!CreatureRig_Build(monster, &rig)) return false;
    GaitProfile gait;
    switch (monster->recipe.gait) {
    case CREATURE_GAIT_NONE:
        if (monster->animation) {
            MonsterAnimation_Free(monster->animation);
            monster->animation = NULL;
        }
        return true;
    case CREATURE_GAIT_SPRAWLING_QUADRUPED:
        gait=GaitPreset_SprawlingQuadruped(monster->phenotype.axial.totalScale);
        break;
    case CREATURE_GAIT_UPRIGHT_QUADRUPED:
        gait=GaitPreset_CanidWalk(&rig);
        break;
    case CREATURE_GAIT_BOVINE_WALK:
        gait=GaitPreset_BovineWalk(&rig);
        break;
    case CREATURE_GAIT_COLUMNAR_WALK:
        gait=GaitPreset_BovineWalk(&rig);
        gait.cycleDuration*=1.28f;
        gait.strideLength*=.80f;
        gait.stepHeight*=.45f;
        gait.bodyBob*=.42f;
        gait.bodyBank*=.55f;
        gait.spineWave*=.55f;
        break;
    case CREATURE_GAIT_FELINE_STALK:
        gait=GaitPreset_FelineStalk(&rig);
        break;
    case CREATURE_GAIT_BIPEDAL_HOP:
        gait=GaitPreset_BipedalHop(&rig);
        break;
    case CREATURE_GAIT_AVIAN_FLIGHT:
        gait=GaitPreset_AvianFlight(&rig);
        break;
    case CREATURE_GAIT_RAY_SWIM:
        gait=GaitPreset_RaySwim(&rig);
        break;
    case CREATURE_GAIT_OCTOPUS_CRAWL:
        gait=GaitPreset_OctopusCrawl(&rig);
        break;
    case CREATURE_GAIT_INSECT_FLIGHT:
        gait=GaitPreset_InsectFlight(&rig);
        break;
    case CREATURE_GAIT_MEDUSA_PULSE:
        gait=GaitPreset_MedusaPulse(&rig);
        break;
    default:
        return false;
    }
    if (!MonsterAnimation_Configure(monster, &rig, gait)) return false;
    if (monster->animation) {
        if (monster->recipe.gait == CREATURE_GAIT_RAY_SWIM)
            monster->animation->animator.finWaveAmplitude = 0.75f;
        else if (monster->recipe.gait == CREATURE_GAIT_MEDUSA_PULSE)
            monster->animation->animator.bellPulseAmplitude = 0.72f;
    }
    return true;
}

bool Creature_BuildMonster(struct Monster* monster,const CreatureRecipe* recipe,const CreaturePhenotype* source) {
    return Creature_ResolveAppearance(monster,recipe,source) && Creature_ConfigureAnimation(monster);
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
            /* Las etapas anteriores a la emergencia aún publican órbitas,
             * pero carecen de globo visible cuyo asiento pueda comprobarse. */
            if (m->eyes[i].scale.z == 0.0f) continue;
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
