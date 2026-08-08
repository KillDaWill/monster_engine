/**
 * @file SDFPrimitives.h
 * @brief Primitivas geométricas continuas representadas mediante Signed Distance Fields (SDF).
 * @author Monster Engine Team
 * @date 2026
 */

#ifndef MONSTER_SDF_PRIMITIVES_H
#define MONSTER_SDF_PRIMITIVES_H

#include "Vector.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief SDF de una esfera centrada en el origen.
 */
float SDF_Sphere(Vector3 point, float radius);

/**
 * @brief SDF aproximado de un elipsoide centrado en el origen con radios (r.x, r.y, r.z).
 */
float SDF_Ellipsoid(Vector3 point, Vector3 radii);

/**
 * @brief SDF de una cápsula (cilindro con tapas esféricas) entre los puntos a y b.
 */
float SDF_Capsule(Vector3 point, Vector3 a, Vector3 b, float radius);

/**
 * @brief SDF de un cono redondeado / cápsula cónica entre los puntos a y b con radios r1 y r2.
 */
float SDF_RoundCone(Vector3 point, Vector3 a, Vector3 b, float r1, float r2);

/**
 * @brief SDF de una caja 3D orientada a los ejes centrada en el origen con mitades de dimensión b.
 */
float SDF_Box(Vector3 point, Vector3 halfExtents);

#ifdef __cplusplus
}
#endif

#endif // MONSTER_SDF_PRIMITIVES_H
