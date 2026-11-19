#include "UiCreatureScene.h"
#include "interface/UiRecipeCatalog.h"
#include "Creature.h"
#include "CreatureMorph.h"
#include "CreatureRecipes.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static const CreatureRecipe *Recipe(const char *directory, const UiVfsNode *node,
                                    CreatureRecipeDocument **owner, char *error, size_t capacity)
{
    if (node->source == UI_VFS_BUILTIN_RECIPE) {
        const CreatureRecipe *recipe = CreatureRecipes_Find(node->recipeId);
        if (!recipe) snprintf(error,capacity,"ERROR: Built-in recipe %u is unavailable",node->recipeId);
        return recipe;
    }

    char *json = UiRecipeCatalog_ReadProjectRecipe(directory,node);
    if (!json) {
        snprintf(error,capacity,"ERROR: Cannot read catalog recipe %s",node->name);
        return NULL;
    }
    CreatureRecipeDocument *doc = malloc(sizeof(*doc));
    char detail[192] = "Out of memory";
    bool valid = doc && CreatureRecipeIO_ReadJSON(doc,json,detail,sizeof(detail));
    free(json);
    if (valid && doc->recipe.id == node->recipeId) {
        *owner = doc;
        return &doc->recipe;
    }
    snprintf(error,capacity,"ERROR: Recipe validation failed for %s: %s",node->name,
             valid ? "Catalog ID changed since startup" : detail);
    free(doc);
    return NULL;
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

UiCreatureScene *UiCreatureScene_Create(const char *directory, const UiVfsNode *first,
                                       const UiVfsNode *second, char *error, size_t capacity)
{
    UiCreatureScene *scene = calloc(1,sizeof(*scene));
    if (!scene) {
        snprintf(error,capacity,"ERROR: Cannot allocate the creature scene");
        return NULL;
    }
    scene->from = first->recipeId;
    scene->to = second ? second->recipeId : 0;
    scene->direction = 1;
    scene->eyeAge = -1.0f;
    scene->adult = Monster_Create();
    scene->reference = Monster_Create();
    scene->sdf = MonsterSDF_Create();

    const CreatureRecipe *a = Recipe(directory,first,&scene->docs[0],error,capacity);
    if (!a) goto fail;
    if (!Creature_BuildMonster(&scene->adult,a,CreatureRecipe_GetStage(a,CREATURE_STAGE_ADULT))) {
        snprintf(error,capacity,"ERROR: Cannot build adult creature %s",first->name);
        goto fail;
    }

    if (second) {
        const CreatureRecipe *b = Recipe(directory,second,&scene->docs[1],error,capacity);
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
