/**
 * @file AxialBodyArthropod.c
 * @brief Cuerpo segmentado exoesquelético de insecto/artrópodo con límites intersegmentales definidos.
 */
#include "AxialBodyArthropod.h"
#include "CreatureModuleInternal.h"
#include "MathUtils.h"
#include <math.h>

bool AxialBodyArthropod_Resolve(const AxialPhenotype* p, uint32_t module,
                                AnatomyGraph* g, AttachmentSlotSet* slots) {
    if (!p || !g || !slots || p->archetype != AXIAL_ARCHETYPE_ARTHROPOD) return false;
    float s = p->totalScale;
    float L = p->trunkLength;
    float refY = p->withersHeight;
    unsigned segs = p->abdomenSegments >= 3 ? p->abdomenSegments : 10u;
    if (segs > 12u) segs = 12u;

    /* Tagmas torácicos (nodos 1..4) con cintura definida antes del abdomen (nodos 20..20+segs-1):
     * 1 (NECK): Protórax corto (cuello articular)
     * 2 (PECTORAL): Mesotórax anterior (alas anteriores y pata II)
     * 3 (THORAX_ANTERIOR): Sintórax dorsal muscular
     * 4 (THORAX_POSTERIOR): Metatórax (alas posteriores y pata III)
     * 5 (ABDOMEN): Base abdominal (somito 1)
     * 6 (PELVIS): Extremo terminal del abdomen (somito N, cercos/apéndices terminales) */
    float thoraxLen = L * 0.26f;
    float abdLen = L * 0.74f;
    float tw = p->thoraxWidth * 0.50f;
    float th = p->thoraxHeight * 0.52f;
    float aw = p->abdomenWidth * 0.50f;
    float ah = p->abdomenHeight * 0.50f;
    float pw = p->pelvicWidth * 0.50f;
    float ph = p->pelvicHeight * 0.50f;

    Vector3 proPos = {0.0f, (refY + th * 0.08f) * s, (thoraxLen * 0.62f + p->neckLength * 0.35f) * s};
    Vector3 mesoPos = {0.0f, (refY + th * 0.16f) * s, (thoraxLen * 0.30f) * s};
    Vector3 synPos = {0.0f, (refY + th * 0.22f) * s, 0.0f};
    Vector3 metaPos = {0.0f, (refY + th * 0.06f) * s, (-thoraxLen * 0.36f) * s};

    if (!Module_Node(g, module, AXIAL_NODE_NECK, proPos,
                     p->neckWidth * 0.46f * s, p->neckWidth * 0.42f * s, 2,
                     ANATOMY_ROLE_AXIAL, ANATOMY_REGION_NECK, ANATOMY_SIDE_CENTER, 1.0f)) return false;
    if (!Module_Node(g, module, AXIAL_NODE_PECTORAL, mesoPos,
                     p->shoulderWidth * 0.48f * s, th * 0.92f * s, 2,
                     ANATOMY_ROLE_AXIAL, ANATOMY_REGION_TRUNK, ANATOMY_SIDE_CENTER, 1.0f)) return false;
    if (!Module_Node(g, module, AXIAL_NODE_THORAX_ANTERIOR, synPos,
                     tw * s, th * s, 2,
                     ANATOMY_ROLE_AXIAL, ANATOMY_REGION_TRUNK, ANATOMY_SIDE_CENTER, 1.0f)) return false;
    if (!Module_Node(g, module, AXIAL_NODE_THORAX_POSTERIOR, metaPos,
                     tw * 0.84f * s, th * 0.80f * s, 2,
                     ANATOMY_ROLE_AXIAL, ANATOMY_REGION_TRUNK, ANATOMY_SIDE_CENTER, 1.0f)) return false;

    /* Conexiones torácicas con mezcla mínima para conservar placas cuticulares nítidas. */
    if (!Module_Edge(g, module, 1, Anatomy_MakeId(module, AXIAL_NODE_NECK),
                     Anatomy_MakeId(module, AXIAL_NODE_PECTORAL), BODY_CONNECTION_LIMB_SEGMENT, 1.0f)) return false;
    g->connections[g->connectionCount - 1].jointBlend = 0.08f;
    if (!Module_Edge(g, module, 2, Anatomy_MakeId(module, AXIAL_NODE_PECTORAL),
                     Anatomy_MakeId(module, AXIAL_NODE_THORAX_ANTERIOR), BODY_CONNECTION_LIMB_SEGMENT, 1.0f)) return false;
    g->connections[g->connectionCount - 1].jointBlend = 0.14f;
    if (!Module_Edge(g, module, 3, Anatomy_MakeId(module, AXIAL_NODE_THORAX_ANTERIOR),
                     Anatomy_MakeId(module, AXIAL_NODE_THORAX_POSTERIOR), BODY_CONNECTION_LIMB_SEGMENT, 1.0f)) return false;
    g->connections[g->connectionCount - 1].jointBlend = 0.12f;

    /* Abdomen alargado y claramente segmentado (somitos S1..SN). */
    float zAbdStart = -thoraxLen * 0.56f;
    float segStep = abdLen / (float)segs;
    AnatomyId prevId = Anatomy_MakeId(module, AXIAL_NODE_THORAX_POSTERIOR);
    for (unsigned k = 0; k < segs; ++k) {
        float u = (float)k / (float)(segs - 1u);
        /* S1-S2 ligeramente ensanchados (base abdominal), S3 cintura fina, S4-S9 cilíndricos, S10 ahusado. */
        float profileW = (k == 0) ? 1.15f : (k == 1 ? 1.05f : (k == 2 ? 0.82f : Math_Lerp(0.96f, 0.72f, u)));
        float profileH = (k == 0) ? 1.10f : (k == 1 ? 1.02f : (k == 2 ? 0.80f : Math_Lerp(0.94f, 0.70f, u)));
        float w = Math_Lerp(aw, pw, u * u) * profileW;
        float h = Math_Lerp(ah, ph, u * u) * profileH;
        float zi = zAbdStart - segStep * ((float)k + 0.5f);
        /* Leve arco dorsal elegante del abdomen de odonato. */
        float yi = refY - th * 0.06f + sinf(u * 3.14159265f) * ah * 0.45f - u * u * ah * 0.35f;
        uint16_t localId = (k == 0) ? AXIAL_NODE_ABDOMEN
                         : (k == segs - 1u) ? AXIAL_NODE_PELVIS
                                            : (uint16_t)(AXIAL_ARTHROPOD_SEGMENT_NODE_BASE + k);
        AnatomyRegion region = (k == segs - 1u) ? ANATOMY_REGION_PELVIS : ANATOMY_REGION_TRUNK;
        if (!Module_Node(g, module, localId, Vec3_Create(0.0f, yi * s, zi * s),
                         w * s, h * s, 2, ANATOMY_ROLE_AXIAL, region, ANATOMY_SIDE_CENTER, 1.0f)) {
            return false;
        }
        AnatomyId curId = Anatomy_MakeId(module, localId);
        uint16_t edgeId = (uint16_t)(10u + k);
        if (!Module_Edge(g, module, edgeId, prevId, curId, BODY_CONNECTION_LIMB_SEGMENT, 1.0f)) return false;
        /* Esculpido segmentado: jointBlend muy bajo y abombamiento central por somito. */
        BodyConnection* e = &g->connections[g->connectionCount - 1];
        e->jointBlend = 0.04f;
        e->widthBulge = w * s * 0.20f;
        e->heightBulge = h * s * 0.18f;
        prevId = curId;
    }

    const AnatomyNode* pro = AnatomyGraph_FindModuleNode(g, module, AXIAL_NODE_NECK);
    const AnatomyNode* meso = AnatomyGraph_FindModuleNode(g, module, AXIAL_NODE_PECTORAL);
    const AnatomyNode* syn = AnatomyGraph_FindModuleNode(g, module, AXIAL_NODE_THORAX_ANTERIOR);
    const AnatomyNode* meta = AnatomyGraph_FindModuleNode(g, module, AXIAL_NODE_THORAX_POSTERIOR);
    const AnatomyNode* tip = AnatomyGraph_FindModuleNode(g, module, AXIAL_NODE_PELVIS);
    if (!pro || !meso || !syn || !meta || !tip) return false;

    /* 1. Ranura cervical para cabeza de insecto con grandes ojos compuestos. */
    AttachmentSlot cervical = {
        .id = AXIAL_SLOT_CERVICAL, .role = ATTACHMENT_CERVICAL,
        .hostModuleInstanceId = module, .hostNode = pro->id,
        .position = pro->center,
        .forward = {0.0f, 0.0f, 1.0f}, .up = {0.0f, 1.0f, 0.0f}, .side = {1.0f, 0.0f, 0.0f},
        .hostRadii = {pro->widthRadius, pro->heightRadius, pro->widthRadius}, .scale = s
    };
    if (slots->count >= ATTACHMENT_MAX_SLOTS) return false;
    slots->slots[slots->count++] = cervical;

    /* 2-7. Seis ranuras ventrales torácicas para tres pares de patas de insecto (nunca en el abdomen). */
    const AnatomyNode* legHosts[3] = {pro, meso, meta};
    const float legZShift[3] = {0.0f, 0.06f * thoraxLen * s, 0.04f * thoraxLen * s};
    const AttachmentSlotId legIds[3][2] = {
        {AXIAL_SLOT_THORACIC_LEG_1_LEFT, AXIAL_SLOT_THORACIC_LEG_1_RIGHT},
        {AXIAL_SLOT_THORACIC_LEG_2_LEFT, AXIAL_SLOT_THORACIC_LEG_2_RIGHT},
        {AXIAL_SLOT_THORACIC_LEG_3_LEFT, AXIAL_SLOT_THORACIC_LEG_3_RIGHT}
    };
    for (int pair = 0; pair < 3; ++pair) {
        const AnatomyNode* hNode = legHosts[pair];
        for (int sideIdx = 0; sideIdx < 2; ++sideIdx) {
            float sign = sideIdx == 0 ? 1.0f : -1.0f;
            AttachmentSlot legSlot = {
                .id = legIds[pair][sideIdx],
                .role = ATTACHMENT_THORACIC_LEG,
                .hostModuleInstanceId = module, .hostNode = hNode->id,
                .position = {
                    sign * hNode->widthRadius * 0.52f,
                    hNode->center.y - hNode->heightRadius * 0.64f,
                    hNode->center.z + legZShift[pair] * 0.45f
                },
                .forward = {0.0f, 0.0f, 1.0f}, .up = {0.0f, 1.0f, 0.0f}, .side = {1.0f, 0.0f, 0.0f},
                .hostRadii = {hNode->widthRadius, hNode->heightRadius, hNode->widthRadius}, .scale = s
            };
            if (slots->count >= ATTACHMENT_MAX_SLOTS) return false;
            slots->slots[slots->count++] = legSlot;
        }
    }

    /* 8-11. Cuatro ranuras alares dorsolaterales independientes (mesotórax y metatórax). */
    for (int pair = 0; pair < 2; ++pair) {
        const AnatomyNode* hNode = (pair == 0) ? syn : meta;
        for (int sideIdx = 0; sideIdx < 2; ++sideIdx) {
            float sign = sideIdx == 0 ? 1.0f : -1.0f;
            AttachmentSlotId wid = (pair == 0)
                ? (AttachmentSlotId)(sideIdx == 0 ? AXIAL_SLOT_WING_FORE_LEFT : AXIAL_SLOT_WING_FORE_RIGHT)
                : (AttachmentSlotId)(sideIdx == 0 ? AXIAL_SLOT_WING_HIND_LEFT : AXIAL_SLOT_WING_HIND_RIGHT);
            AttachmentSlot wingSlot = {
                .id = wid,
                .role = pair == 0 ? ATTACHMENT_WING_FORE : ATTACHMENT_WING_HIND,
                .hostModuleInstanceId = module, .hostNode = hNode->id,
                .position = {
                    sign * hNode->widthRadius * 0.62f,
                    hNode->center.y + hNode->heightRadius * 0.58f,
                    hNode->center.z + (pair == 0 ? hNode->widthRadius * 0.08f : -hNode->widthRadius * 0.04f)
                },
                .forward = {0.0f, 0.0f, 1.0f}, .up = {0.0f, 1.0f, 0.0f}, .side = {1.0f, 0.0f, 0.0f},
                .hostRadii = {hNode->widthRadius, hNode->heightRadius, hNode->widthRadius}, .scale = s
            };
            if (slots->count >= ATTACHMENT_MAX_SLOTS) return false;
            slots->slots[slots->count++] = wingSlot;
        }
    }

    /* 12. Ranura caudal (cercos / apéndices anales al final del somito 10). */
    AttachmentSlot caudal = {
        .id = AXIAL_SLOT_CAUDAL, .role = ATTACHMENT_CAUDAL,
        .hostModuleInstanceId = module, .hostNode = tip->id,
        .position = {0.0f, tip->center.y, tip->center.z - tip->widthRadius * 0.75f},
        .forward = {0.0f, 0.0f, -1.0f}, .up = {0.0f, 1.0f, 0.0f}, .side = {1.0f, 0.0f, 0.0f},
        .hostRadii = {tip->widthRadius, tip->heightRadius, tip->widthRadius}, .scale = s
    };
    AttachmentSlot dorsal = {
        .id = AXIAL_SLOT_DORSAL, .role = ATTACHMENT_DORSAL,
        .hostModuleInstanceId = module, .hostNode = syn->id,
        .position = {0.0f, syn->center.y + syn->heightRadius, syn->center.z},
        .forward = {0.0f, 0.0f, 1.0f}, .up = {0.0f, 1.0f, 0.0f}, .side = {1.0f, 0.0f, 0.0f},
        .hostRadii = {syn->widthRadius, syn->heightRadius, syn->widthRadius}, .scale = s
    };
    AttachmentSlot ventral = {
        .id = AXIAL_SLOT_VENTRAL, .role = ATTACHMENT_VENTRAL,
        .hostModuleInstanceId = module, .hostNode = syn->id,
        .position = {0.0f, syn->center.y - syn->heightRadius, syn->center.z},
        .forward = {0.0f, 0.0f, 1.0f}, .up = {0.0f, -1.0f, 0.0f}, .side = {1.0f, 0.0f, 0.0f},
        .hostRadii = {syn->widthRadius, syn->heightRadius, syn->widthRadius}, .scale = s
    };
    if (slots->count + 3 > ATTACHMENT_MAX_SLOTS) return false;
    slots->slots[slots->count++] = caudal;
    slots->slots[slots->count++] = dorsal;
    slots->slots[slots->count++] = ventral;

    return true;
}
