/** @file CreatureRecipeIO.h
 * @brief Recetas JSON portables, versionadas y sin dependencia del renderizador.
 */
#ifndef CREATURE_RECIPE_IO_H
#define CREATURE_RECIPE_IO_H
#include "CreatureRecipe.h"
#include <stdio.h>

/** Documento propietario del nombre. No copiar por valor sin reajustar recipe.name. */
typedef struct CreatureRecipeDocument {
    CreatureRecipe recipe;
    char name[192];
} CreatureRecipeDocument;

/** @brief Lee JSON completo de versión 1 de forma transaccional.
 * @param out Documento destino; permanece intacto ante errores.
 * @param json Texto UTF-8 terminado en cero.
 * @param error Mensaje opcional. @param errorSize Capacidad del mensaje.
 * @return Éxito sintáctico y anatómico de las cuatro etapas.
 */
bool CreatureRecipeIO_ReadJSON(CreatureRecipeDocument* out, const char* json,
                               char* error, size_t errorSize);
/** @brief Escribe todos los datos semánticos sin punteros ni padding.
 * @param file Flujo destino. @param recipe Receta. @return Éxito de escritura.
 */
bool CreatureRecipeIO_WriteJSON(FILE* file, const CreatureRecipe* recipe);
/** @brief Escribe el esquema de campos, tipos, enumerados y capacidades para herramientas. */
bool CreatureRecipeIO_WriteSchema(FILE* file);
/** @brief Carga un archivo de hasta 2 MiB. @return Éxito; error recibe el motivo. */
bool CreatureRecipeIO_LoadFile(CreatureRecipeDocument* out, const char* path,
                              char* error, size_t errorSize);
#endif
