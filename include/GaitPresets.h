/** @file GaitPresets.h
 * @brief Perfiles de marcha separados del controlador genérico.
 */
#ifndef MONSTER_GENERIC_GAITS_H
#define MONSTER_GENERIC_GAITS_H
#include "Locomotion.h"
/** @brief Marcha alterna diagonal escalada con las proporciones actuales. */
GaitProfile GaitPreset_SprawlingQuadruped(float scale);
/** @brief Paso cánido de cuatro apoyos, dimensionado con todos los segmentos del rig. */
GaitProfile GaitPreset_CanidWalk(const Rig* rig);
#endif
