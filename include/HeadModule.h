/**
 * @file HeadModule.h
 * @brief Módulo anatómico cefálico desacoplado del cuerpo axial.
 * @author Monster Engine Team
 * @date 2026
 */

#ifndef CREATURE_HEAD_MODULE_H
#define CREATURE_HEAD_MODULE_H

#include "Attachment.h"
#include "CreaturePhenotype.h"
#include "Anatomy.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    HEAD_NODE_MAIN = 1
};

enum {
    HEAD_SLOT_CRANIAL_DORSAL = 1,
    HEAD_SLOT_HORN_LEFT,
    HEAD_SLOT_HORN_RIGHT,
    HEAD_SLOT_POSTEROLATERAL_LEFT,
    HEAD_SLOT_POSTEROLATERAL_RIGHT,
    HEAD_SLOT_TEMPORAL_LEFT,
    HEAD_SLOT_TEMPORAL_RIGHT,
    HEAD_SLOT_OCCIPITAL_LEFT,
    HEAD_SLOT_OCCIPITAL_RIGHT,
    HEAD_SLOT_PINNA_LEFT,
    HEAD_SLOT_PINNA_RIGHT,
    HEAD_SLOT_PROBOSCIS,
    HEAD_SLOT_MAXILLARY_LEFT,
    HEAD_SLOT_MAXILLARY_RIGHT,
    HEAD_SLOT_NASAL_ANTERIOR,
    HEAD_SLOT_NASAL_POSTERIOR,
    HEAD_SLOT_FITTED_PROBOSCIS,
    HEAD_SLOT_FITTED_MAXILLARY_LEFT,
    HEAD_SLOT_FITTED_MAXILLARY_RIGHT,
    HEAD_SLOT_FITTED_NASAL_ANTERIOR,
    HEAD_SLOT_FITTED_NASAL_POSTERIOR
};

/* Petición de la ranura nasal original, además del rango de ranuras ajustadas. */
#define HEAD_SITE_LEGACY_PROBOSCIS (1u << 8)

/** @brief Posición local del pedículo nasal en la sección premaxilar. */
Vector3 HeadModule_NasalRoot(const HeadSurfaceRecipe* surface);

/** @brief Comprueba si la ranura corresponde a la prolongación nasal. */
static inline bool HeadModule_IsNasalExtension(AttachmentSlotId id) {
    return id==HEAD_SLOT_PROBOSCIS || id==HEAD_SLOT_FITTED_PROBOSCIS;
}

/** Resuelve el tamaño de soporte de una inserción facial en el perfil local. */
Vector3 HeadModule_FacialSupport(const HeadSurfaceRecipe *surface, AttachmentSlotId fitted);

/**
 * @brief Resuelve la estación cefálica anclada a una ranura cervical.
 * @param phenotype Fenotipo de la cabeza.
 * @param development Nivel de desarrollo cefálico (0..1).
 * @param moduleInstanceId Identificador único de instancia del módulo.
 * @param cervicalSlot Ranura cervical suministrada por el cuerpo axial.
 * @param graph Grafo anatómico donde registrar la estación y la arista.
 * @param slots Colección opcional donde publicar ranuras craneales disponibles.
 * @return true si la resolución fue exitosa; false en caso contrario.
 */
bool HeadModule_Resolve(const HeadPhenotype* phenotype,
                        const HeadEnvelopePhenotype* envelope,
                        float development, uint32_t moduleInstanceId, const AttachmentSlot* slot,
                        AnatomyGraph* graph, AttachmentSlotSet* slots);

/** Resuelve una cabeza con perfil editable y publica sus landmarks de anclaje. */
bool HeadModule_ResolveProfile(const HeadPhenotype* phenotype,
                        const HeadEnvelopePhenotype* envelope,
                        const HeadHybridProfile* hybrid,
                        float development,
                        uint32_t moduleInstanceId,
                        const AttachmentSlot* cervicalSlot,
                        AnatomyGraph* graph,
                        AttachmentSlotSet* slots);

/** Publica además los sitios solicitados por el plan, en orden desde HEAD_SLOT_FITTED_PROBOSCIS. */
bool HeadModule_ResolveProfileWithSites(const HeadPhenotype *, const HeadEnvelopePhenotype *,
    const HeadHybridProfile *, float development, uint32_t module, const AttachmentSlot *,
    AnatomyGraph *, AttachmentSlotSet *, unsigned requestedSites);

#ifdef __cplusplus
}
#endif

#endif
