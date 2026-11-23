/**
 * @file LimbInsectWing.c
 * @brief Articulación y esqueleto costal ligero de alas de insecto (anteriores y posteriores independientes).
 */
#include "LimbInsectWing.h"
#include "CreatureModuleInternal.h"
#include "Limb.h"
#include "MathUtils.h"
#include <math.h>

static Vector3 WorldIW(const AttachmentSlot* slot, Vector3 local) {
    return Vec3_Add(slot->position,
        Vec3_Add(Vec3_Scale(slot->side, local.x),
        Vec3_Add(Vec3_Scale(slot->up, local.y),
                 Vec3_Scale(slot->forward, local.z))));
}

bool LimbInsectWing_Resolve(const LimbPhenotype* p, uint32_t module, const AttachmentSlot* slot, AnatomyGraph* g) {
    if (!p || !slot || !g || p->archetype != LIMB_ARCHETYPE_INSECT_WING) return false;
    float dev = Math_Clamp01(p->development);
    /* En etapas ninfales acuáticas tempranas (hemimetabolismo), las alas existen como
     * esbozos dorsales reducidos (wing pads) conservando las mismas identidades estables. */
    float padMode = Math_Clamp01((dev - 0.20f) / 0.35f);
    float span = p->length * slot->scale * Math_Lerp(0.10f, 1.0f, padMode) * fmaxf(0.25f, dev);
    float chord = fmaxf(0.004f * slot->scale, (p->wingAspect > 0.2f ? span / p->wingAspect : span * 0.24f));
    float sparR = fmaxf(0.0015f * slot->scale, p->thickness * slot->scale * Math_Lerp(0.55f, 1.0f, padMode));
    float side = p->side == LIMB_RIGHT ? -1.0f : 1.0f;
    AnatomySide aSide = p->side == LIMB_RIGHT ? ANATOMY_SIDE_RIGHT : ANATOMY_SIDE_LEFT;

    float sweep = p->finSweep; /* p. ej. ligera flecha posterior o inclinación diédrica */
    float dihedral = p->finCamber * 0.45f;

    /* 4 nodos ligeros a lo largo del margen costal:
     * 1: Bisagra torácica (axilar)
     * 2: Tramo proximal costal
     * 3: Nodo (nodus) alar medio
     * 4: Ápice alar (pterostigma/punta)
     * Usa BODY_CONNECTION_SUPPORT para no generar huesos mamíferos gruesos en el SDF;
     * en estadio ninfal (padMode < 0.25) emite un pequeño esbozo dorsal de estuche alar. */
    float frac[4] = {0.0f, 0.32f, 0.66f, 1.0f};
    for (unsigned i = 0; i < 4; ++i) {
        float u = frac[i];
        /* Cuando está en fase ninfal (padMode ~ 0), los esbozos se pliegan hacia atrás sobre el dorso. */
        float x = side * span * u * Math_Lerp(0.22f, 1.0f, padMode);
        float z = -span * u * Math_Lerp(0.92f, sweep, padMode) + (1.0f - u) * chord * 0.08f;
        float y = span * u * dihedral + sinf(u * 3.14159265f) * span * 0.025f * padMode;
        float rNode = fmaxf(0.0012f * slot->scale, sparR * (1.0f - 0.55f * u));
        if (!Module_Node(g, module, (uint16_t)(i + 1), WorldIW(slot, Vec3_Create(x, y, z)),
                         rNode, rNode * 0.65f, 2, ANATOMY_ROLE_JOINT, ANATOMY_REGION_WING, aSide, dev))
            return false;
        if (i > 0) {
            BodyConnectionKind kind = (padMode < 0.25f) ? BODY_CONNECTION_ORNAMENT_SEGMENT : BODY_CONNECTION_SUPPORT;
            if (!Module_Edge(g, module, (uint16_t)i,
                             Anatomy_MakeId(module, (uint16_t)i),
                             Anatomy_MakeId(module, (uint16_t)(i + 1)),
                             kind, dev))
                return false;
        }
    }
    if (!Module_Edge(g, module, 4, slot->hostNode, Anatomy_MakeId(module, 1),
                     BODY_CONNECTION_SUPPORT, dev))
        return false;
    return true;
}

bool LimbInsectWing_BuildRigDescriptor(const LimbPhenotype* p, uint32_t module, const AnatomyGraph* g, LimbRigDescriptor* out) {
    if (!p || !g || !out) return false;
    *out = (LimbRigDescriptor){
        .moduleInstanceId = module,
        .jointCount = 4,
        .role = LIMB_WING,
        .side = p->side,
        .archetype = p->archetype,
        .locomotionLimb = false,
        .soleHeight = 0.0f,
        .flexionScale = p->flexionScale > 0.0f ? p->flexionScale : 1.0f
    };
    for (unsigned i = 0; i < 4; ++i) {
        out->joints[i] = Anatomy_MakeId(module, (uint16_t)(i + 1));
        if (!AnatomyGraph_FindNode(g, out->joints[i])) return false;
    }
    out->endEffector = out->joints[3];
    return true;
}

void LimbInsectWing_ConfigureRig(const LimbRigDescriptor* d, const AnatomyGraph* g, Rig* rig) {
    if (!d || !g || !rig) return;
    for (size_t i = 0; i < rig->limbCount; ++i) {
        LimbRig* l = &rig->limbs[i];
        if (rig->skeleton.joints[l->rootJoint].anatomyId != d->joints[0]) continue;
        l->role = LIMB_WING;
        l->walking = false;
        l->digitigrade = false;
        l->unguligrade = false;
        float flex = d->flexionScale > 0.0f ? d->flexionScale : 1.0f;
        for (size_t j = 0; j < l->chain.jointCount; ++j) {
            SkeletonJoint* joint = &rig->skeleton.joints[l->chain.jointIndices[j]];
            joint->constraint = (JointConstraint){
                .type = JOINT_BALL,
                .hingeAxis = {0.0f, 0.0f, 1.0f},
                .maxSwingAngle = fminf(1.35f, (j == 0 ? 1.15f : 0.25f) * flex),
                .maxTwistAngle = j == 0 ? 0.45f : 0.12f
            };
        }
    }
}
