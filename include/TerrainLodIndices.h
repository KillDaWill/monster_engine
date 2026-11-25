#ifndef MONSTER_TERRAIN_LOD_INDICES_H
#define MONSTER_TERRAIN_LOD_INDICES_H

#include <stdint.h>

/** @brief Reserva índices CCW, con los bits de costura -Z,+Z,-X,+X.
 *  @param lod Nivel 0..3. En chunks parciales se usa 0.
 *  @param indices El llamador libera el buffer con free; NULL en caso de error.
 */
void TerrainLodIndices_Build(uint32_t cellsX, uint32_t cellsZ, uint32_t lod, uint32_t stitchMask,
                             uint32_t **indices, uint32_t *count);
#endif
