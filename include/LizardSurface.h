/** @file LizardSurface.h
 * @brief Presets y correspondencia anatómica de superficie del lagarto.
 */
#ifndef MONSTER_LIZARD_SURFACE_H
#define MONSTER_LIZARD_SURFACE_H
#include "SurfaceMapper.h"
/** @brief Apariencia reproducible del individuo; madurez continua [0,1].
 * @param seed Identidad del individuo.
 * @param maturity Madurez normalizada.
 * @return Fenotipo de pigmento y escamas válido. */
SurfacePhenotype LizardSurface_FromSeed(uint32_t seed,float maturity);
/** @brief Etiqueta el grafo resuelto y define la unidad corporal de reposo.
 * @param graph Grafo semántico del lagarto.
 * @param totalScale Escala corporal resuelta.
 * @return Dominio y etiquetas indexadas por identidad estable. */
SurfaceMapping LizardSurface_Mapping(const AnatomyGraph* graph,float totalScale);
#endif
