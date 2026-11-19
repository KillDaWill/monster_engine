#ifndef UI_CREATURE_SCENE_H
#define UI_CREATURE_SCENE_H

#include "interface/UiVfs.h"
#include "CreatureRecipeIO.h"
#include "MonsterAger.h"
#include "MonsterSDF.h"
#include "MonsterVisual.h"

typedef struct {
    CreatureRecipeDocument *docs[2];
    CreatureRecipeId from, to;
    Monster adult, reference;
    MonsterAger *ager;
    MonsterSDF sdf;
    bool fieldValid;
    int direction;
    MonsterVisualEye *eyes;
    size_t eyeCapacity;
    float eyeAge;
    LongHairAsync *hair;
} UiCreatureScene;

UiCreatureScene *UiCreatureScene_Create(const char *, const UiVfsNode *, const UiVfsNode *,
                                       char *error, size_t capacity);
void UiCreatureScene_Destroy(UiCreatureScene *);
const Monster *UiCreatureScene_Monster(const UiCreatureScene *);
bool UiCreatureScene_SetAge(UiCreatureScene *, float);
bool UiCreatureScene_Advance(UiCreatureScene *, double dt);
#endif
