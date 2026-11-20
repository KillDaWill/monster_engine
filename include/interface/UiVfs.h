#ifndef UI_VFS_H
#define UI_VFS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { UI_VFS_LIMIT = 4096, UI_VFS_NAME_BYTES = 256, UI_VFS_PATH_BYTES = 1024 };
typedef uint32_t UiVfsId;
typedef struct UiVfs UiVfs;
typedef enum { UI_VFS_DIRECTORY, UI_VFS_FILE } UiVfsType;
typedef enum { UI_VFS_VIRTUAL, UI_VFS_EXTERNAL_RECIPE, UI_VFS_BUILTIN_RECIPE } UiVfsSource;
typedef enum { UI_VFS_OK, UI_VFS_INVALID, UI_VFS_MISSING, UI_VFS_NOT_DIRECTORY } UiVfsResult;
typedef struct {
    UiVfsId id, parent;
    char name[UI_VFS_NAME_BYTES];
    UiVfsType type;
    UiVfsSource source;
    uint32_t recipeId;
    bool readOnly;
} UiVfsNode;

UiVfs *UiVfs_Create(void);
void UiVfs_Destroy(UiVfs *);
UiVfsId UiVfs_Root(const UiVfs *);
/* La aplicación construye el árbol. Los IDs y los nodos viven hasta destruirlo. */
bool UiVfs_CanAdd(const UiVfs *, UiVfsId parent, const char *name);
UiVfsId UiVfs_Add(UiVfs *, UiVfsId parent, const char *name, UiVfsType, UiVfsSource, uint32_t recipeId);
const UiVfsNode *UiVfs_Get(const UiVfs *, UiVfsId);
/* Comprueba los metadatos del nodo. La carga valida el contenido de la receta. */
bool UiVfs_IsRecipe(const UiVfsNode *);
UiVfsId UiVfs_FirstChild(const UiVfs *, UiVfsId);
UiVfsId UiVfs_NextSibling(const UiVfs *, UiVfsId);
UiVfsId UiVfs_FindChild(const UiVfs *, UiVfsId, const char *name);
UiVfsResult UiVfs_Resolve(const UiVfs *, UiVfsId cwd, const char *path, UiVfsId *result);
bool UiVfs_Path(const UiVfs *, UiVfsId, char *out, size_t capacity);
#endif
