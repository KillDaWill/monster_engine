/**
 * @file SDFSampling.h
 * @brief Funciones de muestreo de campo SDF y estimación de normales por diferencias finitas.
 * @author Monster Engine Team
 * @date 2026
 */

#ifndef MONSTER_SDF_SAMPLING_H
#define MONSTER_SDF_SAMPLING_H

#include "Vector.h"
#include "SDFOperations.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Firma de función para evaluar un campo SDF en un punto 3D.
 */
typedef SDFSample (*SDFEvaluateFn)(const void* context, Vector3 point);

/**
 * @struct SDFField
 * @brief Abstracción de un campo escalar/vectorial SDF con su contexto asociado.
 */
typedef struct SDFField {
    SDFEvaluateFn evaluate; /**< Puntero a función de evaluación del campo */
    const void* context;    /**< Contexto o estructura de datos del campo (ej. MonsterSDF*) */
} SDFField;

/**
 * @brief Estima el vector normal unitario en un punto del campo SDF mediante gradiente numérico (diferencias finitas).
 * @param evalFn Función de evaluación SDF.
 * @param context Contexto a pasar a la función evalFn.
 * @param point Punto 3D en el espacio.
 * @param eps Épsilon/Paso de diferencia finita (ej. 0.001f - 0.01f).
 * @return Vector normal unitario (apuntando hacia afuera de la superficie).
 */
Vector3 SDF_EstimateNormal(SDFEvaluateFn evalFn, const void* context, Vector3 point, float eps);

#ifdef __cplusplus
}
#endif

#endif // MONSTER_SDF_SAMPLING_H
