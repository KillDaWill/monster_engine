/**
 * @file CreatureRig.c
 * @brief Construcción de rig modular independiente de especie.
 * @author Monster Engine Team
 * @date 2026
 */

#include "CreatureRig.h"
#include "Monster.h"
#include "Limb.h"
#include "FlexibleAppendage.h"
#include "Transform3D.h"
#include <math.h>
#include <string.h>

#define CREATURE_JAW_JOINT_ID 0xffff0001u
#define CREATURE_CERVICAL_BASE_ID 0xffff0002u

static JointConstraint Ball(Vector3 axis, float swing, float twist) {
    return (JointConstraint){.type = JOINT_BALL, .hingeAxis = axis, .maxSwingAngle = swing, .maxTwistAngle = twist};
}

static JointConstraint Ellipsoid(Vector3 axis, float maxUp, float maxDown, float maxYaw, float twist) {
    float maxSwing = fmaxf(fmaxf(maxUp, maxDown), maxYaw);
    return (JointConstraint){
        .type = JOINT_ELLIPSOID,
        .hingeAxis = axis,
        .maxSwingAngle = maxSwing,
        .maxTwistAngle = twist,
        .maxPitchUp = maxUp,
        .maxPitchDown = maxDown,
        .maxYawAngle = maxYaw
    };
}

bool CreatureRig_Build(const Monster* m, Rig* r) {
    if (!m || !r || !m->hasAnatomyGraph) return false;
    const AnatomyGraph* g = &m->anatomyGraph;
    size_t order[CREATURE_MAX_MODULES];
    if (m->recipe.bodyPlan.moduleCount && !CreatureRecipe_ResolutionOrder(&m->recipe, order)) return false;

    /* Resolver la raíz del rig desde el BodyPlan de la receta; no asumir pelvis */
    AnatomyId rootId = 0;
    if (m->recipe.bodyPlan.root.moduleInstanceId && m->recipe.bodyPlan.root.localNodeId) {
        rootId = Anatomy_MakeId(m->recipe.bodyPlan.root.moduleInstanceId, m->recipe.bodyPlan.root.localNodeId);
    }
    if (!rootId) {
        const AnatomyNode* pelvis = AnatomyGraph_FindFirstRegion(g, ANATOMY_REGION_PELVIS);
        rootId = pelvis ? pelvis->id : (g->nodeCount > 0 ? g->nodes[0].id : 0);
    }
    if (!rootId || !RigBuilder_FromAnatomy(g, rootId, r)) return false;

    Skeleton* s = &r->skeleton;
    const AnatomyNode* pelvis = AnatomyGraph_FindFirstRegion(g, ANATOMY_REGION_PELVIS);
    const AnatomyNode* head = AnatomyGraph_FindFirstRegion(g, ANATOMY_REGION_HEAD);
    const AnatomyNode* neck = AnatomyGraph_FindFirstRegion(g, ANATOMY_REGION_NECK);

    const bool sprawling = (m->phenotype.axial.archetype == AXIAL_ARCHETYPE_SPRAWLING_TETRAPOD);
    float neckUpSlope = 0.0f;
    r->cervicalBaseJoint = -1;
    r->cervicalChainCount = 0;
    /* Articulación auxiliar sin volumen: gira el segmento completo desde su
     * inserción torácica, en lugar de doblar únicamente el extremo del cuello.
     * En cadenas cervicales largas se inserta en la estación cervical más
     * basal (hija directa del tórax) y todas las juntas intermedias reciben
     * límites elipsoidales continuos. */
    int distalNeck = neck ? Skeleton_FindJoint(s, neck->id) : -1;
    int cervical = distalNeck;
    while (cervical >= 0 && s->joints[cervical].parentIndex >= 0) {
        int parent = s->joints[cervical].parentIndex;
        const AnatomyNode* pNode = AnatomyGraph_FindNode(g, s->joints[parent].anatomyId);
        if (!pNode || pNode->region != ANATOMY_REGION_NECK) break;
        cervical = parent;
    }
    if (cervical >= 0 && s->joints[cervical].parentIndex >= 0) {
        int parent = s->joints[cervical].parentIndex;
        const AnatomyNode* insertion=AnatomyGraph_FindNode(g,s->joints[parent].anatomyId);
        /* Pivote anatómico en el opérculo torácico (no en el vientre bajo ni en el suelo),
         * evitando arrastrar la cruz/dorsal hacia atrás al mirar arriba o cizallar al bajar. */
        float ventralScale = sprawling ? 0.14f : 0.42f;
        float ventral = insertion ? insertion->heightRadius * ventralScale : 0.0f;
        if (s->jointCount >= SKELETON_MAX_JOINTS) return false;
        memmove(&s->joints[cervical + 1], &s->joints[cervical],
                (s->jointCount - (size_t)cervical) * sizeof(s->joints[0]));
        ++s->jointCount;
        for (size_t j = (size_t)cervical + 1; j < s->jointCount; ++j)
            if (s->joints[j].parentIndex >= cervical) ++s->joints[j].parentIndex;
        s->joints[cervical + 1].restPosition.y += ventral;
        s->joints[cervical + 1].parentIndex = cervical;
        /* Calibrar rango vertical respecto a la inclinación en reposo del segmento cervical:
         * cuellos muy erguidos (cánidos/équidos/jirafas) necesitan bajar más en ángulo relativo para pastar
         * o beber sin rebasar la vertical al mirar arriba; reptiles reptantes están limitados por el suelo. */
        Vector3 distalPos = (distalNeck != cervical && neck && insertion)
            ? Vec3_Sub(neck->center, insertion->center)
            : s->joints[cervical + 1].restPosition;
        float nHoriz = sqrtf(distalPos.x * distalPos.x + distalPos.z * distalPos.z);
        float restSlope = atan2f(distalPos.y, fmaxf(1e-4f, nHoriz));
        neckUpSlope = fmaxf(0.0f, restSlope);
        JointConstraint baseLimits = sprawling
            ? Ellipsoid(Vec3_Create(0, 0, 1), 0.14f,
                        fminf(0.30f, fmaxf(0.20f, 0.18f + 0.25f * neckUpSlope)), 0.26f, 0.04f)
            : Ellipsoid(Vec3_Create(0, 0, 1),
                        fminf(0.22f, fmaxf(0.11f, 1.15f - neckUpSlope)),
                        fminf(1.68f, fmaxf(0.95f, 0.60f + neckUpSlope)), 0.34f, 0.05f);
        s->joints[cervical] = (SkeletonJoint){.id=CREATURE_CERVICAL_BASE_ID,
            .parentIndex=parent, .restPosition={0,-ventral,0}, .restRotation=Quat_Identity(),
            .constraint=baseLimits};
        r->cervicalBaseJoint = cervical;
    }

    /* Estaciones cefálicas y pélvicas opcionales */
    r->pelvisJoint = pelvis ? Skeleton_FindJoint(s, pelvis->id) : -1;
    r->headJoint = head ? Skeleton_FindJoint(s, head->id) : -1;
    r->neckJoint = neck ? Skeleton_FindJoint(s, neck->id) : -1;
    if (r->neckJoint >= 0) {
        int reverse[RIG_MAX_CERVICAL_JOINTS];
        size_t count = 0;
        int cur = r->neckJoint;
        int stop = r->cervicalBaseJoint >= 0 ? r->cervicalBaseJoint : r->neckJoint;
        while (cur >= 0 && count < RIG_MAX_CERVICAL_JOINTS) {
            reverse[count++] = cur;
            if (cur == stop) break;
            cur = s->joints[cur].parentIndex;
        }
        for (size_t k = 0; k < count; ++k)
            r->cervicalChain[k] = reverse[count - 1 - k];
        r->cervicalChainCount = count;
    }

    /* Orden desde pelvis/tronco hacia cuello para la onda axial */
    float spineSwing = m->phenotype.axial.spineFlexion > 0.0f ? m->phenotype.axial.spineFlexion : 0.12f;
    for (size_t i = g->nodeCount; i > 0; --i) {
        const AnatomyNode* n = &g->nodes[i - 1];
        if (n->region == ANATOMY_REGION_TRUNK && r->spineCount < RIG_MAX_AXIAL_JOINTS) {
            int joint = Skeleton_FindJoint(s, n->id);
            if (joint >= 0) {
                r->spineRadius[r->spineCount]=fmaxf(n->widthRadius,n->heightRadius);
                r->spine[r->spineCount++] = joint;
                s->joints[joint].constraint = Ball(Vec3_Create(0, 0, 1), spineSwing, 0.08f);
            }
        }
    }

    /* Múltiples colas agrupadas por módulo */
    r->tailCount = 0;
    for (size_t index = 0; index < m->recipe.bodyPlan.moduleCount; ++index) {
        const CreatureModuleInstance* module = &m->recipe.bodyPlan.modules[order[index]];
        if (module->kind != CREATURE_MODULE_TAIL) continue;
        if (r->tailCount >= RIG_MAX_TAILS) return false;
        TailRig* tRig = &r->tails[r->tailCount++];
        memset(tRig, 0, sizeof(*tRig));
        tRig->moduleInstanceId = module->instanceId;
        for (size_t i = 0; i < g->nodeCount; ++i) {
            const AnatomyNode* n = &g->nodes[i];
            if (n->moduleInstanceId == module->instanceId && n->region == ANATOMY_REGION_TAIL) {
                if (tRig->jointCount >= RIG_MAX_TAIL_JOINTS) return false;
                int joint = Skeleton_FindJoint(s, n->id);
                if (joint >= 0) {
                    tRig->joints[tRig->jointCount++] = joint;
                    s->joints[joint].constraint = Ball(Vec3_Create(0, 0, 1), 0.3f, 0.12f);
                }
            }
        }
    }
    /* Si no hubo módulos de cola en el BodyPlan, agrupar nodos caudales por moduleInstanceId */
    if (r->tailCount == 0) {
        for (size_t i = 0; i < g->nodeCount; ++i) {
            const AnatomyNode* n = &g->nodes[i];
            if (n->region != ANATOMY_REGION_TAIL) continue;
            TailRig* tRig = NULL;
            for (size_t t = 0; t < r->tailCount; ++t) {
                if (r->tails[t].moduleInstanceId == n->moduleInstanceId) {
                    tRig = &r->tails[t];
                    break;
                }
            }
            if (!tRig) {
                if (r->tailCount >= RIG_MAX_TAILS) return false;
                tRig = &r->tails[r->tailCount++];
                memset(tRig, 0, sizeof(*tRig));
                tRig->moduleInstanceId = n->moduleInstanceId;
            }
            if (tRig->jointCount < RIG_MAX_TAIL_JOINTS) {
                int joint = Skeleton_FindJoint(s, n->id);
                if (joint >= 0) {
                    tRig->joints[tRig->jointCount++] = joint;
                    s->joints[joint].constraint = Ball(Vec3_Create(0, 0, 1), 0.3f, 0.12f);
                }
            }
        }
    }

    if (r->headJoint >= 0) {
        s->joints[r->headJoint].constraint = sprawling
            ? Ellipsoid(Vec3_Create(0, 0, 1), 0.14f, 0.16f, 0.18f, 0.05f)
            : Ellipsoid(Vec3_Create(0, 0, 1), 0.18f, 0.30f, 0.22f, 0.08f);
    }
    if (r->neckJoint >= 0) {
        float uprightNeckUp = fminf(0.21f, fmaxf(0.13f, 0.90f - 0.55f * neckUpSlope));
        JointConstraint neckLimit = sprawling
            ? Ellipsoid(Vec3_Create(0, 0, 1), 0.16f, 0.22f, 0.34f, 0.05f)
            : Ellipsoid(Vec3_Create(0, 0, 1), uprightNeckUp, 0.65f, 0.42f, 0.07f);
        s->joints[r->neckJoint].constraint = neckLimit;
        if (r->cervicalChainCount > 2) {
            float segScale = 1.0f / (float)(r->cervicalChainCount - 1);
            JointConstraint segLimit = Ellipsoid(Vec3_Create(0, 0, 1),
                fmaxf(0.12f, (1.15f - neckUpSlope) * segScale),
                fmaxf(0.32f, (0.90f + neckUpSlope) * segScale),
                0.28f * segScale + 0.08f, 0.04f);
            for (size_t k = 1; k + 1 < r->cervicalChainCount; ++k)
                s->joints[r->cervicalChain[k]].constraint = segLimit;
        }
    }
    if (head && m->hasHead) {
        Vector3 host = m->head.anatomy.attachmentBodyPartIndex < m->bodyPartCount
            ? m->bodyParts[m->head.anatomy.attachmentBodyPartIndex].positionRender : Vec3_Zero();
        const HeadLandmarks* lm = &m->head.anatomy.landmarks;
        r->oralDirection=Vec3_Normalize(Vec3_Sub(lm->muzzleTip,lm->muzzleRoot));
        r->oralPoint = Vec3_Sub(Vec3_Add(host,lm->oralContactPoint),head->center);
        Vector3 neckAtt = Vec3_Add(host, lm->neckAttachment);
        r->cranialAxis=Vec3_Normalize(Vec3_Sub(lm->skullCenter, lm->neckAttachment));
        /* El anillo de transición debe cubrir la nuca entre neckJoint y el
         * occipucio sin deformar orejas ni bisagra mandibular rígidas. */
        float earDot = Vec3_Dot(Vec3_Sub(Vec3_Add(host, lm->leftEarBase), neckAtt), r->cranialAxis);
        float jawDot = Vec3_Dot(Vec3_Sub(Vec3_Add(host, lm->leftJawHinge), neckAtt), r->cranialAxis);
        float skullDot = Vec3_Dot(Vec3_Sub(Vec3_Add(host, lm->skullCenter), neckAtt), r->cranialAxis);
        float minFace = fminf(skullDot, fminf(earDot, jawDot));
        float forward = minFace > 0.0f ? minFace * 0.34f : 0.0f;
        r->cranialAttachment = Vec3_Add(neckAtt, Vec3_Scale(r->cranialAxis, forward));
        r->cranialCollar = fmaxf(m->head.anatomy.surface.craniumRadii.z * 0.85f, minFace * 1.05f);
    }

    /* Ensamblaje genérico de extremidades mediante descriptores modulares */
    for (size_t index = 0; index < m->recipe.bodyPlan.moduleCount; ++index) {
        const CreatureModuleInstance* module = &m->recipe.bodyPlan.modules[order[index]];
        if (module->kind != CREATURE_MODULE_LIMB) continue;
        if (module->phenotypeIndex >= m->phenotype.limbCount) return false;
        const LimbPhenotype* p = &m->phenotype.limbs[module->phenotypeIndex];

        LimbRigDescriptor desc = {0};
        if (!Limb_BuildRigDescriptor(p, module->instanceId, g, &desc)) return false;
        if (m->recipe.gait == CREATURE_GAIT_BIPEDAL_HOP && desc.role == LIMB_FORE)
            desc.locomotionLimb = false;

        if (!RigBuilder_AddLimb(r, desc.joints, desc.jointCount, desc.role, desc.side, desc.locomotionLimb)) {
            return false;
        }

        Limb_ConfigureRig(&desc, g, r);
    }

    /* Cada bisagra flexible conserva el eje transversal publicado al resolver
     * el módulo. El anfitrión no necesita estar orientado en el eje global X. */
    for (size_t i=0; i<g->connectionCount; ++i) {
        const BodyConnection* edge=&g->connections[i];
        if (edge->kind!=BODY_CONNECTION_FLEXIBLE_SEGMENT && edge->kind!=BODY_CONNECTION_PAIRED_OPENINGS)
            continue;
        int joint=Skeleton_FindJoint(s,edge->toId);
        if(joint<0)continue;
        s->joints[joint].constraint=(JointConstraint){.type=JOINT_HINGE,
            .hingeAxis=edge->transverseAxis,.minAngle=-.85f,.maxAngle=.85f};
    }

    /* Articulación mandibular opcional si existe cabeza y cavidad oral */
    if (m->hasHead && m->mouthCount && head && r->headJoint >= 0) {
        const Mouth* mouth = &m->mouths[0];
        r->jawJoint = (int)s->jointCount++;
        Vector3 host = mouth->bodyPartIndex < m->bodyPartCount ? m->bodyParts[mouth->bodyPartIndex].positionRender : Vec3_Zero();
        Vector3 pivot = Vec3_Add(host, Vec3_Add(mouth->offset,
            Transform3D_RotateVector(mouth->rotation,mouth->jawPivot)));
        Vector3 hingeAxis=Transform3D_RotateVector(mouth->rotation,Vec3_Create(1,0,0));
        r->maxJawAngle = mouth->maxJawAngle * 0.017453292519943295f;
        s->joints[r->jawJoint] = (SkeletonJoint){
            .id = CREATURE_JAW_JOINT_ID,
            .anatomyId = head->id,
            .parentIndex = r->headJoint,
            .restPosition = Vec3_Sub(pivot, head->center),
            .restRotation = Quat_Identity(),
            .constraint = {.type = JOINT_HINGE, .hingeAxis = hingeAxis, .minAngle = 0, .maxAngle = r->maxJawAngle}
        };
    }

    return Skeleton_Validate(s);
}
