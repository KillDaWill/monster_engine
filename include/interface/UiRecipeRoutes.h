#ifndef UI_RECIPE_ROUTES_H
#define UI_RECIPE_ROUTES_H
#include "UiVfs.h"

/* Devuelve NULL al resolver la ruta, o un mensaje de error estático. */
const char *UiRecipeRoutes_ReadQuoted(const char **, char path[UI_VFS_PATH_BYTES]);
const char *UiRecipeRoutes_ResolveWithSuffix(const UiVfs *, UiVfsId cwd, const char *, bool pair,
    const UiVfsNode **first, const UiVfsNode **second, const char **suffix);
const char *UiRecipeRoutes_Resolve(const UiVfs *, UiVfsId cwd, const char *, bool pair,
                                  const UiVfsNode **first, const UiVfsNode **second);
#endif
