/**
 * @file Mouth.h
 * @brief Módulo para definir la boca/mandíbula de los monstruos.
 * @author Monster Engine Team
 * @date 2026
 */

#ifndef MONSTER_MOUTH_H
#define MONSTER_MOUTH_H

#include "Vector.h"
#include "Color.h"
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @struct Mouth
 * @brief Estructura que representa la apertura bucal de un monstruo.
 */
typedef struct Mouth {
    size_t bodyPartIndex; /**< Índice de la parte del cuerpo anfitriona (normalmente la cabeza) */
    Vector3 offset;       /**< Posición relativa (offset 3D) respecto al centro de la BodyPart */
    Vector3 rotation;     /**< Rotación Euler 3D (pitch, yaw, roll) */
    Vector3 scale;        /**< Dimensiones (ancho, alto/apertura, profundidad) */
    
    Color insideColor;    /**< Color interior de la cavidad bucal / garganta */
    Color lipColor;       /**< Color del borde bucal / labios */
    float openFactor;     /**< Factor de apertura bucal [0.0 = cerrada, 1.0 = totalmente abierta] */
} Mouth;

/**
 * @brief Crea una estructura Mouth con datos parametrizados.
 * @param bodyPartIndex Índice de la parte del cuerpo anfitriona.
 * @param offset Posición relativa.
 * @param scale Escala (ancho, alto, profundidad).
 * @param insideColor Color interior de la boca.
 * @param lipColor Color exterior de los labios.
 * @return Estructura Mouth inicializada.
 */
Mouth Mouth_Create(size_t bodyPartIndex, Vector3 offset, Vector3 scale, Color insideColor, Color lipColor);

#ifdef __cplusplus
}
#endif

#endif // MONSTER_MOUTH_H
