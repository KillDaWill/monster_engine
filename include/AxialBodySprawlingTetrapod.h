/**
 * @file AxialBodySprawlingTetrapod.h
 * @brief Arquetipo axial de tetrápodo bajo/reptiliano (estaciones de cuello, tórax y pelvis).
 * @author Monster Engine Team
 * @date 2026
 */

#ifndef CREATURE_AXIAL_BODY_SPRAWLING_TETRAPOD_H
#define CREATURE_AXIAL_BODY_SPRAWLING_TETRAPOD_H

#include "Attachment.h"
#include "CreaturePhenotype.h"
#include "Anatomy.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    AXIAL_SPRAWLING_SLOT_CERVICAL = 1,
    AXIAL_SPRAWLING_SLOT_PECTORAL_LEFT,
    AXIAL_SPRAWLING_SLOT_PECTORAL_RIGHT,
    AXIAL_SPRAWLING_SLOT_PELVIC_LEFT,
    AXIAL_SPRAWLING_SLOT_PELVIC_RIGHT,
    AXIAL_SPRAWLING_SLOT_CAUDAL,
    AXIAL_SPRAWLING_SLOT_DORSAL,
    AXIAL_SPRAWLING_SLOT_VENTRAL
};

#define AXIAL_SPRAWLING_SLOT_DORSAL_ARRAY_BASE 100
#define AXIAL_SPRAWLING_SLOT_DORSAL_ARRAY_COUNT 16

/**
 * @brief Resuelve la anatomía axial de un tetrápodo con andadura baja (lagarto/reptil).
 * @param phenotype Fenotipo axial de configuración.
 * @param moduleInstanceId Identificador único de instancia modular.
 * @param graph Grafo anatómico donde publicar las estaciones y aristas.
 * @param slots Conjunto de ranuras de anclaje donde publicar los slots disponibles.
 * @return true si la resolución fue exitosa, false si falló la validación.
 */
bool AxialBodySprawlingTetrapod_Resolve(const AxialPhenotype* phenotype,
                                       uint32_t moduleInstanceId,
                                       AnatomyGraph* graph,
                                       AttachmentSlotSet* slots);

#ifdef __cplusplus
}
#endif

#endif
