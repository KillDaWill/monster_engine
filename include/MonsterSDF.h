/**
 * @file MonsterSDF.h
 * @brief Representación geométrica implícita de un monstruo usando Signed Distance Fields (SDF).
 * @author Monster Engine Team
 * @date 2026
 */

#ifndef MONSTER_SDF_H
#define MONSTER_SDF_H

#include "Monster.h"
#include "SDFOperations.h"
#include "MonsterQueries.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @struct MonsterSDFConfig
 * @brief Parámetros de configuración para la mezcla y evaluación SDF del monstruo.
 */
typedef struct MonsterSDFConfig {
    float bodySmoothness;          /**< Factor k de mezcla suave entre partes del cuerpo */
    float connectionSmoothness;    /**< Factor k de mezcla suave para conectores entre nodos */
    float mouthSmoothness;         /**< Factor k de sustracción suave para cavidades bucales */
    float connectionRadiusFactor;  /**< Escala de radio para los conectores cilíndricos/cónicos */
    float boundsPadding;           /**< Margen extra asignado al Bounding Box (AABB) */
} MonsterSDFConfig;

/**
 * @struct MonsterSDF
 * @brief Campo SDF que vincula a una entidad Monster con la configuración de mezcla.
 */
typedef struct MonsterSDF {
    const Monster* monster; /**< Referencia al monstruo (solo lectura) */
    MonsterSDFConfig config; /**< Configuración del campo */
} MonsterSDF;

/**
 * @brief Retorna la configuración por defecto para la evaluación SDF.
 */
MonsterSDFConfig MonsterSDF_DefaultConfig(void);

/**
 * @brief Crea un objeto MonsterSDF asociado a un monstruo.
 */
MonsterSDF MonsterSDF_Create(const Monster* monster);

/**
 * @brief Configura los parámetros de mezcla SDF.
 */
void MonsterSDF_SetConfig(MonsterSDF* sdf, MonsterSDFConfig config);

/**
 * @brief Evalúa la distancia signed, color y material en cualquier punto 3D del espacio.
 */
SDFSample MonsterSDF_Evaluate(const MonsterSDF* sdf, Vector3 point);

/**
 * @brief Función wrapper estática compatible con la firma SDFEvaluateFn (context = const MonsterSDF*).
 */
SDFSample MonsterSDF_EvaluateWrapper(const void* context, Vector3 point);

/**
 * @brief Calcula el Bounding Box (AABB3D) global que engloba la superficie implícita del monstruo.
 */
AABB3D MonsterSDF_GetBounds(const MonsterSDF* sdf);

#ifdef __cplusplus
}
#endif

#endif // MONSTER_SDF_H
