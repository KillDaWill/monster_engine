/**
 * @file AxialBodyCephalopod.c
 * @brief Manto musculoso, cuello-collar, base cefálica corta, anillo circumoral radial de 8 brazos y sifón.
 */
#include "AxialBodyCephalopod.h"
#include "CreatureModuleInternal.h"
#include "MathUtils.h"
#include <math.h>

bool AxialBodyCephalopod_Resolve(const AxialPhenotype* p, uint32_t module,
                                 AnatomyGraph* g, AttachmentSlotSet* slots) {
    if (!p || !g || !slots || p->archetype != AXIAL_ARCHETYPE_CEPHALOPOD) return false;
    float s = p->totalScale;
    float L = p->trunkLength;
    float refY = p->withersHeight;
    float elong = p->mantleElongation > 0.01f ? p->mantleElongation : 1.0f;

    /* Disposición cefalópoda (de anterior +Z hacia manto posterior-dorsal -Z):
     * 1: Base oral / corona cefálica anterior (raíz de brazos circumorales)
     * 2: Cabeza muscular corta (soporte ocular lateral)
     * 3: Collar muscular / constricción manto-cabeza
     * 4: Manto anterior abombado
     * 5: Cúpula máxima del manto
     * 6: Ápice posterior del manto */
    float widths[6] = {
        p->neckWidth * 0.52f,
        p->shoulderWidth * 0.46f,
        p->neckWidth * 0.40f,
        p->thoraxWidth * 0.56f,
        p->abdomenWidth * 0.62f,
        p->pelvicWidth * 0.36f
    };
    float heights[6] = {
        p->neckWidth * 0.42f,
        p->thoraxHeight * 0.42f,
        p->thoraxHeight * 0.36f,
        p->thoraxHeight * 0.58f,
        p->abdomenHeight * 0.64f,
        p->pelvicHeight * 0.36f
    };
    float z[6] = {
        L * 0.34f,
        L * 0.18f,
        0.0f,
        -L * 0.24f * elong,
        -L * 0.54f * elong,
        -L * 0.80f * elong
    };
    float yOff[6] = {
        -heights[0] * 0.22f,
        heights[1] * 0.16f,
        heights[2] * 0.12f,
        heights[3] * 0.34f,
        heights[4] * 0.44f,
        heights[5] * 0.36f
    };

    for (unsigned i = 0; i < 6; ++i) {
        const AxialStationMorph* morph = &p->profile[i];
        float w = widths[i] * (morph->widthScale > 0.0f ? morph->widthScale : 1.0f);
        float h = heights[i] * (morph->heightScale > 0.0f ? morph->heightScale : 1.0f);
        Vector3 c = {
            0.0f,
            (refY + yOff[i] + morph->verticalOffset) * s,
            (z[i] + morph->longitudinalOffset) * s
        };
        AnatomyRegion region = (i == 5) ? ANATOMY_REGION_PELVIS : ANATOMY_REGION_TRUNK;
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

    const AnatomyNode* crown = AnatomyGraph_FindModuleNode(g, module, AXIAL_NODE_NECK);
    const AnatomyNode* headBase = AnatomyGraph_FindModuleNode(g, module, AXIAL_NODE_PECTORAL);
    const AnatomyNode* collar = AnatomyGraph_FindModuleNode(g, module, AXIAL_NODE_THORAX_ANTERIOR);
    const AnatomyNode* mantle = AnatomyGraph_FindModuleNode(g, module, AXIAL_NODE_ABDOMEN);
    if (!crown || !headBase || !collar || !mantle) return false;

    /* 1. Ranura cefálica (cabeza corta con ojos laterales prominentes). */
    AttachmentSlot cervical = {
        .id = AXIAL_SLOT_CERVICAL, .role = ATTACHMENT_CERVICAL,
        .hostModuleInstanceId = module, .hostNode = headBase->id,
        .position = {0.0f, headBase->center.y + headBase->heightRadius * 0.15f, headBase->center.z},
        .forward = {0.0f, 0.0f, 1.0f}, .up = {0.0f, 1.0f, 0.0f}, .side = {1.0f, 0.0f, 0.0f},
        .hostRadii = {headBase->widthRadius, headBase->heightRadius, headBase->widthRadius}, .scale = s
    };
    if (slots->count >= ATTACHMENT_MAX_SLOTS) return false;
    slots->slots[slots->count++] = cervical;

    /* 2. Anillo circumoral radial de brazos (por defecto 8 brazos independientes).
     * Eje del anillo orientado hacia arriba (+Y) con ligera inclinación anterior,
     * publicando 8 ranuras radiales alrededor de la corona bucal ventral. */
    unsigned armCount = (p->radialSymmetryOrder >= 4 && p->radialSymmetryOrder <= 12)
        ? p->radialSymmetryOrder : 8u;
    Vector3 ringCenter = {
        0.0f,
        crown->center.y - crown->heightRadius * 0.32f,
        Math_Lerp(crown->center.z, headBase->center.z, 0.25f)
    };
    Vector3 ringAxis = Vec3_Normalize(Vec3_Create(0.0f, 0.96f, -0.28f));
    Vector3 radialRef = Vec3_Normalize(Vec3_Create(0.0f, 0.28f, 0.96f));
    float ringRadius = crown->widthRadius * 0.72f;
    float startAngle = 3.14159265f / (float)armCount; /* Simetría bilateral izquierda/derecha exacta */
    if (!AttachmentSlotSet_PublishRadialRing(
            slots, module, crown->id,
            ringCenter, ringAxis, radialRef,
            ringRadius, armCount,
            startAngle, 6.28318530718f,
            AXIAL_SLOT_RADIAL_BASE, ATTACHMENT_RADIAL,
            Vec3_Create(crown->widthRadius, crown->heightRadius, crown->widthRadius), s)) {
        return false;
    }

    /* 3. Sifón / embudo propulsor lateroventral bajo el collar manto-cabeza. */
    Vector3 siphonFwd = Vec3_Normalize(Vec3_Create(-0.82f, -0.28f, -0.50f));
    Vector3 siphonUp = Vec3_Normalize(Vec3_Create(0.0f, 0.92f, -0.39f));
    Vector3 siphonSide = Vec3_Normalize(Vec3_Cross(siphonUp, siphonFwd));
    AttachmentSlot siphon = {
        .id = AXIAL_SLOT_SIPHON, .role = ATTACHMENT_SIPHON,
        .hostModuleInstanceId = module, .hostNode = collar->id,
        .position = {
            -collar->widthRadius * 0.72f,
            collar->center.y - collar->heightRadius * 0.42f,
            collar->center.z - collar->widthRadius * 0.12f
        },
        .forward = siphonFwd, .up = siphonUp, .side = siphonSide,
        .hostRadii = {collar->widthRadius * 0.35f, collar->heightRadius * 0.35f, collar->widthRadius * 0.35f},
        .scale = s
    };
    if (slots->count >= ATTACHMENT_MAX_SLOTS) return false;
    slots->slots[slots->count++] = siphon;

    /* 4. Ranuras dorsal y ventral genéricas (compatibilidad de ornamentos / injertos). */
    AttachmentSlot dorsal = {
        .id = AXIAL_SLOT_DORSAL, .role = ATTACHMENT_DORSAL,
        .hostModuleInstanceId = module, .hostNode = mantle->id,
        .position = {0.0f, mantle->center.y + mantle->heightRadius, mantle->center.z},
        .forward = {0.0f, 0.0f, 1.0f}, .up = {0.0f, 1.0f, 0.0f}, .side = {1.0f, 0.0f, 0.0f},
        .hostRadii = {mantle->widthRadius, mantle->heightRadius, mantle->widthRadius}, .scale = s
    };
    AttachmentSlot ventral = {
        .id = AXIAL_SLOT_VENTRAL, .role = ATTACHMENT_VENTRAL,
        .hostModuleInstanceId = module, .hostNode = crown->id,
        .position = {0.0f, crown->center.y - crown->heightRadius, crown->center.z},
        .forward = {0.0f, 0.0f, 1.0f}, .up = {0.0f, -1.0f, 0.0f}, .side = {1.0f, 0.0f, 0.0f},
        .hostRadii = {crown->widthRadius, crown->heightRadius, crown->widthRadius}, .scale = s
    };
    if (slots->count + 2 > ATTACHMENT_MAX_SLOTS) return false;
    slots->slots[slots->count++] = dorsal;
    slots->slots[slots->count++] = ventral;

    return true;
}

bool AxialBodyCephalopod_BuildSweepStations(const AxialPhenotype* p, const AnatomyGraph* g,
                                            uint32_t module, SDFSweepStation* outStations,
                                            int maxStations, int* outCount) {
    if (!p || !g || !outStations || !outCount || maxStations < 8 || p->archetype != AXIAL_ARCHETYPE_CEPHALOPOD)
        return false;
    const AnatomyNode* nodes[6];
    for (unsigned i = 0; i < 6; ++i) {
        nodes[i] = AnatomyGraph_FindModuleNode(g, module, (uint16_t)(i + 1));
        if (!nodes[i]) return false;
    }
    int c = 0;
    /* Remate anterior redondeado de la corona bucal */
    Vector3 front = nodes[0]->center;
    front.z += nodes[0]->widthRadius * 0.45f;
    outStations[c++] = (SDFSweepStation){
        .center = front,
        .width = nodes[0]->widthRadius * 0.66f,
        .height = nodes[0]->heightRadius * 0.62f
    };
    for (unsigned i = 0; i < 6; ++i) {
        outStations[c++] = (SDFSweepStation){
            .center = nodes[i]->center,
            .width = nodes[i]->widthRadius,
            .height = nodes[i]->heightRadius
        };
    }
    /* Cúpula posterior redondeada y suave del manto (sin pico pellizcado). */
    Vector3 apex = nodes[5]->center;
    apex.z -= nodes[5]->widthRadius * 0.32f;
    apex.y -= nodes[5]->heightRadius * 0.04f;
    outStations[c++] = (SDFSweepStation){
        .center = apex,
        .width = nodes[5]->widthRadius * 0.68f,
        .height = nodes[5]->heightRadius * 0.66f
    };
    *outCount = c;
    return true;
}
