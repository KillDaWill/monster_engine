#include "interface/UiVfs.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    UiVfsNode node;
    UiVfsId first, last, next;
    size_t pathBytes, nameBytes;
} Entry;

struct UiVfs { Entry entries[UI_VFS_LIMIT]; UiVfsId count; };

static const Entry *Get(const UiVfs *fs, UiVfsId id)
{
    return fs && id && id <= fs->count ? &fs->entries[id-1] : NULL;
}

UiVfs *UiVfs_Create(void)
{
    UiVfs *fs = calloc(1, sizeof(*fs));
    if (fs) {
        fs->count = 1;
        fs->entries[0] = (Entry){.node={.id=1,.parent=1,.name="/",
            .type=UI_VFS_DIRECTORY,.readOnly=true},.pathBytes=1};
    }
    return fs;
}

void UiVfs_Destroy(UiVfs *fs) { free(fs); }
UiVfsId UiVfs_Root(const UiVfs *fs) { return fs ? 1 : 0; }

const UiVfsNode *UiVfs_Get(const UiVfs *fs, UiVfsId id)
{
    const Entry *entry = Get(fs,id);
    return entry ? &entry->node : NULL;
}

UiVfsId UiVfs_FirstChild(const UiVfs *fs, UiVfsId id)
{
    const Entry *entry = Get(fs,id);
    return entry ? entry->first : 0;
}

UiVfsId UiVfs_NextSibling(const UiVfs *fs, UiVfsId id)
{
    const Entry *entry = Get(fs,id);
    return entry ? entry->next : 0;
}

static UiVfsId Find(const UiVfs *fs, UiVfsId parent, const char *name, size_t bytes)
{
    for (const Entry *entry=Get(fs,UiVfs_FirstChild(fs,parent)); entry; entry=Get(fs,entry->next))
        if (entry->nameBytes == bytes && !memcmp(entry->node.name,name,bytes))
            return entry->node.id;
    return 0;
}

UiVfsId UiVfs_FindChild(const UiVfs *fs, UiVfsId parent, const char *name)
{
    return name ? Find(fs,parent,name,strlen(name)) : 0;
}

UiVfsId UiVfs_Add(UiVfs *fs, UiVfsId parent, const char *name, UiVfsType type,
                 UiVfsSource source, uint32_t recipeId)
{
    const Entry *p = Get(fs,parent);
    if (!p || p->node.type != UI_VFS_DIRECTORY || fs->count == UI_VFS_LIMIT ||
        !name || !*name || !strcmp(name,".") || !strcmp(name,"..") ||
        strpbrk(name,"/\r\n\t") || UiVfs_FindChild(fs,parent,name)) return 0;
    size_t bytes = strlen(name);
    size_t pathBytes = p->pathBytes + (parent != UiVfs_Root(fs)) + bytes;
    if (bytes >= UI_VFS_NAME_BYTES || pathBytes >= UI_VFS_PATH_BYTES) return 0;

    UiVfsId id = ++fs->count;
    Entry *entry = &fs->entries[id-1], *directory = &fs->entries[parent-1];
    *entry = (Entry){.node={.id=id,.parent=parent,.type=type,.source=source,
        .recipeId=recipeId,.readOnly=true},.pathBytes=pathBytes,.nameBytes=bytes};
    memcpy(entry->node.name,name,bytes+1);
    if (directory->last) fs->entries[directory->last-1].next = id;
    else directory->first = id;
    directory->last = id;
    return id;
}

UiVfsResult UiVfs_Resolve(const UiVfs *fs, UiVfsId cwd, const char *path, UiVfsId *result)
{
    *result = 0;
    if (!path || !*path) return UI_VFS_INVALID;
    UiVfsId id = *path == '/' ? UiVfs_Root(fs) : cwd;
    while (*path) {
        const UiVfsNode *node = UiVfs_Get(fs,id);
        if (!node) return UI_VFS_INVALID;
        if (node->type != UI_VFS_DIRECTORY) return UI_VFS_NOT_DIRECTORY;
        if (*path == '/') {
            path += strspn(path,"/");
            continue;
        }
        size_t bytes = strcspn(path,"/");
        if (bytes == 2 && !memcmp(path,"..",2)) id = node->parent;
        else if (!(bytes == 1 && *path == '.')) id = Find(fs,id,path,bytes);
        if (!id) return UI_VFS_MISSING;
        path += bytes;
    }
    *result = id;
    return UI_VFS_OK;
}

bool UiVfs_Path(const UiVfs *fs, UiVfsId id, char *out, size_t capacity)
{
    const Entry *entry = Get(fs,id);
    if (!entry || capacity <= entry->pathBytes) return false;
    size_t at = entry->pathBytes;
    out[at] = '\0';
    while (entry && entry->node.id != UiVfs_Root(fs)) {
        at -= entry->nameBytes;
        memcpy(out+at,entry->node.name,entry->nameBytes);
        out[--at] = '/';
        entry = Get(fs,entry->node.parent);
    }
    out[0] = '/';
    return true;
}
bool UiVfs_IsRecipe(const UiVfsNode *node)
{
    return node && node->type == UI_VFS_FILE && node->recipeId &&
        (node->source == UI_VFS_EXTERNAL_RECIPE || node->source == UI_VFS_BUILTIN_RECIPE);
}
