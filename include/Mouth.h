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

/** Receta estructural del elemento oral inferior. */
typedef enum MouthShape {
    MOUTH_SHAPE_MANDIBLE = 0, /**< Mandíbula con rama posterior y masa muscular. */
    MOUTH_SHAPE_LOWER_BEAK    /**< Pico inferior ahusado, sin masa de mejilla mamífera. */
} MouthShape;

/**
 * @struct Mouth
 * @brief Fenotipo anatómico de una boca y su mandíbula articulada.
 */
typedef struct Mouth {
    size_t bodyPartIndex; /**< Índice de la parte del cuerpo anfitriona (normalmente la cabeza) */
    Vector3 offset;       /**< Posición relativa (offset 3D) respecto al centro de la BodyPart */
    Vector3 rotation;     /**< Rotación Euler 3D (pitch, yaw, roll) */
    Vector3 scale;        /**< Dimensiones (ancho, alto/apertura, profundidad) */
    
    Color insideColor;    /**< Color interior de la cavidad bucal / garganta */
    Color lipColor;       /**< Reservado para una futura banda de borde oral */
    float openFactor;     /**< Factor de apertura bucal [0.0 = cerrada, 1.0 = totalmente abierta] */
    MouthShape shape;     /**< Modelo anatómico de la pieza oral inferior */

    float slitThickness;  /**< Grosor de la hendidura oral */
    float slitSoftness;   /**< Suavidad del borde de la hendidura */
    float cornerRadius;   /**< Radio anatómico de las comisuras */

    Vector3 jawPivot;     /**< Pivote local posterior de la mandíbula */
    float jawLength;      /**< Longitud de la mandíbula hacia delante */
    float jawWidth;       /**< Anchura posterior de la mandíbula */
    float jawThickness;   /**< Grosor vertical de la mandíbula */
    float jawRearMass;    /**< Volumen posterior que envuelve el cóndilo */
    float jawMuscle;      /**< Volumen de tejido muscular inferior */
    float maxJawAngle;    /**< Ángulo máximo de apertura en grados */
    float hingeRadius;    /**< Radio del tejido blando de la bisagra */
    float throatRadius;   /**< Radio de continuidad hacia la garganta */

    Vector3 cranium;      /**< Escala relativa del volumen craneal */
    Vector3 snout;        /**< Escala relativa del hocico */
    Vector3 cheeks;       /**< Escala relativa de las mejillas */
    Vector3 brows;        /**< Escala relativa de los arcos supraorbitales */
} Mouth;

/**
 * @brief Crea una estructura Mouth con datos parametrizados.
 * @param bodyPartIndex Índice de la parte del cuerpo anfitriona.
 * @param offset Posición relativa.
 * @param scale Escala (ancho, alto máximo de apertura, profundidad).
 * @param insideColor Color interior de la boca.
 * @param lipColor Color exterior de los labios.
 * @return Estructura Mouth inicializada.
 */
Mouth Mouth_Create(size_t bodyPartIndex, Vector3 offset, Vector3 scale, Color insideColor, Color lipColor);

/**
 * @brief Ajusta el factor de apertura de la boca limitándolo al rango [0,1].
 * @param mouth Puntero a la estructura Mouth.
 * @param factor Factor de apertura deseado.
 */
void Mouth_SetOpenFactor(Mouth* mouth, float factor);

/** @brief Normaliza todos los parámetros anatómicos a un estado válido. */
void Mouth_Normalize(Mouth* mouth);

/** @brief Convierte el factor normalizado en el ángulo de mandíbula. */
float Mouth_GetJawAngle(const Mouth* mouth);

#ifdef __cplusplus
}
#endif

#endif // MONSTER_MOUTH_H
