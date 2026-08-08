/**
 * @file SDFOperations.h
 * @brief Operaciones CSG booleanas y mezclas suaves (Smooth Union / Subtract) para SDF.
 * @author Monster Engine Team
 * @date 2026
 */

#ifndef MONSTER_SDF_OPERATIONS_H
#define MONSTER_SDF_OPERATIONS_H

#include "Color.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @enum SDFMaterial
 * @brief Tipo de material o propiedad de superficie resultante de la evaluación del SDF.
 */
typedef enum SDFMaterial {
    SDF_MATERIAL_SKIN,
    SDF_MATERIAL_MOUTH,
    SDF_MATERIAL_LIP,
    SDF_MATERIAL_UNKNOWN
} SDFMaterial;

/**
 * @struct SDFSample
 * @brief Resultado de muestreo en un punto del campo SDF (distancia signed, color y material).
 */
typedef struct SDFSample {
    float distance;       /**< Distancia signed al isosuperficie (0 = superficie) */
    Color color;          /**< Color resultante en el punto */
    SDFMaterial material; /**< Clasificación de material */
} SDFSample;

/* Operaciones escalares puras (solo distancia) */

float SDF_Union(float a, float b);
float SDF_Intersection(float a, float b);
float SDF_Subtract(float body, float cutter);

float SDF_SmoothUnion(float a, float b, float k);
float SDF_SmoothSubtract(float body, float cutter, float k);
float SDF_SmoothIntersection(float a, float b, float k);

/* Operaciones basadas en muestras (distancia + color + material) */

SDFSample SDFSample_Create(float distance, Color color, SDFMaterial material);
SDFSample SDFSample_Union(SDFSample a, SDFSample b);
SDFSample SDFSample_SmoothUnion(SDFSample a, SDFSample b, float k);
SDFSample SDFSample_Subtract(SDFSample body, SDFSample cutter, float k);

#ifdef __cplusplus
}
#endif

#endif // MONSTER_SDF_OPERATIONS_H
