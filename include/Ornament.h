/**
 * @file Ornament.h
 * @brief Módulo anatómico genérico de ornamentos y estructuras dérmicas (cuernos, espinas).
 * @author Monster Engine Team
 * @date 2026
 */

#ifndef CREATURE_ORNAMENT_H
#define CREATURE_ORNAMENT_H

#include "Attachment.h"
#include "CreaturePhenotype.h"
#include "Anatomy.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    ORNAMENT_NODE_BASE = 1,
    ORNAMENT_NODE_MID,
    ORNAMENT_NODE_TIP
};

/**
 * @brief Resuelve la estructura anatómica de un ornamento acoplado a una ranura de anclaje.
 * @param phenotype Fenotipo del ornamento (arquetipo, radios, longitud, curvatura).
 * @param moduleInstanceId Identificador único de instancia del módulo.
 * @param slot Ranura receptora publicada por el módulo anfitrión.
 * @param graph Grafo anatómico donde registrar los nodos y conexiones.
 * @return true si la resolución fue exitosa; false en caso contrario.
 */
bool Ornament_Resolve(const OrnamentPhenotype* phenotype,
                      uint32_t moduleInstanceId,
                      const AttachmentSlot* slot,
                      AnatomyGraph* graph);

#ifdef __cplusplus
}
#endif

#endif
