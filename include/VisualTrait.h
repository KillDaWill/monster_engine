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
 */
typedef struct VisualTrait {
    VisualTraitType type;
    void (*renderUpdate)(struct VisualTrait* self, struct Monster* monster, double diff, double renderPercent);
} VisualTrait;

#ifdef __cplusplus
}
#endif

#endif // MONSTER_VISUAL_TRAIT_H
