/**
 * @file BodyPart.h
 * @brief Módulo para representar segmentos o partes del cuerpo de los monstruos.
 * @author Monster Engine Team
 * @date 2026
 */

#ifndef MONSTER_BODY_PART_H
#define MONSTER_BODY_PART_H

#include "Vector.h"
#include "Color.h"
#include "Trait.h"
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
struct Monster;

/**
 * @struct ColorIndex
 * @brief Índice de referencia a la paleta de colores.
 */
typedef struct {
    int index; /**< Índice entero en la paleta */
} ColorIndex;

/**
 * @struct BodyPart
 * @brief Estructura que representa una sección individual de la anatomía de un monstruo.
 */
typedef struct BodyPart {
    Vector3 position;       /**< Posición física actual en espacio 3D */
    Vector3 oldPosition;    /**< Posición física del tick anterior (usada para lerp) */
    Vector3 positionRender; /**< Posición suavizada para dibujado en frame */

    float widthRender;       /**< Ancho suavizado de renderizado */
    float lengthRender;      /**< Largo suavizado de renderizado */
    float heightRender;      /**< Alto suavizado de renderizado */
    float groundOffsetRender;/**< Desplazamiento sobre el suelo de renderizado */

    float width;        /**< Ancho base */
    float length;       /**< Largo base */
    float height;       /**< Alto base */
    float groundOffset; /**< Desplazamiento respecto al suelo */

    ColorIndex color;      /**< Índice de color primario */
    ColorIndex bellyColor; /**< Índice de color secundario / vientre */
    float bellyThreshold;  /**< Umbral de transición para la zona del vientre */

    struct Trait** traits; /**< Lista dinámica de punteros a Traits adjuntos a esta parte */
    size_t traitCount;     /**< Cantidad actual de traits */
    size_t traitCapacity;  /**< Capacidad de memoria asignada para traits */
} BodyPart;

/**
 * @brief Crea una parte del cuerpo parametrizada.
 * @param x Posición X.
 * @param y Posición Y.
 * @param z Posición Z.
 * @param width Ancho base.
 * @param length Largo base.
 * @param height Alto base.
 * @param groundOffset Desplazamiento sobre el suelo.
 * @return Estructura BodyPart inicializada.
 */
BodyPart BodyPart_Create(float x, float y, float z, float width, float length, float height, float groundOffset);

/**
 * @brief Crea una parte del cuerpo con dimensiones por defecto.
 * @return BodyPart inicializada por defecto.
 */
BodyPart BodyPart_CreateDefault(void);

/**
 * @brief Libera la memoria asignada dinámicamente para los Traits de la parte del cuerpo.
 * @param part Puntero a la estructura BodyPart.
 */
void BodyPart_Free(BodyPart* part);

/**
 * @brief Actualiza la física y rasgos de la parte del cuerpo en cada tick del juego.
 * @param part Puntero a la parte del cuerpo.
 * @param monster Puntero al monstruo propietario.
 * @param index Índice de esta parte dentro del monstruo.
 * @param diff Delta de tiempo desde la última actualización.
 */
void BodyPart_Update(BodyPart* part, struct Monster* monster, int index, double diff);

/**
 * @brief Calcula las transformaciones e interpolaciones para el renderizado.
 * @param part Puntero a la parte del cuerpo.
 * @param monster Puntero al monstruo.
 * @param index Índice de la parte del cuerpo.
 * @param diff Delta de tiempo.
 * @param renderPercent Porcentaje de interpolación (0.0 a 1.0) entre ticks.
 */
void BodyPart_RenderUpdate(BodyPart* part, struct Monster* monster, int index, double diff, double renderPercent);

/**
 * @brief Ejecuta el renderUpdate de todos los Traits adjuntos a esta parte.
 * @param part Puntero a la parte del cuerpo.
 * @param monster Puntero al monstruo.
 * @param index Índice de la parte.
 * @param diff Delta de tiempo.
 * @param renderPercent Factor de interpolación.
 */
void BodyPart_RenderUpdateTraits(BodyPart* part, struct Monster* monster, int index, double diff, double renderPercent);

/**
 * @brief Adjunta un Trait a la parte del cuerpo.
 * @param part Puntero a la parte del cuerpo.
 * @param trait Puntero al trait a añadir.
 * @return true si se añadió con éxito, false en caso de fallo de memoria.
 */
bool BodyPart_AddTrait(BodyPart* part, struct Trait* trait);

/**
 * @brief Busca y retorna un Trait por su tipo.
 * @param part Puntero a la parte del cuerpo.
 * @param traitType Tipo de Trait a buscar.
 * @return Puntero al Trait encontrado o NULL si no existe.
 */
struct Trait* BodyPart_GetTrait(const BodyPart* part, TraitType traitType);

/**
 * @brief Verifica si la parte del cuerpo contiene un tipo de Trait específico.
 * @param part Puntero a la parte del cuerpo.
 * @param traitType Tipo de Trait.
 * @return true si lo contiene, false en caso contrario.
 */
bool BodyPart_ContainsTrait(const BodyPart* part, TraitType traitType);

#ifdef __cplusplus
}
#endif

#endif // MONSTER_BODY_PART_H
