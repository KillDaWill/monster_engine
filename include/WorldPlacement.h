/**
 * @file WorldPlacement.h
 * @brief Filtros de hábitat y colocación determinista sensible al relieve para criaturas,
 *        recursos moleculares, relocalización tras agotamiento, crías y cadáveres.
 */
#ifndef MONSTER_WORLD_PLACEMENT_H
#define MONSTER_WORLD_PLACEMENT_H

#include "WorldCollision.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct WorldHabitatFilter {
    float minAltitudeNorm;   /**< Altitud normalizada mínima [0, 1]. */
    float maxAltitudeNorm;   /**< Altitud normalizada máxima [0, 1]. */
    float maxSlopeRadians;   /**< Pendiente máxima permitida. */
    float minMoisture;       /**< Humedad mínima [0, 1]. */
    float maxMoisture;       /**< Humedad máxima [0, 1]. */
    float minFertility;      /**< Fertilidad mínima [0, 1]. */
    float maxWaterDepth;     /**< Profundidad máxima de agua permitida (0.05 = seco). */
    float minShoreDistance;  /**< Distancia mínima al agua (0 sin límite). */
    float maxShoreDistance;  /**< Distancia máxima al agua (<=0 sin límite). */
    float clearanceRadius;   /**< Radio de holgura contra bordes y rocas. */
    float minDistFromCenter; /**< Radio horizontal mínimo desde (0, 0). */
    float maxDistFromCenter; /**< Radio horizontal máximo desde (0, 0) (<=0 usa borde). */
} WorldHabitatFilter;

/** @brief Filtro predeterminado para vegetación comestible (suelo fértil, pendiente suave, seco). */
WorldHabitatFilter WorldHabitatFilter_Vegetation(float clearanceRadius);

/** @brief Filtro predeterminado para aparición de fauna terrestre. */
WorldHabitatFilter WorldHabitatFilter_LandCreature(const TraversalProfile *profile);

/** @brief Comprueba si (x, z) cumple todos los criterios del filtro y devuelve su posición 3D y normal. */
bool WorldPlacement_ValidatePoint(const WorldStaticData *w, float x, float z,
                                  const WorldHabitatFilter *filter,
                                  Vector3 *outPos, Vector3 *outNormal);

/**
 * @brief Busca deterministamente un punto válido en el dominio respetando el filtro de hábitat
 *        y una distancia mínima opcional a una ubicación previa (p. ej. relocalización).
 */
bool WorldPlacement_SampleHabitat(const WorldStaticData *w, uint32_t *rngState,
                                  const WorldHabitatFilter *filter,
                                  bool hasAvoidPos, Vector3 avoidPos, float minAvoidDist,
                                  unsigned maxAttempts, Vector3 *outPos, Vector3 *outNormal);

/**
 * @brief Coloca un cadáver cerca de la ubicación real de muerte sin enterrarlo ni flotar.
 *
 * Si la muerte ocurre en tierra firme o vado somero soportado, se conserva la posición real.
 * Si ocurre en agua más profunda o sobre una roca, busca el punto seco/somero válido más cercano
 * dentro de un radio acotado; nunca teletransporta a zonas remotas.
 */
bool WorldPlacement_ResolveCarcass(const WorldStaticData *w, Vector3 deathPos, float radius,
                                   Vector3 *outPos, Vector3 *outNormal);

#ifdef __cplusplus
}
#endif

#endif /* MONSTER_WORLD_PLACEMENT_H */
