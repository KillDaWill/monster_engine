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
#include <stddef.h>

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
 * @struct SDFMesherStats
 * @brief Estadísticas del último proceso de generación de malla en SDFMesher.
 */
typedef struct SDFMesherStats {
    int resolutionX;             /**< Resolución efectiva X utilizada */
    int resolutionY;             /**< Resolución efectiva Y utilizada */
    int resolutionZ;             /**< Resolución efectiva Z utilizada */
    Vector3 voxelStep;           /**< Tamaño real del vóxel en cada eje */
    size_t cellCount;            /**< Cantidad total de celdas en la rejilla */
    size_t gridPointCount;       /**< Cantidad total de puntos muestreados en la rejilla */
    size_t fieldEvaluationCount;       /**< Total de evaluaciones de campo (compatibilidad) */
    size_t distanceEvaluationCount;   /**< Evaluaciones de sólo distancia escalar */
    size_t fullSampleEvaluationCount; /**< Evaluaciones completas de color/atributos en vértices */
    size_t gradientEvaluationCount;   /**< Gradientes calculados perezosamente */
    size_t normalFallbackCount;       /**< Casos con gradiente nulo que usaron SDF_EstimateNormal */
    size_t generatedVertexCount; /**< Cantidad de vértices añadidos a la malla */
    size_t generatedTriangleCount; /**< Cantidad de triángulos añadidos a la malla */
    float requestedVoxelSize;    /**< Tamaño de vóxel solicitado originalmente */
    float effectiveVoxelSize;    /**< Tamaño de vóxel efectivo tras presupuesto */
    bool cellBudgetAdjusted;     /**< true si la resolución fue ajustada por presupuesto maxCells */
} SDFMesherStats;

/**
 * @struct SDFMesher
 * @brief Estructura del orquestador SDFMesher con buffers scratch reutilizables.
 */
typedef struct SDFMesher {
    SDFMesherConfig config;      /**< Configuración activa del mesher */

    float* gridDistances;        /**< Buffer reutilizable de distancias escalares en rejilla */
    size_t gridDistanceCapacity; /**< Capacidad reservada de gridDistances */

    Vector3* gridGradients;      /**< Buffer reutilizable de gradientes precortados */
    size_t gridGradientCapacity; /**< Capacidad reservada de gridGradients */

    uint32_t* gradientStamp;          /**< Marcas de generación para cálculo perezoso de gradiente */
    size_t gradientStampCapacity;    /**< Capacidad reservada de gradientStamp */
    uint32_t currentGradientGeneration; /**< Identificador de generación actual */

    MeshIndex* xEdges;           /**< Caché reutilizable de vértices en aristas X */
    size_t xEdgeCapacity;        /**< Capacidad reservada de xEdges */

    MeshIndex* yEdges;           /**< Caché reutilizable de vértices en aristas Y */
    size_t yEdgeCapacity;        /**< Capacidad reservada de yEdges */

    MeshIndex* zEdges;           /**< Caché reutilizable de vértices en aristas Z */
    size_t zEdgeCapacity;        /**< Capacidad reservada de zEdges */

    SDFMesherStats lastStats;    /**< Métricas de la última ejecución */
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
 * @brief Libera los buffers scratch reutilizables poseídos por SDFMesher.
 */
void SDFMesher_Free(SDFMesher* mesher);

/**
 * @brief Retorna un puntero a las métricas de la última ejecución de SDFMesher.
 */
const SDFMesherStats* SDFMesher_GetLastStats(const SDFMesher* mesher);

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
