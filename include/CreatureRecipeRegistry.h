/** @file CreatureRecipeRegistry.h
 * @brief Catálogo externo inmutable, cargado antes de iniciar actores o workers.
 */
#ifndef CREATURE_RECIPE_REGISTRY_H
#define CREATURE_RECIPE_REGISTRY_H
#include "CreatureRecipe.h"
/** @brief Carga un archivo validado; rechaza IDs externos duplicados. @return Éxito. */
bool CreatureRecipeRegistry_LoadFile(const char* path,char* error,size_t errorSize);
/** @brief Carga los archivos enumerados en directorio/catalog.txt, antes de crear actores. */
bool CreatureRecipeRegistry_LoadDirectory(const char* path,char* error,size_t errorSize);
/** @brief Busca una receta externa; su identidad puede sustituir una receta incorporada. */
const CreatureRecipe* CreatureRecipeRegistry_Find(CreatureRecipeId id);
/** @brief Libera el catálogo; sólo al finalizar todos sus consumidores y workers. */
void CreatureRecipeRegistry_Clear(void);
#endif
