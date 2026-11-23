/**
 * @file AxialBodyCurvedVertical.c
 * @brief Implementación parametrizada del cuerpo acuático vertical curvado (signátidos y formas afines).
 */

#include "AxialBodyCurvedVertical.h"
#include "CreatureModuleInternal.h"
#include "MathUtils.h"
#include <math.h>

bool AxialBodyCurvedVertical_Resolve(const AxialPhenotype* p,
                                     uint32_t module,
                                     AnatomyGraph* g,
                                     AttachmentSlotSet* slots) {
    if (!p || !g || !slots || p->archetype != AXIAL_ARCHETYPE_CURVED_VERTICAL) return false;
    float s = p->totalScale;
    float L = p->trunkLength;
    float refY = p->withersHeight;
    float curv = Math_Clamp(p->cervicalCurvature, 0.10f, 1.0f);
    float pitch = Math_Clamp(p->cervicalPitch, 0.0f, 1.45f);
    float flat = Math_Clamp(p->bodyFlattening, 0.35f, 1.0f);

    /* 6 estaciones a lo largo de la curva en S erguida (de cuello superior a base caudal inferior):
     * 0: NECK (arco cervical inclinado hacia adelante +Z)
     * 1: PECTORAL (cuello inferior / cintura pectoral)
     * 2: THORAX_ANTERIOR (pecho superior prominente hacia +Z)
     * 3: THORAX_POSTERIOR (vientre máximo hacia +Z)
     * 4: ABDOMEN (estrechamiento ventral inferior)
     * 5: PELVIS (base del tronco erguido que da paso a la cola prensil descendente) */
    float yRel[6] = {
        L * 0.56f,
        L * 0.36f,
        L * 0.14f,
       -L * 0.10f,
       -L * 0.32f,
       -L * 0.50f
    };
    float zRel[6] = {
        L * (0.14f + 0.14f * curv),
        L * (0.02f - 0.04f * curv),
        L * (0.06f + 0.06f * curv),
        L * (0.10f + 0.08f * curv),
        L * (0.02f),
       -L * (0.04f)
    };
    float widths[6] = {
        p->neckWidth * 0.42f * flat,
        p->shoulderWidth * 0.44f * flat,
        p->thoraxWidth * 0.50f * flat,
        p->abdomenWidth * 0.48f * flat,
        p->abdomenWidth * 0.34f * flat,
        p->pelvicWidth * 0.26f * flat
    };
    float depths[6] = {
        p->neckWidth * 0.52f,
        p->thoraxHeight * 0.46f,
        p->thoraxHeight * 0.64f,
        p->abdomenHeight * 0.68f,
        p->abdomenHeight * 0.44f,
        p->pelvicHeight * 0.30f
    };

    for (unsigned i = 0; i < 6; ++i) {
        const AxialStationMorph* morph = &p->profile[i];
        float w = fmaxf(0.015f, widths[i] * (morph->widthScale > 0.0f ? morph->widthScale : 1.0f));
        float h = fmaxf(0.015f, depths[i] * (morph->heightScale > 0.0f ? morph->heightScale : 1.0f));
        Vector3 c = {
            0.0f,
            (refY + yRel[i] + morph->verticalOffset) * s,
            (zRel[i] + morph->longitudinalOffset) * s
        };
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
    const AnatomyNode* pec  = AnatomyGraph_FindModuleNode(g, module, AXIAL_NODE_PECTORAL);
    const AnatomyNode* thxA = AnatomyGraph_FindModuleNode(g, module, AXIAL_NODE_THORAX_ANTERIOR);
    const AnatomyNode* thxP = AnatomyGraph_FindModuleNode(g, module, AXIAL_NODE_THORAX_POSTERIOR);
    const AnatomyNode* abd  = AnatomyGraph_FindModuleNode(g, module, AXIAL_NODE_ABDOMEN);
    const AnatomyNode* pel  = AnatomyGraph_FindModuleNode(g, module, AXIAL_NODE_PELVIS);
    if (!neck || !pec || !thxA || !thxP || !abd || !pel) return false;

    /* 1. Ranura cefálica: orientada hacia adelante (+Z) con ligera inclinación independiente. */
    float headDip = Math_Clamp(pitch * 0.25f, 0.0f, 0.35f);
    Vector3 headFwd = Vec3_Normalize(Vec3_Create(0.0f, -sinf(headDip), cosf(headDip)));
    Vector3 headUp  = Vec3_Normalize(Vec3_Create(0.0f, cosf(headDip), sinf(headDip)));
    AttachmentSlot cervical = {
        .id = AXIAL_SLOT_CERVICAL, .role = ATTACHMENT_CERVICAL,
        .hostModuleInstanceId = module, .hostNode = neck->id,
        .position = Vec3_Add(neck->center, Vec3_Create(0.0f, neck->heightRadius * 0.25f, neck->heightRadius * 0.30f)),
        .forward = headFwd, .up = headUp, .side = {1.0f, 0.0f, 0.0f},
        .hostRadii = {neck->widthRadius, neck->heightRadius, neck->heightRadius},
        .scale = s
    };
    if (slots->count >= ATTACHMENT_MAX_SLOTS) return false;
    slots->slots[slots->count++] = cervical;

    /* 2-3. Ranuras pectorales detrás de las mejillas/branquias. */
    for (int sideIdx = 0; sideIdx < 2; ++sideIdx) {
        float sign = sideIdx == 0 ? 1.0f : -1.0f;
        AttachmentSlot pecSlot = {
            .id = (AttachmentSlotId)(sideIdx == 0 ? AXIAL_SLOT_PECTORAL_LEFT : AXIAL_SLOT_PECTORAL_RIGHT),
            .role = sideIdx == 0 ? ATTACHMENT_PECTORAL_LEFT : ATTACHMENT_PECTORAL_RIGHT,
            .hostModuleInstanceId = module, .hostNode = pec->id,
            .position = {
                sign * pec->widthRadius * 0.92f,
                Math_Lerp(neck->center.y, pec->center.y, 0.55f),
                pec->center.z - pec->heightRadius * 0.15f
            },
            .forward = {0.0f, 0.0f, 1.0f}, .up = {0.0f, 1.0f, 0.0f}, .side = {1.0f, 0.0f, 0.0f},
            .hostRadii = {pec->widthRadius, pec->heightRadius, pec->heightRadius},
            .scale = s
        };
        if (slots->count >= ATTACHMENT_MAX_SLOTS) return false;
        slots->slots[slots->count++] = pecSlot;
    }

    /* 4. Ranura dorsal: en la espalda posterior (-Z) del tronco erguido.
     * En el marco local de la espalda: up = -Z (hacia afuera de la espalda), forward = +Y (hacia arriba). */
    Vector3 dorsalPos = {
        0.0f,
        Math_Lerp(thxP->center.y, abd->center.y, 0.35f),
        Math_Lerp(thxP->center.z - thxP->heightRadius, abd->center.z - abd->heightRadius, 0.35f) * 0.96f
    };
    AttachmentSlot dorsal = {
        .id = AXIAL_SLOT_DORSAL, .role = ATTACHMENT_DORSAL,
        .hostModuleInstanceId = module, .hostNode = thxP->id,
        .position = dorsalPos,
        .forward = {0.0f, 1.0f, 0.0f}, .up = {0.0f, 0.0f, -1.0f}, .side = {1.0f, 0.0f, 0.0f},
        .hostRadii = {thxP->widthRadius, thxP->heightRadius, L * 0.25f * s},
        .scale = s
    };

    /* 5. Ranura ventral / anal en la base del vientre. */
    AttachmentSlot ventral = {
        .id = AXIAL_SLOT_VENTRAL, .role = ATTACHMENT_VENTRAL,
        .hostModuleInstanceId = module, .hostNode = abd->id,
        .position = {0.0f, abd->center.y, abd->center.z + abd->heightRadius * 0.85f},
        .forward = {0.0f, -1.0f, 0.0f}, .up = {0.0f, 0.0f, 1.0f}, .side = {1.0f, 0.0f, 0.0f},
        .hostRadii = {abd->widthRadius, abd->heightRadius, abd->heightRadius},
        .scale = s
    };
    AttachmentSlot anal = ventral;
    anal.id = AXIAL_SLOT_ANAL;
    anal.role = ATTACHMENT_ANAL;
    anal.position.y = Math_Lerp(abd->center.y, pel->center.y, 0.5f);

    /* 6. Ranura caudal: apunta hacia abajo (-Y) para que la cola prensil descienda y se enrolle hacia adelante (+Z). */
    AttachmentSlot caudal = {
        .id = AXIAL_SLOT_CAUDAL, .role = ATTACHMENT_CAUDAL,
        .hostModuleInstanceId = module, .hostNode = pel->id,
        .position = pel->center,
        .forward = {0.0f, -1.0f, 0.0f}, .up = {0.0f, 0.0f, 1.0f}, .side = {1.0f, 0.0f, 0.0f},
        .hostRadii = {pel->widthRadius, pel->heightRadius, pel->heightRadius},
        .scale = s
    };

    if (slots->count + 4 > ATTACHMENT_MAX_SLOTS) return false;
    slots->slots[slots->count++] = dorsal;
    slots->slots[slots->count++] = ventral;
    slots->slots[slots->count++] = anal;
    slots->slots[slots->count++] = caudal;
    return true;
}
