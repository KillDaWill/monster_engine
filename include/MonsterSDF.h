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
 * @brief Snapshot compilado de una parte del cuerpo.
 */
typedef struct MonsterSDFBodyPart {
    Vector3 center;
    Vector3 radii;
    Color color;
} MonsterSDFBodyPart;

/**
 * @struct MonsterSDFConnector
 * @brief Snapshot compilado de un conector entre partes.
 */
typedef struct MonsterSDFConnector {
    Vector3 a;
    Vector3 b;
    float r1;
    float r2;
    Color color;
} MonsterSDFConnector;

/**
 * @struct MonsterSDFMouth
 * @brief Snapshot compilado de una cavidad bucal.
 */
typedef struct MonsterSDFMouth {
    Transform3D transform;
    Vector3 radii;
    Color insideColor;
    Color lipColor;
    float openFactor;
} MonsterSDFMouth;

/**
 * @struct MonsterSDF
 * @brief Estructura de geometría SDF compilada (desacoplada e independiente tras Build).
 */
typedef struct MonsterSDF {
    MonsterSDFBodyPart* bodyParts;
    size_t bodyPartCount;

    MonsterSDFConnector* connectors;
    size_t connectorCount;

    MonsterSDFMouth* mouths;
    size_t mouthCount;

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
 * @brief Wrapper de evaluación compatible con la firma SDFEvaluateFn.
 */
SDFSample MonsterSDF_EvaluateWrapper(const void* context, Vector3 point);

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
