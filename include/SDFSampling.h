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
#include "AABB.h"
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Firma de función para evaluar un campo SDF en un punto 3D (distancia, color, material).
 */
typedef SDFSample (*SDFEvaluateFn)(const void* context, Vector3 point);

/**
 * @brief Firma de función opcional para evaluar solo la distancia signed escalar en un punto 3D.
 */
typedef float (*SDFDistanceFn)(const void* context, Vector3 point);

/**
 * @brief Firma de función opcional para calcular o retornar los bounds de un campo SDF.
 */
typedef AABB3D (*SDFBoundsFn)(const void* context);

/** @brief Región genérica de detalle; el objetivo describe la escala geométrica. */
typedef struct SDFDetailRegion {
    AABB3D bounds; /**< Región que contiene el rasgo. */
    float targetVoxelSize; /**< Espaciado máximo deseado dentro de la región. */
} SDFDetailRegion;

/** @brief Firma para consultar cajas envolventes (AABB) de componentes individuales de un campo. */
typedef size_t (*SDFComponentBoundsFn)(const void* context, AABB3D* outBoxes, size_t capacity);

/** @brief Intervalo conservador del campo en una caja; false indica intervalo desconocido. */
typedef bool (*SDFCellRangeFn)(const void* context, AABB3D box, float* minimum, float* maximum);

/**
 * @struct SDFField
 * @brief Abstracción de un campo escalar/vectorial SDF con su contexto asociado y delimitación.
 */
typedef struct SDFField {
    SDFEvaluateFn evaluate;         /**< Puntero a función de evaluación del campo completo */
    SDFDistanceFn evaluateDistance; /**< Puntero opcional a función de sólo distancia escalar */
    SDFBoundsFn getBounds;          /**< Puntero a función opcional de límites */
    SDFComponentBoundsFn getComponentBounds; /**< Puntero opcional para consultar cajas de influencia conservadora */
    const void* context;            /**< Contexto o estructura de datos del campo (ej. MonsterSDF*) */
    SDFCellRangeFn getCellRange;     /**< Intervalo opcional, sin asumir que el campo sea 1-Lipschitz. */
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
