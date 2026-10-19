/** @file SurfacePresets.h
 * @brief Presets y correspondencia anatómica de superficie del lagarto.
 */
#ifndef MONSTER_GENERIC_SURFACE_H
#define MONSTER_GENERIC_SURFACE_H
#include "SurfaceMapper.h"
/** @brief Apariencia reproducible del individuo; madurez continua [0,1].
 * @param seed Identidad del individuo.
 * @param maturity Madurez normalizada.
 * @return Fenotipo de pigmento y escamas válido. */
SurfacePhenotype SurfacePreset_ScaledReptile(uint32_t seed,float maturity);
/** @brief Fenotipo de pelaje canino de doble capa corta y perfiles anatómicos regionales.
 * @param seed Identidad del individuo.
 * @param maturity Madurez normalizada [0,1].
 * @return Fenotipo de pigmento y pelaje canino válido. */
SurfacePhenotype SurfacePreset_CanidShortDoubleCoat(uint32_t seed,float maturity);
/** @brief Fenotipo de pelaje bovino largo y desgreñado con perfiles regionales.
 * @param seed Identidad del individuo.
 * @param maturity Madurez normalizada [0,1].
 * @return Fenotipo de pigmento y pelaje bovino válido. */
SurfacePhenotype SurfacePreset_BovineShaggyCoat(uint32_t seed,float maturity);
/** @brief Fenotipo de pelaje felino corto, denso y satinado.
 * @param seed Identidad del individuo.
 * @param maturity Madurez normalizada [0,1].
 * @return Fenotipo de pigmento y pelaje felino válido. */
SurfacePhenotype SurfacePreset_FelineSleekCoat(uint32_t seed,float maturity);
#endif
