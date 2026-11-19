#define _POSIX_C_SOURCE 200809L
#include "../src/interface/UiCreatureScene.h"
#include "interface/UiRecipeCatalog.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const UiVfsNode *Find(const UiVfs *fs, UiVfsId folder, const char *name)
{
    const UiVfsNode *node = UiVfs_Get(fs,UiVfs_FindChild(fs,folder,name));
    assert(node);
    return node;
}

static void CheckField(const UiCreatureScene *scene)
{
    const Monster *monster = UiCreatureScene_Monster(scene);
    assert(monster->hasAnatomyGraph && monster->anatomyGraph.nodeCount && monster->eyeCount);
    assert(scene->sdf.hasSurface);
    assert(scene->sdf.bodyPartCount || scene->sdf.connectorCount || scene->sdf.axialStationCount);
    AABB3D bounds = scene->sdf.bounds;
    assert(isfinite(bounds.start.x) && isfinite(bounds.end.x) && bounds.start.x < bounds.end.x);
    assert(isfinite(bounds.start.y) && isfinite(bounds.end.y) && bounds.start.y < bounds.end.y);
    assert(isfinite(bounds.start.z) && isfinite(bounds.end.z) && bounds.start.z < bounds.end.z);
}

static void Write(const char *path, const char *text)
{
    FILE *file = fopen(path,"wb");
    assert(file && fputs(text,file) >= 0 && fclose(file) == 0);
}

static void InvalidFullRecipe(void)
{
    char root[] = "/tmp/nexus-engine-XXXXXX";
    assert(mkdtemp(root));
    char demos[256], recipes[256], marker[256], manifest[256], file[256];
    snprintf(demos,sizeof(demos),"%s/demos",root);
    snprintf(recipes,sizeof(recipes),"%s/recipes",root);
    snprintf(marker,sizeof(marker),"%s/demos/demo_morph_species.c",root);
    snprintf(manifest,sizeof(manifest),"%s/recipes/catalog.txt",root);
    snprintf(file,sizeof(file),"%s/recipes/invalid.creature.json",root);
    assert(mkdir(demos,0700) == 0 && mkdir(recipes,0700) == 0);
    Write(marker,"/* fixture */\n");
    Write(manifest,"invalid.creature.json\n");
    Write(file,"{\"version\":1,\"recipe\":{\"id\":1008,\"name\":\"Incomplete\"}}");
    UiVfs *fs = UiVfs_Create();
    assert(fs);
    UiVfsId folder = UiVfs_Add(fs,UiVfs_Root(fs),"recepies",UI_VFS_DIRECTORY,UI_VFS_VIRTUAL,0);
    UiRecipeCatalogReport report = UiRecipeCatalog_LoadProject(fs,folder,demos);
    assert(report.external == 1);
    const UiVfsNode *node = Find(fs,folder,"invalid.creature.json");
    char error[256];
    assert(!UiCreatureScene_Create(demos,node,NULL,error,sizeof(error)));
    assert(strstr(error,"Recipe validation failed"));
    assert(unlink(file) == 0 && unlink(manifest) == 0 && unlink(marker) == 0);
    assert(rmdir(demos) == 0 && rmdir(recipes) == 0 && rmdir(root) == 0);
    UiVfs_Destroy(fs);
}

int main(void)
{
    UiVfs *fs = UiVfs_Create();
    assert(fs);
    UiVfsId folder = UiVfs_Add(fs,UiVfs_Root(fs),"recepies",UI_VFS_DIRECTORY,UI_VFS_VIRTUAL,0);
    UiRecipeCatalogReport report = UiRecipeCatalog_LoadProject(fs,folder,"demos");
    assert(report.manifestAvailable);
    const UiVfsNode *dog = Find(fs,folder,"Perro.recipe");
    const UiVfsNode *elephant = Find(fs,folder,"elefante.creature.json");
    const UiVfsNode *pig = Find(fs,folder,"cerdo.creature.json");
    const UiVfsNode *horse = Find(fs,folder,"caballo.creature.json");
    char error[256];

    UiCreatureScene *scene = UiCreatureScene_Create("demos",dog,NULL,error,sizeof(error));
    assert(scene && scene->from == dog->recipeId && !scene->docs[0] && !scene->ager);
    CheckField(scene);
    UiCreatureScene_Destroy(scene);
    scene = UiCreatureScene_Create("demos",elephant,NULL,error,sizeof(error));
    assert(scene && scene->from == elephant->recipeId && scene->docs[0]);
    assert(scene->docs[0]->recipe.name == scene->docs[0]->name);
    CheckField(scene);
    UiCreatureScene_Destroy(scene);

    scene = UiCreatureScene_Create("demos",pig,elephant,error,sizeof(error));
    assert(scene && scene->ager && scene->ager->perc == 0 && !scene->ager->geometrySnapshotEnabled);
    for (int i=0; i<=8; ++i) {
        float expected = (i<=4 ? i : 8-i)*0.25f;
        assert(UiCreatureScene_SetAge(scene,expected) && scene->ager->perc == expected);
        CheckField(scene);
        uint64_t count = scene->ager->interpolationCount;
        assert(UiCreatureScene_SetAge(scene,expected) && scene->ager->interpolationCount == count);
    }
    bool reachedEnd = false, returned = false;
    for (int i=0; i<110; ++i) {
        assert(UiCreatureScene_Advance(scene,0.1));
        if (scene->ager->perc == 1.0f) reachedEnd = true;
        if (reachedEnd && scene->ager->perc == 0.0f) returned = true;
    }
    assert(reachedEnd && returned);
    UiCreatureScene *next = UiCreatureScene_Create("demos",horse,NULL,error,sizeof(error));
    assert(next && next->adult.surface.longHairCount > 0);
    UiCreatureScene_Destroy(scene);
    next->hair = LongHairAsync_Create();
    assert(next->hair && LongHairAsync_Update(next->hair,UiCreatureScene_Monster(next),false));
    UiCreatureScene_Destroy(next);
    UiVfs_Destroy(fs);
    InvalidFullRecipe();
    puts("Interface creature engine tests passed");
}
