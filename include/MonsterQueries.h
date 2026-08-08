/**
 * @file MonsterQueries.h
 * @brief Funciones de consulta pura (no mutadoras) sobre la entidad Monster.
 * @author Monster Engine Team
 * @date 2026
 */

#ifndef MONSTER_QUERIES_H
#define MONSTER_QUERIES_H

#include "Vector.h"
#include "BodyPart.h"

#ifdef __cplusplus
extern "C" {
#endif

struct Monster;

/**
 * @struct AABB3D
 * @brief Bounding Box alineado a los ejes para colisión y frustum culling.
 */
typedef struct AABB3D {
    Vector3 start; /**< Esquina inferior/mínima */
    Vector3 end;   /**< Esquina superior/máxima */
} AABB3D;

/**
 * @brief Consulta la altura del terreno en las coordenadas especificadas del mundo.
 */
float Monster_GetWorldHeight(struct Monster* monster, float worldX, float worldZ);

/**
 * @brief Calcula el centro de masa geométrico promediando todas las partes del cuerpo.
 */
Vector3 Monster_GetCenter(const struct Monster* monster);

/**
 * @brief Calcula el centro excluyendo las partes que contengan rasgos de ondulación (WiggleTrait).
 */
Vector3 Monster_GetNonWiggleCenter(const struct Monster* monster);

/**
 * @brief Obtiene el ancho interpolado continuo a lo largo del cuerpo dado un índice flotante.
 */
float Monster_GetPartWidth(const struct Monster* monster, float index);

/**
 * @brief Obtiene el alto interpolado continuo a lo largo del cuerpo dado un índice flotante.
 */
float Monster_GetPartHeight(const struct Monster* monster, float index);

/**
 * @brief Retorna el vector de dirección basado en la parte actual y la anterior.
 */
Vector3 Monster_GetDirection(const struct Monster* monster, int index);

/**
 * @brief Retorna el vector de dirección basado en la parte actual y la posterior.
 */
Vector3 Monster_GetDirectionFromNextPart(const struct Monster* monster, int index);

/**
 * @brief Obtiene la posición exacta en el cuerpo según un porcentaje flotante de su longitud total [0.0 - 1.0].
 */
Vector3 Monster_GetPosition(const struct Monster* monster, double percent);

/**
 * @brief Retorna la longitud total acumulada de todas las partes del cuerpo.
 */
float Monster_GetTotalLength(const struct Monster* monster);

/**
 * @brief Calcula la caja envolvente (AABB) basada en los nodos de las partes del cuerpo.
 */
AABB3D Monster_GetBoundingBox(const struct Monster* monster, AABB3D dst);

#ifdef __cplusplus
}
#endif

#endif // MONSTER_QUERIES_H
