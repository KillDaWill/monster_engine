/**
 * @file WorldInterface.h
 * @brief Interfaz agnóstica de consulta del mundo/entorno.
 * @author Monster Engine Team
 * @date 2026
 */

#ifndef MONSTER_WORLD_INTERFACE_H
#define MONSTER_WORLD_INTERFACE_H

#include "Vector.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @struct World
 * @brief Interfaz del mundo/mapa para consultar la altura del terreno.
 */
typedef struct SurfaceHit { Vector3 position, normal; } SurfaceHit;

typedef struct World {
    float (*getWalkingHeight)(struct World* self, float x, float z);
    bool (*sampleGround)(struct World* self, Vector3 query, SurfaceHit* hit);
} World;

/** @brief Consulta superficie; adapta altura heredada y estima normal por diferencias finitas. */
bool World_SampleGround(World* world, Vector3 query, SurfaceHit* hit);

#ifdef __cplusplus
}
#endif

#endif // MONSTER_WORLD_INTERFACE_H
