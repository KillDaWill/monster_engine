#ifndef CREATURE_OFFSPRING_INTERNAL_H
#define CREATURE_OFFSPRING_INTERNAL_H

#include "CreatureOffspring.h"

/** Repite una propuesta concreta de la generación inicial para diagnóstico. */
bool CreatureOffspring_ReplayProposal(const CreatureRecipe *, const CreatureRecipe *,
    CreatureOffspringConfig, size_t index, unsigned attempt, CreatureRecipeDocument *,
    char *, size_t);

#endif
