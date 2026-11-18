#define _POSIX_C_SOURCE 200809L
#include "interface/UiRecipeCatalog.h"
#include <fcntl.h>
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

static uint32_t RecipeId(int directory, const char *name)
{
    size_t bytes;
    FILE *file = OpenRegular(directory,name,RECIPE_BYTES,&bytes);
    if (!file) return 0;
    char *data = malloc(bytes+1);
    uint32_t id = 0;
    if (data && fread(data,1,bytes,file) == bytes && fgetc(file) == EOF &&
        !ferror(file) && !memchr(data,0,bytes)) {
        data[bytes] = '\0';
        Json json = {.at=data};
        bool valid = Object(&json,0,ENVELOPE);
        if (valid) {
            Space(&json);
            if (!*json.at && json.version == 1 && json.hasRecipe && json.hasName) id = json.id;
        }
    }
    free(data);
    fclose(file);
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

UiRecipeCatalogReport UiRecipeCatalog_LoadProject(UiVfs *fs, UiVfsId folder,
                                                 const char *executableDirectory)
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
    return Load(fs,folder,recipes);
}
