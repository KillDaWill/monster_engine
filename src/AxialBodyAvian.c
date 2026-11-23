/**
 * @file AxialBodyAvian.c
 * @brief Implementación de tronco aviar compacto con esternón prominente y ranuras alares dorsales.
 */
#include "AxialBodyAvian.h"
#include "CreatureModuleInternal.h"
#include "AttachmentPath.h"
#include "MathUtils.h"
#include <math.h>

bool AxialBodyAvian_Resolve(const AxialPhenotype* p, uint32_t module,
                            AnatomyGraph* g, AttachmentSlotSet* slots) {
    if (!p || !g || !slots || p->archetype != AXIAL_ARCHETYPE_AVIAN) return false;
    float s = p->totalScale;
    float L = p->trunkLength;
    float refY = p->withersHeight;

    /* Estaciones semánticas de torso aviar: cuello integrado -> cintura escapular alta ->
     * pecho aquillado profundo -> tórax medio -> abdomen estrecho -> sinsacro/uropigio. */
    float widths[6] = {
        p->neckWidth * 0.56f,
        p->shoulderWidth * 0.52f,
        p->thoraxWidth * 0.54f,
        p->thoraxWidth * 0.48f,
        p->abdomenWidth * 0.42f,
        p->pelvicWidth * 0.32f
    };
    float heights[6] = {
        p->neckWidth * 0.52f,
        p->thoraxHeight * 0.48f,
        p->thoraxHeight * 0.52f,
        p->thoraxHeight * 0.46f,
        p->abdomenHeight * 0.38f,
        p->pelvicHeight * 0.28f
    };
    /* Postura aviar aerodinámica en gota: pecho lleno redondeado, cuello corto e integrado. */
    float z[6] = {
        L * 0.24f + p->neckLength * 0.62f,
        L * 0.16f,
        -L * 0.08f,
        -L * 0.36f,
        -L * 0.66f,
        -L * 0.94f
    };
    float yRel[6] = {
        p->neckLength * 0.42f + heights[1] * 0.28f,
        heights[1] * 0.06f,
        -heights[2] * 0.10f - p->brisketDepth * p->thoraxHeight * 0.18f,
        -heights[3] * 0.16f,
        -heights[4] * 0.24f,
        -heights[5] * 0.30f
    };

    for (unsigned i = 0; i < 6; ++i) {
        const AxialStationMorph* morph = &p->profile[i];
        float w = widths[i] * (morph->widthScale > 0.0f ? morph->widthScale : 1.0f);
        float h = heights[i] * (morph->heightScale > 0.0f ? morph->heightScale : 1.0f);
        float y = refY + yRel[i] + morph->verticalOffset;
        float zi = z[i] + morph->longitudinalOffset;
        if (i == 0 && p->cervicalPitch > 0.0f) {
            float pecY = refY + yRel[1] + p->profile[1].verticalOffset;
            float pecZ = z[1] + p->profile[1].longitudinalOffset;
            zi = pecZ + p->neckLength * cosf(p->cervicalPitch) + morph->longitudinalOffset;
            y = pecY + p->neckLength * sinf(p->cervicalPitch) + morph->verticalOffset;
        }
        Vector3 c = {0.0f, y * s, zi * s};
        AnatomyRegion region = (i == 0) ? ANATOMY_REGION_NECK
                             : (i == 5) ? ANATOMY_REGION_PELVIS
                                        : ANATOMY_REGION_TRUNK;
        if (!Module_Node(g, module, (uint16_t)(i + 1), c, w * s, h * s, 2,
                         ANATOMY_ROLE_AXIAL, region, ANATOMY_SIDE_CENTER, 1.0f)) {
            return false;
        }
        if (i >= 1 && !Module_Edge(g, module, (uint16_t)i,
                Anatomy_MakeId(module, (uint16_t)i),
                Anatomy_MakeId(module, (uint16_t)(i + 1)),
                BODY_CONNECTION_AXIAL_LOFT, 1.0f)) {
            return false;
        }
    }

    const AnatomyNode* neck = AnatomyGraph_FindModuleNode(g, module, AXIAL_NODE_NECK);
    const AnatomyNode* pec = AnatomyGraph_FindModuleNode(g, module, AXIAL_NODE_PECTORAL);
    const AnatomyNode* thxA = AnatomyGraph_FindModuleNode(g, module, AXIAL_NODE_THORAX_ANTERIOR);
    const AnatomyNode* thxP = AnatomyGraph_FindModuleNode(g, module, AXIAL_NODE_THORAX_POSTERIOR);
    const AnatomyNode* abd = AnatomyGraph_FindModuleNode(g, module, AXIAL_NODE_ABDOMEN);
    const AnatomyNode* pel = AnatomyGraph_FindModuleNode(g, module, AXIAL_NODE_PELVIS);
    if (!neck || !pec || !thxA || !thxP || !abd || !pel) return false;

    /* 1. Ranura cervical (cabeza integrada sobre cuello corto). */
    AttachmentSlot cervical = {
        .id = AXIAL_SLOT_CERVICAL, .role = ATTACHMENT_CERVICAL,
        .hostModuleInstanceId = module, .hostNode = neck->id,
        .position = neck->center,
        .forward = {0.0f, 0.0f, 1.0f}, .up = {0.0f, 1.0f, 0.0f}, .side = {1.0f, 0.0f, 0.0f},
        .hostRadii = {neck->widthRadius, neck->heightRadius, neck->widthRadius}, .scale = s
    };
    if (slots->count >= ATTACHMENT_MAX_SLOTS) return false;
    slots->slots[slots->count++] = cervical;

    /* 2-3. Ranuras alares escapulares altas (dorsolaterales sobre tórax anterior). */
    for (int sideIdx = 0; sideIdx < 2; ++sideIdx) {
        float sign = sideIdx == 0 ? 1.0f : -1.0f;
        AttachmentSlot wing = {
            .id = (AttachmentSlotId)(sideIdx == 0 ? AXIAL_SLOT_PECTORAL_LEFT : AXIAL_SLOT_PECTORAL_RIGHT),
            .role = sideIdx == 0 ? ATTACHMENT_PECTORAL_LEFT : ATTACHMENT_PECTORAL_RIGHT,
            .hostModuleInstanceId = module,
            .hostNode = pec->id,
            .position = {
                sign * pec->widthRadius * p->limbAttachmentLateral,
                pec->center.y + pec->heightRadius * 0.34f,
                Math_Lerp(pec->center.z, thxA->center.z, 0.32f)
            },
            .forward = {0.0f, 0.0f, 1.0f}, .up = {0.0f, 1.0f, 0.0f}, .side = {1.0f, 0.0f, 0.0f},
            .hostRadii = {pec->widthRadius, pec->heightRadius, pec->widthRadius}, .scale = s
        };
        if (slots->count >= ATTACHMENT_MAX_SLOTS) return false;
        slots->slots[slots->count++] = wing;
    }

    /* 4-5. Ranuras pélvicas ventrales bajo el centro de gravedad para patas prensiles. */
    for (int sideIdx = 0; sideIdx < 2; ++sideIdx) {
        float sign = sideIdx == 0 ? 1.0f : -1.0f;
        AttachmentSlot leg = {
            .id = (AttachmentSlotId)(sideIdx == 0 ? AXIAL_SLOT_PELVIC_LEFT : AXIAL_SLOT_PELVIC_RIGHT),
            .role = sideIdx == 0 ? ATTACHMENT_PELVIC_LEFT : ATTACHMENT_PELVIC_RIGHT,
            .hostModuleInstanceId = module,
            .hostNode = abd->id,
            .position = {
                sign * abd->widthRadius * 0.50f,
                abd->center.y - abd->heightRadius * 0.44f,
                Math_Lerp(thxP->center.z, abd->center.z, 0.72f)
            },
            .forward = {0.0f, 0.0f, 1.0f}, .up = {0.0f, 1.0f, 0.0f}, .side = {1.0f, 0.0f, 0.0f},
            .hostRadii = {abd->widthRadius, abd->heightRadius, abd->widthRadius}, .scale = s
        };
        if (slots->count >= ATTACHMENT_MAX_SLOTS) return false;
        slots->slots[slots->count++] = leg;
    }

    /* 6. Ranura caudal (uropigio -> abanico de rectrices). */
    Vector3 tailFwd = Vec3_Normalize(Vec3_Create(0.0f, -0.18f, -1.0f));
    Vector3 tailUp = Vec3_Normalize(Vec3_Cross(Vec3_Create(1.0f, 0.0f, 0.0f), Vec3_Scale(tailFwd, -1.0f)));
    AttachmentSlot caudal = {
        .id = AXIAL_SLOT_CAUDAL, .role = ATTACHMENT_CAUDAL,
        .hostModuleInstanceId = module, .hostNode = pel->id,
        .position = {0.0f, pel->center.y + pel->heightRadius * 0.06f, pel->center.z - pel->widthRadius * 0.35f},
        .forward = tailFwd, .up = tailUp, .side = {1.0f, 0.0f, 0.0f},
        .hostRadii = {pel->widthRadius, pel->heightRadius, pel->widthRadius}, .scale = s
    };
    if (slots->count >= ATTACHMENT_MAX_SLOTS) return false;
    slots->slots[slots->count++] = caudal;

    /* 7-8. Ranuras dorsal y ventral. */
    AttachmentSlot dorsal = {
        .id = AXIAL_SLOT_DORSAL, .role = ATTACHMENT_DORSAL,
        .hostModuleInstanceId = module, .hostNode = thxP->id,
        .position = {0.0f, thxP->center.y + thxP->heightRadius, thxP->center.z},
        .forward = {0.0f, 0.0f, 1.0f}, .up = {0.0f, 1.0f, 0.0f}, .side = {1.0f, 0.0f, 0.0f},
        .hostRadii = {thxP->widthRadius, thxP->heightRadius, thxP->widthRadius}, .scale = s
    };
    AttachmentSlot ventral = {
        .id = AXIAL_SLOT_VENTRAL, .role = ATTACHMENT_VENTRAL,
        .hostModuleInstanceId = module, .hostNode = thxA->id,
        .position = {0.0f, thxA->center.y - thxA->heightRadius, thxA->center.z},
        .forward = {0.0f, 0.0f, 1.0f}, .up = {0.0f, -1.0f, 0.0f}, .side = {1.0f, 0.0f, 0.0f},
        .hostRadii = {thxA->widthRadius, thxA->heightRadius, thxA->widthRadius}, .scale = s
    };
    if (slots->count + 2 > ATTACHMENT_MAX_SLOTS) return false;
    slots->slots[slots->count++] = dorsal;
    slots->slots[slots->count++] = ventral;

    AttachmentPath path = AttachmentPath_FromAxialDorsal(g, module);
    AttachmentPath_PublishSlots(&path, 8, 100, module, slots);
    return true;
}

bool AxialBodyAvian_BuildSweepStations(const AxialPhenotype* p, const AnatomyGraph* g,
                                       uint32_t module, SDFSweepStation* outStations,
                                       int maxStations, int* outCount) {
    if (!p || !g || !outStations || !outCount || maxStations < 9 || p->archetype != AXIAL_ARCHETYPE_AVIAN)
        return false;
    const AnatomyNode* nodes[6];
    for (unsigned i = 0; i < 6; ++i) {
        nodes[i] = AnatomyGraph_FindModuleNode(g, module, (uint16_t)(i + 1));
        if (!nodes[i]) return false;
    }
    int c = 0;
    /* Transición cuello -> pecho aquillado con 3 estaciones suaves sin escalón cervical. */
    for (unsigned k = 0; k < 3; ++k) {
        float t = (float)k / 3.0f;
        float smooth = t * t * (3.0f - 2.0f * t);
        Vector3 pos = Vec3_Lerp(nodes[0]->center, nodes[1]->center, t);
        /* Garganta redondeada hacia adelante */
        pos.y -= sinf(t * 3.14159265f) * nodes[1]->heightRadius * 0.10f;
        outStations[c++] = (SDFSweepStation){
            .center = pos,
            .width = Math_Lerp(nodes[0]->widthRadius, nodes[1]->widthRadius, smooth),
            .height = Math_Lerp(nodes[0]->heightRadius, nodes[1]->heightRadius, smooth)
        };
    }
    /* Estación escapular, pecho prominente (quilla), tórax posterior, abdomen y uropigio */
    for (unsigned i = 1; i < 6; ++i) {
        SDFSweepStation st = {
            .center = nodes[i]->center,
            .width = nodes[i]->widthRadius,
            .height = nodes[i]->heightRadius
        };
        if (i == 2 && p->brisketDepth > 0.0f) {
            float keel = p->brisketDepth * nodes[2]->heightRadius * 0.45f;
            st.center.y -= keel * 0.5f;
            st.height += keel * 0.5f;
        }
        outStations[c++] = st;
    }
    /* Cierre caudal suave en el uropigio */
    Vector3 tip = nodes[5]->center;
    tip.z -= nodes[5]->widthRadius * 0.72f;
    tip.y -= nodes[5]->heightRadius * 0.10f;
    outStations[c++] = (SDFSweepStation){
        .center = tip,
        .width = nodes[5]->widthRadius * 0.42f,
        .height = nodes[5]->heightRadius * 0.36f
    };
    *outCount = c;
    return true;
}
