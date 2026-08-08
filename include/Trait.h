/**
 * @file Trait.h
 * @brief Definición genérica y desacoplada de Rasgos (Traits) para entidades y partes del cuerpo.
 * @author Monster Engine Team
 * @date 2026
 */

#ifndef MONSTER_TRAIT_H
#define MONSTER_TRAIT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Monster;
struct MonsterRenderer;
struct ICamera;

/** Tipo de identificador numérico de rasgo */
typedef int TraitType;

/**
 * @struct Trait
 * @brief Estructura base de comportamiento/rasgo dinámico para partes del cuerpo o la entidad Monster.
 */
typedef struct Trait {
    TraitType type; /**< Identificador del tipo de rasgo */

    /** Callback de actualización de lógica */
    void (*update)(struct Trait* self, struct Monster* monster, int index, double diff);

    /** Callback de actualización de interpolación visual */
    void (*renderUpdate)(struct Trait* self, struct Monster* monster, int index, double diff, double renderPercent);

    /** Callback opcional de dibujado/renderizado */
    void (*render)(struct Trait* self, struct Monster* monster, int index, struct MonsterRenderer* renderer, struct ICamera* camera, double diff, int pass);
} Trait;

#ifdef __cplusplus
}
#endif

#endif // MONSTER_TRAIT_H
