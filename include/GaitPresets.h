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
/** @brief Salto bípedo sincronizado para especies macrópodas de apoyo posterior. */
GaitProfile GaitPreset_BipedalHop(const Rig* rig);
/** @brief Vuelo y aleteo aviar con postura posada estable. */
GaitProfile GaitPreset_AvianFlight(const Rig* rig);
/** @brief Natación ondulatoria batoidea con onda propagada en aletas pectorales. */
GaitProfile GaitPreset_RaySwim(const Rig* rig);
/** @brief Desplazamiento y rastreo independiente de brazos hidrostáticos de cefalópodo. */
GaitProfile GaitPreset_OctopusCrawl(const Rig* rig);
/** @brief Vuelo estacionario/batido independiente de cuatro alas de insecto. */
GaitProfile GaitPreset_InsectFlight(const Rig* rig);
/** @brief Pulsación rítmica de contracción y relajación de campana medusoide. */
GaitProfile GaitPreset_MedusaPulse(const Rig* rig);
#endif
