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
    HEAD_SLOT_OCCIPITAL_RIGHT
};

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
                        float development,
                        uint32_t moduleInstanceId,
                        const AttachmentSlot* cervicalSlot,
                        AnatomyGraph* graph,
                        AttachmentSlotSet* slots);

#ifdef __cplusplus
}
#endif

#endif
