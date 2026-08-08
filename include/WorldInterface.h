/**
 * @file WorldInterface.h
 * @brief Interfaz agnóstica de consulta del mundo/entorno.
 * @author Monster Engine Team
 * @date 2026
 */

#ifndef MONSTER_WORLD_INTERFACE_H
#define MONSTER_WORLD_INTERFACE_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @struct World
 * @brief Interfaz del mundo/mapa para consultar la altura del terreno.
 */
typedef struct World {
    float (*getWalkingHeight)(struct World* self, float x, float z);
} World;

#ifdef __cplusplus
}
#endif

#endif // MONSTER_WORLD_INTERFACE_H
