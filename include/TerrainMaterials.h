/** @file TerrainMaterials.h
 *  @brief Texturas periódicas compartidas del entorno, propiedad del contexto OpenGL.
 */
#ifndef MONSTER_TERRAIN_MATERIALS_H
#define MONSTER_TERRAIN_MATERIALS_H

#include <stdbool.h>
#include <stddef.h>

typedef struct TerrainMaterials {
    unsigned int textures[3]; /* Albedo sRGB, normal XY, rugosidad/AO. */
    size_t vramBytes;
    bool attempted;
    int packVersion;
} TerrainMaterials;

typedef struct TerrainMaterialBindings {
    int enabled;
    int sampler[3];
    int packVersion;
} TerrainMaterialBindings;

typedef struct TerrainMaterialState {
    int activeUnit;
    int texture[3];
    unsigned touched;
} TerrainMaterialState;

/** @brief Carga una vez los recursos locales del entorno en el hilo GL. */
void TerrainMaterials_Load(TerrainMaterials *materials);
/** @brief Libera las texturas en el contexto propietario. */
void TerrainMaterials_Free(TerrainMaterials *materials);
/** @brief Resuelve ubicaciones para el programa de terreno, agua o rocas. */
TerrainMaterialBindings TerrainMaterials_GetBindings(unsigned int program);
/** @brief Vincula las matrices de texturas y conserva su estado anterior. */
TerrainMaterialState TerrainMaterials_Bind(const TerrainMaterials *materials,
                                           const TerrainMaterialBindings *bindings);
/** @brief Restaura las unidades tocadas por TerrainMaterials_Bind. */
void TerrainMaterials_Restore(const TerrainMaterialState *state);

#endif
