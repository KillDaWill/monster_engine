#include "interface/UiShell.h"
#include <stdio.h>
#include <string.h>

void UiShell_Init(UiShell *shell, const UiVfs *fs)
{
    *shell = (UiShell){.fs=fs,.cwd=UiVfs_Root(fs)};
}

bool UiShell_Location(const void *state, char *out, size_t capacity)
{
    const UiShell *shell = state;
    return UiVfs_Path(shell->fs,shell->cwd,out,capacity);
}

static UiCommandResult Pwd(void *state, const char *args, char *out, size_t capacity)
{
    if (*args) {
        snprintf(out,capacity,"pwd: Expected no arguments");
        return UI_COMMAND_ERROR;
    }
    return UiShell_Location(state,out,capacity) ? UI_COMMAND_OK : UI_COMMAND_ERROR;
}

static UiCommandResult Directory(const UiShell *shell, const char *command, const char *args,
                                 UiVfsId fallback, UiVfsId *target, char *out, size_t capacity)
{
    size_t bytes = strlen(args);
    while (bytes && (args[bytes-1] == ' ' || args[bytes-1] == '\t')) --bytes;
    if (!bytes) {
        *target = fallback;
        return UI_COMMAND_OK;
    }
    const char *error;
    if (*args == '-') error = "Options are not supported";
    else if (bytes >= UI_VFS_PATH_BYTES) error = "Path too long";
    else {
        char path[UI_VFS_PATH_BYTES];
        memcpy(path,args,bytes);
        path[bytes] = '\0';
        UiVfsResult result = UiVfs_Resolve(shell->fs,shell->cwd,path,target);
        if (result == UI_VFS_OK && UiVfs_Get(shell->fs,*target)->type == UI_VFS_DIRECTORY)
            return UI_COMMAND_OK;
        error = result == UI_VFS_MISSING ? "No such directory" :
                result == UI_VFS_INVALID ? "Invalid path" : "Not a directory";
    }
    snprintf(out,capacity,"%s: %.*s: %s",command,(int)bytes,args,error);
    return UI_COMMAND_ERROR;
}

static UiCommandResult Cd(void *state, const char *args, char *out, size_t capacity)
{
    UiShell *shell = state;
    UiVfsId target;
    if (Directory(shell,"cd",args,UiVfs_Root(shell->fs),&target,out,capacity) == UI_COMMAND_ERROR)
        return UI_COMMAND_ERROR;
    shell->cwd = target;
    return UI_COMMAND_SILENT;
}

static UiCommandResult Ls(void *state, const char *args, UiCommandOutput output)
{
    const UiShell *shell = state;
    char line[UI_COMMAND_REPLY_BYTES];
    UiVfsId target;
    if (Directory(shell,"ls",args,shell->cwd,&target,line,sizeof(line)) == UI_COMMAND_ERROR) {
        if (output.write) output.write(output.state,UI_COMMAND_ERROR,line);
        return UI_COMMAND_ERROR;
    }
    for (UiVfsId id=UiVfs_FirstChild(shell->fs,target); id; id=UiVfs_NextSibling(shell->fs,id)) {
        const UiVfsNode *node = UiVfs_Get(shell->fs,id);
        snprintf(line,sizeof(line),"%s%s",node->name,node->type == UI_VFS_DIRECTORY ? "/" : "");
        if (output.write) output.write(output.state,UI_COMMAND_OK,line);
    }
    return UI_COMMAND_OK;
}

bool UiShell_Register(UiShell *shell, UiCommands *registry)
{
    if (registry->count > UI_COMMAND_LIMIT-3 || UiCommands_Find(registry,"cd") ||
        UiCommands_Find(registry,"ls") || UiCommands_Find(registry,"pwd")) return false;
    return UiCommands_Register(registry,"cd",Cd,shell) &&
           UiCommands_RegisterLines(registry,"ls",Ls,shell) &&
           UiCommands_Register(registry,"pwd",Pwd,shell);
}
