#include "BodyPart.h"
#include <stdlib.h>
#include <string.h>

#include "Monster.h"

/* Implementación de BodyPart */

BodyPart BodyPart_Create(float x, float y, float z, float width, float length, float height, float groundOffset) {
    BodyPart part;

    part.position = Vec3_Create(x, groundOffset, z);
    part.oldPosition = part.position;
    part.positionRender = part.position;

    part.width = width;
    part.length = length;
    part.height = height;
    part.groundOffset = groundOffset;

    part.widthRender = width;
    part.lengthRender = length;
    part.heightRender = height;
    part.groundOffsetRender = groundOffset;

    part.color.index = 0;
    part.bellyColor.index = 0;
    part.bellyThreshold = 0.4f;

    part.traits = NULL;
    part.traitCount = 0;
    part.traitCapacity = 0;

    return part;
}

BodyPart BodyPart_CreateDefault(void) {
    return BodyPart_Create(0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f);
}

void BodyPart_Free(BodyPart* part) {
    if (part && part->traits) {
        free(part->traits);
        part->traits = NULL;
        part->traitCount = 0;
        part->traitCapacity = 0;
    }
}

void BodyPart_Update(BodyPart* part, Monster* monster, int index, double diff) {
    if (!part) return;

    if (monster) {
        part->position.y = part->groundOffsetRender + Monster_GetWorldHeight(monster, part->position.x, part->position.z);
    }

    for (size_t i = 0; i < part->traitCount; ++i) {
        Trait* trait = part->traits[i];
        if (trait && trait->update) {
            trait->update(trait, monster, index, diff);
        }
    }
}

void BodyPart_RenderUpdate(BodyPart* part, Monster* monster, int index, double diff, double renderPercent) {
    if (!part) return;

    part->positionRender = Vec3_Lerp(part->oldPosition, part->position, (float)renderPercent);
    part->widthRender = part->width;
    part->heightRender = part->height;
    part->lengthRender = part->length;
    part->groundOffsetRender = part->groundOffset;
}

void BodyPart_RenderUpdateTraits(BodyPart* part, Monster* monster, int index, double diff, double renderPercent) {
    if (!part) return;

    for (size_t i = 0; i < part->traitCount; ++i) {
        Trait* trait = part->traits[i];
        if (trait && trait->renderUpdate) {
            trait->renderUpdate(trait, monster, index, diff, renderPercent);
        }
    }
}

void BodyPart_Render(BodyPart* part, Monster* monster, int index, struct MonsterRenderer* renderer, struct ICamera* camera, double diff, Pass pass) {
    if (!part) return;

    for (size_t i = 0; i < part->traitCount; ++i) {
        Trait* trait = part->traits[i];
        if (trait && trait->render) {
            trait->render(trait, monster, index, renderer, camera, diff, pass);
        }
    }
}

bool BodyPart_AddTrait(BodyPart* part, Trait* trait) {
    if (!part || !trait) return false;

    if (part->traitCount >= part->traitCapacity) {
        size_t newCap = (part->traitCapacity == 0) ? 4 : part->traitCapacity * 2;
        Trait** newArray = (Trait**)realloc(part->traits, newCap * sizeof(Trait*));
        if (!newArray) return false;

        part->traits = newArray;
        part->traitCapacity = newCap;
    }

    part->traits[part->traitCount++] = trait;
    return true;
}

Trait* BodyPart_GetTrait(const BodyPart* part, TraitType traitType) {
    if (!part) return NULL;

    for (size_t i = 0; i < part->traitCount; ++i) {
        if (part->traits[i] && part->traits[i]->type == traitType) {
            return part->traits[i];
        }
    }
    return NULL;
}

bool BodyPart_ContainsTrait(const BodyPart* part, TraitType traitType) {
    return BodyPart_GetTrait(part, traitType) != NULL;
}
