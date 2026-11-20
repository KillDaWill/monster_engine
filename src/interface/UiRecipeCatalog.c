#define _POSIX_C_SOURCE 200809L
#include "interface/UiRecipeCatalog.h"
#include <fcntl.h>
#include <fts.h>
#include <errno.h>
#include <sys/file.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum { RECIPE_BYTES = 2*1024*1024, JSON_DEPTH = 32 };

/* Metadatos de CreatureRecipes_Find, sin incluir ni inicializar las recetas C. */
static const struct { uint32_t id; const char *name; } builtins[] = {
    {1,"Lagarto.recipe"}, {2,"Perro.recipe"}, {3,"Brumacerno.recipe"}, {4,"Velkar.recipe"}
};

typedef struct { const char *at; uint32_t id, version; bool hasName, hasRecipe; } Json;
typedef enum { GENERIC, ENVELOPE, RECIPE } ObjectKind;

static void Space(Json *j) { j->at += strspn(j->at," \t\r\n"); }
static bool Take(Json *j, char c)
{
    Space(j);
    if (*j->at != c) return false;
    ++j->at;
    return true;
}

static bool Digit(char c) { return c >= '0' && c <= '9'; }
static bool Hex(char c)
{
    return Digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static bool String(Json *j, const char **start, size_t *bytes)
{
    if (!Take(j,'"')) return false;
    const char *position = j->at;
    while (*j->at && *j->at != '"') {
        unsigned char c = *j->at++;
        if (c < 32) return false;
        if (c != '\\') continue;
        char escape = *j->at;
        if (!escape) return false;
        ++j->at;
        if (escape == 'u') {
            for (int i=0; i<4; ++i) {
                if (!Hex(*j->at)) return false;
                ++j->at;
            }
        } else if (!strchr("\"\\/bfnrt",escape)) return false;
    }
    if (*j->at != '"') return false;
    if (start) *start = position;
    if (bytes) *bytes = j->at-position;
    ++j->at;
    return true;
}

static bool Number(Json *j, uint32_t *integer)
{
    Space(j);
    bool whole = *j->at != '-';
    if (!whole) ++j->at;
    if (!Digit(*j->at)) return false;
    uint32_t value = 0;
    if (*j->at == '0') ++j->at;
    else while (Digit(*j->at)) {
        unsigned digit = *j->at++ - '0';
        if (value > (UINT32_MAX-digit)/10) whole = false;
        if (whole) value = value*10+digit;
    }
    if (*j->at == '.') {
        whole = false;
        if (!Digit(*++j->at)) return false;
        while (Digit(*j->at)) ++j->at;
    }
    if (*j->at == 'e' || *j->at == 'E') {
        whole = false;
        ++j->at;
        if (*j->at == '+' || *j->at == '-') ++j->at;
        if (!Digit(*j->at)) return false;
        while (Digit(*j->at)) ++j->at;
    }
    if (integer) *integer = whole ? value : 0;
    return true;
}

static bool Value(Json *j, int depth);
static bool Key(const char *key, size_t bytes, const char *name)
{
    return bytes == strlen(name) && !memcmp(key,name,bytes);
}

static bool Object(Json *j, int depth, ObjectKind kind)
{
    if (depth == JSON_DEPTH || !Take(j,'{')) return false;
    if (Take(j,'}')) return true;
    do {
        const char *key;
        size_t bytes;
        if (!String(j,&key,&bytes) || !Take(j,':')) return false;
        if (kind == ENVELOPE && Key(key,bytes,"version")) {
            if (j->version || !Number(j,&j->version) || j->version != 1) return false;
        } else if (kind == ENVELOPE && Key(key,bytes,"recipe")) {
            if (j->hasRecipe || !Object(j,depth+1,RECIPE)) return false;
            j->hasRecipe = true;
        } else if (kind == RECIPE && Key(key,bytes,"id")) {
            if (j->id || !Number(j,&j->id) || !j->id) return false;
        } else if (kind == RECIPE && Key(key,bytes,"name")) {
            if (j->hasName || !String(j,NULL,&bytes) || !bytes) return false;
            j->hasName = true;
        } else if (!Value(j,depth+1)) return false;
    } while (Take(j,','));
    return Take(j,'}');
}

static bool Value(Json *j, int depth)
{
    if (depth == JSON_DEPTH) return false;
    Space(j);
    switch (*j->at) {
    case '{': return Object(j,depth,GENERIC);
    case '[':
        ++j->at;
        if (Take(j,']')) return true;
        do { if (!Value(j,depth+1)) return false; } while (Take(j,','));
        return Take(j,']');
    case '"': return String(j,NULL,NULL);
    default: {
        const char *word = *j->at == 't' ? "true" : *j->at == 'f' ? "false" :
                           *j->at == 'n' ? "null" : NULL;
        if (!word) return Number(j,NULL);
        size_t bytes = strlen(word);
        if (strncmp(j->at,word,bytes)) return false;
        j->at += bytes;
        return true;
    }
    }
}

static FILE *OpenRegular(int directory, const char *name, size_t limit, size_t *size)
{
    int fd = openat(directory,name,O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);
    if (fd < 0) return NULL;
    struct stat info;
    FILE *file = NULL;
    if (fstat(fd,&info) == 0 && S_ISREG(info.st_mode) && info.st_size >= 0 &&
        (uintmax_t)info.st_size <= limit) {
        file = fdopen(fd,"rb");
        if (size) *size = (size_t)info.st_size;
    }
    if (!file) close(fd);
    return file;
}

static char *ReadRecipe(int directory, const char *name)
{
    size_t bytes;
    FILE *file = OpenRegular(directory,name,RECIPE_BYTES,&bytes);
    if (!file) return NULL;
    struct stat before, after;
    char *data = malloc(bytes+1);
    if (fstat(fileno(file),&before) != 0 || !data || fread(data,1,bytes,file) != bytes || fgetc(file) != EOF ||
        ferror(file) || memchr(data,0,bytes) || fstat(fileno(file),&after) != 0 ||
        before.st_size != after.st_size || before.st_mtim.tv_sec != after.st_mtim.tv_sec ||
        before.st_mtim.tv_nsec != after.st_mtim.tv_nsec) {
        free(data);
        data = NULL;
    } else {
        data[bytes] = '\0';
    }
    fclose(file);
    return data;
}

static uint32_t RecipeId(int directory, const char *name)
{
    char *data = ReadRecipe(directory,name);
    if (!data) return 0;
    uint32_t id = 0;
    Json json = {.at=data};
    if (Object(&json,0,ENVELOPE)) {
        Space(&json);
        if (!*json.at && json.version == 1 && json.hasRecipe && json.hasName) id = json.id;
    }
    free(data);
    return id;
}

static bool Filename(const char *name, size_t bytes)
{
    const char suffix[] = ".creature.json";
    if (bytes < sizeof(suffix) || strchr(name,'/') || strstr(name,"..") ||
        strcmp(name+bytes-(sizeof(suffix)-1),suffix)) return false;
    for (size_t i=0; i<bytes; ++i) {
        char c = name[i];
        if (!Digit(c) && !(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z') &&
            c != '_' && c != '-' && c != '.') return false;
    }
    return true;
}

static bool ContainsId(const UiVfs *fs, UiVfsId folder, uint32_t recipe)
{
    for (UiVfsId id=UiVfs_FirstChild(fs,folder); id; id=UiVfs_NextSibling(fs,id))
        if (UiVfs_Get(fs,id)->recipeId == recipe) return true;
    return false;
}

static void ReadManifest(UiVfs *fs, UiVfsId folder, int directory, FILE *file,
                         UiRecipeCatalogReport *report)
{
    char name[UI_VFS_NAME_BYTES];
    int c;
    do {
        size_t length = 0;
        bool invalid = false;
        while ((c = fgetc(file)) != EOF && c != '\n') {
            if (!c || length == sizeof(name)-1) invalid = true;
            else name[length++] = (char)c;
        }
        if (invalid) { ++report->rejected; continue; }
        while (length && strchr(" \t\r",name[length-1])) --length;
        name[length] = '\0';
        if (!length || *name == '#') continue;
        if (!Filename(name,length)) { ++report->rejected; continue; }
        if (UiVfs_FindChild(fs,folder,name)) { ++report->duplicates; continue; }
        uint32_t recipe = RecipeId(directory,name);
        if (!recipe) { ++report->rejected; continue; }
        if (ContainsId(fs,folder,recipe)) { ++report->duplicates; continue; }
        if (UiVfs_Add(fs,folder,name,UI_VFS_FILE,UI_VFS_EXTERNAL_RECIPE,recipe))
            ++report->external;
        else ++report->rejected;
    } while (c != EOF);
    if (ferror(file)) ++report->rejected;
}

static UiRecipeCatalogReport Load(UiVfs *fs, UiVfsId folder, int fd)
{
    UiRecipeCatalogReport report = {0};
    if (fd >= 0) {
        FILE *file = OpenRegular(fd,"catalog.txt",RECIPE_BYTES,NULL);
        if (file) {
            report.manifestAvailable = true;
            ReadManifest(fs,folder,fd,file,&report);
            fclose(file);
        }
        close(fd);
    }
    for (size_t i=0; i<sizeof(builtins)/sizeof(builtins[0]); ++i) {
        if (ContainsId(fs,folder,builtins[i].id)) continue;
        if (UiVfs_Add(fs,folder,builtins[i].name,UI_VFS_FILE,UI_VFS_BUILTIN_RECIPE,builtins[i].id))
            ++report.builtin;
        else ++report.rejected;
    }
    return report;
}

UiRecipeCatalogReport UiRecipeCatalog_Load(UiVfs *fs, UiVfsId folder, const char *directory)
{
    int fd = directory ? open(directory,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC) : -1;
    return Load(fs,folder,fd);
}

static int ProjectRecipes(const char *executableDirectory)
{
    int directory = executableDirectory ? open(executableDirectory,O_RDONLY|O_DIRECTORY|O_CLOEXEC) : -1;
    int recipes = -1;
    if (directory >= 0) {
        FILE *marker = OpenRegular(directory,"demo_morph_species.c",RECIPE_BYTES,NULL);
        if (marker) {
            fclose(marker);
            recipes = openat(directory,"../recipes",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
        }
        close(directory);
    }
    return recipes;
}

UiRecipeCatalogReport UiRecipeCatalog_LoadProject(UiVfs *fs, UiVfsId folder,
                                                 const char *executableDirectory)
{
    return Load(fs,folder,ProjectRecipes(executableDirectory));
}

char *UiRecipeCatalog_ReadProjectRecipe(const char *executableDirectory, const UiVfsNode *node)
{
    if (!node || node->type != UI_VFS_FILE || node->source != UI_VFS_EXTERNAL_RECIPE ||
        !Filename(node->name,strlen(node->name))) return NULL;
    int recipes = ProjectRecipes(executableDirectory);
    if (recipes < 0) return NULL;
    char *json = ReadRecipe(recipes,node->name);
    close(recipes);
    return json;
}

static int CompareId(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return (x>y)-(x<y);
}

static uint32_t FreeId(int directory, const UiVfs *fs, UiVfsId folder, uint32_t preferred,
    const char *executableDirectory)
{
    struct stat pinned;
    if (fstat(directory,&pinned) != 0) return 0;
    const char suffix[] = "/../recipes";
    size_t length = strlen(executableDirectory);
    char *root = malloc(length+sizeof(suffix));
    if (!root) return 0;
    memcpy(root,executableDirectory,length);
    memcpy(root+length,suffix,sizeof(suffix));
    char *paths[] = {root,NULL};
    FTS *tree = fts_open(paths,FTS_PHYSICAL|FTS_NOCHDIR,NULL);
    if (!tree) { free(root); return 0; }
    size_t count = 0, capacity = 64;
    uint32_t *ids = malloc(capacity*sizeof(*ids));
    bool verifiedRoot = false, ok = ids != NULL;
    while (ok) {
        errno = 0;
        FTSENT *entry = fts_read(tree);
        if (!entry) { ok = errno == 0 && verifiedRoot; break; }
        if (entry->fts_level == 0 && !verifiedRoot) {
            verifiedRoot = entry->fts_info == FTS_D &&
                entry->fts_statp->st_dev == pinned.st_dev && entry->fts_statp->st_ino == pinned.st_ino;
            ok = verifiedRoot;
            continue;
        }
        if (entry->fts_info == FTS_D && entry->fts_level > 0) {
            ok = fts_set(tree,entry,FTS_SKIP) == 0;
            continue;
        }
        if (entry->fts_level == 0 && (entry->fts_info == FTS_ERR ||
            entry->fts_info == FTS_DNR || entry->fts_info == FTS_NS)) { ok = false; break; }
        if (entry->fts_info != FTS_F || !Filename(entry->fts_name,entry->fts_namelen)) continue;
        uint32_t id = RecipeId(directory,entry->fts_name);
        if (!id) continue;
        if (count == capacity) {
            if (capacity > SIZE_MAX/2/sizeof(*ids)) { ok = false; break; }
            capacity *= 2;
            uint32_t *next = realloc(ids,capacity*sizeof(*ids));
            if (!next) { ok = false; break; }
            ids = next;
        }
        ids[count++] = id;
    }
    fts_close(tree);
    free(root);
    if (!ok) { free(ids); return 0; }
    qsort(ids,count,sizeof(*ids),CompareId);
    uint32_t result = preferred >= 1000 ? preferred : 1000;
    while (ContainsId(fs,folder,result) || bsearch(&result,ids,count,sizeof(*ids),CompareId))
        result = result == UINT32_MAX ? 1000 : result+1;
    free(ids);
    return result;
}

static FILE *Temporary(int directory, char name[80])
{
    for (unsigned i=0; i<128; ++i) {
        snprintf(name,80,".nexus-recipe-%ld-%u.tmp",(long)getpid(),i);
        int fd = openat(directory,name,O_RDWR|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0666);
        if (fd < 0 && errno == EEXIST) continue;
        if (fd < 0) break;
        FILE *file = fdopen(fd,"w+b");
        if (file) return file;
        close(fd);
        unlinkat(directory,name,0);
        break;
    }
    *name = 0;
    return NULL;
}

static bool Finish(FILE *file)
{
    bool ok = !ferror(file) && fflush(file) == 0 && fsync(fileno(file)) == 0;
    return fclose(file) == 0 && ok;
}

static bool CopyManifest(FILE *source, FILE *target, const char *filename)
{
    char buffer[8192];
    size_t n;
    while ((n = fread(buffer,1,sizeof(buffer),source)))
        if (fwrite(buffer,1,n,target) != n) return false;
    return !ferror(source) && fprintf(target,"\n%s\n",filename) > 0;
}

UiVfsId UiRecipeCatalog_SaveProject(UiVfs *fs, UiVfsId folder, const char *directory,
    const char *filename, uint32_t preferred, UiRecipeWriter writer, void *context,
    char *error, size_t capacity)
{
    if (!filename || !Filename(filename,strlen(filename)) || !writer ||
        !UiVfs_CanAdd(fs,folder,filename)) {
        snprintf(error,capacity,"Invalid filename, existing recipe, or full Files directory");
        return 0;
    }
    int fd = ProjectRecipes(directory);
    if (fd < 0 || flock(fd,LOCK_EX) != 0) {
        if (fd >= 0) close(fd);
        snprintf(error,capacity,"Cannot lock the project recipes directory");
        return 0;
    }
    const char *failure = "Cannot read the project recipe catalog";
    FILE *manifest = OpenRegular(fd,"catalog.txt",RECIPE_BYTES,NULL);
    char recipeTmp[80] = "", manifestTmp[80] = "";
    UiVfsId result = 0;
    if (!manifest) goto done;
    struct stat original, current;
    if (fstat(fileno(manifest),&original) != 0) goto done;
    if ((uintmax_t)original.st_size+strlen(filename)+2 > RECIPE_BYTES) {
        failure = "Recipe catalog is full";
        goto done;
    }
    if (fstatat(fd,filename,&current,AT_SYMLINK_NOFOLLOW) == 0 || errno != ENOENT) {
        failure = "Recipe filename already exists or cannot be checked";
        goto done;
    }
    uint32_t id = FreeId(fd,fs,folder,preferred,directory);
    if (!id) { failure = "Cannot allocate a unique recipe ID"; goto done; }
    FILE *recipe = Temporary(fd,recipeTmp);
    failure = "Cannot write and validate the recipe JSON";
    if (!recipe) goto done;
    bool written = writer(recipe,id,context);
    if (!Finish(recipe) || !written) goto done;

    failure = "Cannot write the updated recipe catalog";
    FILE *updated = Temporary(fd,manifestTmp);
    if (!updated) goto done;
    written = fchmod(fileno(updated),original.st_mode & 0777) == 0 &&
        CopyManifest(manifest,updated,filename);
    if (!Finish(updated) || !written) goto done;

    failure = "Recipe catalog changed during save";
    if (fstatat(fd,"catalog.txt",&current,AT_SYMLINK_NOFOLLOW) != 0 ||
        original.st_ino != current.st_ino || original.st_dev != current.st_dev ||
        original.st_size != current.st_size || original.st_mtim.tv_sec != current.st_mtim.tv_sec ||
        original.st_mtim.tv_nsec != current.st_mtim.tv_nsec) goto done;
    failure = "Cannot publish a new recipe file";
    if (linkat(fd,recipeTmp,fd,filename,0) != 0) goto done;
    if (fsync(fd) != 0 || renameat(fd,manifestTmp,fd,"catalog.txt") != 0) {
        failure = "Cannot publish the updated recipe catalog";
        unlinkat(fd,filename,0);
        goto done;
    }
    *manifestTmp = 0;
    result = UiVfs_Add(fs,folder,filename,UI_VFS_FILE,UI_VFS_EXTERNAL_RECIPE,id);
    failure = "Recipe saved, but Files registration failed";
    if (fsync(fd) != 0 && result)
        snprintf(error,capacity,"Recipe saved, but catalog durability could not be confirmed");
done:
    if (manifest) fclose(manifest);
    if (*recipeTmp) unlinkat(fd,recipeTmp,0);
    if (*manifestTmp) unlinkat(fd,manifestTmp,0);
    close(fd);
    if (!result) snprintf(error,capacity,"%s",failure);
    return result;
}
