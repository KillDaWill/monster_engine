/**
 * @file WorldGenerator.h
 * @brief Pipeline completo determinista de generación y validación de WorldStaticData.
 */
#ifndef MONSTER_WORLD_GENERATOR_H
#define MONSTER_WORLD_GENERATOR_H

#include "WorldStaticData.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Ejecuta el pipeline completo: relieve -> erosión -> hidrología -> biomas -> geología
 *        -> revalidación de orillas -> rejilla base de navegación -> congelación y huella.
 *
 * @param recipe Receta validada (plana o procedural).
 * @param outWorld Recibe un WorldStaticData con refCount = 1 (el llamador debe liberarlo).
 */
bool WorldGenerator_Generate(const WorldRecipe *recipe, WorldStaticData **outWorld,
                             char *error, size_t errorSize);

/**
 * @brief Crea un WorldStaticData a partir de un campo de alturas sintético ya rellenado
 *        (útil para fixtures deterministas de prueba: rampas, acantilados, cuencas, crestas).
 */
bool WorldGenerator_FinalizeFromHeightfield(WorldStaticData *w, bool runHydrology,
                                            bool runGeology, char *error, size_t errorSize);

#ifdef __cplusplus
}
#endif

#endif /* MONSTER_WORLD_GENERATOR_H */
