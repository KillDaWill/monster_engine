/**
 * @file AxialBodyDiscRay.c
 * @brief Cuerpo central batoideo aplanado con ranuras pectorales continuas, lóbulos cefálicos y aletas pélvicas.
 */
#include "AxialBodyDiscRay.h"
#include "CreatureModuleInternal.h"
#include "MathUtils.h"
#include <math.h>

bool AxialBodyDiscRay_Resolve(const AxialPhenotype* p, uint32_t module,
                              AnatomyGraph* g, AttachmentSlotSet* slots) {
    if (!p || !g || !slots || p->archetype != AXIAL_ARCHETYPE_DISC_RAY) return false;
    float s = p->totalScale;
    float L = p->trunkLength;
    float refY = p->withersHeight;
    float flat = Math_Clamp(p->bodyFlattening, 0.25f, 1.0f);

    /* Perfil central hidrodinámico: borde cefálico frontal ancho -> núcleo pectoral ancho ->
     * cintura posterior -> pedúnculo caudal estrecho. Superficie dorsal convexa, vientre plano. */
    float widths[6] = {
        p->neckWidth * 0.65f,
        p->shoulderWidth * 0.62f,
        p->thoraxWidth * 0.68f,
        p->thoraxWidth * 0.60f,
        p->abdomenWidth * 0.44f,
        p->pelvicWidth * 0.24f
    };
    float heights[6] = {
        p->thoraxHeight * 0.30f * flat,
        p->thoraxHeight * 0.48f * flat,
        p->thoraxHeight * 0.56f * flat,
        p->thoraxHeight * 0.46f * flat,
        p->abdomenHeight * 0.34f * flat,
        p->pelvicHeight * 0.22f * flat
    };
    float z[6] = {
        L * 0.42f,
        L * 0.22f,
        0.0f,
        -L * 0.24f,
        -L * 0.48f,
        -L * 0.68f
    };

    for (unsigned i = 0; i < 6; ++i) {
        const AxialStationMorph* morph = &p->profile[i];
        float w = widths[i] * (morph->widthScale > 0.0f ? morph->widthScale : 1.0f);
        float h = heights[i] * (morph->heightScale > 0.0f ? morph->heightScale : 1.0f);
        /* Vientre casi plano a altura refY; el abombamiento dorsal eleva el centro según h. */
        float y = refY + h * 0.85f + morph->verticalOffset;
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

    const AnatomyNode* front = AnatomyGraph_FindModuleNode(g, module, AXIAL_NODE_NECK);
    const AnatomyNode* pec = AnatomyGraph_FindModuleNode(g, module, AXIAL_NODE_PECTORAL);
    const AnatomyNode* thxA = AnatomyGraph_FindModuleNode(g, module, AXIAL_NODE_THORAX_ANTERIOR);
    const AnatomyNode* thxP = AnatomyGraph_FindModuleNode(g, module, AXIAL_NODE_THORAX_POSTERIOR);
    const AnatomyNode* abd = AnatomyGraph_FindModuleNode(g, module, AXIAL_NODE_ABDOMEN);
    const AnatomyNode* pel = AnatomyGraph_FindModuleNode(g, module, AXIAL_NODE_PELVIS);
    if (!front || !pec || !thxA || !thxP || !abd || !pel) return false;

    /* 1. Ranura cefálica frontal amplia (cabeza batoidea plana). */
    AttachmentSlot cervical = {
        .id = AXIAL_SLOT_CERVICAL, .role = ATTACHMENT_CERVICAL,
        .hostModuleInstanceId = module, .hostNode = front->id,
        .position = front->center,
        .forward = {0.0f, 0.0f, 1.0f}, .up = {0.0f, 1.0f, 0.0f}, .side = {1.0f, 0.0f, 0.0f},
        .hostRadii = {front->widthRadius, front->heightRadius, front->widthRadius * 0.55f}, .scale = s
    };
    if (slots->count >= ATTACHMENT_MAX_SLOTS) return false;
    slots->slots[slots->count++] = cervical;

    /* 2-3. Ranuras pectorales laterales continuas (raíces embebidas en el disco central). */
    for (int sideIdx = 0; sideIdx < 2; ++sideIdx) {
        float sign = sideIdx == 0 ? 1.0f : -1.0f;
        AttachmentSlot fin = {
            .id = (AttachmentSlotId)(sideIdx == 0 ? AXIAL_SLOT_PECTORAL_LEFT : AXIAL_SLOT_PECTORAL_RIGHT),
            .role = sideIdx == 0 ? ATTACHMENT_PECTORAL_LEFT : ATTACHMENT_PECTORAL_RIGHT,
            .hostModuleInstanceId = module, .hostNode = thxA->id,
            .position = {
                sign * thxA->widthRadius * 0.58f,
                thxA->center.y,
                Math_Lerp(pec->center.z, thxA->center.z, 0.45f)
            },
            .forward = {0.0f, 0.0f, 1.0f}, .up = {0.0f, 1.0f, 0.0f}, .side = {1.0f, 0.0f, 0.0f},
            .hostRadii = {thxA->widthRadius, thxA->heightRadius, L * 0.42f * s}, .scale = s
        };
        if (slots->count >= ATTACHMENT_MAX_SLOTS) return false;
        slots->slots[slots->count++] = fin;
    }

    /* 4-5. Ranuras pélvicas posteriores (pequeñas aletas pélvicas). */
    for (int sideIdx = 0; sideIdx < 2; ++sideIdx) {
        float sign = sideIdx == 0 ? 1.0f : -1.0f;
        AttachmentSlot pelvicFin = {
            .id = (AttachmentSlotId)(sideIdx == 0 ? AXIAL_SLOT_PELVIC_LEFT : AXIAL_SLOT_PELVIC_RIGHT),
            .role = sideIdx == 0 ? ATTACHMENT_PELVIC_LEFT : ATTACHMENT_PELVIC_RIGHT,
            .hostModuleInstanceId = module, .hostNode = abd->id,
            .position = {
                sign * abd->widthRadius * 0.62f,
                abd->center.y - abd->heightRadius * 0.15f,
                Math_Lerp(abd->center.z, pel->center.z, 0.35f)
            },
            .forward = {0.0f, 0.0f, 1.0f}, .up = {0.0f, 1.0f, 0.0f}, .side = {1.0f, 0.0f, 0.0f},
            .hostRadii = {abd->widthRadius, abd->heightRadius, abd->widthRadius}, .scale = s
        };
        if (slots->count >= ATTACHMENT_MAX_SLOTS) return false;
        slots->slots[slots->count++] = pelvicFin;
    }

    /* 6. Ranura caudal (cola larga y delgada tipo látigo). */
    AttachmentSlot caudal = {
        .id = AXIAL_SLOT_CAUDAL, .role = ATTACHMENT_CAUDAL,
        .hostModuleInstanceId = module, .hostNode = pel->id,
        .position = {0.0f, pel->center.y, pel->center.z - pel->widthRadius * 0.30f},
        .forward = {0.0f, -0.02f, -1.0f}, .up = {0.0f, 1.0f, -0.02f}, .side = {1.0f, 0.0f, 0.0f},
        .hostRadii = {pel->widthRadius, pel->heightRadius, pel->widthRadius}, .scale = s
    };
    if (slots->count >= ATTACHMENT_MAX_SLOTS) return false;
    slots->slots[slots->count++] = caudal;

    /* 7-8. Dorsal (pequeña aleta dorsal en la base caudal) y ventral. */
    AttachmentSlot dorsal = {
        .id = AXIAL_SLOT_DORSAL, .role = ATTACHMENT_DORSAL,
        .hostModuleInstanceId = module, .hostNode = pel->id,
        .position = {0.0f, pel->center.y + pel->heightRadius * 0.78f, Math_Lerp(abd->center.z, pel->center.z, 0.58f)},
        .forward = {0.0f, 0.0f, 1.0f}, .up = {0.0f, 1.0f, 0.0f}, .side = {1.0f, 0.0f, 0.0f},
        .hostRadii = {pel->widthRadius, pel->heightRadius, pel->widthRadius}, .scale = s
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

    /* 9-10. Lóbulos cefálicos pareados flanqueando la boca frontal terminal. */
    for (int sideIdx = 0; sideIdx < 2; ++sideIdx) {
        float sign = sideIdx == 0 ? 1.0f : -1.0f;
        AttachmentSlot lobe = {
            .id = (AttachmentSlotId)(sideIdx == 0 ? AXIAL_SLOT_CEPHALIC_LEFT : AXIAL_SLOT_CEPHALIC_RIGHT),
            .role = ATTACHMENT_CEPHALIC_LOBE,
            .hostModuleInstanceId = module, .hostNode = front->id,
            .position = {
                sign * front->widthRadius * 0.86f,
                front->center.y - front->heightRadius * 0.06f,
                front->center.z + front->heightRadius * 0.35f
            },
            .forward = Vec3_Normalize(Vec3_Create(sign * 0.08f, -0.06f, 1.0f)),
            .up = {0.0f, 1.0f, 0.0f},
            .side = {sign, 0.0f, 0.0f},
            .hostRadii = {front->widthRadius * 0.26f, front->heightRadius * 0.72f, front->heightRadius * 0.72f},
            .scale = s
        };
        if (slots->count >= ATTACHMENT_MAX_SLOTS) return false;
        slots->slots[slots->count++] = lobe;
    }

    return true;
}

bool AxialBodyDiscRay_BuildSweepStations(const AxialPhenotype* p, const AnatomyGraph* g,
                                         uint32_t module, SDFSweepStation* outStations,
                                         int maxStations, int* outCount) {
    if (!p || !g || !outStations || !outCount || maxStations < 8 || p->archetype != AXIAL_ARCHETYPE_DISC_RAY)
        return false;
    const AnatomyNode* nodes[6];
    for (unsigned i = 0; i < 6; ++i) {
        nodes[i] = AnatomyGraph_FindModuleNode(g, module, (uint16_t)(i + 1));
        if (!nodes[i]) return false;
    }
    int c = 0;
    /* Margen frontal ancho (boca terminal sin hocico) */
    Vector3 frontTip = nodes[0]->center;
    frontTip.z += nodes[0]->heightRadius * 0.85f;
    outStations[c++] = (SDFSweepStation){
        .center = frontTip,
        .width = nodes[0]->widthRadius * 0.88f,
        .height = nodes[0]->heightRadius * 0.56f
    };
    for (unsigned i = 0; i < 6; ++i) {
        outStations[c++] = (SDFSweepStation){
            .center = nodes[i]->center,
            .width = nodes[i]->widthRadius,
            .height = nodes[i]->heightRadius
        };
    }
    Vector3 rear = nodes[5]->center;
    rear.z -= nodes[5]->widthRadius * 0.55f;
    outStations[c++] = (SDFSweepStation){
        .center = rear,
        .width = nodes[5]->widthRadius * 0.50f,
        .height = nodes[5]->heightRadius * 0.55f
    };
    *outCount = c;
    return true;
}
