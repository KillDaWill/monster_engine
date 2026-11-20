#ifndef OFFSPRING_MORPH_FITTER_H
#define OFFSPRING_MORPH_FITTER_H

#include "CreatureOffspring.h"
#include "Monster.h"

float OffspringMorph_HeadError(const HeadAnatomy *, float scale,
    const HeadAnatomy *target, float targetScale);
void OffspringMorph_Fit(CreatureRecipe *, const Monster *target, const Monster parents[2], OffspringReport *);
void OffspringMorph_Measure(const Monster *, const Monster *target, OffspringReport *);

#endif
