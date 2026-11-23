/**
 * @file AxialBodyMedusa.h
 * @brief Estrategia axial de simetría radial para campana gelatinosa medusoide (cnidarios).
 */
#ifndef CREATURE_AXIALBODY_MEDUSA_H
#define CREATURE_AXIALBODY_MEDUSA_H

#include "AxialBody.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Nodos radiales del borde marginal de la campana (20..35). */
#define AXIAL_MEDUSA_RIM_NODE_BASE 20

/** Parámetros geométricos resueltos de la campana cóncava de una medusa. */
typedef struct MedusaBellGeometry {
    Vector3 center;      /**< Centro del margen inferior de la campana en coordenadas del modelo. */
    float radius;        /**< Radio horizontal exterior del borde marginal. */
    float height;        /**< Altura vertical de la cúpula exumbrelar sobre el margen. */
    float wallThickness; /**< Espesor de la mesoglea en el ápice/pared. */
    float concavity;     /**< Profundidad relativa de la cavidad subumbrelar [0.3..0.92]. */
    float scallopAmp;    /**< Amplitud del festoneado marginal [0..0.18]. */
    unsigned lappetCount;/**< Número de lóbulos marginales (normalmente 16 o 24). */
} MedusaBellGeometry;

bool AxialBodyMedusa_Resolve(const AxialPhenotype* phenotype,
                             uint32_t moduleInstanceId,
                             AnatomyGraph* graph,
                             AttachmentSlotSet* slots);

/** @brief Extrae los parámetros de la campana cóncava para evaluación SDF y malla suplementaria. */
bool AxialBodyMedusa_GetBellGeometry(const AxialPhenotype* phenotype,
                                     const AnatomyGraph* graph,
                                     uint32_t moduleInstanceId,
                                     MedusaBellGeometry* out);

#ifdef __cplusplus
}
#endif

#endif
