/**
 * @file CreatureRig.c
 * @brief Construcción de rig modular independiente de especie.
 * @author Monster Engine Team
 * @date 2026
 */

#include "CreatureRig.h"
#include "Monster.h"
#include "Limb.h"
#include <math.h>
#include <string.h>

#define CREATURE_JAW_JOINT_ID 0xffff0001u

static JointConstraint Ball(Vector3 axis, float swing, float twist) {
    return (JointConstraint){.type = JOINT_BALL, .hingeAxis = axis, .maxSwingAngle = swing, .maxTwistAngle = twist};
}

bool CreatureRig_Build(const Monster* m, Rig* r) {
    if (!m || !r || !m->hasAnatomyGraph) return false;
    const AnatomyGraph* g = &m->anatomyGraph;

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

    /* Estaciones cefálicas y pélvicas opcionales */
    r->pelvisJoint = pelvis ? Skeleton_FindJoint(s, pelvis->id) : -1;
    r->headJoint = head ? Skeleton_FindJoint(s, head->id) : -1;
    r->neckJoint = neck ? Skeleton_FindJoint(s, neck->id) : -1;

    /* Orden desde pelvis/tronco hacia cuello para la onda axial */
    for (size_t i = g->nodeCount; i > 0; --i) {
        const AnatomyNode* n = &g->nodes[i - 1];
        if (n->region == ANATOMY_REGION_TRUNK && r->spineCount < RIG_MAX_AXIAL_JOINTS) {
            int joint = Skeleton_FindJoint(s, n->id);
            if (joint >= 0) {
                r->spine[r->spineCount++] = joint;
                s->joints[joint].constraint = Ball(Vec3_Create(0, 0, 1), 0.12f, 0.08f);
            }
        }
    }

    /* Múltiples colas agrupadas por módulo */
    r->tailCount = 0;
    for (size_t index = 0; index < m->recipe.bodyPlan.moduleCount; ++index) {
        const CreatureModuleInstance* module = &m->recipe.bodyPlan.modules[index];
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
        s->joints[r->headJoint].constraint = Ball(Vec3_Create(0, 0, 1), 0.35f, 0.15f);
    }
    if (r->neckJoint >= 0) {
        s->joints[r->neckJoint].constraint = Ball(Vec3_Create(0, 0, 1), 0.25f, 0.12f);
    }

    /* Ensamblaje genérico de extremidades mediante descriptores modulares */
    for (size_t index = 0; index < m->recipe.bodyPlan.moduleCount; ++index) {
        const CreatureModuleInstance* module = &m->recipe.bodyPlan.modules[index];
        if (module->kind != CREATURE_MODULE_LIMB) continue;
        if (module->phenotypeIndex >= m->phenotype.limbCount) return false;
        const LimbPhenotype* p = &m->phenotype.limbs[module->phenotypeIndex];

        LimbRigDescriptor desc = {0};
        if (!Limb_BuildRigDescriptor(p, module->instanceId, g, &desc)) return false;

        if (!RigBuilder_AddLimb(r, desc.joints, desc.jointCount, desc.role, desc.side, desc.locomotionLimb)) {
            return false;
        }

        Limb_ConfigureRig(&desc, g, r);
    }

    /* Articulación mandibular opcional si existe cabeza y cavidad oral */
    if (m->hasHead && m->mouthCount && head && r->headJoint >= 0) {
        const Mouth* mouth = &m->mouths[0];
        r->jawJoint = (int)s->jointCount++;
        Vector3 host = mouth->bodyPartIndex < m->bodyPartCount ? m->bodyParts[mouth->bodyPartIndex].positionRender : Vec3_Zero();
        Vector3 pivot = Vec3_Add(host, Vec3_Add(mouth->offset, mouth->jawPivot));
        r->maxJawAngle = mouth->maxJawAngle * 0.017453292519943295f;
        s->joints[r->jawJoint] = (SkeletonJoint){
            .id = CREATURE_JAW_JOINT_ID,
            .anatomyId = head->id,
            .parentIndex = r->headJoint,
            .restPosition = Vec3_Sub(pivot, head->center),
            .restRotation = Quat_Identity(),
            .constraint = {.type = JOINT_HINGE, .hingeAxis = {1, 0, 0}, .minAngle = 0, .maxAngle = r->maxJawAngle}
        };
    }

    return Skeleton_Validate(s);
}
