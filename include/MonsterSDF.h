/**
 * @file MonsterSDF.h
 * @brief Representación geométrica implícita compilada de un monstruo (Signed Distance Fields).
 * @author Monster Engine Team
 * @date 2026
 */

#ifndef MONSTER_SDF_H
#define MONSTER_SDF_H

#include "Vector.h"
#include "Color.h"
#include "Transform3D.h"
#include "AABB.h"
#include "SDFOperations.h"
#include "SDFSampling.h"
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Monster;

/**
 * @struct MonsterSDFConfig
 * @brief Parámetros de configuración para la mezcla y evaluación SDF del monstruo.
 */
typedef struct MonsterSDFConfig {
    float bodySmoothness;          /**< Factor k de mezcla suave entre partes del cuerpo */
    float connectionSmoothness;    /**< Factor k de mezcla suave para conectores entre nodos */
    float mouthSmoothness;         /**< Factor k de sustracción suave para cavidades bucales */
    float connectionRadiusFactor;  /**< Escala de radio para los conectores cónicos */
    float boundsPadding;           /**< Margen extra asignado al Bounding Box (AABB) */
} MonsterSDFConfig;

/**
 * @struct MonsterSDFBodyPart
 * @brief Snapshot compilado de una parte del cuerpo con invariantes geométricas precálculadas.
 */
typedef struct MonsterSDFBodyPart {
    Vector3 center;
    Vector3 radii;
    Vector3 invRadii;
    Vector3 invRadiiSquared;
    float minRadius;
    Color color;
} MonsterSDFBodyPart;

/**
 * @struct MonsterSDFConnector
 * @brief Snapshot compilado de un conector entre partes con invariantes precálculadas.
 */
typedef struct MonsterSDFConnector {
    Vector3 a;
    Vector3 b;
    Vector3 ba;
    float invBaLengthSquared;
    float r1;
    float r2;
    float radiusDelta;
    Color color;
} MonsterSDFConnector;

/**
 * @struct MonsterSDFMouth
 * @brief Snapshot compilado de una cavidad bucal con rotación matricial e invariantes precálculadas.
 */
typedef struct MonsterSDFMouth {
    Vector3 center;
    RotationBasis3D inverseRotation;
    Vector3 entranceCenterLocal;
    Vector3 entranceHalfExtents;
    Vector3 cavityCenterLocal;
    Vector3 cavityRadii;
    Color insideColor;
    float entranceToCavitySmoothness;
    AABB3D influenceBounds;
} MonsterSDFMouth;

/**
 * @struct MonsterSDF
 * @brief Estructura de geometría SDF compilada (desacoplada e independiente tras Build).
 * @note No es segura para hilos: no invocar MonsterSDF_Build mientras otra hebra
 *       lee la misma instancia (Build reutiliza los buffers internos en sitio).
 */
typedef struct MonsterSDF {
    MonsterSDFBodyPart* bodyParts;
    size_t bodyPartCount;
    size_t bodyPartCapacity; /**< Capacidad reservada del buffer bodyParts */

    MonsterSDFConnector* connectors;
    size_t connectorCount;
    size_t connectorCapacity; /**< Capacidad reservada del buffer connectors */

    MonsterSDFMouth* mouths;
    size_t mouthCount;
    size_t mouthCapacity; /**< Capacidad reservada del buffer mouths */

    MonsterSDFConfig config;
    AABB3D bounds;
} MonsterSDF;

/**
 * @brief Retorna la configuración por defecto para la evaluación SDF.
 */
MonsterSDFConfig MonsterSDF_DefaultConfig(void);

/**
 * @brief Crea una estructura MonsterSDF vacía.
 */
MonsterSDF MonsterSDF_Create(void);

/**
 * @brief Compila/Snapshot la geometría de un Monster en un objeto MonsterSDF.
 *
 * Reutiliza los buffers internos de builds previos (crecimiento únicamente),
 * por lo que puede invocarse repetidamente sobre la misma instancia sin
 * fragmentar memoria. En caso de fallo de asignación la instancia queda vacía.
 * @return true si se construyó exitosamente, false si ocurrió fallo de asignación.
 */
bool MonsterSDF_Build(MonsterSDF* sdf, const struct Monster* monster, MonsterSDFConfig config);

/**
 * @brief Libera los recursos de memoria asignados por MonsterSDF_Build.
 */
void MonsterSDF_Free(MonsterSDF* sdf);

/**
 * @brief Evalúa la distancia signed, color y material en cualquier punto 3D del espacio.
 */
SDFSample MonsterSDF_Evaluate(const MonsterSDF* sdf, Vector3 point);

/**
 * @brief Wrapper de evaluación completa compatible con la firma SDFEvaluateFn.
 */
SDFSample MonsterSDF_EvaluateWrapper(const void* context, Vector3 point);

/**
 * @brief Evalúa única y exclusivamente la distancia escalar en cualquier punto 3D del espacio.
 */
float MonsterSDF_EvaluateDistance(const MonsterSDF* sdf, Vector3 point);

/**
 * @brief Wrapper de evaluación de sólo distancia escalar compatible con la firma SDFDistanceFn.
 */
float MonsterSDF_EvaluateDistanceWrapper(const void* context, Vector3 point);

/**
 * @brief Retorna el Bounding Box (AABB3D) compilado.
 */
AABB3D MonsterSDF_GetBounds(const MonsterSDF* sdf);

/**
 * @brief Wrapper de bounds compatible con la firma SDFBoundsFn.
 */
AABB3D MonsterSDF_GetBoundsWrapper(const void* context);

/**
 * @brief Construye y retorna la estructura agnóstica SDFField vinculada a esta instancia.
 */
SDFField MonsterSDF_GetField(const MonsterSDF* sdf);

#ifdef __cplusplus
}
#endif

#endif // MONSTER_SDF_H
