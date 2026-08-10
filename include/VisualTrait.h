/**
 * @file VisualTrait.h
 * @brief Interfaz para rasgos puramente visuales de rendering.
 * @author Monster Engine Team
 * @date 2026
 */

#ifndef MONSTER_VISUAL_TRAIT_H
#define MONSTER_VISUAL_TRAIT_H

#ifdef __cplusplus
extern "C" {
#endif

struct Monster;

typedef int VisualTraitType;

/**
 * @struct VisualTrait
 * @brief Rasgo visual especializado.
 *
 * @note Regla de desacoplamiento de renderizado:
 * VisualTrait NUNCA debe invocar llamadas directas a OpenGL o APIs de renderizado.
 * En su lugar, los futuros VisualTraits contribuirán estado visual a través del coordinador MonsterVisual.
 *
 * Patron de diseño futuro esperado:
 * - Anatomía positiva (ej. cuernos, alas): contribuir geometría de unión SDF.
 * - Anatomía negativa (ej. fosas nasales, hendiduras): contribuir geometría de sustracción SDF.
 * - Geometría visual desacoplada (ej. cabello, accesorios): contribuir geometría direct a Mesh.
 */
typedef struct VisualTrait {
    VisualTraitType type;
    void (*renderUpdate)(struct VisualTrait* self, struct Monster* monster, double diff, double renderPercent);
} VisualTrait;

#ifdef __cplusplus
}
#endif

#endif // MONSTER_VISUAL_TRAIT_H
