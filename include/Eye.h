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
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum PupilShape {
    PUPIL_ROUND = 0,
    PUPIL_VERTICAL,
    PUPIL_HORIZONTAL,
    PUPIL_DIAMOND
} PupilShape;

/**
 * @struct Eye
 * @brief Representación anatómica de un ojo adjunto a una BodyPart o al Monstruo.
 */
typedef struct Eye {
    size_t bodyPartIndex; /**< Índice de la parte del cuerpo a la que está anclado el ojo */
    Vector3 offset;       /**< Posición relativa (offset 3D) respecto al centro de la BodyPart */
    Vector3 rotation;     /**< Rotación Euler 3D en grados. */
    Vector3 forward;      /**< Dirección visual exterior, equivalente al +Z local. */
    Vector3 scale;        /**< Radios reales del elipsoide ocular. */
    
    Color scleraColor;    /**< Color de la esclerótica (parte blanca/base del ojo) */
    Color pupilColor;     /**< Color de la pupila. */
    Color irisColor;      /**< Color del iris separado de la pupila. */
    float irisScale;      /**< Fracción visible del iris respecto al globo. */
    float pupilScale;     /**< Escala relativa de la pupila [0.0 - 1.0] respecto al ojo */
    float pupilAspect;    /**< Relación ancho/alto; menor que uno produce pupila vertical. */
    PupilShape pupilShape; /**< Forma geométrica de la pupila. */
    float limbalRingStrength; /**< Contraste del borde limbal [0,1]. */
    float irisRadialNoise; /**< Variación radial determinista del iris [0,1]. */
    float irisColorVariation; /**< Variación cromática radial [0,1]. */
    float scleraPigmentation; /**< Pigmentación difusa de la esclerótica [0,1]. */
    float cornealGloss; /**< Intensidad especular de la córnea [0,1]. */
    float pupilDilation; /**< Dilatación fisiológica [0,1]; 0 contraída, 1 dilatada. */
    uint32_t textureSeed; /**< Semilla estable de la apariencia ocular. */
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
