#include "Monster.h"
#include "MathUtils.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>

/* Implementación del Módulo Monster */

Monster Monster_Create(void) {
    Monster m;
    memset(&m, 0, sizeof(Monster));
    m.colorPalette = ColorPalette_Create();
    m.updateSpeed = 1.0f;
    return m;
}

void Monster_Init(Monster* monster) {
    if (!monster) return;

    /* Agregar BodyPart inicial por defecto */
    Monster_AddBodyPart(monster, BodyPart_CreateDefault());
}

void Monster_Free(Monster* monster) {
    if (!monster) return;

    for (size_t i = 0; i < monster->bodyPartCount; ++i) {
        BodyPart_Free(&monster->bodyParts[i]);
    }
    if (monster->bodyParts) free(monster->bodyParts);
    if (monster->eyes) free(monster->eyes);
    if (monster->mouths) free(monster->mouths);

    if (monster->traits) free(monster->traits);
    if (monster->combatTraits) free(monster->combatTraits);
    if (monster->visualTraits) free(monster->visualTraits);

    memset(monster, 0, sizeof(Monster));
}

Monster Monster_Clone(const Monster* src) {
    Monster dst = Monster_Create();
    if (!src) return dst;

    dst.id = src->id;
    dst.angle = src->angle;
    dst.updateSpeed = src->updateSpeed;
    dst.world = src->world;
    dst.behavior = src->behavior;
    dst.meta = src->meta;

    /* Copiar paleta de colores */
    dst.colorPalette.count = src->colorPalette.count;
    for (size_t i = 0; i < src->colorPalette.count; ++i) {
        dst.colorPalette.colors[i] = src->colorPalette.colors[i];
    }

    /* Copiar partes del cuerpo */
    for (size_t i = 0; i < src->bodyPartCount; ++i) {
        BodyPart p = src->bodyParts[i];
        p.traits = NULL;
        p.traitCount = 0;
        p.traitCapacity = 0;

        for (size_t t = 0; t < src->bodyParts[i].traitCount; ++t) {
            BodyPart_AddTrait(&p, src->bodyParts[i].traits[t]);
        }

        Monster_AddBodyPart(&dst, p);
    }

    /* Copiar ojos */
    for (size_t i = 0; i < src->eyeCount; ++i) {
        Monster_AddEye(&dst, src->eyes[i]);
    }

    /* Copiar bocas */
    for (size_t i = 0; i < src->mouthCount; ++i) {
        Monster_AddMouth(&dst, src->mouths[i]);
    }

    /* Copiar traits de monstruo */
    for (size_t i = 0; i < src->traitCount; ++i) {
        Monster_AddTrait(&dst, src->traits[i]);
    }
    for (size_t i = 0; i < src->combatTraitCount; ++i) {
        Monster_AddCombatTrait(&dst, src->combatTraits[i]);
    }
    for (size_t i = 0; i < src->visualTraitCount; ++i) {
        Monster_AddVisualTrait(&dst, src->visualTraits[i]);
    }

    return dst;
}

bool Monster_AddEye(Monster* monster, Eye eye) {
    if (!monster) return false;

    if (monster->eyeCount >= monster->eyeCapacity) {
        size_t newCap = 0;
        if (!Math_GrowCapacity(monster->eyeCapacity, monster->eyeCount + 1, sizeof(Eye), &newCap)) return false;
        Eye* newArr = (Eye*)realloc(monster->eyes, newCap * sizeof(Eye));
        if (!newArr) return false;
        monster->eyes = newArr;
        monster->eyeCapacity = newCap;
    }

    monster->eyes[monster->eyeCount++] = eye;
    return true;
}

Eye* Monster_GetEye(Monster* monster, size_t index) {
    if (!monster || index >= monster->eyeCount) return NULL;
    return &monster->eyes[index];
}

void Monster_ClearEyes(Monster* monster) {
    if (!monster) return;
    if (monster->eyes) {
        free(monster->eyes);
        monster->eyes = NULL;
    }
    monster->eyeCount = 0;
    monster->eyeCapacity = 0;
}

bool Monster_AddMouth(Monster* monster, Mouth mouth) {
    if (!monster) return false;

    if (monster->mouthCount >= monster->mouthCapacity) {
        size_t newCap = 0;
        if (!Math_GrowCapacity(monster->mouthCapacity, monster->mouthCount + 1, sizeof(Mouth), &newCap)) return false;
        Mouth* newArr = (Mouth*)realloc(monster->mouths, newCap * sizeof(Mouth));
        if (!newArr) return false;
        monster->mouths = newArr;
        monster->mouthCapacity = newCap;
    }

    monster->mouths[monster->mouthCount++] = mouth;
    return true;
}

Mouth* Monster_GetMouth(Monster* monster, size_t index) {
    if (!monster || index >= monster->mouthCount) return NULL;
    return &monster->mouths[index];
}

void Monster_ClearMouths(Monster* monster) {
    if (!monster) return;
    if (monster->mouths) {
        free(monster->mouths);
        monster->mouths = NULL;
    }
    monster->mouthCount = 0;
    monster->mouthCapacity = 0;
}

void Monster_Update(Monster* monster, double diff) {
    if (!monster) return;

    diff *= monster->updateSpeed;

    if (monster->behavior && monster->behavior->update) {
        monster->behavior->update(monster->behavior, monster, diff);
    }
}

void Monster_RenderUpdate(Monster* monster, double diff, double renderPercent) {
    if (!monster) return;

    diff *= monster->updateSpeed;

    for (size_t i = 0; i < monster->bodyPartCount; ++i) {
        BodyPart_RenderUpdate(&monster->bodyParts[i], monster, (int)i, diff, renderPercent);
    }

    for (size_t t = 0; t < monster->traitCount; ++t) {
        struct Trait* trait = monster->traits[t];
        if (trait && trait->renderUpdate) {
            for (size_t i = 0; i < monster->bodyPartCount; ++i) {
                trait->renderUpdate(trait, monster, (int)i, diff, renderPercent);
            }
        }
    }

    for (size_t v = 0; v < monster->visualTraitCount; ++v) {
        VisualTrait* vt = monster->visualTraits[v];
        if (vt && vt->renderUpdate) {
            vt->renderUpdate(vt, monster, diff, renderPercent);
        }
    }

    for (size_t i = 0; i < monster->bodyPartCount; ++i) {
        BodyPart_RenderUpdateTraits(&monster->bodyParts[i], monster, (int)i, diff, renderPercent);
    }
}

bool Monster_AddBodyPart(Monster* monster, BodyPart bodyPart) {
    if (!monster) return false;

    if (monster->bodyPartCount >= monster->bodyPartCapacity) {
        size_t newCap = 0;
        if (!Math_GrowCapacity(monster->bodyPartCapacity, monster->bodyPartCount + 1, sizeof(BodyPart), &newCap)) return false;
        BodyPart* newArray = (BodyPart*)realloc(monster->bodyParts, newCap * sizeof(BodyPart));
        if (!newArray) return false;

        monster->bodyParts = newArray;
        monster->bodyPartCapacity = newCap;
    }

    monster->bodyParts[monster->bodyPartCount++] = bodyPart;
    return true;
}

BodyPart* Monster_GetHead(Monster* monster) {
    if (!monster || monster->bodyPartCount == 0) return NULL;
    return &monster->bodyParts[0];
}

Color Monster_GetColorFromIndex(const Monster* monster, int index) {
    if (!monster || monster->colorPalette.count == 0) {
        return COLOR_WHITE;
    }
    return ColorPalette_GetColor(&monster->colorPalette, (size_t)index);
}

Color Monster_GetColorFromIndexStruct(const Monster* monster, ColorIndex index) {
    return Monster_GetColorFromIndex(monster, index.index);
}

void Monster_SetAngleTarget(Monster* monster, float targetX, float targetZ) {
    if (!monster || monster->bodyPartCount == 0) return;
    BodyPart* head = Monster_GetHead(monster);

    float dz = targetZ - head->position.z;
    float dx = targetX - head->position.x;
    monster->angle = -atan2f(dz, dx) - (3.14159265f / 2.0f);
}

/* Manejo genérico de listas dinámicas de Trait */
static bool AddTraitToList(struct Trait*** list, size_t* count, size_t* capacity, struct Trait* trait) {
    if (!list || !count || !capacity || !trait) return false;

    if (*count >= *capacity) {
        size_t newCap = 0;
        if (!Math_GrowCapacity(*capacity, *count + 1, sizeof(struct Trait*), &newCap)) return false;
        struct Trait** newArray = (struct Trait**)realloc(*list, newCap * sizeof(struct Trait*));
        if (!newArray) return false;
        *list = newArray;
        *capacity = newCap;
    }
    (*list)[(*count)++] = trait;
    return true;
}

bool Monster_AddTrait(Monster* monster, struct Trait* trait) {
    return monster ? AddTraitToList(&monster->traits, &monster->traitCount, &monster->traitCapacity, trait) : false;
}

bool Monster_AddCombatTrait(Monster* monster, struct Trait* trait) {
    return monster ? AddTraitToList(&monster->combatTraits, &monster->combatTraitCount, &monster->combatTraitCapacity, trait) : false;
}

struct Trait* Monster_GetTrait(const Monster* monster, TraitType type) {
    if (!monster) return NULL;
    for (size_t i = 0; i < monster->traitCount; ++i) {
        if (monster->traits[i] && monster->traits[i]->type == type) return monster->traits[i];
    }
    return NULL;
}

struct Trait* Monster_GetCombatTrait(const Monster* monster, TraitType type) {
    if (!monster) return NULL;
    for (size_t i = 0; i < monster->combatTraitCount; ++i) {
        if (monster->combatTraits[i] && monster->combatTraits[i]->type == type) return monster->combatTraits[i];
    }
    return NULL;
}

void Monster_SetId(Monster* monster, int id) {
    if (monster) monster->id = id;
}

int Monster_GetId(const Monster* monster) {
    return monster ? monster->id : 0;
}

float Monster_GetAngle(const Monster* monster) {
    return monster ? monster->angle : 0.0f;
}

void Monster_SetAngle(Monster* monster, float angle) {
    if (monster) monster->angle = angle;
}

static bool AddVisualTraitToList(struct VisualTrait*** list, size_t* count, size_t* capacity, struct VisualTrait* trait) {
    if (!list || !count || !capacity || !trait) return false;

    if (*count >= *capacity) {
        size_t newCap = 0;
        if (!Math_GrowCapacity(*capacity, *count + 1, sizeof(struct VisualTrait*), &newCap)) return false;
        struct VisualTrait** newArray = (struct VisualTrait**)realloc(*list, newCap * sizeof(struct VisualTrait*));
        if (!newArray) return false;
        *list = newArray;
        *capacity = newCap;
    }
    (*list)[(*count)++] = trait;
    return true;
}

bool Monster_AddVisualTrait(Monster* monster, struct VisualTrait* trait) {
    return monster ? AddVisualTraitToList(&monster->visualTraits, &monster->visualTraitCount, &monster->visualTraitCapacity, trait) : false;
}

