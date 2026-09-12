/** @file LizardRig.h
 * @brief Adaptador de anatomía resuelta de lagarto al rig genérico.
 */
#ifndef MONSTER_LIZARD_RIG_H
#define MONSTER_LIZARD_RIG_H
#include "RigBuilder.h"
struct Monster;
/** @brief Deriva ejes y longitudes de la anatomía; añade mandíbula asociada a la cabeza. */
bool LizardRig_Build(const struct Monster* monster,Rig* rig);
#endif
