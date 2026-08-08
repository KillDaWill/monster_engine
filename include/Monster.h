/**
 * @file Monster.h
 * @brief Módulo principal que define la entidad Monster, su lógica, partes del cuerpo y rasgos.
 * @author Monster Engine Team
 * @date 2026
 */

#ifndef MONSTER_H
#define MONSTER_H

#include "Vector.h"
#include "ColorPalette.h"
#include "BodyPart.h"
#include "Eye.h"
#include "Mouth.h"
#include "MonsterQueries.h"
#include "Trait.h"
#include "VisualTrait.h"
#include "WorldInterface.h"
#include "MonsterBehavior.h"
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
struct MonsterMeta;



/**
 * @struct Monster
 * @brief Entidad principal que engloba paleta, partes anatómicas, rasgos y comportamiento.
 */
typedef struct Monster {
    ColorPalette colorPalette; /**< Paleta de colores asociada al monstruo */

    BodyPart* bodyParts;     /**< Arreglo dinámico de partes del cuerpo */
    size_t bodyPartCount;    /**< Cantidad actual de partes */
    size_t bodyPartCapacity; /**< Capacidad reservada de partes */

    Eye* eyes;               /**< Arreglo dinámico de ojos adjuntos a la criatura */
    size_t eyeCount;         /**< Cantidad actual de ojos */
    size_t eyeCapacity;      /**< Capacidad reservada de ojos */

    Mouth* mouths;           /**< Arreglo dinámico de bocas / cavidades bucales */
    size_t mouthCount;       /**< Cantidad actual de bocas */
    size_t mouthCapacity;    /**< Capacidad reservada de bocas */

    struct Trait** traits;     /**< Arreglo dinámico de rasgos generales */
    size_t traitCount;        /**< Cantidad de rasgos generales */
    size_t traitCapacity;     /**< Capacidad de rasgos generales */

    struct Trait** combatTraits;  /**< Arreglo dinámico de rasgos de combate */
    size_t combatTraitCount;     /**< Cantidad de rasgos de combate */
    size_t combatTraitCapacity;  /**< Capacidad de rasgos de combate */

    struct VisualTrait** visualTraits; /**< Arreglo dinámico de rasgos visuales de render */
    size_t visualTraitCount;          /**< Cantidad de rasgos visuales */
    size_t visualTraitCapacity;       /**< Capacidad de rasgos visuales */

    struct MonsterBehavior* behavior; /**< Controlador de comportamiento del monstruo */
    struct MonsterMeta* meta;         /**< Metadatos asociados al monstruo */
    struct World* world;              /**< Referencia al mundo/mapa */

    float angle;       /**< Orientación/ángulo actual en radianes */
    float updateSpeed; /**< Multiplicador de velocidad de simulación */
    int id;            /**< Identificador único del monstruo */
} Monster;

/**
 * @brief Crea una nueva estructura Monster.
 * @return Estructura Monster inicializada a valores base.
 */
Monster Monster_Create(void);

/**
 * @brief Inicializa las partes del cuerpo y rasgos iniciales por defecto en el monstruo.
 * @param monster Puntero al monstruo.
 */
void Monster_Init(Monster* monster);

/**
 * @brief Libera la memoria asignada dinámicamente para el monstruo y sus sub-estructuras.
 * @param monster Puntero al monstruo.
 */
void Monster_Free(Monster* monster);

/**
 * @brief Clona un monstruo realizando copias profundas de sus arreglos dinámicos.
 * @param source Puntero al monstruo origen.
 * @return Nueva instancia clonada de Monster.
 */
Monster Monster_Clone(const Monster* source);

/**
 * @brief Actualiza la lógica interna y el comportamiento del monstruo.
 * @param monster Puntero al monstruo.
 * @param diff Delta de tiempo.
 */
void Monster_Update(Monster* monster, double diff);

/**
 * @brief Prepara el estado de interpolación visual para el cuadro de render actual.
 * @param monster Puntero al monstruo.
 * @param diff Delta de tiempo.
 * @param renderPercent Porcentaje entre ticks [0.0 - 1.0].
 */
void Monster_RenderUpdate(Monster* monster, double diff, double renderPercent);

/**
 * @brief Consulta la altura del terreno en las coordenadas especificadas del mundo.
 * @param monster Puntero al monstruo.
 * @param worldX Coordenada X global.
 * @param worldZ Coordenada Z global.
 * @return Altura Y del terreno.
 */
float Monster_GetWorldHeight(Monster* monster, float worldX, float worldZ);

/**
 * @brief Calcula el centro de masa geométrico promediando todas las partes del cuerpo.
 * @param monster Puntero al monstruo constante.
 * @return Vector3 con la posición del centro.
 */
Vector3 Monster_GetCenter(const Monster* monster);

/**
 * @brief Calcula el centro excluyendo las partes que contengan rasgos de ondulación (WiggleTrait).
 * @param monster Puntero al monstruo constante.
 * @return Vector3 con la posición calculada.
 */
Vector3 Monster_GetNonWiggleCenter(const Monster* monster);

/**
 * @brief Obtiene el ancho interpolado continuo a lo largo del cuerpo dado un índice flotante.
 * @param monster Puntero al monstruo.
 * @param index Índice flotante (ej. 1.5).
 * @return Ancho interpolado.
 */
float Monster_GetPartWidth(const Monster* monster, float index);

/**
 * @brief Obtiene el alto interpolado continuo a lo largo del cuerpo dado un índice flotante.
 * @param monster Puntero al monstruo.
 * @param index Índice flotante.
 * @return Alto interpolado.
 */
float Monster_GetPartHeight(const Monster* monster, float index);

/**
 * @brief Retorna el vector de dirección basado en la parte actual y la anterior.
 * @param monster Puntero al monstruo.
 * @param index Índice de la parte.
 * @return Vector3 con la dirección.
 */
Vector3 Monster_GetDirection(const Monster* monster, int index);

/**
 * @brief Retorna el vector de dirección basado en la parte actual y la posterior.
 * @param monster Puntero al monstruo.
 * @param index Índice de la parte.
 * @return Vector3 de dirección.
 */
Vector3 Monster_GetDirectionFromNextPart(const Monster* monster, int index);

/**
 * @brief Obtiene la posición exacta en el cuerpo según un porcentaje flotante de su longitud total [0.0 - 1.0].
 * @param monster Puntero al monstruo constante.
 * @param percent Porcentaje de longitud.
 * @return Vector3 con la posición 3D.
 */
Vector3 Monster_GetPosition(const Monster* monster, double percent);

/**
 * @brief Retorna la longitud total acumulada de todas las partes del cuerpo.
 * @param monster Puntero al monstruo.
 * @return Longitud flotante acumulada.
 */
float Monster_GetTotalLength(const Monster* monster);

/**
 * @brief Obtiene un objeto Color de la paleta del monstruo usando un índice.
 * @param monster Puntero al monstruo.
 * @param index Índice entero en la paleta.
 * @return Color correspondiente.
 */
Color Monster_GetColorFromIndex(const Monster* monster, int index);

/**
 * @brief Obtiene un Color usando la estructura ColorIndex.
 * @param monster Puntero al monstruo.
 * @param index Estructura ColorIndex.
 * @return Color correspondiente.
 */
Color Monster_GetColorFromIndexStruct(const Monster* monster, ColorIndex index);

/**
 * @brief Calcula la caja envolvente (AABB) básica basada en los nodos de las partes del cuerpo.
 * @param monster Puntero al monstruo.
 * @param dst Estructura AABB3D destino.
 * @return AABB3D calculada.
 */
AABB3D Monster_GetBoundingBox(const Monster* monster, AABB3D dst);

/**
 * @brief Añade un nuevo ojo parametrizado al monstruo.
 * @param monster Puntero al monstruo.
 * @param eye Estructura Eye a añadir.
 * @return true si se añadió correctamente, false en caso contrario.
 */
bool Monster_AddEye(Monster* monster, Eye eye);

/**
 * @brief Obtiene un ojo dado su índice en la lista de ojos del monstruo.
 * @param monster Puntero constante al monstruo.
 * @param index Índice del ojo.
 * @return Puntero a la estructura Eye o NULL si está fuera de rango.
 */
Eye* Monster_GetEye(Monster* monster, size_t index);

/**
 * @brief Elimina todos los ojos almacenados en el monstruo.
 * @param monster Puntero al monstruo.
 */
void Monster_ClearEyes(Monster* monster);

/**
 * @brief Añade una boca parametrizada al monstruo.
 * @param monster Puntero al monstruo.
 * @param mouth Estructura Mouth a añadir.
 * @return true si se añadió correctamente, false en caso contrario.
 */
bool Monster_AddMouth(Monster* monster, Mouth mouth);

/**
 * @brief Obtiene una boca dado su índice.
 * @param monster Puntero constante al monstruo.
 * @param index Índice de la boca.
 * @return Puntero a la estructura Mouth o NULL si está fuera de rango.
 */
Mouth* Monster_GetMouth(Monster* monster, size_t index);

/**
 * @brief Elimina todas las bocas almacenadas en el monstruo.
 * @param monster Puntero al monstruo.
 */
void Monster_ClearMouths(Monster* monster);

/**
 * @brief Añade una parte del cuerpo al monstruo.
 * @param monster Puntero al monstruo.
 * @param bodyPart Estructura BodyPart a añadir.
 * @return true si se añadió correctamente.
 */
bool Monster_AddBodyPart(Monster* monster, BodyPart bodyPart);

/**
 * @brief Retorna un puntero a la parte del cuerpo de la cabeza (índice 0).
 * @param monster Puntero al monstruo.
 * @return Puntero a la BodyPart o NULL si no existen partes.
 */
BodyPart* Monster_GetHead(Monster* monster);

/** @brief Añade un Trait general al monstruo. */
bool Monster_AddTrait(Monster* monster, struct Trait* trait);
/** @brief Obtiene un Trait general por su tipo. */
struct Trait* Monster_GetTrait(const Monster* monster, TraitType type);
/** @brief Elimina y retorna un Trait general por su tipo. */
struct Trait* Monster_RemoveTrait(Monster* monster, TraitType type);
/** @brief Verifica si contiene un Trait general. */
bool Monster_ContainsTrait(const Monster* monster, TraitType type);

/** @brief Añade un Trait de combate. */
bool Monster_AddCombatTrait(Monster* monster, struct Trait* trait);
/** @brief Obtiene un Trait de combate por su tipo. */
struct Trait* Monster_GetCombatTrait(const Monster* monster, TraitType type);

/** @brief Añade un Trait visual. */
bool Monster_AddVisualTrait(Monster* monster, struct VisualTrait* trait);

/* Getters y Setters */
void Monster_SetWorld(Monster* monster, struct World* world);
struct World* Monster_GetWorld(const Monster* monster);
void Monster_SetId(Monster* monster, int id);
int Monster_GetId(const Monster* monster);
float Monster_GetAngle(const Monster* monster);
void Monster_SetAngle(Monster* monster, float angle);
void Monster_SetAngleTarget(Monster* monster, float targetX, float targetZ);
void Monster_SetBehavior(Monster* monster, struct MonsterBehavior* behavior);
struct MonsterBehavior* Monster_GetBehavior(const Monster* monster);

#ifdef __cplusplus
}
#endif

#endif // MONSTER_H
