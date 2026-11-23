/**
 * @file AxialBodyMedusa.c
 * @brief Cuerpo radial cnidario con eje aboral-oral vertical, nodos marginales pulsátiles y ranuras radiales.
 */
#include "AxialBodyMedusa.h"
#include "CreatureModuleInternal.h"
#include "MathUtils.h"
#include <math.h>

bool AxialBodyMedusa_Resolve(const AxialPhenotype* p, uint32_t module,
                             AnatomyGraph* g, AttachmentSlotSet* slots) {
    if (!p || !g || !slots || p->archetype != AXIAL_ARCHETYPE_MEDUSA_RADIAL) return false;
    float s = p->totalScale;
    float bellR = Math_Max(p->thoraxWidth * 0.50f, 0.12f);
    float bellH = Math_Max(p->thoraxHeight * 0.56f, 0.08f);
    float marginY = p->withersHeight;
    unsigned order = (p->radialSymmetryOrder >= 4 && p->radialSymmetryOrder <= 12)
        ? p->radialSymmetryOrder : 8u;

    /* Estaciones verticales a lo largo del eje aboral-oral (sin columna ni cabeza vertebrada):
     * 1 (NECK): Ápice aboral de la exumbrella
     * 2 (PECTORAL): Cúpula superior
     * 3 (THORAX_ANTERIOR): Centro estructural de la campana (raíz del BodyPlan)
     * 4 (THORAX_POSTERIOR): Manubrio / centro oral subumbrelar */
    Vector3 apexPos = {0.0f, (marginY + bellH) * s, 0.0f};
    Vector3 upperPos = {0.0f, (marginY + bellH * 0.64f) * s, 0.0f};
    Vector3 hubPos = {0.0f, (marginY + bellH * 0.28f) * s, 0.0f};
    Vector3 oralPos = {0.0f, (marginY + bellH * 0.08f) * s, 0.0f};

    float coreR = bellR * 0.24f * s;
    if (!Module_Node(g, module, AXIAL_NODE_NECK, apexPos, coreR * 0.85f, coreR * 0.45f, 2,
                     ANATOMY_ROLE_AXIAL, ANATOMY_REGION_TRUNK, ANATOMY_SIDE_CENTER, 1.0f)) return false;
    if (!Module_Node(g, module, AXIAL_NODE_PECTORAL, upperPos, bellR * 0.62f * s, bellH * 0.26f * s, 2,
                     ANATOMY_ROLE_AXIAL, ANATOMY_REGION_TRUNK, ANATOMY_SIDE_CENTER, 1.0f)) return false;
    if (!Module_Node(g, module, AXIAL_NODE_THORAX_ANTERIOR, hubPos, bellR * 0.34f * s, bellH * 0.22f * s, 2,
                     ANATOMY_ROLE_AXIAL, ANATOMY_REGION_TRUNK, ANATOMY_SIDE_CENTER, 1.0f)) return false;
    if (!Module_Node(g, module, AXIAL_NODE_THORAX_POSTERIOR, oralPos, bellR * 0.22f * s, bellH * 0.16f * s, 2,
                     ANATOMY_ROLE_AXIAL, ANATOMY_REGION_PELVIS, ANATOMY_SIDE_CENTER, 1.0f)) return false;

    /* Conexiones de soporte internas (el volumen real de la campana cóncava se evalúa como cáscara analítica). */
    if (!Module_Edge(g, module, 1, Anatomy_MakeId(module, AXIAL_NODE_THORAX_ANTERIOR),
                     Anatomy_MakeId(module, AXIAL_NODE_PECTORAL), BODY_CONNECTION_SUPPORT, 1.0f)) return false;
    if (!Module_Edge(g, module, 2, Anatomy_MakeId(module, AXIAL_NODE_PECTORAL),
                     Anatomy_MakeId(module, AXIAL_NODE_NECK), BODY_CONNECTION_SUPPORT, 1.0f)) return false;
    if (!Module_Edge(g, module, 3, Anatomy_MakeId(module, AXIAL_NODE_THORAX_ANTERIOR),
                     Anatomy_MakeId(module, AXIAL_NODE_THORAX_POSTERIOR), BODY_CONNECTION_SUPPORT, 1.0f)) return false;

    /* Anillo radial de nodos marginales para animación de contracción/relajación de la campana. */
    AnatomyId hubId = Anatomy_MakeId(module, AXIAL_NODE_THORAX_ANTERIOR);
    for (unsigned i = 0; i < order; ++i) {
        float angle = (6.28318530718f * (float)i) / (float)order;
        Vector3 rimPos = {
            cosf(angle) * bellR * s,
            marginY * s,
            sinf(angle) * bellR * s
        };
        uint16_t localId = (uint16_t)(AXIAL_MEDUSA_RIM_NODE_BASE + i);
        if (!Module_Node(g, module, localId, rimPos, bellR * 0.10f * s, bellH * 0.10f * s, 2,
                         ANATOMY_ROLE_JOINT, ANATOMY_REGION_TRUNK, ANATOMY_SIDE_CENTER, 1.0f)) {
            return false;
        }
        if (!Module_Edge(g, module, localId, hubId, Anatomy_MakeId(module, localId),
                         BODY_CONNECTION_SUPPORT, 1.0f)) {
            return false;
        }
    }

    /* Publicar anillo radial de ranuras marginales (tentáculos o apéndices periféricos). */
    if (!AttachmentSlotSet_PublishRadialRing(
            slots, module, hubId,
            Vec3_Create(0.0f, marginY * s, 0.0f),
            Vec3_Create(0.0f, 1.0f, 0.0f),
            Vec3_Create(1.0f, 0.0f, 0.0f),
            bellR * 0.96f * s, order,
            0.0f, 6.28318530718f,
            AXIAL_SLOT_RADIAL_BASE, ATTACHMENT_RADIAL,
            Vec3_Create(bellR * s, bellH * s, bellR * s), s)) {
        return false;
    }

    /* Publicar 4 ranuras orales centrales subumbrelares (brazos orales rizados). */
    AnatomyId oralId = Anatomy_MakeId(module, AXIAL_NODE_THORAX_POSTERIOR);
    if (!AttachmentSlotSet_PublishRadialRing(
            slots, module, oralId,
            oralPos,
            Vec3_Create(0.0f, 1.0f, 0.0f),
            Vec3_Create(1.0f, 0.0f, 0.0f),
            bellR * 0.18f * s, 4u,
            0.78539816f, 6.28318530718f,
            AXIAL_SLOT_ORAL_BASE, ATTACHMENT_ORAL,
            Vec3_Create(bellR * 0.22f * s, bellH * 0.16f * s, bellR * 0.22f * s), s)) {
        return false;
    }

    /* Ranuras aboral/dorsal y oral/ventral. */
    AttachmentSlot dorsal = {
        .id = AXIAL_SLOT_DORSAL, .role = ATTACHMENT_DORSAL,
        .hostModuleInstanceId = module, .hostNode = Anatomy_MakeId(module, AXIAL_NODE_NECK),
        .position = apexPos,
        .forward = {0.0f, 0.0f, 1.0f}, .up = {0.0f, 1.0f, 0.0f}, .side = {1.0f, 0.0f, 0.0f},
        .hostRadii = {bellR * s, bellH * s, bellR * s}, .scale = s
    };
    AttachmentSlot ventral = {
        .id = AXIAL_SLOT_VENTRAL, .role = ATTACHMENT_VENTRAL,
        .hostModuleInstanceId = module, .hostNode = oralId,
        .position = oralPos,
        .forward = {0.0f, 0.0f, 1.0f}, .up = {0.0f, -1.0f, 0.0f}, .side = {1.0f, 0.0f, 0.0f},
        .hostRadii = {bellR * 0.22f * s, bellH * 0.16f * s, bellR * 0.22f * s}, .scale = s
    };
    if (slots->count + 2 > ATTACHMENT_MAX_SLOTS) return false;
    slots->slots[slots->count++] = dorsal;
    slots->slots[slots->count++] = ventral;

    return true;
}

bool AxialBodyMedusa_GetBellGeometry(const AxialPhenotype* p, const AnatomyGraph* g,
                                     uint32_t module, MedusaBellGeometry* out) {
    if (!p || !g || !out || p->archetype != AXIAL_ARCHETYPE_MEDUSA_RADIAL) return false;
    const AnatomyNode* apex = AnatomyGraph_FindModuleNode(g, module, AXIAL_NODE_NECK);
    const AnatomyNode* rim0 = AnatomyGraph_FindModuleNode(g, module, AXIAL_MEDUSA_RIM_NODE_BASE);
    const AnatomyNode* hub = AnatomyGraph_FindModuleNode(g, module, AXIAL_NODE_THORAX_ANTERIOR);
    if (!apex || !hub) return false;

    float marginY = rim0 ? rim0->center.y : (hub->center.y - p->thoraxHeight * 0.16f * p->totalScale);
    float radius = rim0 ? sqrtf(rim0->center.x * rim0->center.x + rim0->center.z * rim0->center.z)
                        : (p->thoraxWidth * 0.50f * p->totalScale);
    float height = fmaxf(apex->center.y - marginY, radius * 0.28f);
    float concavity = p->bellConcavity > 0.05f ? Math_Clamp(p->bellConcavity, 0.30f, 0.90f) : 0.72f;
    float wall = fmaxf(radius * (0.14f - 0.06f * concavity), 0.015f * p->totalScale);

    out->center = Vec3_Create(hub->center.x, marginY, hub->center.z);
    out->radius = fmaxf(radius, 0.05f * p->totalScale);
    out->height = height;
    out->wallThickness = wall;
    out->concavity = concavity;
    out->scallopAmp = Math_Clamp(p->bellScallopAmplitude, 0.0f, 0.20f);
    out->lappetCount = (p->radialSymmetryOrder >= 4) ? (p->radialSymmetryOrder * 3u) : 24u;
    return true;
}
