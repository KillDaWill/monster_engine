/** @file AIWorld.h
 * @brief Inteligencia modular independiente del render y de la anatomía. */
#ifndef AIWORLD_H
#define AIWORLD_H
#include "Vector.h"
#include "InteractionTarget.h"
#include <stdint.h>
#define AI_WORLD_CAPACITY 128
typedef uint32_t AIObjectId;
typedef enum AIObjectType { AI_CREATURE, AI_WATER, AI_OBSTACLE, AI_INTEREST, AI_FOOD } AIObjectType;
typedef struct AIWorldObject {
    AIObjectId id;
    AIObjectType type;
    unsigned group, tags;
    Vector3 position, surfaceNormal, approachDirection, velocity;
    bool moving;
    float radius, height;
    bool active, visible, occludes, walkable, drinkable;
} AIWorldObject;
typedef struct AIWorld {
    AIWorldObject objects[AI_WORLD_CAPACITY];
    AIObjectId nextId;
    unsigned revision;
    float halfSize;
} AIWorld;
/** @brief Inicializa registro acotado; IDs nunca se reutilizan. */
void AIWorld_Init(AIWorld *, float halfSize);
/** @brief Registra objeto por valor; cero indica capacidad agotada. */
AIObjectId AIWorld_Add(AIWorld *, AIWorldObject);
/** @brief Consulta por identificador estable, NULL si fue retirado. */
const AIWorldObject *AIWorld_Get(const AIWorld *, AIObjectId);
/** @brief Publica transformaciones; cambios de obstáculos invalidan caminos. */
bool AIWorld_Move(AIWorld *, AIObjectId, Vector3);
/** @brief Actualiza tamaño manteniendo ID; invalida rutas sólo para obstáculos.
 * @param radius Radio físico no negativo. @param height Altura de oclusión.
 * @return false si no existe el objeto o las dimensiones no son válidas. */
bool AIWorld_SetBounds(AIWorld *, AIObjectId, float radius, float height);
/** @brief Retira sin dejar referencias crudas. */
void AIWorld_Remove(AIWorld *, AIObjectId);
/** @brief Broadphase por esfera, devuelve IDs hasta capacidad. */
unsigned AIWorld_Candidates(const AIWorld *, Vector3, float, AIObjectId *, unsigned);
/** @brief Visibilidad por cilindros estáticos; Y vertical. */
bool AIWorld_LineOfSight(const AIWorld *, Vector3, Vector3, AIObjectId ignore);
/** @brief Barrido volumétrico contra obstáculos y oclusores cilíndricos. */
bool AIWorld_ClearSegment(const AIWorld *,Vector3,Vector3,float radius,AIObjectId ignore);
/** @brief Espacio libre para disco conservador XZ. */
bool AIWorld_Walkable(const AIWorld *, Vector3, float clearance, bool swimming);
/** @brief Barrido conservador de segmento para navegación. */
bool AIWorld_SegmentWalkable(const AIWorld *, Vector3, Vector3, float, bool);
/** @brief Selecciona contacto sobre disco superficial a la altura real del recurso. */
bool AIWorld_InteractionTarget(const AIWorld *, AIObjectId, Vector3 mouth, float tolerance, InteractionTarget *);
/** @brief Valida interacción 3D desde boca, orilla cercana y visión sin
 * obstáculos. */
bool AIWorld_CanDrink(const AIWorld *, AIObjectId, Vector3 mouth, float reach);

#endif
