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
    unsigned temporaryCandidate;
    Monster adult, reference;
    MonsterAger *ager;
    MonsterSDF sdf;
    Mesh membranes, armor;
    AABB3D framing;
    bool fieldValid;
    unsigned fieldBuilds;
    int direction;
    MonsterVisualEye *eyes;
    size_t eyeCapacity;
    float eyeAge;
    LongHairAsync *hair;
} UiCreatureScene;

bool UiCreatureScene_LoadRecipe(const char *, const UiVfsNode *, CreatureRecipeDocument *,
    char *error, size_t capacity);
UiCreatureScene *UiCreatureScene_CreateTemporary(const CreatureRecipe *, unsigned candidate,
    char *error, size_t capacity);
UiCreatureScene *UiCreatureScene_Create(const char *, const UiVfsNode *, const UiVfsNode *,
                                       char *error, size_t capacity);
void UiCreatureScene_Destroy(UiCreatureScene *);
const Monster *UiCreatureScene_Monster(const UiCreatureScene *);
bool UiCreatureScene_SetAge(UiCreatureScene *, float);
bool UiCreatureScene_Advance(UiCreatureScene *, double dt);
#endif
