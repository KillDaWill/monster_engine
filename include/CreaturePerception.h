/** @file CreaturePerception.h
 * @brief Inteligencia modular independiente del render y de la anatomía. */
#ifndef CREATUREPERCEPTION_H
#define CREATUREPERCEPTION_H
#include "AIWorld.h"
#include "BrainProfile.h"
#define CREATURE_MEMORY_CAPACITY 32
typedef struct PerceptionEntry {
    AIObjectId id;
    AIObjectType type;
    unsigned group, species, category, foodCategory;
    bool available, reproductiveReady;
    Vector3 position;
    float radius, maximumSpeed;
    double observed;
    bool visible, drinkable;
    double unreachableUntil;
} PerceptionEntry;
typedef struct CreaturePerception {
    PerceptionEntry entries[CREATURE_MEMORY_CAPACITY];
    unsigned count;
    double nextRefresh;
} CreaturePerception;
/** @brief Expira/invalida siempre; observa sólo al vencer la planificación. */
void CreaturePerception_Update(CreaturePerception *, const PerceptionProfile *, const AIWorld *,
                               AIObjectId self, Vector3 eye, float yaw, double time);
/** @brief Consulta exclusiva del conocimiento individual. */
const PerceptionEntry *CreaturePerception_Find(const CreaturePerception *, AIObjectId);

#endif
