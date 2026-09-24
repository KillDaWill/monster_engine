/**
 * @file HeadModule.c
 * @brief Implementación del módulo cefálico.
 * @author Monster Engine Team
 * @date 2026
 */

#include "HeadModule.h"
#include "CreatureModuleInternal.h"
#include "MathUtils.h"

bool HeadModule_Resolve(const HeadPhenotype* phenotype,
                        const HeadEnvelopePhenotype* envelope,
                        float development,
                        uint32_t module,
                        const AttachmentSlot* slot,
                        AnatomyGraph* graph,
                        AttachmentSlotSet* slots) {
    if (!phenotype || !slot || !graph || !Anatomy_MakeId(module, HEAD_NODE_MAIN)) return false;

    float s = slot->scale;
    float d = development;
    float t = d * d * (3.0f - 2.0f * d);

    /* Resolver una sola envolvente y colocar la unión cervical en su anclaje. */
    Vector3 radii = Vec3_Create(
        Math_Lerp(slot->hostRadii.x * 1.5f, 1.025f * s, t),
        Math_Lerp(slot->hostRadii.y * 1.847825f, .60f * s, t),
        Math_Lerp(slot->hostRadii.x * 1.10f, 1.10f * s, t));
    if (envelope) {
        radii.x *= envelope->scale * envelope->widthScale;
        radii.y *= envelope->scale * envelope->heightScale;
        radii.z *= envelope->scale * envelope->lengthScale;
    }
    HeadAnatomy anatomy;
    if (!HeadAnatomy_Resolve(phenotype, 0, radii, &anatomy)) return false;
    Vector3 c = Vec3_Sub(slot->position, anatomy.landmarks.neckAttachment);
    Vector3 skull = Vec3_Add(c, anatomy.surface.craniumCenter);
    float w = anatomy.surface.craniumRadii.x;
    float h = anatomy.surface.craniumRadii.y;
    float depth = anatomy.surface.craniumRadii.z;

    AnatomyId headId = Anatomy_MakeId(module, HEAD_NODE_MAIN);
    if (!Module_Node(graph, module, HEAD_NODE_MAIN, c, w, h, 3,
                     ANATOMY_ROLE_AXIAL, ANATOMY_REGION_HEAD, ANATOMY_SIDE_CENTER, d) ||
        !Module_Edge(graph, module, 1, headId, slot->hostNode, BODY_CONNECTION_AXIAL_LOFT, d)) {
        return false;
    }

    graph->nodes[graph->nodeCount - 1].envelopeRadii = radii;

    if (slots) {
        AttachmentSlot cranial = {
            .id = HEAD_SLOT_CRANIAL_DORSAL,
            .role = ATTACHMENT_CRANIAL,
            .hostModuleInstanceId = module,
            .hostNode = headId,
            .position = Vec3_Add(skull, Vec3_Scale(slot->up, h * .95f)),
            .forward = slot->forward,
            .up = slot->up,
            .side = slot->side,
            .hostRadii = Vec3_Create(w, h, depth),
            .scale = s
        };
        if (slots->count < ATTACHMENT_MAX_SLOTS) slots->slots[slots->count++] = cranial;

        Vector3 occUpL = Vec3_Normalize(Vec3_Create(0.12f, 0.65f, -0.75f));
        Vector3 occUpR = Vec3_Normalize(Vec3_Create(-0.12f, 0.65f, -0.75f));
        AttachmentSlot hornL = {
            .id = HEAD_SLOT_HORN_LEFT,
            .role = ATTACHMENT_CRANIAL,
            .hostModuleInstanceId = module,
            .hostNode = headId,
            .position = Vec3_Add(skull, Vec3_Add(Vec3_Scale(slot->side, w * 0.28f),
                                                 Vec3_Add(Vec3_Scale(slot->up, h * 0.75f),
                                                          Vec3_Scale(slot->forward, -depth * 0.42f)))),
            .forward = slot->forward,
            .up = occUpL,
            .side = Vec3_Normalize(Vec3_Cross(slot->forward, occUpL)),
            .hostRadii = Vec3_Create(w, h, depth),
            .scale = s
        };
        if (slots->count < ATTACHMENT_MAX_SLOTS) slots->slots[slots->count++] = hornL;

        AttachmentSlot hornR = {
            .id = HEAD_SLOT_HORN_RIGHT,
            .role = ATTACHMENT_CRANIAL,
            .hostModuleInstanceId = module,
            .hostNode = headId,
            .position = Vec3_Add(skull, Vec3_Add(Vec3_Scale(slot->side, -w * 0.28f),
                                                 Vec3_Add(Vec3_Scale(slot->up, h * 0.75f),
                                                          Vec3_Scale(slot->forward, -depth * 0.42f)))),
            .forward = slot->forward,
            .up = occUpR,
            .side = Vec3_Normalize(Vec3_Cross(slot->forward, occUpR)),
            .hostRadii = Vec3_Create(w, h, depth),
            .scale = s
        };
        if (slots->count < ATTACHMENT_MAX_SLOTS) slots->slots[slots->count++] = hornR;

        /* Ranuras occipitales específicas para coronas craneales (orientadas principalmente hacia atrás) */
        Vector3 crownOccUpL = Vec3_Normalize(Vec3_Create(0.12f, 0.28f, -0.94f));
        Vector3 crownOccUpR = Vec3_Normalize(Vec3_Create(-0.12f, 0.28f, -0.94f));
        AttachmentSlot crownOccL = {
            .id = HEAD_SLOT_OCCIPITAL_LEFT,
            .role = ATTACHMENT_CRANIAL,
            .hostModuleInstanceId = module,
            .hostNode = headId,
            .position = Vec3_Add(skull, Vec3_Add(Vec3_Scale(slot->side, w * 0.32f),
                                                 Vec3_Add(Vec3_Scale(slot->up, h * 0.68f),
                                                          Vec3_Scale(slot->forward, -depth * 0.44f)))),
            .forward = slot->forward,
            .up = crownOccUpL,
            .side = Vec3_Normalize(Vec3_Cross(slot->forward, crownOccUpL)),
            .hostRadii = Vec3_Create(w, h, depth),
            .scale = s
        };
        if (slots->count < ATTACHMENT_MAX_SLOTS) slots->slots[slots->count++] = crownOccL;

        AttachmentSlot crownOccR = {
            .id = HEAD_SLOT_OCCIPITAL_RIGHT,
            .role = ATTACHMENT_CRANIAL,
            .hostModuleInstanceId = module,
            .hostNode = headId,
            .position = Vec3_Add(skull, Vec3_Add(Vec3_Scale(slot->side, -w * 0.32f),
                                                 Vec3_Add(Vec3_Scale(slot->up, h * 0.68f),
                                                          Vec3_Scale(slot->forward, -depth * 0.44f)))),
            .forward = slot->forward,
            .up = crownOccUpR,
            .side = Vec3_Normalize(Vec3_Cross(slot->forward, crownOccUpR)),
            .hostRadii = Vec3_Create(w, h, depth),
            .scale = s
        };
        if (slots->count < ATTACHMENT_MAX_SLOTS) slots->slots[slots->count++] = crownOccR;

        Vector3 plUpL = Vec3_Normalize(Vec3_Create(0.48f, 0.48f, -0.73f));
        Vector3 plUpR = Vec3_Normalize(Vec3_Create(-0.48f, 0.48f, -0.73f));
        AttachmentSlot hornPLL = {
            .id = HEAD_SLOT_POSTEROLATERAL_LEFT,
            .role = ATTACHMENT_CRANIAL,
            .hostModuleInstanceId = module,
            .hostNode = headId,
            .position = Vec3_Add(skull, Vec3_Add(Vec3_Scale(slot->side, w * 0.68f),
                                                 Vec3_Add(Vec3_Scale(slot->up, h * 0.58f),
                                                          Vec3_Scale(slot->forward, -depth * 0.35f)))),
            .forward = slot->forward,
            .up = plUpL,
            .side = Vec3_Normalize(Vec3_Cross(slot->forward, plUpL)),
            .hostRadii = Vec3_Create(w, h, depth),
            .scale = s
        };
        if (slots->count < ATTACHMENT_MAX_SLOTS) slots->slots[slots->count++] = hornPLL;

        AttachmentSlot hornPLR = {
            .id = HEAD_SLOT_POSTEROLATERAL_RIGHT,
            .role = ATTACHMENT_CRANIAL,
            .hostModuleInstanceId = module,
            .hostNode = headId,
            .position = Vec3_Add(skull, Vec3_Add(Vec3_Scale(slot->side, -w * 0.68f),
                                                 Vec3_Add(Vec3_Scale(slot->up, h * 0.58f),
                                                          Vec3_Scale(slot->forward, -depth * 0.35f)))),
            .forward = slot->forward,
            .up = plUpR,
            .side = Vec3_Normalize(Vec3_Cross(slot->forward, plUpR)),
            .hostRadii = Vec3_Create(w, h, depth),
            .scale = s
        };
        if (slots->count < ATTACHMENT_MAX_SLOTS) slots->slots[slots->count++] = hornPLR;

        Vector3 tempUpL = Vec3_Normalize(Vec3_Create(0.88f, 0.22f, -0.42f));
        Vector3 tempUpR = Vec3_Normalize(Vec3_Create(-0.88f, 0.22f, -0.42f));
        AttachmentSlot hornTL = {
            .id = HEAD_SLOT_TEMPORAL_LEFT,
            .role = ATTACHMENT_CRANIAL,
            .hostModuleInstanceId = module,
            .hostNode = headId,
            .position = Vec3_Add(skull, Vec3_Add(Vec3_Scale(slot->side, w * 0.95f),
                                                 Vec3_Add(Vec3_Scale(slot->up, h * 0.25f),
                                                          Vec3_Scale(slot->forward, -depth * 0.18f)))),
            .forward = slot->forward,
            .up = tempUpL,
            .side = Vec3_Normalize(Vec3_Cross(slot->forward, tempUpL)),
            .hostRadii = Vec3_Create(w, h, depth),
            .scale = s
        };
        if (slots->count < ATTACHMENT_MAX_SLOTS) slots->slots[slots->count++] = hornTL;

        AttachmentSlot hornTR = {
            .id = HEAD_SLOT_TEMPORAL_RIGHT,
            .role = ATTACHMENT_CRANIAL,
            .hostModuleInstanceId = module,
            .hostNode = headId,
            .position = Vec3_Add(skull, Vec3_Add(Vec3_Scale(slot->side, -w * 0.95f),
                                                 Vec3_Add(Vec3_Scale(slot->up, h * 0.25f),
                                                          Vec3_Scale(slot->forward, -depth * 0.18f)))),
            .forward = slot->forward,
            .up = tempUpR,
            .side = Vec3_Normalize(Vec3_Cross(slot->forward, tempUpR)),
            .hostRadii = Vec3_Create(w, h, depth),
            .scale = s
        };
        if (slots->count < ATTACHMENT_MAX_SLOTS) slots->slots[slots->count++] = hornTR;
    }

    return true;
}
