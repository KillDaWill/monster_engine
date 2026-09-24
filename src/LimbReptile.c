/**
 * @file LimbReptile.c
 * @brief Implementación del arquetipo de miembro de reptil.
 * @author Monster Engine Team
 * @date 2026
 */

#include "LimbReptile.h"
#include "Limb.h"
#include "CreatureModuleInternal.h"
#include "MathUtils.h"
#include <math.h>

static JointConstraint Ball(Vector3 axis, float swing, float twist) {
    return (JointConstraint){.type = JOINT_BALL, .hingeAxis = axis, .maxSwingAngle = swing, .maxTwistAngle = twist};
}

static bool LimbNode(AnatomyGraph* g, uint32_t module, uint16_t local, Vector3 c, float w, float h,
                     int color, AnatomyNodeRole role, const LimbPhenotype* p, float dev) {
    AnatomyRegion region = (role == ANATOMY_ROLE_DIGIT) ? ANATOMY_REGION_DIGIT :
                           (p->role == LIMB_FORE) ? ANATOMY_REGION_FORELIMB :
                           (p->role == LIMB_HIND) ? ANATOMY_REGION_HINDLIMB :
                           (p->role == LIMB_WING) ? ANATOMY_REGION_WING : ANATOMY_REGION_LIMB;
    return Module_Node(g, module, local, c, w, h, color, role, region,
                       p->side == LIMB_LEFT ? ANATOMY_SIDE_LEFT : p->side == LIMB_RIGHT ? ANATOMY_SIDE_RIGHT : ANATOMY_SIDE_CENTER, dev);
}

typedef struct DigitProfile {
    unsigned phalanges;
    float length, base, splay, bend;
} DigitProfile;

static float Development(float age, float start, float end) {
    float t = Math_Clamp01((age - start) / (end - start));
    return t * t * (3.0f - 2.0f * t);
}

static bool AddAutopod(AnatomyGraph* g, AnatomyId handId, Vector3 hand,
                       unsigned limb, bool hind, float length, float r, int color,
                       const LimbPhenotype* phenotype, float sprawl, float development) {
    float side = phenotype->side == LIMB_RIGHT ? -1.0f : 1.0f;
    float forward = hind ? -1.0f : 1.0f;
    for (unsigned digit = 0; digit < phenotype->digitCount; ++digit) {
        DigitProfile p = {
            phenotype->digitPhalanges[digit],
            phenotype->digitLengths[digit],
            ((float)digit - (phenotype->digitCount - 1) * 0.5f) * 0.50f,
            phenotype->digitAngles[digit] * phenotype->digitSpread,
            0.34f
        };
        float footW = phenotype->footWidthScale > 0 ? phenotype->footWidthScale : 1.0f;
        float digitThick = phenotype->digitThicknessScale > 0 ? phenotype->digitThicknessScale : 1.0f;
        float effectiveFootR = r * footW;
        Vector3 rootOffset = Vec3_Create(side * effectiveFootR * p.base, 0,
                                         forward * effectiveFootR * (0.32f - 0.20f * fabsf(p.base)) * sprawl);
        float rootStage = Development(development, 0.22f, 0.50f);
        Vector3 root = Vec3_Add(hand, Vec3_Scale(rootOffset, fmaxf(rootStage, 0.0005f)));
        AnatomyId previous = handId;
        Vector3 position = root;
        float digitalLength = length * (hind ? 0.29f : 0.26f) * p.length;
        float weights = 0.0f;
        for (unsigned j = 0; j < p.phalanges; ++j) weights += 1.0f - 0.12f * j;
        for (unsigned station = 0; station <= p.phalanges + 1; ++station) {
            AnatomyId id = Anatomy_MakeId(limb, Limb_DigitLocalId(digit, station));
            float progress = station > 0 ? (float)(station - 1) / p.phalanges : 0.0f;
            float stage = Development(development, 0.22f + 0.02f * station, 0.50f + 0.045f * station);
            float digitDevelopment = Development(development, 0.22f, 0.50f);
            float radius = fmaxf(1e-6f, r * digitThick * (station == 0 ? 0.40f : (0.32f - 0.18f * progress)) * sqrtf(digitDevelopment));
            if (station > 0) {
                float segment = station == 1 ? effectiveFootR * (0.30f + 0.35f * sprawl) : digitalLength * (1.0f - 0.12f * (station - 2)) / weights;
                segment *= fmaxf(stage, 0.0005f);
                float angle = p.splay + p.bend * progress;
                position.x += side * sinf(angle) * segment;
                position.z += forward * cosf(angle) * segment;
                float yLift = (station == p.phalanges + 1 ? -0.45f : (0.12f - 0.34f * progress));
                if (yLift > 0.0f) yLift *= sprawl;
                position.y += segment * yLift;
            }
            /* Girar el pie completo alrededor del tobillo mantiene longitudes
             * y conexiones digitales durante la interpolación de postura. */
            Vector3 offset = Vec3_Sub(position, hand);
            float yaw = phenotype->footYaw * side * sprawl;
            float co = cosf(yaw), si = sinf(yaw);
            Vector3 oriented = Vec3_Add(hand, Vec3_Create(co*offset.x + si*offset.z,
                                                         offset.y, -si*offset.x + co*offset.z));
            if (!LimbNode(g, limb, (uint16_t)id, oriented, radius, radius * 0.88f, color, ANATOMY_ROLE_DIGIT, phenotype, development) ||
                !Module_Edge(g, limb, (uint16_t)id, previous, id, BODY_CONNECTION_DIGIT_SEGMENT, development)) {
                return false;
            }
            g->dormantConnections[g->connectionCount - 1] = (stage <= 0.0f);
            previous = id;
        }
    }
    return true;
}

bool LimbReptile_Resolve(const LimbPhenotype* phenotype,
                         uint32_t module,
                         const AttachmentSlot* slot,
                         AnatomyGraph* g) {
    if (!phenotype || !slot || !g || phenotype->archetype != LIMB_ARCHETYPE_REPTILE ||
        phenotype->digitCount > CREATURE_MAX_DIGITS || !Anatomy_MakeId(module, 1)) {
        return false;
    }
    for (unsigned d = 0; d < phenotype->digitCount; ++d) {
        if (!phenotype->digitPhalanges[d] || phenotype->digitPhalanges[d] > 5) return false;
    }

    size_t firstNode = g->nodeCount;
    bool left = (phenotype->side != LIMB_RIGHT);
    bool hind = (phenotype->role == LIMB_HIND);
    float length = phenotype->length * slot->scale;
    float thickness = phenotype->thickness * slot->scale;
    float development = phenotype->development;
    int color = 2;

    float emergence = Development(development, 0.0f, 0.30f);
    length *= fmaxf(emergence, 0.0005f);
    thickness = Math_Lerp(Math_Max(thickness * 0.012f, 0.0005f), thickness, emergence);

    float sprawl = Math_Clamp01((development - 0.15f) / 0.35f);
    sprawl = sprawl * sprawl * (3.0f - 2.0f * sprawl) * phenotype->sprawl;

    float side = left ? 1.0f : -1.0f;
    AnatomyId base = Anatomy_MakeId(module, LIMB_NODE_ROOT);

    float r = thickness;
    float rootScale = phenotype->rootThicknessScale > 0 ? phenotype->rootThicknessScale : 1.0f;
    float midScale = phenotype->middleThicknessScale > 0 ? phenotype->middleThicknessScale : 1.0f;
    float distalScale = phenotype->distalThicknessScale > 0 ? phenotype->distalThicknessScale : 1.0f;
    float footW = phenotype->footWidthScale > 0 ? phenotype->footWidthScale : 1.0f;
    float footH = phenotype->footHeightScale > 0 ? phenotype->footHeightScale : 1.0f;

    /* Factores moderados por rol: el fenotipo describe directamente los radios relativos */
    float roleRootFactor = hind ? 1.15f : 1.05f;
    float rootRadius = r * roleRootFactor * rootScale;
    float rootH = rootRadius * 0.90f;

    /* Inserción controlada en la superficie del cuerpo para garantizar solapamiento SDF */
    float insetFactor = phenotype->attachmentInset > 0.0f ? phenotype->attachmentInset : 0.45f;
    float rootInsetDistance = rootRadius * insetFactor;
    float girdleX = -rootInsetDistance;
    float girdleY = 0, z = 0;

    float upper = length * (hind ? 0.36f : 0.34f) * phenotype->proximalScale;
    float lower = length * (hind ? 0.31f : 0.32f) * phenotype->middleScale;
    Vector3 root = Vec3_Create(side * girdleX, girdleY, z);
    Vector3 elbow = Vec3_Create(side * (girdleX + upper),
                                girdleY - length * Math_Lerp(0.25f, 0.10f, sprawl),
                                z + length * ((hind ? 0.16f : -0.14f) + phenotype->proximalSweep) * sprawl);
    Vector3 wrist = Vec3_Create(side * (girdleX + upper * Math_Lerp(0.95f, 0.72f, sprawl)),
                                girdleY - lower * Math_Lerp(1.25f, 0.98f, sprawl),
                                z + ((hind ? -lower * 0.36f : lower * 0.44f) + length * phenotype->distalSweep) * sprawl);
    Vector3 handOffset = Vec3_Create(
        side * upper * (Math_Lerp(1.05f, 0.78f, sprawl) - Math_Lerp(0.95f, 0.72f, sprawl)),
        - lower * (Math_Lerp(1.40f, 1.10f, sprawl) - Math_Lerp(1.25f, 0.98f, sprawl)),
        (hind ? -length * 0.16f : length * 0.14f) * sprawl);

    float lowerStage = Development(development, 0.10f, 0.45f);
    float palmStage = Development(development, 0.20f, 0.60f);
    wrist = Vec3_Lerp(elbow, wrist, fmaxf(lowerStage, 0.0005f));
    Vector3 hand = Vec3_Add(wrist, Vec3_Scale(handOffset, fmaxf(palmStage, 0.0005f) * phenotype->distalScale));

    float midFactor = hind ? 0.92f : 0.88f;
    float elbowRadius = r * midFactor * midScale;
    float elbowH = elbowRadius * 0.88f;

    float distalFactor = hind ? 0.80f : 0.76f;
    float wristRadius = r * distalFactor * distalScale;
    float wristH = wristRadius * 0.82f;

    float palmWFactor = hind ? 1.15f : 1.08f;
    float handW = r * palmWFactor * phenotype->footScale * footW;
    float handH = r * 0.40f * phenotype->footScale * footH;

    if (!LimbNode(g, module, (uint16_t)base, root, rootRadius, rootH, color, ANATOMY_ROLE_JOINT, phenotype, development) ||
        !LimbNode(g, module, (uint16_t)(base + 1), elbow, elbowRadius, elbowH, color, ANATOMY_ROLE_JOINT, phenotype, development) ||
        !LimbNode(g, module, (uint16_t)(base + 2), wrist, wristRadius, wristH, color, ANATOMY_ROLE_JOINT, phenotype, development) ||
        !LimbNode(g, module, (uint16_t)(base + 3), hand, handW, handH, color, ANATOMY_ROLE_JOINT, phenotype, development) ||
        !Module_Edge(g, module, 1, base, base + 1, BODY_CONNECTION_LIMB_SEGMENT, development) ||
        !Module_Edge(g, module, 2, base + 1, base + 2, BODY_CONNECTION_LIMB_SEGMENT, development) ||
        !Module_Edge(g, module, 3, base + 2, base + 3, BODY_CONNECTION_LIMB_SEGMENT, development)) {
        return false;
    }

    g->dormantConnections[g->connectionCount - 3] = (emergence <= 0.0f);
    g->dormantConnections[g->connectionCount - 2] = (lowerStage <= 0.0f);
    g->dormantConnections[g->connectionCount - 1] = (palmStage <= 0.0f);

    if (!AddAutopod(g, base + 3, hand, module, hind, length * phenotype->footScale, r * phenotype->footScale, color, phenotype, sprawl, development)) {
        return false;
    }

    for (size_t i = firstNode; i < g->nodeCount; ++i) {
        Vector3 c = g->nodes[i].center;
        g->nodes[i].center = Vec3_Add(slot->position,
            Vec3_Add(Vec3_Scale(slot->side, c.x),
            Vec3_Add(Vec3_Scale(slot->up, c.y),
                     Vec3_Scale(slot->forward, c.z))));
    }

    /* La unión con el cuerpo se realiza mediante solapamiento SDF con BODY_CONNECTION_SUPPORT */
    if (!Module_Edge(g, module, 4, slot->hostNode, base, BODY_CONNECTION_SUPPORT, development)) {
        return false;
    }
    g->dormantConnections[g->connectionCount - 1] = (development <= 0.0f);
    return true;
}

bool LimbReptile_BuildRigDescriptor(const LimbPhenotype* phenotype,
                                    uint32_t moduleInstanceId,
                                    const AnatomyGraph* graph,
                                    LimbRigDescriptor* out) {
    if (!phenotype || !graph || !out || !Anatomy_MakeId(moduleInstanceId, 1)) return false;
    out->moduleInstanceId = moduleInstanceId;
    out->jointCount = 4;
    for (unsigned j = 0; j < 4; ++j) {
        out->joints[j] = Anatomy_MakeId(moduleInstanceId, (uint16_t)(LIMB_NODE_ROOT + j));
    }
    out->endEffector = out->joints[3];
    out->role = phenotype->role;
    out->side = phenotype->side;
    out->archetype = phenotype->archetype;

    const AnatomyNode* root = AnatomyGraph_FindNode(graph, out->joints[0]);
    out->locomotionLimb = root && (root->development > 0.05f) &&
        (phenotype->role == LIMB_FORE || phenotype->role == LIMB_HIND || phenotype->role == LIMB_LEG);

    const AnatomyNode* handNode = AnatomyGraph_FindNode(graph, out->joints[3]);
    float sole = handNode ? handNode->heightRadius : 0.0f;
    for (size_t j = 0; j < graph->nodeCount; ++j) {
        const AnatomyNode* toe = &graph->nodes[j];
        if (toe->moduleInstanceId == moduleInstanceId && toe->region == ANATOMY_REGION_DIGIT && handNode) {
            sole = fmaxf(sole, handNode->center.y - toe->center.y + toe->heightRadius);
        }
    }
    out->soleHeight = sole;
    return true;
}

void LimbReptile_ConfigureRig(const LimbRigDescriptor* limb,
                              const AnatomyGraph* graph,
                              Rig* rig) {
    if (!limb || !graph || !rig) return;
    /* Buscar el LimbRig correspondiente en el rig */
    LimbRig* rLimb = NULL;
    for (size_t i = 0; i < rig->limbCount; ++i) {
        if (rig->limbs[i].chain.jointCount >= 4 &&
            rig->skeleton.joints[rig->limbs[i].rootJoint].anatomyId == limb->joints[0]) {
            rLimb = &rig->limbs[i];
            break;
        }
    }
    if (!rLimb) return;

    Skeleton* s = &rig->skeleton;
    int a = rLimb->chain.jointIndices[0];
    int b = rLimb->chain.jointIndices[1];
    int c = rLimb->chain.jointIndices[2];
    Vector3 upper = s->joints[b].restPosition;
    Vector3 lower = s->joints[c].restPosition;
    Vector3 axis = Vec3_Normalize(Vec3_Cross(upper, lower));
    float bend = acosf(fmaxf(-1.0f, fminf(1.0f, Vec3_Dot(Vec3_Normalize(upper), Vec3_Normalize(lower)))));

    s->joints[a].constraint = Ball(Vec3_Normalize(upper), 0.85f, 0.65f);
    s->joints[b].constraint = (JointConstraint){
        .type = JOINT_HINGE,
        .hingeAxis = axis,
        .minAngle = -fmaxf(0.0f, bend - 0.15f),
        .maxAngle = fmaxf(0.0f, 2.95f - bend)
    };
    s->joints[c].constraint = Ball(Vec3_Normalize(s->joints[rLimb->endEffectorJoint].restPosition), 0.55f, 0.25f);
    s->joints[rLimb->endEffectorJoint].constraint = Ball(Vec3_Create(0, 1, 0), 0.65f, 1.0f);
    rLimb->soleHeight = limb->soleHeight;
}
