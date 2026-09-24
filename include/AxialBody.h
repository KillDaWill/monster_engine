/**
 * @file AxialBody.h
 * @brief Despacho de resolución de anatomía axial modular reutilizable.
 * @author Monster Engine Team
 * @date 2026
 */

#ifndef CREATURE_AXIALBODY_H
#define CREATURE_AXIALBODY_H

#include "Attachment.h"
#include "CreaturePhenotype.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    AXIAL_NODE_NECK = 1,
    AXIAL_NODE_PECTORAL,
    AXIAL_NODE_THORAX_ANTERIOR,
    AXIAL_NODE_THORAX_POSTERIOR,
    AXIAL_NODE_ABDOMEN,
    AXIAL_NODE_PELVIS
};

enum {
    AXIAL_SLOT_CERVICAL = 1, AXIAL_SLOT_PECTORAL_LEFT, AXIAL_SLOT_PECTORAL_RIGHT,
    AXIAL_SLOT_PELVIC_LEFT, AXIAL_SLOT_PELVIC_RIGHT, AXIAL_SLOT_CAUDAL,
    AXIAL_SLOT_DORSAL, AXIAL_SLOT_VENTRAL
};

/** @brief Indica si el módulo implementa el arquetipo axial. */
bool AxialBody_Supports(AxialArchetype archetype);

/**
 * @brief Despacha la resolución axial al arquetipo correspondiente.
 * @param phenotype Fenotipo axial con el arquetipo y proporciones.
 * @param moduleInstanceId Identificador único de instancia modular.
 * @param graph Grafo anatómico donde publicar las estaciones y aristas.
 * @param slots Conjunto de slots donde publicar las ranuras disponibles.
 * @return true si el arquetipo es soportado y se resolvió con éxito; false en caso contrario.
 */
bool AxialBody_Resolve(const AxialPhenotype* phenotype,
                       uint32_t moduleInstanceId,
                       AnatomyGraph* graph,
                       AttachmentSlotSet* slots);

#ifdef __cplusplus
}
#endif

#endif
