#include "UiCreatureScene.h"
#include "interface/UiRecipeCatalog.h"
#include "Creature.h"
#include "CreatureMorph.h"
#include "CreatureRecipes.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

bool UiCreatureScene_LoadRecipe(const char *directory, const UiVfsNode *node,
    CreatureRecipeDocument *doc, char *error, size_t capacity)
{
    if (node->source == UI_VFS_BUILTIN_RECIPE) {
        const CreatureRecipe *recipe = CreatureRecipes_Find(node->recipeId);
        if (!recipe) {
            snprintf(error,capacity,"Built-in recipe %u is unavailable",node->recipeId);
            return false;
        }
        CreatureRecipeDocument_Copy(doc,recipe);
        return true;
    }
    char *json = UiRecipeCatalog_ReadProjectRecipe(directory,node);
    if (!json) {
        snprintf(error,capacity,"Cannot read catalog recipe %s",node->name);
        return false;
    }
    char detail[192];
    bool valid = CreatureRecipeIO_ReadJSON(doc,json,detail,sizeof(detail));
    free(json);
    if (valid && doc->recipe.id == node->recipeId) return true;
    snprintf(error,capacity,"Recipe validation failed for %s: %s",node->name,
             valid ? "Catalog ID changed since startup" : detail);
    return false;
}

const Monster *UiCreatureScene_Monster(const UiCreatureScene *scene)
{
    return scene->ager ? MonsterAger_GetResultConst(scene->ager) : &scene->adult;
}

bool UiCreatureScene_SetAge(UiCreatureScene *scene, float age)
{
    if (scene->ager && scene->ager->perc != age) {
        MonsterAger_SetPerc(scene->ager,age);
        scene->fieldValid = false;
    }
    if (!scene->fieldValid) {
        ++scene->fieldBuilds;
        scene->fieldValid = MonsterSDF_Build(&scene->sdf,UiCreatureScene_Monster(scene),
                                             MonsterSDF_DefaultConfig());
    }
    return scene->fieldValid;
}

bool UiCreatureScene_Advance(UiCreatureScene *scene, double dt)
{
    if (!scene->ager) return UiCreatureScene_SetAge(scene,0.0f);
    float next = scene->ager->perc+scene->direction*(float)(fmin(fmax(dt,0.0),0.1)/5.0);
    if (next >= 1.0f || next <= 0.0f) {
        next = next >= 1.0f ? 1.0f : 0.0f;
        if (dt > 0.0) scene->direction = next == 1.0f ? -1 : 1;
    }
    return UiCreatureScene_SetAge(scene,next);
}

void UiCreatureScene_Destroy(UiCreatureScene *scene)
{
    if (!scene) return;
    LongHairAsync_Destroy(scene->hair);
    for (size_t i=0; i<scene->eyeCapacity; ++i) Mesh_Free(&scene->eyes[i].globe);
    free(scene->eyes);
    MonsterSDF_Free(&scene->sdf);
    if (scene->ager) {
        MonsterAger_Free(scene->ager);
        free(scene->ager);
    }
    Monster_Free(&scene->reference);
    Monster_Free(&scene->adult);
    free(scene->docs[1]);
    free(scene->docs[0]);
    free(scene);
}

static UiCreatureScene *Allocate(char *error, size_t capacity)
{
    UiCreatureScene *scene = calloc(1,sizeof(*scene));
    if (!scene) {
        snprintf(error,capacity,"ERROR: Cannot allocate the creature scene");
        return NULL;
    }
    scene->direction = 1;
    scene->eyeAge = -1.0f;
    scene->adult = Monster_Create();
    scene->reference = Monster_Create();
    scene->sdf = MonsterSDF_Create();
    return scene;
}

static const CreatureRecipe *Load(const char *directory, const UiVfsNode *node,
    CreatureRecipeDocument **owner, char *error, size_t capacity)
{
    if (node->source == UI_VFS_BUILTIN_RECIPE) {
        const CreatureRecipe *recipe = CreatureRecipes_Find(node->recipeId);
        if (!recipe) snprintf(error,capacity,"ERROR: Built-in recipe %u is unavailable",node->recipeId);
        return recipe;
    }
    *owner = malloc(sizeof(**owner));
    if (!*owner) {
        snprintf(error,capacity,"ERROR: Cannot allocate the recipe");
        return NULL;
    }
    return UiCreatureScene_LoadRecipe(directory,node,*owner,error,capacity) ? &(*owner)->recipe : NULL;
}

UiCreatureScene *UiCreatureScene_CreateTemporary(const CreatureRecipe *recipe, unsigned candidate,
    char *error, size_t capacity)
{
    UiCreatureScene *scene = Allocate(error,capacity);
    if (!scene) return NULL;
    scene->docs[0] = malloc(sizeof(*scene->docs[0]));
    if (scene->docs[0]) {
        CreatureRecipeDocument_Copy(scene->docs[0],recipe);
        const CreatureRecipe *copy = &scene->docs[0]->recipe;
        scene->temporaryCandidate = candidate;
        scene->from = copy->id;
        if (Creature_BuildMonster(&scene->adult,copy,&copy->adult) &&
            UiCreatureScene_SetAge(scene,0.0f)) return scene;
    }
    snprintf(error,capacity,"ERROR: Cannot build the offspring scene");
    UiCreatureScene_Destroy(scene);
    return NULL;
}

UiCreatureScene *UiCreatureScene_Create(const char *directory, const UiVfsNode *first,
                                       const UiVfsNode *second, char *error, size_t capacity)
{
    UiCreatureScene *scene = Allocate(error,capacity);
    if (!scene) return NULL;
    scene->from = first->recipeId;
    scene->to = second ? second->recipeId : 0;
    const CreatureRecipe *a = Load(directory,first,&scene->docs[0],error,capacity);
    if (!a) goto fail;
    if (!Creature_BuildMonster(&scene->adult,a,CreatureRecipe_GetStage(a,CREATURE_STAGE_ADULT))) {
        snprintf(error,capacity,"ERROR: Cannot build adult creature %s",first->name);
        goto fail;
    }

    if (second) {
        const CreatureRecipe *b = Load(directory,second,&scene->docs[1],error,capacity);
        if (!b) goto fail;
        Monster target = Monster_Create();
        bool built = Creature_BuildMonster(&target,b,CreatureRecipe_GetStage(b,CREATURE_STAGE_ADULT));
        if (built && CreatureMorph_Interpolate(&scene->adult,&target,0.5f,&scene->reference)) {
            scene->ager = malloc(sizeof(*scene->ager));
            if (scene->ager) {
                *scene->ager = MonsterAger_Create(&scene->adult,&target,0.0f);
                MonsterAger_SetGeometrySnapshotEnabled(scene->ager,false);
            }
        }
        Monster_Free(&target);
        if (!scene->ager) {
            snprintf(error,capacity,"ERROR: Cannot construct the morph endpoints");
            goto fail;
        }
    }
    if (UiCreatureScene_SetAge(scene,0.0f)) return scene;
    snprintf(error,capacity,"ERROR: Cannot build the creature SDF");

fail:
    UiCreatureScene_Destroy(scene);
    return NULL;
}
