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
     * 2: Cabeza muscular compacta (deja sobresalir las órbitas dorsolaterales)
     * 3: Collar muscular suave cabeza-manto
     * 4-6: Manto ovoide continuo sobre sección elipsoidal real (sin picos ni mamelón) */
    float mantleRx = p->abdomenWidth * 0.56f;
    float mantleRy = p->abdomenHeight * 0.52f;
    float mantleCenterY = refY + mantleRy * 0.38f;
    float mantleCenterZ = -L * 0.48f * elong;
    float mantleRz = L * 0.42f * elong;

    float widths[6] = {
        p->neckWidth * 0.52f,
        p->shoulderWidth * 0.36f,
        p->neckWidth * 0.44f,
        mantleRx * 0.88f,
        mantleRx * 1.00f,
        mantleRx * 0.64f
    };
    float heights[6] = {
        p->neckWidth * 0.40f,
        p->thoraxHeight * 0.34f,
        p->thoraxHeight * 0.38f,
        mantleRy * 0.88f,
        mantleRy * 1.00f,
        mantleRy * 0.64f
    };
    float z[6] = {
        L * 0.28f,
        L * 0.14f,
        -L * 0.02f,
        mantleCenterZ + mantleRz * 0.48f,
        mantleCenterZ - mantleRz * 0.04f,
        mantleCenterZ - mantleRz * 0.72f
    };
    float yAbs[6] = {
        refY - heights[0] * 0.26f,
        refY + heights[1] * 0.04f,
        refY + heights[2] * 0.16f,
        mantleCenterY - mantleRy * 0.04f,
        mantleCenterY + mantleRy * 0.04f,
        mantleCenterY - mantleRy * 0.02f
    };

    for (unsigned i = 0; i < 6; ++i) {
        const AxialStationMorph* morph = &p->profile[i];
        float w = widths[i] * (morph->widthScale > 0.0f ? morph->widthScale : 1.0f);
        float h = heights[i] * (morph->heightScale > 0.0f ? morph->heightScale : 1.0f);
        Vector3 c = {
            0.0f,
            (yAbs[i] + morph->verticalOffset) * s,
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

    /* 1. Ranura cefálica (cabeza corta con ojos dorsolaterales claramente visibles sobre la corona). */
    AttachmentSlot cervical = {
        .id = AXIAL_SLOT_CERVICAL, .role = ATTACHMENT_CERVICAL,
        .hostModuleInstanceId = module, .hostNode = headBase->id,
        .position = {0.0f, headBase->center.y + headBase->heightRadius * 0.34f, headBase->center.z},
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

    /* 3. Sifón / embudo propulsor lateroventral bajo el collar manto-cabeza con boca visible. */
    Vector3 siphonFwd = Vec3_Normalize(Vec3_Create(-0.76f, -0.16f, -0.62f));
    Vector3 siphonUp = Vec3_Normalize(Vec3_Create(0.0f, 0.98f, -0.20f));
    Vector3 siphonSide = Vec3_Normalize(Vec3_Cross(siphonUp, siphonFwd));
    AttachmentSlot siphon = {
        .id = AXIAL_SLOT_SIPHON, .role = ATTACHMENT_SIPHON,
        .hostModuleInstanceId = module, .hostNode = collar->id,
        .position = {
            -collar->widthRadius * 0.68f,
            collar->center.y - collar->heightRadius * 0.34f,
            collar->center.z - collar->widthRadius * 0.04f
        },
        .forward = siphonFwd, .up = siphonUp, .side = siphonSide,
        .hostRadii = {collar->widthRadius * 0.34f, collar->heightRadius * 0.34f, collar->widthRadius * 0.34f},
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
    /* Sin estación terminal estrecha adicional: el remate posterior usa la cúpula amplia de nodes[5]
     * con MatchSweepCapTangent en MonsterSDF para un cierre ovoide hemisférico continuo sin mamelón. */
    *outCount = c;
    return true;
}
