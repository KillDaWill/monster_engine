/** @file CreatureRecipeRegistry.c
 * @brief Archivos compartidos por anatomía física, presentación y herramientas.
 */
#include "CreatureRecipeRegistry.h"
#include "CreatureRecipeIO.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <ctype.h>
static CreatureRecipeDocument* documents[64];
static size_t documentCount;
static bool registeredCleanup;
const CreatureRecipe* CreatureRecipeRegistry_Find(CreatureRecipeId id){
    for(size_t i=0;i<documentCount;++i)if(documents[i]->recipe.id==id)return &documents[i]->recipe;
    return NULL;
}
void CreatureRecipeRegistry_Clear(void){
    for(size_t i=0;i<documentCount;++i)free(documents[i]);
    documentCount=0;
}
bool CreatureRecipeRegistry_LoadFile(const char* path,char* error,size_t capacity){
    if(documentCount==64){if(error&&capacity)snprintf(error,capacity,"Catálogo externo lleno.");return false;}
    CreatureRecipeDocument* document=malloc(sizeof(*document));if(!document)return false;
    if(!CreatureRecipeIO_LoadFile(document,path,error,capacity)){free(document);return false;}
    if(CreatureRecipeRegistry_Find(document->recipe.id)){
        if(error&&capacity)snprintf(error,capacity,"ID externo duplicado: %u.",document->recipe.id);
        free(document);return false;
    }
    if(!registeredCleanup){atexit(CreatureRecipeRegistry_Clear);registeredCleanup=true;}
    documents[documentCount++]=document;return true;
}
bool CreatureRecipeRegistry_LoadDirectory(const char* path,char* error,size_t capacity){
    if(error&&capacity)error[0]=0;
    if(!path)return false;
    size_t bytes=strlen(path)+512;char* filename=malloc(bytes);if(!filename)return false;
    snprintf(filename,bytes,"%s/catalog.txt",path);FILE* catalog=fopen(filename,"r");
    if(!catalog){bool absent=errno==ENOENT;free(filename);return absent;}
    bool ok=true;size_t initial=documentCount;char line[256];
    while(ok&&fgets(line,sizeof(line),catalog)){
        size_t length=strlen(line);while(length&&isspace((unsigned char)line[length-1]))line[--length]=0;
        if(!length||line[0]=='#')continue;
        for(size_t i=0;i<length;++i)if(!isalnum((unsigned char)line[i])&&line[i]!='-'&&line[i]!='_'&&line[i]!='.')ok=false;
        if(strstr(line,"..")||length<14||strcmp(line+length-14,".creature.json"))ok=false;
        if(!ok)break;
        snprintf(filename,bytes,"%s/%s",path,line);ok=CreatureRecipeRegistry_LoadFile(filename,error,capacity);
    }
    if(ferror(catalog))ok=false;
    fclose(catalog);free(filename);
    if(!ok){while(documentCount>initial)free(documents[--documentCount]);if(error&&capacity&&!error[0])snprintf(error,capacity,"No se pudo cargar el catálogo externo.");}
    return ok;
}
