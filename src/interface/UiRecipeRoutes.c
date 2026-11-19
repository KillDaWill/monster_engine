#include "interface/UiRecipeRoutes.h"
#include <string.h>

static const char *ReadPath(const char **route, char path[UI_VFS_PATH_BYTES])
{
    const char *start = *route;
    if (*start != '"') return "Expected a double-quoted recipe path";
    ++start;
    const char *end = strchr(start,'"');
    if (!end) return "Missing closing double quote in recipe path";
    size_t bytes = end-start;
    if (!bytes) return "Recipe path must not be empty";
    if (bytes >= UI_VFS_PATH_BYTES) return "Recipe path too long";
    memcpy(path,start,bytes);
    path[bytes] = '\0';
    *route = end+1;
    return NULL;
}

static const char *Recipe(const UiVfs *fs, UiVfsId cwd, const char *path,
                          const UiVfsNode **recipe, bool second)
{
    UiVfsId id;
    if (UiVfs_Resolve(fs,cwd,path,&id) != UI_VFS_OK)
        return second ? "Second recipe not found" : "Recipe not found";
    const UiVfsNode *node = UiVfs_Get(fs,id);
    if (!UiVfs_IsRecipe(node))
        return second ? "Expected a recipe file for the second path" : "Expected a recipe file";
    *recipe = node;
    return NULL;
}

const char *UiRecipeRoutes_Resolve(const UiVfs *fs, UiVfsId cwd, const char *route, bool pair,
                                  const UiVfsNode **first, const UiVfsNode **second)
{
    *first = *second = NULL;
    char left[UI_VFS_PATH_BYTES], right[UI_VFS_PATH_BYTES];
    const char *error = ReadPath(&route,left);
    if (error) return error;
    if (pair) {
        if (*route != '.') return "Expected a period between two double-quoted recipe paths";
        ++route;
        error = ReadPath(&route,right);
        if (error) return error;
    }
    if (*route) return "Unexpected text after recipe path";

    const UiVfsNode *a, *b = NULL;
    error = Recipe(fs,cwd,left,&a,false);
    if (error) return error;
    if (pair) {
        error = Recipe(fs,cwd,right,&b,true);
        if (error) return error;
    }
    *first = a;
    *second = b;
    return NULL;
}
