/** @file CreatureRecipes.h
 * @brief Catálogo de composiciones de datos.
 */
#ifndef CREATURE_RECIPES_H
#define CREATURE_RECIPES_H
#include "CreatureRecipe.h"
/** @return Receta inmutable del lagarto. */
const CreatureRecipe* CreatureRecipes_Lizard(void);
/** @return Receta inmutable del primer cánido digitígrado (ID persistente 2). */
const CreatureRecipe* CreatureRecipes_Dog(void);
/** @param id Identidad persistente. @return Receta registrada, o NULL. */
const CreatureRecipe* CreatureRecipes_Find(CreatureRecipeId id);
#endif
