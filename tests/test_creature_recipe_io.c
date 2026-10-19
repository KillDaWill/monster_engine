/** @file test_creature_recipe_io.c
 * @brief Persistencia fiel, rechazo transaccional y construcción desde archivos externos.
 */
#include "CreatureRecipeIO.h"
#include "Creature.h"
#include "Monster.h"
#include "test_utils.h"
#include <stdlib.h>
#include <string.h>

static char* Encode(const CreatureRecipe* recipe){
    FILE* file=tmpfile();TEST_ASSERT(file,"Flujo temporal JSON");
    TEST_ASSERT(CreatureRecipeIO_WriteJSON(file,recipe),"Serializar receta completa");
    long size=ftell(file);rewind(file);char* text=malloc((size_t)size+1);
    TEST_ASSERT(text,"Reservar documento");TEST_ASSERT(fread(text,1,(size_t)size,file)==(size_t)size,"Leer JSON");
    text[size]=0;fclose(file);return text;
}
void run_creature_recipe_io_tests(void){
    CreatureRecipeDocument* doc=malloc(sizeof(*doc));Monster* monster=malloc(sizeof(*monster));
    TEST_ASSERT(doc&&monster,"Reservar documentos y criatura");char error[256];
    for(unsigned id=1;id<=4;++id){const CreatureRecipe* recipe=CreatureRecipes_Find(id);char* original=Encode(recipe);
        TEST_ASSERT(CreatureRecipeIO_ReadJSON(doc,original,error,sizeof(error)),"Importar las cuatro recetas actuales");
        char* restored=Encode(&doc->recipe);TEST_ASSERT(!strcmp(original,restored),"Conservar todos los campos y etapas exactamente");
        TEST_ASSERT(doc->recipe.name==doc->name,"El documento posee su nombre");
        for(int stage=0;stage<4;++stage){
            AnatomyGraph* source=malloc(sizeof(*source));AnatomyGraph* loaded=malloc(sizeof(*loaded));
            TEST_ASSERT(source&&loaded,"Reservar comparación anatómica");
            TEST_ASSERT(Creature_ResolveAnatomy(recipe,CreatureRecipe_GetStage(recipe,(CreatureStage)stage),source),"Resolver etapa fuente");
            TEST_ASSERT(Creature_ResolveAnatomy(&doc->recipe,CreatureRecipe_GetStage(&doc->recipe,(CreatureStage)stage),loaded),"Resolver etapa persistida");
            TEST_ASSERT(AnatomyGraph_Fingerprint(source)==AnatomyGraph_Fingerprint(loaded),"La receta persistida conserva la anatomía de cada etapa");
            free(source);free(loaded);
        }
        *monster=Monster_Create();TEST_ASSERT(Creature_BuildMonster(monster,&doc->recipe,&doc->recipe.adult),"Construir rig desde JSON");Monster_Free(monster);
        free(original);free(restored);
    }
    doc->recipe.id=12345;strcpy(doc->name,"Criatura \"Ámbar\" \\ bosque");
    doc->recipe.adult.axial.trunkLength*=1.2f;
    char* text=Encode(&doc->recipe);
    TEST_ASSERT(CreatureRecipeIO_ReadJSON(doc,text,error,sizeof(error)),"Nombre UTF-8, comillas y barras");
    TEST_ASSERT(doc->recipe.id==12345&&!strcmp(doc->name,"Criatura \"Ámbar\" \\ bosque"),"Identidad externa estable");
    char* version=strstr(text,"\"version\":1");TEST_ASSERT(version,"Versión explícita");version[10]='2';
    TEST_ASSERT(!CreatureRecipeIO_ReadJSON(doc,text,error,sizeof(error)),"Rechazar versiones futuras");
    TEST_ASSERT(doc->recipe.id==12345,"Error no modifica el documento destino");version[10]='1';
    char* modules=strstr(text,"\"moduleCount\":");TEST_ASSERT(modules,"Conteo serializado");
    char previous=modules[14];modules[14]='0';
    TEST_ASSERT(!CreatureRecipeIO_ReadJSON(doc,text,error,sizeof(error)),"Rechazar discrepancia entre array y conteo");modules[14]=previous;
    TEST_ASSERT(!CreatureRecipeIO_ReadJSON(doc,"{\"version\":1,\"version\":1}",error,sizeof(error)),"Rechazar campos duplicados");
    TEST_ASSERT(!CreatureRecipeIO_ReadJSON(doc,"{}",error,sizeof(error)),"No completar documentos vacíos con otra especie");
    TEST_ASSERT(!CreatureRecipeIO_ReadJSON(doc,"{",error,sizeof(error)),"Rechazar JSON truncado");
    TEST_ASSERT(!CreatureRecipeIO_ReadJSON(doc,"{\"version\":1,\"recipe\":{\"name\":\"\\",error,sizeof(error)),"Rechazar escape truncado sin leer fuera del texto");
    TEST_ASSERT(!CreatureRecipeIO_ReadJSON(doc,"{\"version\":1,\"recipe\":{\"name\":\"\\ud800 \\udc00\"}}",error,sizeof(error)),"No aceptar espacios dentro de un par Unicode");
    char* extra=malloc(strlen(text)+8);TEST_ASSERT(extra,"Reservar JSON extendido");strcpy(extra,text);strcat(extra,"false");
    TEST_ASSERT(!CreatureRecipeIO_ReadJSON(doc,extra,error,sizeof(error)),"Rechazar contenido después del documento");
    free(extra);free(text);free(doc);free(monster);
}
