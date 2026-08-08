/**
 * @file Transform3D.h
 * @brief Módulo para transformaciones 3D (posición, rotación Euler y escala).
 * @author Monster Engine Team
 * @date 2026
 */

#ifndef MONSTER_TRANSFORM_3D_H
#define MONSTER_TRANSFORM_3D_H

#include "Vector.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @struct Transform3D
 * @brief Representa la posición, rotación en grados Euler (X, Y, Z) y escala en 3D.
 */
typedef struct Transform3D {
    Vector3 position; /**< Posición en el espacio 3D */
    Vector3 rotation; /**< Rotación en grados Euler (X, Y, Z) */
    Vector3 scale;    /**< Escala en cada eje (X, Y, Z) */
} Transform3D;

/**
 * @brief Retorna una transformación identidad (pos=0, rot=0, escala=1).
 */
Transform3D Transform3D_Identity(void);

/**
 * @brief Crea una estructura Transform3D con valores dados.
 */
Transform3D Transform3D_Create(Vector3 position, Vector3 rotationDegrees, Vector3 scale);

/**
 * @brief Rota un vector 3D aplicando rotaciones Euler (orden X, luego Y, luego Z).
 */
Vector3 Transform3D_RotateVector(Vector3 rotationDegrees, Vector3 vector);

/**
 * @brief Inversa de la rotación Euler (orden -Z, luego -Y, luego -X).
 */
Vector3 Transform3D_InverseRotateVector(Vector3 rotationDegrees, Vector3 vector);

/**
 * @brief Transforma un punto local a espacio mundo (Escala -> Rotación -> Traslación).
 */
Vector3 Transform3D_LocalToWorldPoint(Transform3D transform, Vector3 localPoint);

/**
 * @brief Transforma un punto del espacio mundo a espacio local (Traslación Inversa -> Rotación Inversa -> Escala Inversa).
 */
Vector3 Transform3D_WorldToLocalPoint(Transform3D transform, Vector3 worldPoint);

#ifdef __cplusplus
}
#endif

#endif // MONSTER_TRANSFORM_3D_H
