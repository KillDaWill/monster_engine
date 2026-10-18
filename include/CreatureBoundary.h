/** @file CreatureBoundary.h
 * @brief Contacto articulado y contención conservadora contra paredes verticales.
 */
#ifndef CREATURE_BOUNDARY_H
#define CREATURE_BOUNDARY_H
#include "AnatomyDeformer.h"
#include "MonsterAnimation.h"
#include "MonsterSDF.h"
/** @brief Soportes de skinning en reposo; inicializar con {0}. Contienen mezclas convexas. */
typedef struct CreatureBoundary {
    AABB3D support[SKELETON_MAX_JOINTS];
    AnatomyId ids[SKELETON_MAX_JOINTS];
    float bend[SKELETON_MAX_JOINTS];
    float contactRadius[SKELETON_MAX_JOINTS];
    size_t contactTrials;
    Quaternion inverseRest[SKELETON_MAX_JOINTS];
    SkeletonPose rest;
    uint64_t fingerprint;
    size_t jointCount;
    float sampleSpacing;
    AABB3D fieldBounds;
} CreatureBoundary;
/** @brief Compila soportes conservadores por influencia de la malla CPU. */
bool CreatureBoundary_BindMesh(CreatureBoundary *, const AnatomyDeformer *, const Skeleton *);
/** @brief Añade geometría rígida (cabeza, ojos, mandíbula) a su articulación. */
bool CreatureBoundary_AddRigid(CreatureBoundary *, const Mesh *, int joint);
/** @brief Compila una envolvente conservadora del dominio de skinning GPU actual.
 * @param sampleSpacing Espaciado de extracción MC; incluye el error de interpolación. */
bool CreatureBoundary_BindSDF(CreatureBoundary *, const struct Monster *, const MonsterSDF *,
                              float sampleSpacing);
/** @brief Envolvente mundial incluyendo yaw, escala, recentrado y margen exterior. */
AABB3D CreatureBoundary_Bounds(const CreatureBoundary *, const MonsterAnimation *, Vector3 position,
                               float yaw, float scale, float padding);
/** @brief Radio de giro conservador de los soportes reales, sin esquinas ficticias
 * de su AABB agregada. @param scale Escala del actor. @param padding Margen exterior.
 * @return Máxima distancia horizontal al origen locomotor. */
float CreatureBoundary_TurningRadius(const CreatureBoundary *, const MonsterAnimation *,
                                     float scale, float padding);
/** @brief Flexiona sin alterar longitudes ni límites articulares; resuelve contacto residual
 * mediante traslación mínima y par sobre la raíz si hace falta para caber. Devuelve false si la
 * criatura no cabe o los datos no son válidos.
 * @param halfSize Semiancho de la plataforma. @param dt Paso de simulación en segundos. */
bool CreatureBoundary_Solve(CreatureBoundary *, MonsterAnimation *, Vector3 *position, float *yaw,
                            float scale, float halfSize, float padding, float dt);
#endif
