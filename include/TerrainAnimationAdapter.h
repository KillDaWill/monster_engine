/**
 * @file TerrainAnimationAdapter.h
 * @brief Adaptador local <-> mundial de muestreo de suelo para IK de patas en terrenos 3D.
 *
 * EcosystemVisual anima cada ranura en coordenadas locales del modelo; este adaptador
 * transforma cada consulta de apoyo al espacio mundial (centro, escala y orientación 3D),
 * muestrea el terreno autoritativo y devuelve el impacto y normal de vuelta a espacio local.
 */
#ifndef MONSTER_TERRAIN_ANIMATION_ADAPTER_H
#define MONSTER_TERRAIN_ANIMATION_ADAPTER_H

#include "WorldInterface.h"
#include "WorldCollision.h"
#include "Quaternion.h"
#include "Locomotion.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TerrainAnimationAdapter {
    World worldInterface;              /**< Primer miembro para conversión segura World* -> adaptador. */
    const WorldStaticData *staticWorld;/**< Préstamo inmutable durante la actualización visual. */
    Vector3 worldOrigin;               /**< Posición autoritativa del cuerpo en espacio mundial. */
    Quaternion bodyOrientation;        /**< Orientación 3D completa (yaw + inclinación acotada). */
    Vector3 modelCenter;               /**< Centro de soporte local del modelo. */
    float scale;                       /**< Escala uniforme modelo -> mundo. */
} TerrainAnimationAdapter;

/** @brief Inicializa el adaptador con el marco corporal y escala actuales de la ranura. */
void TerrainAnimationAdapter_Init(TerrainAnimationAdapter *adapter,
                                  const WorldStaticData *staticWorld,
                                  Vector3 worldOrigin,
                                  Quaternion bodyOrientation,
                                  Vector3 modelCenter,
                                  float scale);

/** @brief Transporta anclas y normales de locomoción entre dos marcos de presentación. */
void TerrainAnimationAdapter_RebaseFeet(Locomotion *locomotion, const Rig *rig,
                                        const TerrainAnimationAdapter *before,
                                        const TerrainAnimationAdapter *after);

/** @brief Devuelve el puntero World* listo para asignar a Monster.world. */
static inline World *TerrainAnimationAdapter_AsWorld(TerrainAnimationAdapter *adapter) {
    return adapter ? &adapter->worldInterface : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* MONSTER_TERRAIN_ANIMATION_ADAPTER_H */
