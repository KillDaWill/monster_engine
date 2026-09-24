/** @file CreatureRig.h
 * @brief Adaptador de anatomía resuelta modular al rig genérico.
 */
#ifndef MONSTER_CREATURE_RIG_H
#define MONSTER_CREATURE_RIG_H
#include "RigBuilder.h"
struct Monster;
/** @brief Deriva ejes y longitudes de la anatomía; añade mandíbula asociada a la cabeza. */
bool CreatureRig_Build(const struct Monster* monster,Rig* rig);
#endif
