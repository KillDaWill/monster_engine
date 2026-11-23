/**
 * @file AxialBodyAquaticFusiform.c
 * @brief Implementación del cuerpo axial acuático fusiforme continuo y parametrizado.
 */

#include "AxialBodyAquaticFusiform.h"
#include "CreatureModuleInternal.h"
#include "MathUtils.h"
#include <math.h>

bool AxialBodyAquaticFusiform_Resolve(const AxialPhenotype* p,
                                      uint32_t module,
                                      AnatomyGraph* g,
                                      AttachmentSlotSet* slots) {
    if (!p || !g || !slots || p->archetype != AXIAL_ARCHETYPE_AQUATIC_FUSIFORM) return false;
    float s = p->totalScale;
    float L = p->trunkLength;
    float refY = p->withersHeight;
    float flat = Math_Clamp(p->bodyFlattening, 0.35f, 1.0f);

    /* 6 estaciones fusiformes: collar cefálico -> cintura pectoral -> máximo volumen ->
     * tronco medio-posterior -> estrechamiento precaudal -> base del pedúnculo caudal. */
    float widths[6] = {
        p->neckWidth * 0.50f,
        p->shoulderWidth * 0.50f,
        p->thoraxWidth * 0.50f,
        p->abdomenWidth * 0.46f,
        p->abdomenWidth * 0.28f,
        p->pelvicWidth * 0.22f
    };
    float heights[6] = {
        p->thoraxHeight * 0.40f * flat,
        p->thoraxHeight * 0.50f * flat,
        p->thoraxHeight * 0.54f * flat,
        p->abdomenHeight * 0.48f * flat,
        p->abdomenHeight * 0.30f * flat,
        p->pelvicHeight * 0.22f * flat
    };
    float z[6] = {
        L * 0.46f,
        L * 0.25f,
        L * 0.05f,
        -L * 0.16f,
        -L * 0.36f,
        -L * 0.54f
    };
    /* Ligero arqueamiento dorsal natural hidrodinámico (cero en extremos, suave en tórax). */
    float yOff[6] = {
        -heights[0] * 0.04f,
         heights[1] * 0.03f,
         heights[2] * 0.05f,
         heights[3] * 0.02f,
         0.0f,
         0.0f
    };

    for (unsigned i = 0; i < 6; ++i) {
        const AxialStationMorph* morph = &p->profile[i];
        float w = fmaxf(0.02f, widths[i] * (morph->widthScale > 0.0f ? morph->widthScale : 1.0f));
        float h = fmaxf(0.02f, heights[i] * (morph->heightScale > 0.0f ? morph->heightScale : 1.0f));
        float y = refY + yOff[i] + morph->verticalOffset;
        float zi = z[i] + morph->longitudinalOffset;
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
    const AnatomyNode* pec  = AnatomyGraph_FindModuleNode(g, module, AXIAL_NODE_PECTORAL);
    const AnatomyNode* thxA = AnatomyGraph_FindModuleNode(g, module, AXIAL_NODE_THORAX_ANTERIOR);
    const AnatomyNode* thxP = AnatomyGraph_FindModuleNode(g, module, AXIAL_NODE_THORAX_POSTERIOR);
    const AnatomyNode* abd  = AnatomyGraph_FindModuleNode(g, module, AXIAL_NODE_ABDOMEN);
    const AnatomyNode* pel  = AnatomyGraph_FindModuleNode(g, module, AXIAL_NODE_PELVIS);
    if (!neck || !pec || !thxA || !thxP || !abd || !pel) return false;

    /* 1. Ranura cervical/cefálica anterior continua con el collar troncal. */
    AttachmentSlot cervical = {
        .id = AXIAL_SLOT_CERVICAL, .role = ATTACHMENT_CERVICAL,
        .hostModuleInstanceId = module, .hostNode = neck->id,
        .position = neck->center,
        .forward = {0.0f, 0.0f, 1.0f}, .up = {0.0f, 1.0f, 0.0f}, .side = {1.0f, 0.0f, 0.0f},
        .hostRadii = {neck->widthRadius, neck->heightRadius, fmaxf(neck->widthRadius, neck->heightRadius) * 0.92f},
        .scale = s
    };
    if (slots->count >= ATTACHMENT_MAX_SLOTS) return false;
    slots->slots[slots->count++] = cervical;

    /* 2-3. Ranuras pectorales ventrolaterales (aletas pectorales / flippers). */
    float lat = Math_Clamp(p->limbAttachmentLateral, 0.55f, 1.05f);
    for (int sideIdx = 0; sideIdx < 2; ++sideIdx) {
        float sign = sideIdx == 0 ? 1.0f : -1.0f;
        AttachmentSlot pecSlot = {
            .id = (AttachmentSlotId)(sideIdx == 0 ? AXIAL_SLOT_PECTORAL_LEFT : AXIAL_SLOT_PECTORAL_RIGHT),
            .role = sideIdx == 0 ? ATTACHMENT_PECTORAL_LEFT : ATTACHMENT_PECTORAL_RIGHT,
            .hostModuleInstanceId = module, .hostNode = pec->id,
            .position = {
                sign * pec->widthRadius * lat * 0.86f,
                pec->center.y - pec->heightRadius * 0.34f,
                Math_Lerp(pec->center.z, thxA->center.z, 0.25f)
            },
            .forward = {0.0f, 0.0f, 1.0f}, .up = {0.0f, 1.0f, 0.0f}, .side = {1.0f, 0.0f, 0.0f},
            .hostRadii = {pec->widthRadius, pec->heightRadius, L * 0.24f * s},
            .scale = s
        };
        if (slots->count >= ATTACHMENT_MAX_SLOTS) return false;
        slots->slots[slots->count++] = pecSlot;
    }

    /* 4-5. Ranuras pélvicas ventrolaterales (aletas pélvicas en peces/escualos). */
    for (int sideIdx = 0; sideIdx < 2; ++sideIdx) {
        float sign = sideIdx == 0 ? 1.0f : -1.0f;
        AttachmentSlot pelSlot = {
            .id = (AttachmentSlotId)(sideIdx == 0 ? AXIAL_SLOT_PELVIC_LEFT : AXIAL_SLOT_PELVIC_RIGHT),
            .role = sideIdx == 0 ? ATTACHMENT_PELVIC_LEFT : ATTACHMENT_PELVIC_RIGHT,
            .hostModuleInstanceId = module, .hostNode = thxP->id,
            .position = {
                sign * thxP->widthRadius * 0.52f,
                thxP->center.y - thxP->heightRadius * 0.78f,
                Math_Lerp(thxA->center.z, thxP->center.z, 0.65f)
            },
            .forward = {0.0f, 0.0f, 1.0f}, .up = {0.0f, 1.0f, 0.0f}, .side = {1.0f, 0.0f, 0.0f},
            .hostRadii = {thxP->widthRadius, thxP->heightRadius, thxP->widthRadius},
            .scale = s
        };
        if (slots->count >= ATTACHMENT_MAX_SLOTS) return false;
        slots->slots[slots->count++] = pelSlot;
    }

    /* 6. Ranura caudal en el pedúnculo posterior. */
    AttachmentSlot caudal = {
        .id = AXIAL_SLOT_CAUDAL, .role = ATTACHMENT_CAUDAL,
        .hostModuleInstanceId = module, .hostNode = pel->id,
        .position = pel->center,
        .forward = {0.0f, 0.0f, -1.0f}, .up = {0.0f, 1.0f, 0.0f}, .side = {1.0f, 0.0f, 0.0f},
        .hostRadii = {pel->widthRadius, pel->heightRadius, pel->widthRadius},
        .scale = s
    };
    if (slots->count >= ATTACHMENT_MAX_SLOTS) return false;
    slots->slots[slots->count++] = caudal;

    /* 7. Ranura dorsal principal (primera aleta dorsal / aleta dorsal única). */
    AttachmentSlot dorsal = {
        .id = AXIAL_SLOT_DORSAL, .role = ATTACHMENT_DORSAL,
        .hostModuleInstanceId = module, .hostNode = thxA->id,
        .position = {0.0f, thxA->center.y + thxA->heightRadius * 0.88f, thxA->center.z},
        .forward = {0.0f, 0.0f, 1.0f}, .up = {0.0f, 1.0f, 0.0f}, .side = {1.0f, 0.0f, 0.0f},
        .hostRadii = {thxA->widthRadius, thxA->heightRadius, L * 0.25f * s},
        .scale = s
    };
    /* 8. Ranura dorsal posterior (segunda aleta dorsal / pínulas dorsales). */
    AttachmentSlot dorsalPost = {
        .id = AXIAL_SLOT_DORSAL_POSTERIOR, .role = ATTACHMENT_DORSAL_POSTERIOR,
        .hostModuleInstanceId = module, .hostNode = abd->id,
        .position = {0.0f, abd->center.y + abd->heightRadius * 0.84f, abd->center.z},
        .forward = {0.0f, 0.0f, 1.0f}, .up = {0.0f, 1.0f, 0.0f}, .side = {1.0f, 0.0f, 0.0f},
        .hostRadii = {abd->widthRadius, abd->heightRadius, L * 0.22f * s},
        .scale = s
    };
    /* 9. Ranura ventral media. */
    AttachmentSlot ventral = {
        .id = AXIAL_SLOT_VENTRAL, .role = ATTACHMENT_VENTRAL,
        .hostModuleInstanceId = module, .hostNode = thxP->id,
        .position = {0.0f, thxP->center.y - thxP->heightRadius * 0.84f, thxP->center.z},
        .forward = {0.0f, 0.0f, 1.0f}, .up = {0.0f, -1.0f, 0.0f}, .side = {1.0f, 0.0f, 0.0f},
        .hostRadii = {thxP->widthRadius, thxP->heightRadius, L * 0.20f * s},
        .scale = s
    };
    /* 10. Ranura anal posterior (aleta anal / pínulas anales). */
    AttachmentSlot anal = {
        .id = AXIAL_SLOT_ANAL, .role = ATTACHMENT_ANAL,
        .hostModuleInstanceId = module, .hostNode = abd->id,
        .position = {0.0f, abd->center.y - abd->heightRadius * 0.84f, abd->center.z},
        .forward = {0.0f, 0.0f, 1.0f}, .up = {0.0f, -1.0f, 0.0f}, .side = {1.0f, 0.0f, 0.0f},
        .hostRadii = {abd->widthRadius, abd->heightRadius, L * 0.20f * s},
        .scale = s
    };
    /* 11. Ranura troncal de caparazón (para híbridos compatibles). */
    AttachmentSlot shell = {
        .id = AXIAL_SLOT_SHELL, .role = ATTACHMENT_SHELL,
        .hostModuleInstanceId = module, .hostNode = thxA->id,
        .position = thxA->center,
        .forward = {0.0f, 0.0f, 1.0f}, .up = {0.0f, 1.0f, 0.0f}, .side = {1.0f, 0.0f, 0.0f},
        .hostRadii = {thxA->widthRadius, thxA->heightRadius, L * 0.50f * s},
        .scale = s
    };

    if (slots->count + 5 > ATTACHMENT_MAX_SLOTS) return false;
    slots->slots[slots->count++] = dorsal;
    slots->slots[slots->count++] = dorsalPost;
    slots->slots[slots->count++] = ventral;
    slots->slots[slots->count++] = anal;
    slots->slots[slots->count++] = shell;
    return true;
}

bool AxialBodyAquaticFusiform_BuildSweepStations(const AxialPhenotype* p,
                                                 const AnatomyGraph* g,
                                                 uint32_t module,
                                                 SDFSweepStation* outStations,
                                                 int maxStations,
                                                 int* outCount) {
    if (!p || !g || !outStations || !outCount || maxStations < 6 ||
        p->archetype != AXIAL_ARCHETYPE_AQUATIC_FUSIFORM) {
        return false;
    }
    int c = 0;
    for (unsigned i = 0; i < 6; ++i) {
        const AnatomyNode* n = AnatomyGraph_FindModuleNode(g, module, (uint16_t)(i + 1));
        if (!n) return false;
        outStations[c++] = (SDFSweepStation){
            .center = n->center,
            .width = n->widthRadius,
            .height = n->heightRadius
        };
    }
    *outCount = c;
    return true;
}
