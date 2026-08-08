/**
 * @file SDFMesher.h
 * @brief Orquestador de triangulación de campos SDF a mallas poligonales usando Marching Cubes con caché de aristas.
 * @author Monster Engine Team
 * @date 2026
 */

#ifndef MONSTER_SDF_MESHER_H
#define MONSTER_SDF_MESHER_H

#include "Mesh.h"
#include "AABB.h"
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
    float voxelSize;   /**< Tamaño objetivo de vóxel (si es > 0, anula resolución fija) */
    int maxResolution; /**< Límite superior por dimensión (por defecto 128) */
    size_t maxCells;   /**< Límite superior de celdas totales (por defecto 500,000) */
    float isolevel;    /**< Valor de la isosuperficie (defecto 0.0f) */
    float normalEps;   /**< Paso épsilon para gradiente numérico de normales (si <= 0, auto: min(step)*0.25) */
    AABB3D bounds;     /**< Bounding Box 3D para el volumen de muestreo */
    bool useAutoBounds;/**< Si es true, recalcula las fronteras usando field->getBounds */
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
 * @brief Genera una malla 3D evaluando el campo SDF dado sobre la rejilla regular con caché de aristas.
 * @param mesher Instancia de SDFMesher.
 * @param field Puntero a la estructura de campo SDF (evaluador + bounds + contexto).
 * @param outMesh Malla de salida a rellenar con vértices e índices.
 * @return true si la generación se completó exitosamente, false si ocurrió error de parámetros o de memoria.
 */
bool SDFMesher_GenerateMesh(
    SDFMesher* mesher,
    const SDFField* field,
    Mesh* outMesh
);

#ifdef __cplusplus
}
#endif

#endif // MONSTER_SDF_MESHER_H
