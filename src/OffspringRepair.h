#ifndef OFFSPRING_REPAIR_H
#define OFFSPRING_REPAIR_H

#include "CreatureOffspring.h"

bool OffspringRepair_Validate(CreatureRecipe *, const CreatureRecipe *reference,
    const AnatomyGraph referenceStages[4],
    float strength, bool repair, OffspringReport *, CreatureOffspringCancel, void *,
    char *, size_t);

#endif
