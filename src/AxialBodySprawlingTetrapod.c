/**
 * @file AxialBodySprawlingTetrapod.c
 * @brief Implementación del arquetipo axial de tetrápodo bajo.
 * @author Monster Engine Team
 * @date 2026
 */

#include "AxialBodySprawlingTetrapod.h"
#include "AxialBody.h"
#include "AttachmentPath.h"
#include "CreatureModuleInternal.h"

static bool AxialNode(AnatomyGraph* g, uint32_t m, uint16_t local, Vector3 c, float w, float h, int color, AnatomyNodeRole role) {
    AnatomyRegion region = (local == AXIAL_NODE_NECK) ? ANATOMY_REGION_NECK :
                           (local == AXIAL_NODE_PELVIS) ? ANATOMY_REGION_PELVIS : ANATOMY_REGION_TRUNK;
    return Module_Node(g, m, local, c, w, h, color, role, region, ANATOMY_SIDE_CENTER, 1.0f);
}

bool AxialBodySprawlingTetrapod_Resolve(const AxialPhenotype* source,
                                       uint32_t module,
                                       AnatomyGraph* graph,
                                       AttachmentSlotSet* slots) {
    if (!source || !graph || !slots || !Anatomy_MakeId(module, 1)) return false;
    AxialPhenotype p = *source;
    float s = p.totalScale;
    float vertical = p.bodyFlattening;
    float neckZ = -p.neckLength * s;
    float pectoralZ = neckZ - p.trunkLength * 0.10f * s;
    float thoraxAZ = neckZ - p.trunkLength * 0.28f * s;
    float thoraxPZ = neckZ - p.trunkLength * 0.49f * s;
    float abdomenZ = neckZ - p.trunkLength * 0.70f * s;
    float pelvisZ = neckZ - p.trunkLength * s;

    if (!AxialNode(graph, module, AXIAL_NODE_NECK, Vec3_Create(0, 0.30f * s, neckZ),
                   p.neckWidth * 0.50f * s, p.neckWidth * 0.46f * vertical * s, 2, ANATOMY_ROLE_AXIAL) ||
        !AxialNode(graph, module, AXIAL_NODE_PECTORAL, Vec3_Create(0, 0.28f * s, pectoralZ),
                   p.shoulderWidth * 0.50f * s, p.thoraxHeight * 0.48f * vertical * s, 2, ANATOMY_ROLE_AXIAL) ||
        !AxialNode(graph, module, AXIAL_NODE_THORAX_ANTERIOR, Vec3_Create(0, 0.34f * s, thoraxAZ),
                   p.thoraxWidth * 0.50f * s, p.thoraxHeight * 0.50f * vertical * s, 3, ANATOMY_ROLE_AXIAL) ||
        !AxialNode(graph, module, AXIAL_NODE_THORAX_POSTERIOR, Vec3_Create(0, 0.31f * s, thoraxPZ),
                   p.thoraxWidth * 0.47f * s, p.thoraxHeight * 0.47f * vertical * s, 3, ANATOMY_ROLE_AXIAL) ||
        !AxialNode(graph, module, AXIAL_NODE_ABDOMEN, Vec3_Create(0, 0.25f * s, abdomenZ),
                   p.abdomenWidth * 0.50f * s, p.abdomenHeight * 0.50f * vertical * s, 2, ANATOMY_ROLE_AXIAL) ||
        !AxialNode(graph, module, AXIAL_NODE_PELVIS, Vec3_Create(0, 0.24f * s, pelvisZ),
                   p.pelvicWidth * 0.50f * s, p.pelvicHeight * 0.50f * vertical * s, 2, ANATOMY_ROLE_AXIAL)) {
        return false;
    }

    /* Aplicar el perfil antes de construir conexiones y ranuras. */
    for (size_t i = 0; i < AXIAL_PROFILE_STATIONS; ++i) {
        AnatomyNode* n = &graph->nodes[graph->nodeCount - AXIAL_PROFILE_STATIONS + i];
        n->center.z += p.profile[i].longitudinalOffset * s;
        n->center.y += p.profile[i].verticalOffset * s;
        n->widthRadius *= p.profile[i].widthScale > 0 ? p.profile[i].widthScale : 1;
        n->heightRadius *= p.profile[i].heightScale > 0 ? p.profile[i].heightScale : 1;
    }
    uint16_t axial[] = {
        AXIAL_NODE_NECK, AXIAL_NODE_PECTORAL,
        AXIAL_NODE_THORAX_ANTERIOR, AXIAL_NODE_THORAX_POSTERIOR,
        AXIAL_NODE_ABDOMEN, AXIAL_NODE_PELVIS
    };
    for (size_t i = 0; i + 1 < sizeof(axial) / sizeof(axial[0]); ++i) {
        if (!Module_Edge(graph, module, (uint16_t)(i + 1),
                         Anatomy_MakeId(module, axial[i]),
                         Anatomy_MakeId(module, axial[i + 1]),
                         BODY_CONNECTION_AXIAL_LOFT, 1.0f)) {
            return false;
        }
    }

    const AttachmentRole roles[] = {
        ATTACHMENT_CERVICAL,
        ATTACHMENT_PECTORAL_LEFT,
        ATTACHMENT_PECTORAL_RIGHT,
        ATTACHMENT_PELVIC_LEFT,
        ATTACHMENT_PELVIC_RIGHT,
        ATTACHMENT_CAUDAL,
        ATTACHMENT_DORSAL,
        ATTACHMENT_VENTRAL
    };
    const AttachmentSlotId slotIds[] = {
        AXIAL_SPRAWLING_SLOT_CERVICAL,
        AXIAL_SPRAWLING_SLOT_PECTORAL_LEFT,
        AXIAL_SPRAWLING_SLOT_PECTORAL_RIGHT,
        AXIAL_SPRAWLING_SLOT_PELVIC_LEFT,
        AXIAL_SPRAWLING_SLOT_PELVIC_RIGHT,
        AXIAL_SPRAWLING_SLOT_CAUDAL,
        AXIAL_SPRAWLING_SLOT_DORSAL,
        AXIAL_SPRAWLING_SLOT_VENTRAL
    };
    const uint16_t hosts[] = {
        AXIAL_NODE_NECK,
        AXIAL_NODE_PECTORAL,
        AXIAL_NODE_PECTORAL,
        AXIAL_NODE_PELVIS,
        AXIAL_NODE_PELVIS,
        AXIAL_NODE_PELVIS,
        AXIAL_NODE_THORAX_POSTERIOR,
        AXIAL_NODE_THORAX_POSTERIOR
    };

    for (size_t i = 0; i < 8; ++i) {
        const AnatomyNode* n = AnatomyGraph_FindModuleNode(graph, module, hosts[i]);
        if (!n) return false;
        AttachmentSlot slot = {
            .id = slotIds[i],
            .role = roles[i],
            .hostModuleInstanceId = module,
            .hostNode = n->id,
            .position = n->center,
            .forward = {0, 0, 1},
            .up = {0, 1, 0},
            .side = {1, 0, 0},
            .hostRadii = {n->widthRadius, n->heightRadius, (i == 0 ? p.neckLength * s : n->widthRadius)},
            .scale = s
        };
        if (i >= 1 && i <= 4) {
            float latFactor = (p.limbAttachmentLateral > 0.0f) ? p.limbAttachmentLateral : 1.0f;
            slot.position.x = (i % 2 ? 1.0f : -1.0f) * n->widthRadius * latFactor;
        }
        if (i == 5) {
            slot.forward.z = -1.0f;
        }
        if (i == 6) {
            slot.position.y += n->heightRadius;
        }
        if (i == 7) {
            slot.position.y -= n->heightRadius;
        }
        if (slots->count < ATTACHMENT_MAX_SLOTS) {
            slots->slots[slots->count++] = slot;
        }
    }

    AttachmentPath path = AttachmentPath_FromAxialDorsal(graph, module);
    AttachmentPath_PublishSlots(&path, AXIAL_SPRAWLING_SLOT_DORSAL_ARRAY_COUNT,
                                AXIAL_SPRAWLING_SLOT_DORSAL_ARRAY_BASE, module, slots);

    return true;
}
