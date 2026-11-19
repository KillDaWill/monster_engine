#include "interface/UiRecipeRoutes.h"
#include <string.h>

static const char *Recipe(const UiVfs *fs, UiVfsId cwd, const char *path, const UiVfsNode **recipe)
{
    UiVfsId id;
    if (UiVfs_Resolve(fs,cwd,path,&id) != UI_VFS_OK) return "Recipe not found";
    const UiVfsNode *node = UiVfs_Get(fs,id);
    if (node->type != UI_VFS_FILE || !node->recipeId ||
        (node->source != UI_VFS_EXTERNAL_RECIPE && node->source != UI_VFS_BUILTIN_RECIPE))
        return "Expected a recipe file";
    *recipe = node;
    return NULL;
}

const char *UiRecipeRoutes_Resolve(const UiVfs *fs, UiVfsId cwd, const char *route, bool pair,
                                  const UiVfsNode **first, const UiVfsNode **second)
{
    *first = *second = NULL;
    size_t size = strlen(route);
    if (!size) return pair ? "Expected two recipe routes" : "Expected a recipe route";
    if (size >= UI_VFS_PATH_BYTES) return "Recipe route too long";
    if (!pair) return Recipe(fs,cwd,route,first);

    char left[UI_VFS_PATH_BYTES];
    const UiVfsNode *a = NULL, *b = NULL;
    bool firstFound = false, secondInvalidType = false;
    for (const char *dot = strchr(route,'.'); dot; dot = strchr(dot+1,'.')) {
        if (dot == route) continue;
        size_t bytes = dot-route;
        memcpy(left,route,bytes);
        left[bytes] = '\0';
        const UiVfsNode *candidateA, *candidateB;
        if (Recipe(fs,cwd,left,&candidateA)) continue;
        firstFound = true;
        if (!dot[1]) continue;
        const char *issue = Recipe(fs,cwd,dot+1,&candidateB);
        if (issue) {
            UiVfsId id;
            if (UiVfs_Resolve(fs,cwd,dot+1,&id) == UI_VFS_OK) secondInvalidType = true;
            continue;
        }
        if (a) return "Ambiguous recipe routes";
        a = candidateA;
        b = candidateB;
    }
    if (!a) {
        if (secondInvalidType) return "Expected a recipe file for the second route";
        if (firstFound) return "Second recipe not found";
        return "Expected two valid recipe routes";
    }
    *first = a;
    *second = b;
    return NULL;
}
