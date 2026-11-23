/**
 * @file LimbFin.c
 * @brief Esqueleto radial-cordal continuo de aletas pectorales y pélvicas batoideas.
 */
#include "LimbFin.h"
#include "CreatureModuleInternal.h"
#include "Limb.h"
#include "MathUtils.h"
#include <math.h>

static Vector3 WorldFin(const AttachmentSlot* slot, Vector3 local) {
    return Vec3_Add(slot->position,
        Vec3_Add(Vec3_Scale(slot->side, local.x),
        Vec3_Add(Vec3_Scale(slot->up, local.y),
                 Vec3_Scale(slot->forward, local.z))));
}

bool LimbFin_Resolve(const LimbPhenotype* p, uint32_t module, const AttachmentSlot* slot, AnatomyGraph* g) {
    if (!p || !slot || !g || p->archetype != LIMB_ARCHETYPE_FLIPPER) return false;
    float dev = fmaxf(0.08f, p->development);
    float span = p->length * slot->scale * dev;
    float thick = fmaxf(0.003f * slot->scale, p->thickness * slot->scale * dev);
    float side = p->side == LIMB_RIGHT ? -1.0f : 1.0f;
    AnatomySide aSide = p->side == LIMB_RIGHT ? ANATOMY_SIDE_RIGHT : ANATOMY_SIDE_LEFT;
    bool pectoral = (p->role == LIMB_FORE || p->role == LIMB_WING);
    float sweep = p->finSweep;
    float camber = p->finCamber;

    /* Cadena principal spanwise (4 estaciones desde raíz embebida en el disco hasta punta).
     * Las secciones son anchas en cuerda (Z) y aplanadas en espesor (Y), con unión muy suave
     * para fusionarse en una única silueta continua con el cuerpo central batoideo. */
    float baseChord = pectoral ? fmaxf(slot->hostRadii.z * 0.78f, span * 0.34f)
                               : fmaxf(slot->hostRadii.z * 0.45f, span * 0.48f);
    float rootInset = -side * slot->hostRadii.x * 0.35f;

    float frac[4] = {0.0f, 0.34f, 0.68f, 1.0f};
    float chordFactor[4] = {1.00f, 0.80f, 0.46f, 0.14f};
    float thickFactor[4] = {1.00f, 0.68f, 0.36f, 0.14f};

    for (unsigned i = 0; i < 4; ++i) {
        float u = frac[i];
        float x = rootInset + side * span * u;
        /* Flecha hacia atrás en el tercio externo típica de Mobula birostris */
        float z = -span * sweep * (0.35f * u + 0.65f * u * u);
        float y = span * camber * sinf(u * 3.14159265f) * 0.25f;
        float wr = fmaxf(0.004f * slot->scale, baseChord * chordFactor[i]);
        float hr = fmaxf(0.003f * slot->scale, thick * thickFactor[i]);
        if (!Module_Node(g, module, (uint16_t)(i + 1), WorldFin(slot, Vec3_Create(x, y, z)),
                         wr, hr, 2, ANATOMY_ROLE_JOINT,
                         pectoral ? ANATOMY_REGION_WING : ANATOMY_REGION_HINDLIMB, aSide, p->development))
            return false;
        if (i > 0) {
            /* En aletas pectorales grandes el tramo proximal (i==1) se funde volumétricamente
             * con el disco SDF; los tramos distales usan conexión de soporte y delegan el
             * perfil hidrodinámico continuo a SupplementaryMesh (sin pliegues ni coste voxel). */
            BodyConnectionKind cKind = (pectoral && i >= 2) ? BODY_CONNECTION_SUPPORT : BODY_CONNECTION_LIMB_SEGMENT;
            if (!Module_Edge(g, module, (uint16_t)i,
                             Anatomy_MakeId(module, (uint16_t)i),
                             Anatomy_MakeId(module, (uint16_t)(i + 1)),
                             cKind, p->development))
                return false;
            BodyConnection* e = &g->connections[g->connectionCount - 1];
            e->transverseAxis = slot->forward;
            e->jointBlend = 0.95f;
            e->widthBulge = (i == 1 && pectoral) ? wr * 0.14f : 0.0f;
            e->heightBulge = (i == 1 && pectoral) ? hr * 0.08f : 0.0f;
        }
    }
    if (!Module_Edge(g, module, 4, slot->hostNode, Anatomy_MakeId(module, 1),
                     BODY_CONNECTION_SUPPORT, p->development))
        return false;

    /* Para aletas pectorales grandes, añadir 2 radios internos de soporte para anclaje/skinning. */
    if (pectoral) {
        Vector3 leadMid = {
            rootInset + side * span * 0.44f,
            span * camber * 0.06f,
            baseChord * 0.44f - span * sweep * 0.16f
        };
        Vector3 trailMid = {
            rootInset + side * span * 0.40f,
            -thick * 0.08f,
            -baseChord * 0.54f - span * sweep * 0.30f
        };
        uint16_t leadId = Limb_DigitLocalId(0, 0);
        uint16_t trailId = Limb_DigitLocalId(1, 0);
        if (!Module_Node(g, module, leadId, WorldFin(slot, leadMid),
                         baseChord * 0.28f, thick * 0.42f, 2,
                         ANATOMY_ROLE_DIGIT, ANATOMY_REGION_WING, aSide, p->development) ||
            !Module_Edge(g, module, leadId, Anatomy_MakeId(module, LIMB_NODE_ROOT),
                         Anatomy_MakeId(module, leadId), BODY_CONNECTION_SUPPORT, p->development))
            return false;

        if (!Module_Node(g, module, trailId, WorldFin(slot, trailMid),
                         baseChord * 0.32f, thick * 0.36f, 2,
                         ANATOMY_ROLE_DIGIT, ANATOMY_REGION_WING, aSide, p->development) ||
            !Module_Edge(g, module, trailId, Anatomy_MakeId(module, LIMB_NODE_ROOT),
                         Anatomy_MakeId(module, trailId), BODY_CONNECTION_SUPPORT, p->development))
            return false;
        g->connections[g->connectionCount - 1].transverseAxis = slot->forward;
        g->connections[g->connectionCount - 1].jointBlend = 0.95f;
    }
    return true;
}

bool LimbFin_BuildRigDescriptor(const LimbPhenotype* p, uint32_t module, const AnatomyGraph* g, LimbRigDescriptor* out) {
    if (!p || !g || !out) return false;
    bool pectoral = (p->role == LIMB_FORE || p->role == LIMB_WING);
    *out = (LimbRigDescriptor){
        .moduleInstanceId = module,
        .jointCount = 4,
        .role = pectoral ? LIMB_WING : LIMB_OTHER,
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

void LimbFin_ConfigureRig(const LimbRigDescriptor* d, const AnatomyGraph* g, Rig* rig) {
    if (!d || !g || !rig) return;
    for (size_t i = 0; i < rig->limbCount; ++i) {
        LimbRig* l = &rig->limbs[i];
        if (rig->skeleton.joints[l->rootJoint].anatomyId != d->joints[0]) continue;
        l->role = d->role;
        l->walking = false;
        l->digitigrade = false;
        l->unguligrade = false;
        l->soleHeight = 0.0f;
        float flex = d->flexionScale > 0.0f ? d->flexionScale : 1.0f;
        for (size_t j = 0; j < l->chain.jointCount; ++j) {
            SkeletonJoint* joint = &rig->skeleton.joints[l->chain.jointIndices[j]];
            joint->constraint = (JointConstraint){
                .type = JOINT_BALL,
                .hingeAxis = {0.0f, 0.0f, 1.0f},
                .maxSwingAngle = fminf(1.25f, (0.55f + 0.16f * (float)j) * flex),
                .maxTwistAngle = 0.25f
            };
        }
    }
}
