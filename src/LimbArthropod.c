/**
 * @file LimbArthropod.c
 * @brief Implementación de pata exoesquelética de insecto/artrópodo con segmentos nítidos y uñas terminales.
 */
#include "LimbArthropod.h"
#include "CreatureModuleInternal.h"
#include "Limb.h"
#include "MathUtils.h"
#include <math.h>

static Vector3 WorldLeg(const AttachmentSlot* slot, Vector3 local) {
    return Vec3_Add(slot->position,
        Vec3_Add(Vec3_Scale(slot->side, local.x),
        Vec3_Add(Vec3_Scale(slot->up, local.y),
                 Vec3_Scale(slot->forward, local.z))));
}

bool LimbArthropod_Resolve(const LimbPhenotype* p, uint32_t module, const AttachmentSlot* slot, AnatomyGraph* g) {
    if (!p || !slot || !g || p->archetype != LIMB_ARCHETYPE_ARTHROPOD_LEG) return false;
    float dev = fmaxf(0.10f, p->development);
    float len = p->length * slot->scale * dev;
    float r = fmaxf(0.002f * slot->scale, p->thickness * slot->scale * dev);
    float side = p->side == LIMB_RIGHT ? -1.0f : 1.0f;
    AnatomySide aSide = p->side == LIMB_RIGHT ? ANATOMY_SIDE_RIGHT : ANATOMY_SIDE_LEFT;

    /* Cadena articular de insecto:
     * 1 (ROOT): Coxa / trocánter en la cara ventral del tórax
     * 2 (MIDDLE): Extremo distal del fémur (elevado/abierto lateralmente)
     * 3 (DISTAL): Extremo distal de la tibia (desciende grácilmente)
     * 4 (AUTOPOD): Tarso segmentado con par de uñas terminales (ungues) */
    float fem = len * 0.34f * fmaxf(0.4f, p->proximalScale);
    float tib = len * 0.40f * fmaxf(0.4f, p->middleScale);
    float tar = len * 0.26f * fmaxf(0.4f, p->distalScale);
    float sweep = p->proximalSweep; /* positivo: patas delanteras hacia delante; negativo: traseras hacia atrás */
    float sprawl = Math_Clamp(p->sprawl, 0.15f, 1.0f);

    Vector3 coxa = {-side * r * p->attachmentInset * 0.4f, 0.0f, 0.0f};
    Vector3 femurTip = {
        coxa.x + side * fem * (0.48f + 0.36f * sprawl),
        -fem * (0.28f - 0.22f * sprawl),
        fem * (0.18f + 0.65f * sweep)
    };
    Vector3 tibiaTip = {
        femurTip.x + side * tib * (0.24f + 0.16f * sprawl),
        femurTip.y - tib * 0.84f,
        femurTip.z + tib * (0.26f * sweep + 0.18f * p->distalSweep)
    };
    Vector3 tarsusTip = {
        tibiaTip.x + side * tar * 0.36f,
        tibiaTip.y - tar * 0.42f,
        tibiaTip.z + tar * (0.46f + 0.32f * sweep)
    };

    Vector3 pts[4] = {coxa, femurTip, tibiaTip, tarsusTip};
    float wr[4] = {
        r * 1.30f * p->rootThicknessScale,
        r * 0.92f * p->proximalThicknessScale,
        r * 0.64f * p->middleThicknessScale,
        r * 0.44f * p->distalThicknessScale
    };

    for (unsigned i = 0; i < 4; ++i) {
        if (!Module_Node(g, module, (uint16_t)(i + 1), WorldLeg(slot, pts[i]),
                         wr[i], wr[i], 2, ANATOMY_ROLE_JOINT, ANATOMY_REGION_LIMB, aSide, p->development))
            return false;
        if (i > 0) {
            if (!Module_Edge(g, module, (uint16_t)i,
                             Anatomy_MakeId(module, (uint16_t)i),
                             Anatomy_MakeId(module, (uint16_t)(i + 1)),
                             BODY_CONNECTION_LIMB_SEGMENT, p->development))
                return false;
            /* Uniones cuticulares rígidas sin abultamientos musculares vertebrados. */
            BodyConnection* e = &g->connections[g->connectionCount - 1];
            e->jointBlend = 0.06f;
        }
    }
    if (!Module_Edge(g, module, 4, slot->hostNode, Anatomy_MakeId(module, 1),
                     BODY_CONNECTION_SUPPORT, p->development))
        return false;

    /* Par de uñas tarsales terminales (ungues). */
    Vector3 tarDir = Vec3_Normalize(Vec3_Sub(tarsusTip, tibiaTip));
    Vector3 sideDir = Vec3_Normalize(Vec3_Cross(Vec3_Create(0, 1, 0), tarDir));
    if (Vec3_LengthSq(sideDir) < 1e-4f) sideDir = Vec3_Create(1, 0, 0);
    float clawLen = fmaxf(tar * 0.24f, r * 1.4f);
    for (unsigned c = 0; c < 2; ++c) {
        float sgn = c == 0 ? 1.0f : -1.0f;
        Vector3 tip = Vec3_Add(tarsusTip, Vec3_Add(Vec3_Scale(tarDir, clawLen * 0.72f),
                      Vec3_Add(Vec3_Scale(sideDir, sgn * clawLen * 0.34f), Vec3_Create(0, -clawLen * 0.42f, 0))));
        uint16_t cid = Limb_DigitLocalId(c, 0);
        if (!Module_Node(g, module, cid, WorldLeg(slot, tip),
                         wr[3] * 0.28f, wr[3] * 0.28f, 2,
                         ANATOMY_ROLE_DIGIT, ANATOMY_REGION_DIGIT, aSide, p->development) ||
            !Module_Edge(g, module, cid, Anatomy_MakeId(module, LIMB_NODE_AUTOPOD),
                         Anatomy_MakeId(module, cid), BODY_CONNECTION_NAIL, p->development))
            return false;
    }
    return true;
}

bool LimbArthropod_BuildRigDescriptor(const LimbPhenotype* p, uint32_t module, const AnatomyGraph* g, LimbRigDescriptor* out) {
    if (!p || !g || !out) return false;
    *out = (LimbRigDescriptor){
        .moduleInstanceId = module,
        .jointCount = 4,
        .role = LIMB_LEG,
        .side = p->side,
        .archetype = p->archetype,
        .locomotionLimb = false, /* En insectos voladores las patas se posicionan procedimentalmente sin IK de cuadrúpedo. */
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

void LimbArthropod_ConfigureRig(const LimbRigDescriptor* d, const AnatomyGraph* g, Rig* rig) {
    if (!d || !g || !rig) return;
    for (size_t i = 0; i < rig->limbCount; ++i) {
        LimbRig* l = &rig->limbs[i];
        if (rig->skeleton.joints[l->rootJoint].anatomyId != d->joints[0]) continue;
        l->role = LIMB_LEG;
        l->walking = d->locomotionLimb;
        l->digitigrade = false;
        l->unguligrade = false;
        float flex = d->flexionScale > 0.0f ? d->flexionScale : 1.0f;
        for (size_t j = 0; j < l->chain.jointCount; ++j) {
            SkeletonJoint* joint = &rig->skeleton.joints[l->chain.jointIndices[j]];
            joint->constraint = (JointConstraint){
                .type = JOINT_BALL,
                .hingeAxis = {0.0f, 1.0f, 0.0f},
                .maxSwingAngle = fminf(1.35f, 0.95f * flex),
                .maxTwistAngle = 0.22f
            };
        }
    }
}
