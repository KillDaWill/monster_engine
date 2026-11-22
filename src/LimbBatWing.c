/**
 * @file LimbBatWing.c
 * @brief Implementación genérica de extremidad alada quiroptera (LIMB_ARCHETYPE_WING).
 */
#include "LimbBatWing.h"
#include "CreatureModuleInternal.h"
#include "Limb.h"
#include "MathUtils.h"
#include <math.h>
#include <stddef.h>

static Vector3 WorldWing(const AttachmentSlot* slot, Vector3 local) {
    return Vec3_Add(slot->position,
        Vec3_Add(Vec3_Scale(slot->side, local.x),
        Vec3_Add(Vec3_Scale(slot->up, local.y),
                 Vec3_Scale(slot->forward, local.z))));
}

static bool AddWingNode(AnatomyGraph* g, uint32_t module, uint16_t local, Vector3 c,
                        float w, float h, const LimbPhenotype* p, bool digit) {
    AnatomyNodeRole role = digit ? ANATOMY_ROLE_DIGIT : ANATOMY_ROLE_JOINT;
    AnatomyRegion region = digit ? ANATOMY_REGION_DIGIT : ANATOMY_REGION_WING;
    AnatomySide side = p->side == LIMB_RIGHT ? ANATOMY_SIDE_RIGHT : ANATOMY_SIDE_LEFT;
    return Module_Node(g, module, local, c, w, h, 2, role, region, side, p->development);
}

bool LimbBatWing_Resolve(const LimbPhenotype* p, uint32_t module, const AttachmentSlot* slot, AnatomyGraph* g) {
    if (!p || !slot || !g || p->archetype != LIMB_ARCHETYPE_WING || p->digitCount > CREATURE_MAX_DIGITS)
        return false;
    for (unsigned d = 0; d < p->digitCount; ++d) {
        if (!p->digitPhalanges[d] || p->digitPhalanges[d] > 5) return false;
    }

    float dev = fmaxf(0.05f, p->development);
    float length = p->length * slot->scale * dev;
    float r = fmaxf(0.002f * slot->scale, p->thickness * slot->scale * dev);
    float side = p->side == LIMB_RIGHT ? -1.0f : 1.0f;

    float hum = length * 0.23f * fmaxf(0.4f, p->proximalScale);
    float rad = length * 0.36f * fmaxf(0.4f, p->middleScale);
    float tipSpan = length * 0.42f * fmaxf(0.4f, p->distalScale);
    float spread = Math_Clamp(1.0f - 0.55f * p->sprawl, 0.25f, 1.15f);
    float inset = -side * r * p->attachmentInset;

    /* Cadena ósea principal: hombro -> codo caudal -> tercio distal antebrazo -> carpo/muñeca. */
    Vector3 shoulder = {inset, 0.0f, 0.0f};
    Vector3 elbow = {
        inset + side * hum * (0.86f * spread),
        -hum * 0.14f,
        -hum * (0.44f - 0.12f * p->proximalSweep)
    };
    Vector3 midForearm = {
        elbow.x + side * rad * (0.46f * spread),
        elbow.y + rad * 0.08f,
        elbow.z + rad * 0.32f
    };
    Vector3 wrist = {
        elbow.x + side * rad * (0.94f * spread),
        elbow.y + rad * 0.18f,
        elbow.z + rad * (0.66f + 0.10f * p->distalSweep)
    };

    Vector3 pts[4] = {shoulder, elbow, midForearm, wrist};
    float wr[4] = {r * p->rootThicknessScale, r * 0.72f * p->middleThicknessScale,
                   r * 0.50f * p->distalThicknessScale, r * 0.46f * p->footScale};
    float hr[4] = {wr[0] * 1.04f, wr[1], wr[2] * 0.96f, wr[3] * 0.90f};

    for (unsigned i = 0; i < 4; ++i) {
        if (!AddWingNode(g, module, (uint16_t)(i + 1), WorldWing(slot, pts[i]), wr[i], hr[i], p, false))
            return false;
        if (i && !Module_Edge(g, module, (uint16_t)i,
                Anatomy_MakeId(module, i), Anatomy_MakeId(module, i + 1),
                BODY_CONNECTION_LIMB_SEGMENT, p->development))
            return false;
        if (i) {
            BodyConnection* edge = &g->connections[g->connectionCount - 1];
            edge->transverseAxis = slot->up;
            edge->jointBlend = 0.85f;
            edge->widthBulge = (i == 1) ? r * 0.14f * p->proximalThicknessScale : 0.0f;
            edge->heightBulge = edge->widthBulge * 0.85f;
        }
    }
    if (!Module_Edge(g, module, 4, slot->hostNode, Anatomy_MakeId(module, 1),
                     BODY_CONNECTION_SUPPORT, p->development))
        return false;

    /* Dedos del ala:
     * d == 0: pulgar libre cranial con garra curva de trepa.
     * d >= 1: dedos II-V alargados en abanico quiroptero sosteniendo los dactilopatagios. */
    static const float defaultFanAngles[CREATURE_MAX_DIGITS] = {0.0f, -0.06f, -0.24f, -0.72f, -1.28f};
    for (unsigned d = 0; d < p->digitCount; ++d) {
        AnatomyId previous = Anatomy_MakeId(module, LIMB_NODE_AUTOPOD);
        unsigned count = p->digitPhalanges[d] + 1;
        if (count < 2) count = 2;

        if (d == 0) {
            /* Pulgar corto libre dirigido cráneo-dorsalmente. */
            float thumbLen = Math_Max(length * 0.065f, rad * 0.18f) * fmaxf(0.3f, p->digitLengths[0]);
            float tr = r * 0.30f * p->digitThicknessScale;
            Vector3 dir = Vec3_Normalize(Vec3_Create(side * 0.24f, 0.35f, 0.90f));
            for (unsigned j = 0; j < count; ++j) {
                float t = (float)(j + 1) / (float)count;
                Vector3 c = WorldWing(slot, Vec3_Add(wrist, Vec3_Scale(dir, thumbLen * t)));
                float radj = tr * (1.0f - 0.35f * t);
                unsigned local = Limb_DigitLocalId(d, j);
                if (!AddWingNode(g, module, (uint16_t)local, c, radj, radj, p, true) ||
                    !Module_Edge(g, module, (uint16_t)local, previous, Anatomy_MakeId(module, local),
                                 BODY_CONNECTION_DIGIT_SEGMENT, p->development))
                    return false;
                previous = Anatomy_MakeId(module, local);
            }
            if (p->clawScale > 0.0f) {
                const AnatomyNode* toe = AnatomyGraph_FindNode(g, previous);
                float cr = tr * 0.65f;
                float cl = cr * p->clawScale * 1.4f;
                Vector3 cdir = Vec3_Normalize(Vec3_Add(slot->forward, Vec3_Scale(slot->up, 0.25f)));
                Vector3 tip = Vec3_Add(toe->center, Vec3_Scale(cdir, cl));
                unsigned local = Limb_DigitLocalId(d, count);
                if (!AddWingNode(g, module, (uint16_t)local, tip, cr * 0.16f, cr * 0.20f, p, true) ||
                    !Module_Edge(g, module, (uint16_t)local, previous, Anatomy_MakeId(module, local),
                                 BODY_CONNECTION_NAIL, p->development))
                    return false;
                BodyConnection* edge = &g->connections[g->connectionCount - 1];
                edge->crownInset = 0.40f;
                edge->toeRounding = 0.88f;
            }
            continue;
        }

        /* Radios digitales II..V: abanico desde el carpo en plano alar ligeramente combado. */
        float rayAngle = defaultFanAngles[d] + p->digitAngles[d] * 0.45f;
        float rayLen = tipSpan * fmaxf(0.15f, p->digitLengths[d]) * 1.25f;
        float rayBaseR = r * 0.26f * p->digitThicknessScale;
        for (unsigned j = 0; j < count; ++j) {
            float t = (float)(j + 1) / (float)count;
            /* Comba alar suave: metacarpiano se arquea levemente dorsal y falanges caen hacia el borde. */
            float camberY = tipSpan * (0.045f * sinf(t * 3.14159265f) - 0.035f * t * t);
            float angle = rayAngle - (.08f + .035f * (float)d) * t * t;
            Vector3 radialOut = {side * cosf(angle), 0.0f, sinf(angle)};
            Vector3 localPos = Vec3_Add(wrist, Vec3_Scale(radialOut, rayLen * t));
            localPos.y += camberY;
            float radj = fmaxf(0.0012f * slot->scale, rayBaseR * (1.0f - 0.62f * t));
            unsigned local = Limb_DigitLocalId(d, j);
            if (!AddWingNode(g, module, (uint16_t)local, WorldWing(slot, localPos), radj, radj * 0.92f, p, true) ||
                !Module_Edge(g, module, (uint16_t)local, previous, Anatomy_MakeId(module, local),
                             BODY_CONNECTION_DIGIT_SEGMENT, p->development))
                return false;
            g->connections[g->connectionCount - 1].transverseAxis = slot->up;
            previous = Anatomy_MakeId(module, local);
        }
    }
    return true;
}

bool LimbBatWing_BuildRigDescriptor(const LimbPhenotype* p, uint32_t module, const AnatomyGraph* g, LimbRigDescriptor* out) {
    if (!p || !g || !out) return false;
    *out = (LimbRigDescriptor){
        .moduleInstanceId = module,
        .jointCount = 4,
        .role = p->role,
        .side = p->side,
        .archetype = p->archetype,
        .locomotionLimb = false,
        .flexionScale = p->flexionScale > 0.0f ? p->flexionScale : 1.0f
    };
    for (unsigned i = 0; i < 4; ++i) {
        out->joints[i] = Anatomy_MakeId(module, i + 1);
        if (!AnatomyGraph_FindNode(g, out->joints[i])) return false;
    }
    out->endEffector = out->joints[3];
    out->soleHeight = 0.0f;
    return true;
}

void LimbBatWing_ConfigureRig(const LimbRigDescriptor* d, const AnatomyGraph* g, Rig* rig) {
    if (!d || !g || !rig) return;
    for (size_t i = 0; i < rig->limbCount; ++i) {
        LimbRig* l = &rig->limbs[i];
        if (rig->skeleton.joints[l->rootJoint].anatomyId != d->joints[0]) continue;
        l->soleHeight = 0.0f;
        l->role = LIMB_WING;
        l->walking = false;
        l->digitigrade = false;
        l->unguligrade = false;
        l->chain.maxIterations = 48;
        float flexion = d->flexionScale > 0.0f ? d->flexionScale : 1.0f;
        for (size_t j = 0; j < l->chain.jointCount; ++j) {
            SkeletonJoint* joint = &rig->skeleton.joints[l->chain.jointIndices[j]];
            if (j == 0) {
                /* Hombro quiroptero: amplísimo batido vertical, proacción y recogida al tronco. */
                joint->constraint = (JointConstraint){
                    .type = JOINT_ELLIPSOID,
                    .hingeAxis = {d->side == LIMB_RIGHT ? -1.0f : 1.0f, 0.0f, 0.0f},
                    .maxSwingAngle = fminf(1.55f, 1.45f * flexion),
                    .maxTwistAngle = 0.45f,
                    .maxPitchUp = 1.20f * flexion,
                    .maxPitchDown = 1.25f * flexion,
                    .maxYawAngle = 1.35f * flexion
                };
            } else {
                joint->constraint = (JointConstraint){
                    .type = JOINT_BALL,
                    .hingeAxis = {0.0f, 1.0f, 0.0f},
                    .maxSwingAngle = fminf(1.55f, (j == 1 ? 1.35f : 1.15f) * flexion),
                    .maxTwistAngle = 0.30f
                };
            }
        }
        /* Distribuir el plegado entre bases metacarpianas y falanges de los dedos II–V. */
        uint32_t modId = d->moduleInstanceId;
        for (unsigned dig = 1; dig < CREATURE_MAX_DIGITS; ++dig) {
            for (unsigned digitJoint = 0; digitJoint < 6; ++digitJoint) {
                int mj = Skeleton_FindJoint(&rig->skeleton,
                    Anatomy_MakeId(modId, Limb_DigitLocalId(dig, digitJoint)));
                if (mj < 0) break;
                rig->skeleton.joints[mj].constraint = (JointConstraint){
                    .type = JOINT_BALL,
                    .hingeAxis = {0.0f, 1.0f, 0.0f},
                    .maxSwingAngle = (digitJoint == 0 ? 1.25f : .43f) * flexion,
                    .maxTwistAngle = 0.25f
                };
            }
        }
    }
}
