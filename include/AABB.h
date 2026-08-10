/**
 * @file AABB.h
 * @brief Estructuras y utilidades genéricas para Bounding Boxes 3D (AABB).
 * @author Monster Engine Team
 * @date 2026
 */

#ifndef MONSTER_AABB_H
#define MONSTER_AABB_H

#include "Vector.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @struct AABB3D
 * @brief Bounding Box alineada a los ejes en el espacio 3D.
 */
typedef struct AABB3D {
    Vector3 start; /**< Esquina inferior/mínima */
    Vector3 end;   /**< Esquina superior/máxima */
} AABB3D;

/**
 * @brief Crea una AABB3D vacía/invertida lista para ser expandida.
 */
AABB3D AABB_Empty(void);

/**
 * @brief Crea una AABB3D dadas las esquinas mínima y máxima.
 */
AABB3D AABB_FromMinMax(Vector3 min, Vector3 max);

/**
 * @brief Expande la AABB3D para incluir un punto dado.
 */
void AABB_ExpandPoint(AABB3D* bounds, Vector3 point);

/**
 * @brief Expande la AABB3D para incluir una esfera definida por un centro y radio.
 */
void AABB_ExpandRadius(AABB3D* bounds, Vector3 center, Vector3 radius);

/**
 * @brief Aplica un margen/padding uniforme a todos los ejes de la AABB.
 */
void AABB_Pad(AABB3D* bounds, float padding);

/**
 * @brief Retorna el tamaño (dimensiones largo, alto, ancho) de la AABB.
 */
Vector3 AABB_Size(AABB3D bounds);

/**
 * @brief Comprueba si una AABB3D contiene un punto 3D en el espacio.
 */
bool AABB_ContainsPoint(AABB3D bounds, Vector3 point);

#ifdef __cplusplus
}
#endif

#endif // MONSTER_AABB_H
