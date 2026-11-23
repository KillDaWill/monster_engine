/**
 * @file LimbAvian.c
 * @brief Implementación de ala aviar (húmero, radio-ulna, carpo, mano reducida) y pata rapaz prensil.
 */
#include "LimbAvian.h"
#include "CreatureModuleInternal.h"
#include "Limb.h"
#include "MathUtils.h"
#include <math.h>

static Vector3 WorldFrame(const AttachmentSlot* slot, Vector3 local) {
    return Vec3_Add(slot->position,
        Vec3_Add(Vec3_Scale(slot->side, local.x),
        Vec3_Add(Vec3_Scale(slot->up, local.y),
                 Vec3_Scale(slot->forward, local.z))));
}

static bool IsAvianWing(const LimbPhenotype* p) {
    return p->role == LIMB_WING || p->role == LIMB_FORE;
}

static bool ResolveWing(const LimbPhenotype* p, uint32_t module, const AttachmentSlot* slot, AnatomyGraph* g) {
    float dev = fmaxf(0.08f, p->development);
    float length = p->length * slot->scale * dev;
    float r = fmaxf(0.002f * slot->scale, p->thickness * slot->scale * dev);
    float side = p->side == LIMB_RIGHT ? -1.0f : 1.0f;
    AnatomySide aSide = p->side == LIMB_RIGHT ? ANATOMY_SIDE_RIGHT : ANATOMY_SIDE_LEFT;

    /* Plegado continuo [0=ala extendida en vuelo, 1=ala plegada contra los flancos]. */
    float fold = Math_Clamp01(p->sprawl);
    float ext = 1.0f - fold;

    float hum = length * 0.28f * fmaxf(0.4f, p->proximalScale);
    float ulna = length * 0.34f * fmaxf(0.4f, p->middleScale);
    float hand = length * 0.36f * fmaxf(0.4f, p->distalScale);
    float inset = -side * r * p->attachmentInset;

    /* Cadena ósea aviar auténtica:
     * 1: Hombro
     * 2: Codo (dirigido atrás-abajo; pegado al flanco al plegar)
     * 3: Punto medio radio/ulna (sostiene rémiges secundarias)
     * 4: Carpo/muñeca (borde de ataque anterior; gira hacia delante al plegar) */
    Vector3 shoulder = {inset, 0.0f, 0.0f};
    Vector3 elbow = {
        inset + side * hum * Math_Lerp(0.18f, 0.92f, ext),
        -hum * Math_Lerp(0.26f, 0.02f, ext),
        -hum * Math_Lerp(0.86f, 0.18f - 0.08f * p->proximalSweep, ext)
    };
    Vector3 midUlna = {
        elbow.x + side * ulna * Math_Lerp(0.06f, 0.52f, ext),
        elbow.y + ulna * Math_Lerp(0.10f, 0.04f, ext),
        elbow.z + ulna * Math_Lerp(0.44f, 0.14f, ext)
    };
    Vector3 wrist = {
        elbow.x + side * ulna * Math_Lerp(0.14f, 0.98f, ext),
        elbow.y + ulna * Math_Lerp(0.20f, 0.07f, ext),
        elbow.z + ulna * Math_Lerp(0.84f, 0.24f + 0.08f * p->distalSweep, ext)
    };

    Vector3 pts[4] = {shoulder, elbow, midUlna, wrist};
    float wr[4] = {
        r * 1.15f * p->rootThicknessScale,
        r * 0.84f * p->proximalThicknessScale,
        r * 0.64f * p->middleThicknessScale,
        r * 0.48f * p->distalThicknessScale
    };
    float hr[4] = {wr[0] * 1.05f, wr[1] * 0.92f, wr[2] * 0.82f, wr[3] * 0.72f};

    for (unsigned i = 0; i < 4; ++i) {
        if (!Module_Node(g, module, (uint16_t)(i + 1), WorldFrame(slot, pts[i]),
                         wr[i], hr[i], 2, ANATOMY_ROLE_JOINT, ANATOMY_REGION_WING, aSide, p->development))
            return false;
        if (i > 0) {
            if (!Module_Edge(g, module, (uint16_t)i,
                             Anatomy_MakeId(module, (uint16_t)i),
                             Anatomy_MakeId(module, (uint16_t)(i + 1)),
                             BODY_CONNECTION_LIMB_SEGMENT, p->development))
                return false;
            BodyConnection* e = &g->connections[g->connectionCount - 1];
            e->transverseAxis = slot->up;
            e->jointBlend = 0.72f;
        }
    }
    if (!Module_Edge(g, module, 4, slot->hostNode, Anatomy_MakeId(module, 1),
                     BODY_CONNECTION_SUPPORT, p->development))
        return false;

    /* Mano distal reducida (carpometacarpo + falange alar que sostiene las primarias).
     * A diferencia del murciélago, las aves NO tienen 4 dedos alargados en abanico. */
    Vector3 handDir = Vec3_Normalize(Vec3_Create(
        side * Math_Lerp(0.10f, 0.92f, ext),
        Math_Lerp(-0.18f, 0.02f, ext),
        Math_Lerp(-0.96f, -0.34f - 0.14f * p->wingPointedness, ext)
    ));
    AnatomyId prev = Anatomy_MakeId(module, LIMB_NODE_AUTOPOD);
    for (unsigned j = 0; j < 3; ++j) {
        float t = (float)(j + 1) / 3.0f;
        Vector3 hp = Vec3_Add(wrist, Vec3_Scale(handDir, hand * t));
        float hrj = fmaxf(0.0014f * slot->scale, wr[3] * (1.0f - 0.68f * t));
        uint16_t lid = Limb_DigitLocalId(0, j);
        if (!Module_Node(g, module, lid, WorldFrame(slot, hp), hrj, hrj * 0.75f, 2,
                         ANATOMY_ROLE_DIGIT, ANATOMY_REGION_DIGIT, aSide, p->development) ||
            !Module_Edge(g, module, lid, prev, Anatomy_MakeId(module, lid),
                         BODY_CONNECTION_DIGIT_SEGMENT, p->development))
            return false;
        g->connections[g->connectionCount - 1].transverseAxis = slot->up;
        prev = Anatomy_MakeId(module, lid);
    }
    /* Álula compacta en el borde anterior del carpo */
    Vector3 alulaDir = Vec3_Normalize(Vec3_Create(side * 0.55f, 0.12f, 0.82f));
    Vector3 alulaPos = Vec3_Add(wrist, Vec3_Scale(alulaDir, hand * 0.18f));
    uint16_t alulaId = Limb_DigitLocalId(1, 0);
    if (!Module_Node(g, module, alulaId, WorldFrame(slot, alulaPos),
                     wr[3] * 0.42f, wr[3] * 0.34f, 2,
                     ANATOMY_ROLE_DIGIT, ANATOMY_REGION_DIGIT, aSide, p->development) ||
        !Module_Edge(g, module, alulaId, Anatomy_MakeId(module, LIMB_NODE_AUTOPOD),
                     Anatomy_MakeId(module, alulaId), BODY_CONNECTION_DIGIT_SEGMENT, p->development))
        return false;
    return true;
}

static bool ResolveLeg(const LimbPhenotype* p, uint32_t module, const AttachmentSlot* slot, AnatomyGraph* g) {
    float dev = fmaxf(0.08f, p->development);
    float length = p->length * slot->scale * dev;
    float r = fmaxf(0.002f * slot->scale, p->thickness * slot->scale * dev);
    float side = p->side == LIMB_RIGHT ? -1.0f : 1.0f;
    AnatomySide aSide = p->side == LIMB_RIGHT ? ANATOMY_SIDE_RIGHT : ANATOMY_SIDE_LEFT;

    /* Anatomía de pata rapaz:
     * 1: Cadera / muslo alto emplumado
     * 2: Rodilla (adelantada y corta dentro del contorno corporal)
     * 3: Articulación intertarsal (tobillos angulados hacia atrás)
     * 4: Base podal (tarsometatarso escamoso delgado apoyado en suelo/posadero) */
    float femur = length * 0.28f * fmaxf(0.4f, p->proximalScale);
    float tibio = length * 0.40f * fmaxf(0.4f, p->middleScale);
    float tarso = length * 0.32f * fmaxf(0.4f, p->distalScale);

    Vector3 hip = {-side * r * p->attachmentInset * 0.5f, 0.0f, 0.0f};
    Vector3 knee = {
        hip.x + side * r * 0.35f,
        -femur * 0.72f,
        femur * (0.58f + 0.18f * p->proximalSweep)
    };
    Vector3 ankle = {
        knee.x + side * r * 0.15f,
        knee.y - tibio * 0.86f,
        knee.z - tibio * (0.48f - 0.15f * p->distalSweep)
    };
    Vector3 foot = {
        ankle.x,
        ankle.y - tarso * 0.94f,
        ankle.z + tarso * 0.34f
    };

    Vector3 pts[4] = {hip, knee, ankle, foot};
    float wr[4] = {
        r * 1.65f * p->rootThicknessScale,
        r * 1.25f * p->proximalThicknessScale,
        r * 0.62f * p->middleThicknessScale,
        r * 0.54f * p->distalThicknessScale
    };
    float hr[4] = {wr[0] * 1.15f, wr[1] * 1.08f, wr[2] * 1.05f, wr[3] * 0.90f};

    for (unsigned i = 0; i < 4; ++i) {
        if (!Module_Node(g, module, (uint16_t)(i + 1), WorldFrame(slot, pts[i]),
                         wr[i], hr[i], 2, ANATOMY_ROLE_JOINT, ANATOMY_REGION_HINDLIMB, aSide, p->development))
            return false;
        if (i > 0) {
            if (!Module_Edge(g, module, (uint16_t)i,
                             Anatomy_MakeId(module, (uint16_t)i),
                             Anatomy_MakeId(module, (uint16_t)(i + 1)),
                             BODY_CONNECTION_LIMB_SEGMENT, p->development))
                return false;
            g->connections[g->connectionCount - 1].jointBlend = (i == 1) ? 0.62f : 0.28f;
        }
    }
    if (!Module_Edge(g, module, 4, slot->hostNode, Anatomy_MakeId(module, 1),
                     BODY_CONNECTION_SUPPORT, p->development))
        return false;

    /* Pie prensil anisodáctilo: 3 dedos delanteros (II, III central largo, IV) + 1 hallux trasero (I). */
    unsigned digits = p->digitCount >= 3 ? (p->digitCount > 4 ? 4u : p->digitCount) : 4u;
    static const float defaultYaw[4] = {-0.46f, 0.0f, 0.48f, 3.14159265f};
    static const float defaultLen[4] = {0.82f, 1.08f, 0.88f, 0.72f};
    float toeBaseLen = length * 0.26f * fmaxf(0.4f, p->footScale);
    float toeR = fmaxf(0.0016f * slot->scale, wr[3] * 0.56f * p->digitThicknessScale);

    for (unsigned d = 0; d < digits; ++d) {
        bool hallux = (d == 3);
        float yaw = defaultYaw[d];
        if (!hallux) {
            yaw = defaultYaw[d] * fmaxf(0.45f, p->digitSpread) + p->digitAngles[d] * 0.35f;
            yaw *= side;
        } else {
            yaw = 3.14159265f - side * 0.10f;
        }
        float dLen = toeBaseLen * defaultLen[d] * (p->digitLengths[d] > 0.1f ? p->digitLengths[d] : 1.0f);
        unsigned phal = (p->digitPhalanges[d] >= 2 && p->digitPhalanges[d] <= 4) ? p->digitPhalanges[d] : (hallux ? 2u : 3u);
        Vector3 dirXZ = {sinf(yaw), 0.0f, cosf(yaw)};
        AnatomyId prev = Anatomy_MakeId(module, LIMB_NODE_AUTOPOD);
        Vector3 cur = foot;
        cur.y -= wr[3] * 0.18f;
        for (unsigned j = 0; j < phal; ++j) {
            float t = (float)(j + 1) / (float)phal;
            float archY = sinf(t * 3.14159265f) * toeR * 0.45f - t * toeR * 0.35f;
            Vector3 pNext = {
                foot.x + dirXZ.x * dLen * t,
                foot.y - wr[3] * 0.16f + archY,
                foot.z + dirXZ.z * dLen * t
            };
            float radj = fmaxf(0.0012f * slot->scale, toeR * (1.0f - 0.36f * t));
            uint16_t lid = Limb_DigitLocalId(d, j);
            if (!Module_Node(g, module, lid, WorldFrame(slot, pNext), radj, radj * 0.92f, 2,
                             ANATOMY_ROLE_DIGIT, ANATOMY_REGION_DIGIT, aSide, p->development) ||
                !Module_Edge(g, module, lid, prev, Anatomy_MakeId(module, lid),
                             BODY_CONNECTION_DIGIT_SEGMENT, p->development))
                return false;
            prev = Anatomy_MakeId(module, lid);
            cur = pNext;
        }
        /* Garra rapaz curvada y afilada (talon) al extremo de cada dedo. */
        float clawScale = p->clawScale > 0.01f ? p->clawScale : 1.65f;
        float clawLen = toeR * (1.5f + 0.9f * clawScale) * (hallux ? 1.18f : 1.0f);
        Vector3 clawMid = {
            cur.x + dirXZ.x * clawLen * 0.56f,
            cur.y - clawLen * 0.24f,
            cur.z + dirXZ.z * clawLen * 0.56f
        };
        Vector3 clawTip = {
            cur.x + dirXZ.x * clawLen * 0.80f,
            cur.y - clawLen * 0.78f,
            cur.z + dirXZ.z * clawLen * 0.80f
        };
        uint16_t cmId = Limb_DigitLocalId(d, phal);
        uint16_t ctId = Limb_DigitLocalId(d, phal + 1);
        if (!Module_Node(g, module, cmId, WorldFrame(slot, clawMid), toeR * 0.44f, toeR * 0.50f, 2,
                         ANATOMY_ROLE_DIGIT, ANATOMY_REGION_DIGIT, aSide, p->development) ||
            !Module_Edge(g, module, cmId, prev, Anatomy_MakeId(module, cmId),
                         BODY_CONNECTION_NAIL, p->development) ||
            !Module_Node(g, module, ctId, WorldFrame(slot, clawTip), toeR * 0.14f, toeR * 0.18f, 2,
                         ANATOMY_ROLE_DIGIT, ANATOMY_REGION_DIGIT, aSide, p->development) ||
            !Module_Edge(g, module, ctId, Anatomy_MakeId(module, cmId), Anatomy_MakeId(module, ctId),
                         BODY_CONNECTION_NAIL, p->development))
            return false;
        g->connections[g->connectionCount - 1].crownInset = 0.42f;
        g->connections[g->connectionCount - 1].toeRounding = 0.85f;
    }
    return true;
}

bool LimbAvian_Resolve(const LimbPhenotype* p, uint32_t module, const AttachmentSlot* slot, AnatomyGraph* g) {
    if (!p || !slot || !g || p->archetype != LIMB_ARCHETYPE_AVIAN) return false;
    return IsAvianWing(p) ? ResolveWing(p, module, slot, g) : ResolveLeg(p, module, slot, g);
}

bool LimbAvian_BuildRigDescriptor(const LimbPhenotype* p, uint32_t module, const AnatomyGraph* g, LimbRigDescriptor* out) {
    if (!p || !g || !out) return false;
    bool wing = IsAvianWing(p);
    *out = (LimbRigDescriptor){
        .moduleInstanceId = module,
        .jointCount = 4,
        .role = wing ? LIMB_WING : LIMB_HIND,
        .side = p->side,
        .archetype = p->archetype,
        .locomotionLimb = !wing,
        .flexionScale = p->flexionScale > 0.0f ? p->flexionScale : 1.0f
    };
    for (unsigned i = 0; i < 4; ++i) {
        out->joints[i] = Anatomy_MakeId(module, (uint16_t)(i + 1));
        if (!AnatomyGraph_FindNode(g, out->joints[i])) return false;
    }
    out->endEffector = out->joints[3];
    const AnatomyNode* foot = AnatomyGraph_FindNode(g, out->endEffector);
    out->soleHeight = (!wing && foot) ? foot->heightRadius * 0.90f : 0.0f;
    return true;
}

void LimbAvian_ConfigureRig(const LimbRigDescriptor* d, const AnatomyGraph* g, Rig* rig) {
    if (!d || !g || !rig) return;
    bool wing = d->role == LIMB_WING;
    for (size_t i = 0; i < rig->limbCount; ++i) {
        LimbRig* l = &rig->limbs[i];
        if (rig->skeleton.joints[l->rootJoint].anatomyId != d->joints[0]) continue;
        l->role = d->role;
        l->walking = d->locomotionLimb;
        l->digitigrade = !wing;
        l->unguligrade = false;
        l->soleHeight = d->soleHeight;
        l->chain.maxIterations = 48;
        float flex = d->flexionScale > 0.0f ? d->flexionScale : 1.0f;
        for (size_t j = 0; j < l->chain.jointCount; ++j) {
            SkeletonJoint* joint = &rig->skeleton.joints[l->chain.jointIndices[j]];
            if (wing) {
                if (j == 0) {
                    joint->constraint = (JointConstraint){
                        .type = JOINT_ELLIPSOID,
                        .hingeAxis = {d->side == LIMB_RIGHT ? -1.0f : 1.0f, 0.0f, 0.0f},
                        .maxSwingAngle = fminf(1.55f, 1.40f * flex),
                        .maxTwistAngle = 0.45f,
                        .maxPitchUp = 1.25f * flex,
                        .maxPitchDown = 1.25f * flex,
                        .maxYawAngle = 1.35f * flex
                    };
                } else {
                    joint->constraint = (JointConstraint){
                        .type = JOINT_BALL,
                        .hingeAxis = {0.0f, 1.0f, 0.0f},
                        .maxSwingAngle = fminf(1.50f, (j == 1 ? 1.35f : 1.15f) * flex),
                        .maxTwistAngle = 0.25f
                    };
                }
            } else {
                joint->constraint = (JointConstraint){
                    .type = JOINT_BALL,
                    .hingeAxis = {1.0f, 0.0f, 0.0f},
                    .maxSwingAngle = (j == 0 ? 0.85f : 1.15f) * flex,
                    .maxTwistAngle = 0.20f
                };
            }
        }
        if (wing) {
            for (unsigned st = 0; st < 3; ++st) {
                int j = Skeleton_FindJoint(&rig->skeleton, Anatomy_MakeId(d->moduleInstanceId, Limb_DigitLocalId(0, st)));
                if (j >= 0) {
                    rig->skeleton.joints[j].constraint = (JointConstraint){
                        .type = JOINT_BALL, .hingeAxis = {0.0f, 1.0f, 0.0f},
                        .maxSwingAngle = 1.15f * flex, .maxTwistAngle = 0.20f
                    };
                }
            }
        }
    }
}
