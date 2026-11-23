/**
 * @file AxialBodyCurvedVertical.h
 * @brief Cuerpo axial acuático vertical curvado reutilizable (caballitos de mar, peces pipa, dragones de mar).
 * @author Monster Engine Team
 * @date 2026
 */

#ifndef CREATURE_AXIALBODY_CURVED_VERTICAL_H
#define CREATURE_AXIALBODY_CURVED_VERTICAL_H

#include "AxialBody.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Resuelve una columna acuática vertical con curvatura cervical en S y ranuras semánticas. */
bool AxialBodyCurvedVertical_Resolve(const AxialPhenotype* phenotype,
                                     uint32_t moduleInstanceId,
                                     AnatomyGraph* graph,
                                     AttachmentSlotSet* slots);

#ifdef __cplusplus
}
#endif

#endif
