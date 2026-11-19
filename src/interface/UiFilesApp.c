#include "interface/UiFilesApp.h"
#include <string.h>
#include <stdio.h>

void UiFilesApp_Init(UiFilesApp *app, const UiVfs *fs)
{
    *app = (UiFilesApp){.fs=fs,.directory=UiVfs_Root(fs)};
}

static void Reset(void *state)
{
    UiFilesApp *app = state;
    UiFilesApp_Init(app,app->fs);
}

bool UiFilesApp_Open(UiFilesLaunch *launch)
{
    UiFilesApp *app = launch->files;
    if (UiDesktop_Find(launch->desktop,app->window)) {
        UiDesktop_Activate(launch->desktop,app->window);
        return true;
    }
    UiWindow presentation = launch->presentation;
    presentation.state = app;
    presentation.onClose = Reset;
    app->window = UiDesktop_Add(launch->desktop,presentation);
    return app->window != 0;
}

UiCommandResult UiFilesApp_Command(void *state, const char *args, char *out, size_t capacity)
{
    if (*args) snprintf(out,capacity,"Files: Expected no arguments");
    else if (UiFilesApp_Open(state)) return UI_COMMAND_SILENT;
    else snprintf(out,capacity,"Files: Desktop window limit reached");
    return UI_COMMAND_ERROR;
}

UiCommandResult UiFilesApp_CloseCommand(void *state, const char *args, char *out, size_t capacity)
{
    UiFilesLaunch *launch = state;
    if (*args) {
        snprintf(out,capacity,"Files.close: Expected no arguments");
        return UI_COMMAND_ERROR;
    }
    if (UiDesktop_Close(launch->desktop,launch->files->window)) return UI_COMMAND_SILENT;
    snprintf(out,capacity,"Files is already closed.");
    return UI_COMMAND_OK;
}

static void Location(UiFilesApp *app, UiVfsId id)
{
    app->directory = id;
    app->selected = 0;
    app->firstRow = 0;
    app->previewUnavailable = false;
}

bool UiFilesApp_Go(UiFilesApp *app, UiVfsId id)
{
    const UiVfsNode *node = UiVfs_Get(app->fs,id);
    if (!node || node->type != UI_VFS_DIRECTORY || id == app->directory) return false;
    if (app->backCount == UI_FILES_HISTORY) {
        --app->backCount;
        memmove(app->back,app->back+1,(size_t)app->backCount*sizeof(app->back[0]));
    }
    app->back[app->backCount++] = app->directory;
    Location(app,id);
    return true;
}

bool UiFilesApp_Back(UiFilesApp *app)
{
    if (!app->backCount) return false;
    Location(app,app->back[--app->backCount]);
    return true;
}

bool UiFilesApp_Up(UiFilesApp *app)
{
    const UiVfsNode *directory = UiVfs_Get(app->fs,app->directory);
    return directory && UiFilesApp_Go(app,directory->parent);
}

void UiFilesApp_Select(UiFilesApp *app, UiVfsId id)
{
    const UiVfsNode *node = UiVfs_Get(app->fs,id);
    app->selected = node && node->parent == app->directory && id != app->directory ? id : 0;
    app->previewUnavailable = false;
}

void UiFilesApp_Activate(UiFilesApp *app)
{
    const UiVfsNode *node = UiVfs_Get(app->fs,app->selected);
    if (!node) return;
    if (node->type == UI_VFS_DIRECTORY) UiFilesApp_Go(app,node->id);
    else app->previewUnavailable = true;
}

int UiFilesApp_Count(const UiFilesApp *app)
{
    int count = 0;
    for (UiVfsId id=UiVfs_FirstChild(app->fs,app->directory); id; id=UiVfs_NextSibling(app->fs,id))
        ++count;
    return count;
}

UiVfsId UiFilesApp_Item(const UiFilesApp *app, int row)
{
    if (row < 0) return 0;
    UiVfsId id = UiVfs_FirstChild(app->fs,app->directory);
    while (id && row-- > 0) id = UiVfs_NextSibling(app->fs,id);
    return id;
}

void UiFilesApp_Scroll(UiFilesApp *app, int delta, int visibleRows)
{
    int limit = UiFilesApp_Count(app)-visibleRows;
    if (limit < 0) limit = 0;
    int64_t top = (int64_t)app->firstRow+delta;
    app->firstRow = top < 0 ? 0 : top > limit ? limit : (int)top;
}

void UiFilesApp_MoveSelection(UiFilesApp *app, int direction, int visibleRows)
{
    int row = 0;
    UiVfsId id = UiVfs_FirstChild(app->fs,app->directory);
    while (id && id != app->selected) {
        id = UiVfs_NextSibling(app->fs,id);
        ++row;
    }
    int count = id ? UiFilesApp_Count(app) : row;
    if (!count) return;
    int next = id ? row+direction : direction < 0 ? count-1 : 0;
    if (next < 0) next = 0;
    if (next >= count) next = count-1;
    UiFilesApp_Select(app,UiFilesApp_Item(app,next));
    if (next < app->firstRow) app->firstRow = next;
    if (next >= app->firstRow+visibleRows) app->firstRow = next-visibleRows+1;
    UiFilesApp_Scroll(app,0,visibleRows);
}
