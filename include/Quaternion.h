/** @file Quaternion.h
 * @brief Rotaciones unitarias; composición a*b aplica b y después a. Ángulos en radianes.
 */
#ifndef MONSTER_QUATERNION_H
#define MONSTER_QUATERNION_H
#include "Vector.h"
typedef struct Quaternion { float x, y, z, w; } Quaternion;
/** @brief Rotación identidad. */
Quaternion Quat_Identity(void);
/** @brief Normaliza; entradas nulas/no finitas producen identidad. */
Quaternion Quat_Normalize(Quaternion q);
/** @brief Producto de Hamilton. */
Quaternion Quat_Multiply(Quaternion a, Quaternion b);
/** @brief Rotación alrededor de un eje; eje nulo produce identidad. */
Quaternion Quat_FromAxisAngle(Vector3 axis, float angle);
/** @brief Rotación mínima entre direcciones, incluidos vectores opuestos. */
Quaternion Quat_FromTo(Vector3 from, Vector3 to);
/** @brief Rota un vector sin alterar su longitud. */
Vector3 Quat_RotateVector(Quaternion q, Vector3 v);
/** @brief Interpolación esférica por el arco corto, t acotado a [0,1]. */
Quaternion Quat_Slerp(Quaternion a, Quaternion b, float t);
/** @brief Conjugado. */
Quaternion Quat_Conjugate(Quaternion q);
/** @brief Inversa; entrada degenerada produce identidad. */
Quaternion Quat_Inverse(Quaternion q);
#endif
