#define _POSIX_C_SOURCE 200809L
#include "interface/UiOffspringApp.h"
#include "interface/UiRecipeCatalog.h"
#include "CreatureRecipeRegistry.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

void InterfaceTest_CheckRecipe(const UiCreatureEngine *, UiVfsId, UiVfsId);
void InterfaceTest_CheckTemporary(const UiCreatureEngine *, unsigned);

static void Execute(UiCommands *ui, const char *command, UiCommandResult expected,
    const char *contains, char out[UI_COMMAND_REPLY_BYTES])
{
    UiCommandResult result = UiCommands_Execute(ui,command,out,UI_COMMAND_REPLY_BYTES);
    if (result != expected || (contains && !strstr(out,contains)))
        fprintf(stderr,"%s · result %u · %s\n",command,result,out);
    assert(result == expected && (!contains || strstr(out,contains)));
}

static void Finish(UiOffspringApp *app)
{
    struct timespec delay = {.tv_nsec=1000000};
    for (unsigned i=0; i<4000 && UiOffspringApp_NeedsFrame(app); ++i) {
        UiOffspringApp_Update(app);
        nanosleep(&delay,NULL);
    }
    assert(!UiOffspringApp_NeedsFrame(app));
}

static UiVfs *FileSystem(UiVfsId *folder, const char *directory)
{
    UiVfs *fs = UiVfs_Create();
    assert(fs);
    *folder = UiVfs_Add(fs,UiVfs_Root(fs),"recepies",UI_VFS_DIRECTORY,UI_VFS_VIRTUAL,0);
    assert(*folder);
    assert(UiRecipeCatalog_LoadProject(fs,*folder,directory).manifestAvailable);
    return fs;
}

static void File(const char *path, const char *text)
{
    FILE *file = fopen(path,"w");
    assert(file && fputs(text,file)>=0 && fclose(file)==0);
}

static void Cleanup(const char *directory, const char *name)
{
    char path[1024];
    snprintf(path,sizeof(path),"%s/%s",directory,name);
    assert(unlink(path)==0);
}

int main(void)
{
    char root[] = "/tmp/nexus-offspring-XXXXXX";
    assert(mkdtemp(root));
    char demos[256], recipes[256], path[1024];
    snprintf(demos,sizeof(demos),"%s/demos",root);
    snprintf(recipes,sizeof(recipes),"%s/recipes",root);
    assert(mkdir(demos,0700)==0 && mkdir(recipes,0700)==0);
    snprintf(path,sizeof(path),"%s/demo_morph_species.c",demos);
    File(path,"");
    snprintf(path,sizeof(path),"%s/catalog.txt",recipes);
    File(path,"# Catalog\n");
    UiVfsId folder;
    UiVfs *fs = FileSystem(&folder,demos);
    UiShell shell;
    UiShell_Init(&shell,fs);
    UiDesktop desktop = {.area={0,0,1000,640}};
    UiCommands commands = {0};
    UiCreatureVisualizer cv = {.desktop=&desktop,.shell=&shell,.executableDirectory=demos};
    UiOffspringApp co = {.desktop=&desktop,.fs=fs,.folder=folder,.visualizer=&cv};
    assert(UiShell_Register(&shell,&commands) && UiCreatureVisualizer_Register(&cv,&commands));
    assert(UiOffspringApp_Register(&co,&commands));
    char out[UI_COMMAND_REPLY_BYTES];

    Execute(&commands,"CO.help",UI_COMMAND_OK,"CO.reroll",out);
    Execute(&commands,"CO.save.4",UI_COMMAND_ERROR,"closed",out);
    Execute(&commands,"CV.view.CO.1",UI_COMMAND_ERROR,"closed",out);
    Execute(&commands,"CreatureOffspring",UI_COMMAND_SILENT,NULL,out);
    UiWindowId id = co.window;
    Execute(&commands,"CO",UI_COMMAND_SILENT,NULL,out);
    assert(co.window==id);
    Execute(&commands,"CO.list",UI_COMMAND_OK,"No active",out);
    Execute(&commands,"CO.save.4",UI_COMMAND_ERROR,"No active",out);
    const char *malformed[] = {
        "CO.create.-4.\"x\".\"y\"", "CO.create.0.\"x\".\"y\"", "CO.create.42949672960.\"x\".\"y\"",
        "CO.create.17.\"x\".\"y\"", "CO.create.5.\"x\"", "CO.create.3.\"\".\"x\"",
        "CO.create.3.x.y", "CO.create.3.\"x\".\"y"
    };
    for (size_t i=0; i<sizeof(malformed)/sizeof(*malformed); ++i)
        Execute(&commands,malformed[i],UI_COMMAND_ERROR,NULL,out);
    Execute(&commands,"cd /recepies",UI_COMMAND_SILENT,NULL,out);
    Execute(&commands,"CO.create.3.\"Perro.recipe\".\"/recepies/Lagarto.recipe\".seed.4294967296",
        UI_COMMAND_ERROR,"Invalid seed",out);
    Execute(&commands,"CO.create.3.\"Perro.recipe\".\"Lagarto.recipe\".seed.1.extra",
        UI_COMMAND_ERROR,"Invalid seed",out);
    Execute(&commands,"CO.create.3.\"Perro.recipe\".\"Lagarto.recipe\".other",
        UI_COMMAND_ERROR,"Expected optional",out);
    Execute(&commands,"CreatureOffspring.create.5.\"Perro.recipe\".\"/recepies/Lagarto.recipe\".seed.48216",
        UI_COMMAND_OK,"48216",out);
    Finish(&co);
    assert(CreatureOffspring_Count(co.batch)==5);
    Execute(&commands,"CO.list",UI_COMMAND_OK,"#5",out);
    Execute(&commands,"CO.info.4",UI_COMMAND_OK,"Batch seed 48216",out);
    Execute(&commands,"CO.info.4294967296",UI_COMMAND_ERROR,"positive",out);
    Execute(&commands,"CO.info.99",UI_COMMAND_ERROR,"does not exist",out);
    Execute(&commands,"CO.info.4.extra",UI_COMMAND_ERROR,"Unexpected",out);
    Execute(&commands,"CreatureVisualizer.view.CO.4",UI_COMMAND_SILENT,NULL,out);
    InterfaceTest_CheckTemporary(cv.engine,4);
    UiDesktop_Hide(&desktop,id);
    Execute(&commands,"CreatureOffspring",UI_COMMAND_SILENT,NULL,out);
    assert(!UiDesktop_Find(&desktop,id)->hidden && CreatureOffspring_Count(co.batch)==5);
    Execute(&commands,"CO.save.4.\"../escaped\"",UI_COMMAND_ERROR,"Invalid custom",out);
    snprintf(path,sizeof(path),"%s/Dangling.creature.json",recipes);
    assert(symlink("/dev/null",path)==0);
    Execute(&commands,"CO.save.4.\"Dangling\"",UI_COMMAND_ERROR,"already exists",out);
    assert(!co.saved[3]);
    snprintf(path,sizeof(path),"%s/Orphan.creature.json",recipes);
    FILE *orphan = fopen(path,"w");
    assert(orphan && CreatureRecipeIO_WriteJSON(orphan,CreatureOffspring_Get(co.batch,3,NULL)) &&
        fclose(orphan)==0);
    uint32_t occupiedId = CreatureOffspring_Get(co.batch,3,NULL)->id;
    char manifest[512], backup[512];
    snprintf(manifest,sizeof(manifest),"%s/catalog.txt",recipes);
    snprintf(backup,sizeof(backup),"%s/catalog.backup",recipes);
    assert(rename(manifest,backup)==0 && symlink("catalog.backup",manifest)==0);
    Execute(&commands,"CO.save.4.\"NexusOffspring\"",UI_COMMAND_ERROR,"Cannot read",out);
    assert(!co.saved[3] && !UiVfs_FindChild(fs,folder,"NexusOffspring.creature.json"));
    assert(unlink(manifest)==0 && rename(backup,manifest)==0);
    Execute(&commands,"CO.save.4.\"NexusOffspring\"",UI_COMMAND_OK,"/recepies/NexusOffspring.creature.json",out);
    assert(co.saved[3] && UiVfs_FindChild(fs,folder,"NexusOffspring.creature.json")==co.saved[3]);
    UiVfsId saved = co.saved[3];
    assert(UiVfs_Get(fs,saved)->recipeId!=occupiedId);
    Execute(&commands,"CO.save.4.\"Other\"",UI_COMMAND_OK,"NexusOffspring",out);
    assert(!UiVfs_FindChild(fs,folder,"Other.creature.json"));
    Execute(&commands,"CV.view.CO.4",UI_COMMAND_SILENT,NULL,out);
    InterfaceTest_CheckRecipe(cv.engine,saved,0);
    Execute(&commands,"CV.view.\"NexusOffspring.creature.json\"",UI_COMMAND_SILENT,NULL,out);
    InterfaceTest_CheckRecipe(cv.engine,saved,0);
    Execute(&commands,"CO.save.3.\"NexusOffspring\"",UI_COMMAND_ERROR,"existing recipe",out);
    Execute(&commands,"CO.save.2",UI_COMMAND_OK,"/recepies/perro-lagarto-offspring-2-",out);
    char automatic[UI_VFS_NAME_BYTES];
    strcpy(automatic,UiVfs_Get(fs,co.saved[1])->name);
    const CreatureRecipe *old = CreatureOffspring_Get(co.batch,3,NULL);
    const CreatureRecipe *neighbor = CreatureOffspring_Get(co.batch,2,NULL);
    uint32_t oldId = old->id;
    Execute(&commands,"CO.reroll.4",UI_COMMAND_OK,"Rerolling",out);
    Finish(&co);
    assert(CreatureOffspring_Get(co.batch,3,NULL)->id!=oldId && !co.saved[3]);
    assert(CreatureOffspring_Get(co.batch,2,NULL)==neighbor && UiVfs_Get(fs,saved));
    const UiVfsNode *stable = UiVfs_Get(fs,saved);
    assert(UiVfs_Add(fs,folder,"Unsafe.creature.json",UI_VFS_FILE,UI_VFS_EXTERNAL_RECIPE,907));
    assert(stable==UiVfs_Get(fs,saved));
    Execute(&commands,"CO.create.3.\"Unsafe.creature.json\".\"Lagarto.recipe\"",
        UI_COMMAND_OK,"Generating",out);
    Finish(&co);
    assert(co.error && strstr(co.status,"Cannot read") && CreatureOffspring_Count(co.batch)==5);
    Execute(&commands,"CV.view.CO.4",UI_COMMAND_SILENT,NULL,out);
    InterfaceTest_CheckTemporary(cv.engine,4);
    Execute(&commands,"CO.clear",UI_COMMAND_OK,"cleared",out);
    InterfaceTest_CheckTemporary(cv.engine,0);
    assert(!co.batch && UiDesktop_Find(&desktop,co.window));
    Execute(&commands,"CO.create.16.\"Perro.recipe\".\"Lagarto.recipe\".seed.42",
        UI_COMMAND_OK,"Generating",out);
    Execute(&commands,"CreatureOffspring.close",UI_COMMAND_SILENT,NULL,out);
    assert(!co.batch && !co.job);
    Execute(&commands,"CO",UI_COMMAND_SILENT,NULL,out);
    assert(co.window!=id && !co.batch);
    UiDesktop_Update(&desktop,1.0);
    Execute(&commands,"CO.create.3.\"Perro.recipe\".\"Lagarto.recipe\".seed.7",
        UI_COMMAND_OK,"Generating",out);
    Finish(&co);
    UiDesktop_Update(&desktop,1.0);
    const UiWindow *gallery = UiDesktop_Find(&desktop,co.window);
    assert(UiWindow_IsInteractive(gallery));
    UiRect galleryContent = UiWindow_Content(gallery->rect);
    gallery->input(gallery->state,galleryContent,&(UiEvent){
        .type=UI_PRESS,.x=galleryContent.x+20,.y=galleryContent.y+92,.clicks=2
    });
    InterfaceTest_CheckTemporary(cv.engine,1);
    Execute(&commands,"CV.view.CO.1",UI_COMMAND_SILENT,NULL,out);
    InterfaceTest_CheckTemporary(cv.engine,1);
    UiDesktop_Update(&desktop,1.0);
    const UiWindow *window = UiDesktop_Find(&desktop,co.window);
    assert(window && UiWindow_IsInteractive(window));
    UiRect close = UiWindow_CloseButton(window->rect);
    UiDesktop_Press(&desktop,close.x+3,close.y+3);
    UiOffspringApp_Update(&co);
    UiDesktop_Update(&desktop,1.0);
    assert(!co.window && !co.batch && !co.job);
    InterfaceTest_CheckTemporary(cv.engine,0);
    UiDesktop_CloseAll(&desktop);
    UiVfs_Destroy(fs);

    fs = FileSystem(&folder,demos);
    saved = UiVfs_FindChild(fs,folder,"NexusOffspring.creature.json");
    assert(saved && UiVfs_FindChild(fs,folder,automatic));
    char error[256];
    assert(CreatureRecipeRegistry_LoadDirectory(recipes,error,sizeof(error)));
    const CreatureRecipe *recipe = CreatureRecipeRegistry_Find(UiVfs_Get(fs,saved)->recipeId);
    assert(recipe && !strcmp(recipe->name,"NexusOffspring"));
    CreatureRecipeRegistry_Clear();
    UiVfs_Destroy(fs);
    Cleanup(recipes,"Dangling.creature.json");
    Cleanup(recipes,"Orphan.creature.json");
    Cleanup(recipes,"NexusOffspring.creature.json");
    Cleanup(recipes,automatic);
    Cleanup(recipes,"catalog.txt");
    Cleanup(demos,"demo_morph_species.c");
    assert(!rmdir(recipes) && !rmdir(demos) && !rmdir(root));
    puts("Interface offspring tests passed");
}
