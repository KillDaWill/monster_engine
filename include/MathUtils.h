/**
 * @file MathUtils.h
 * @brief Módulo de utilidades matemáticas escalares genéricas para Monster Engine.
 * @author Monster Engine Team
 * @date 2026
 */

#ifndef MONSTER_MATH_UTILS_H
#define MONSTER_MATH_UTILS_H

#include <math.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

/**
 * @brief Restringe un valor flotante a un rango [minimum, maximum].
 */
float Math_Clamp(float value, float minimum, float maximum);

/**
 * @brief Restringe un valor flotante al rango [0.0f, 1.0f].
 */
float Math_Clamp01(float value);

/**
 * @brief Interpolación lineal escalar entre a y b mediante un factor t.
 */
float Math_Lerp(float a, float b, float t);

/**
 * @brief Convierte grados a radianes.
 */
float Math_DegToRad(float degrees);

/**
 * @brief Convierte radianes a grados.
 */
float Math_RadToDeg(float radians);

/**
 * @brief Retorna el valor mínimo entre dos flotantes.
 */
float Math_Min(float a, float b);

/**
 * @brief Retorna el valor máximo entre dos flotantes.
 */
float Math_Max(float a, float b);

/**
 * @brief Calcula una nueva capacidad para arreglos dinámicos validando desbordamiento de memoria.
 */
bool Math_GrowCapacity(size_t currentCapacity, size_t minimumRequired, size_t elementSize, size_t* outNewCapacity);

#ifdef __cplusplus
}
#endif

#endif // MONSTER_MATH_UTILS_H
