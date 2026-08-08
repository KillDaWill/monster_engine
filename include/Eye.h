/**
 * @file Eye.h
 * @brief Módulo para definir ojos individuales en las criaturas.
 * @author Monster Engine Team
 * @date 2026
 */

#ifndef MONSTER_EYE_H
#define MONSTER_EYE_H

#include "Vector.h"
#include "Color.h"
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @struct Eye
 * @brief Representación anatómica de un ojo adjunto a una BodyPart o al Monstruo.
 */
typedef struct Eye {
    size_t bodyPartIndex; /**< Índice de la parte del cuerpo a la que está anclado el ojo */
    Vector3 offset;       /**< Posición relativa (offset 3D) respecto al centro de la BodyPart */
    Vector3 rotation;     /**< Rotación Euler 3D (pitch, yaw, roll) en grados o radianes */
    Vector3 scale;        /**< Escala 3D del ojo (ancho, alto, profundidad de la esclerótica/pupila) */
    
    Color scleraColor;    /**< Color de la esclerótica (parte blanca/base del ojo) */
    Color pupilColor;     /**< Color de la pupila / iris */
    float pupilScale;     /**< Escala relativa de la pupila [0.0 - 1.0] respecto al ojo */
} Eye;

/**
 * @brief Crea una estructura Eye con valores por defecto.
 * @param bodyPartIndex Índice de la parte del cuerpo anfitriona.
 * @param offset Posición relativa (X, Y, Z).
 * @param scale Escala (ancho, alto, profundidad).
 * @param scleraColor Color base del ojo.
 * @param pupilColor Color de la pupila.
 * @return Estructura Eye inicializada.
 */
Eye Eye_Create(size_t bodyPartIndex, Vector3 offset, Vector3 scale, Color scleraColor, Color pupilColor);

#ifdef __cplusplus
}
#endif

#endif // MONSTER_EYE_H
