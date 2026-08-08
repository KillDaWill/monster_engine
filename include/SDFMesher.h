/**
 * @file SDFMesher.h
 * @brief Orquestador de triangulación de campos SDF a mallas poligonales usando Marching Cubes.
 * @author Monster Engine Team
 * @date 2026
 */

#ifndef MONSTER_SDF_MESHER_H
#define MONSTER_SDF_MESHER_H

#include "Mesh.h"
#include "MonsterQueries.h"
#include "SDFSampling.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @struct SDFMesherConfig
 * @brief Configuración para el proceso de generación de malla 3D.
 */
typedef struct SDFMesherConfig {
    int resolutionX;   /**< Resolución de rejilla en el eje X (ej. 32) */
    int resolutionY;   /**< Resolución de rejilla en el eje Y (ej. 32) */
    int resolutionZ;   /**< Resolución de rejilla en el eje Z (ej. 32) */
    float isolevel;    /**< Valor de la isosuperficie (defecto 0.0f) */
    float normalEps;   /**< Paso épsilon para gradiente numérico de normales */
    AABB3D bounds;     /**< Bounding Box 3D para el volumen de muestreo */
    bool useAutoBounds;/**< Si es true, recalcula las fronteras automáticamente */
} SDFMesherConfig;

/**
 * @struct SDFMesher
 * @brief Estructura del orquestador SDFMesher.
 */
typedef struct SDFMesher {
    SDFMesherConfig config; /**< Configuración activa del mesher */
} SDFMesher;

/**
 * @brief Retorna una configuración por defecto adecuada (ej. 32x32x32).
 */
SDFMesherConfig SDFMesher_DefaultConfig(void);

/**
 * @brief Crea una instancia de SDFMesher con la configuración dada.
 */
SDFMesher SDFMesher_Create(SDFMesherConfig config);

/**
 * @brief Genera una malla 3D evaluando el campo SDF dado sobre la rejilla regular.
 * @param mesher Instancia de SDFMesher.
 * @param evalFn Función evaluadora del campo SDF.
 * @param context Contexto a pasar a evalFn (ej. const MonsterSDF*).
 * @param outMesh Malla de salida a rellenar con vértices e índices.
 * @return true si la generación se completó exitosamente.
 */
bool SDFMesher_GenerateMesh(
    SDFMesher* mesher,
    SDFEvaluateFn evalFn,
    const void* context,
    Mesh* outMesh
);

#ifdef __cplusplus
}
#endif

#endif // MONSTER_SDF_MESHER_H
