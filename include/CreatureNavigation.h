/** @file CreatureNavigation.h
 * @brief Inteligencia modular independiente del render y de la anatomía. */
#ifndef CREATURENAVIGATION_H
#define CREATURENAVIGATION_H
#include "AIWorld.h"
#include "NavigationProgress.h"
#define CREATURE_PATH_CAPACITY 256
typedef struct CreatureNavigation {
    Vector3 points[CREATURE_PATH_CAPACITY], goal;
    unsigned count, index, revision;
    float clearance;
    bool swimming, failed;
    NavigationProgress progress;
    unsigned failures;
} CreatureNavigation;
/** @brief A* XZ acotado; buffers automáticos, ningún malloc en actualización.
 */
bool CreatureNavigation_Plan(CreatureNavigation *, const AIWorld *, Vector3 from, Vector3 goal,
                             float clearance, bool swimming);
/** @brief Reutiliza camino, detecta llegada/estancamiento y devuelve waypoint.
 */
NavigationGoalStatus CreatureNavigation_Update(CreatureNavigation *, Vector3, float yaw,
                                               float speed, float turn, float dt,
                                               Vector3 *waypoint);

#endif
