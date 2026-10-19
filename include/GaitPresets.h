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
/**
 * @brief Marcha bovina digitígrada de cadencia lenta y transferencia pesada.
 * @param rig Rig anatómico que dimensiona el perfil base.
 * @return Perfil de marcha completamente inicializado.
 */
GaitProfile GaitPreset_BovineWalk(const Rig* rig);
/**
 * @brief Merodeo felino de ciclo largo, zancada corta y cuerpo agachado.
 * @param rig Rig anatómico que dimensiona el perfil base.
 * @return Perfil de marcha completamente inicializado.
 */
GaitProfile GaitPreset_FelineStalk(const Rig* rig);
/** @brief Mezcla paso y carrera según velocidad local y capacidades del rig. */
GaitProfile GaitPreset_QuadrupedMotion(const Rig* rig,bool agile,float speed,float running);
#endif
