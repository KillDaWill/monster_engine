/** @file TerrainLodIndices.c
 *  @brief Triangulación de chunks con bordes compartidos por niveles LOD.
 */
#include "TerrainLodIndices.h"
#include <stdbool.h>
#include <stdlib.h>

static uint32_t localIndex(uint32_t x, uint32_t z, uint32_t stride) {
    return z * stride + x;
}

/* Genera índices sin grietas para un chunk 32x32 en el nivel LOD dado y máscara de vecinos más gruesos:
 * stitchMask bit0: North(z=0), bit1: South(z=N), bit2: West(x=0), bit3: East(x=N).
 * Cuando un borde tiene vecino 1 nivel más grueso (paso 2*step), colapsa los vértices intermedios
 * impares del borde hacia los pares para eliminar completamente cualquier T-junction / grieta. */
void TerrainLodIndices_Build(uint32_t cellsX, uint32_t cellsZ, uint32_t lod, uint32_t stitchMask,
                                 uint32_t **outIndices, uint32_t *outCount) {
    uint32_t step = 1u << lod;
    if (step > cellsX) step = cellsX;
    if (step > cellsZ) step = cellsZ;
    uint32_t rowStride = cellsX + 1u;
    uint32_t maxTris = (cellsX / step + 2u) * (cellsZ / step + 2u) * 2u;
    uint32_t *idx = malloc((size_t)maxTris * 3u * sizeof(uint32_t));
    uint32_t n = 0u;
    if (!idx) { *outIndices = NULL; *outCount = 0u; return; }

    bool stitchN = (stitchMask & 1u) != 0u;
    bool stitchS = (stitchMask & 2u) != 0u;
    bool stitchW = (stitchMask & 4u) != 0u;
    bool stitchE = (stitchMask & 8u) != 0u;
    uint32_t coarseStep = step * 2u;

    for (uint32_t z = 0; z < cellsZ; z += step) {
        uint32_t z1 = z + step;
        if (z1 > cellsZ) z1 = cellsZ;
        for (uint32_t x = 0; x < cellsX; x += step) {
            uint32_t x1 = x + step;
            if (x1 > cellsX) x1 = cellsX;

            uint32_t x00 = x, z00 = z;
            uint32_t x10 = x1, z10 = z;
            uint32_t x01 = x, z01 = z1;
            uint32_t x11 = x1, z11 = z1;

            /* Ajustar vértices intermedios en bordes que colindan con un chunk de LOD más grueso. */
            if (stitchN && z == 0u) {
                if (x00 < cellsX) x00 -= x00 % coarseStep;
                if (x10 < cellsX) x10 -= x10 % coarseStep;
            }
            if (stitchS && z1 == cellsZ) {
                if (x01 < cellsX) x01 -= x01 % coarseStep;
                if (x11 < cellsX) x11 -= x11 % coarseStep;
            }
            if (stitchW && x == 0u) {
                if (z00 < cellsZ) z00 -= z00 % coarseStep;
                if (z01 < cellsZ) z01 -= z01 % coarseStep;
            }
            if (stitchE && x1 == cellsX) {
                if (z10 < cellsZ) z10 -= z10 % coarseStep;
                if (z11 < cellsZ) z11 -= z11 % coarseStep;
            }

            uint32_t i00 = localIndex(x00, z00, rowStride);
            uint32_t i10 = localIndex(x10, z10, rowStride);
            uint32_t i01 = localIndex(x01, z01, rowStride);
            uint32_t i11 = localIndex(x11, z11, rowStride);

            /* Misma diagonal autoritativa que TerrainHeightfield: T0=(v00,v11,v10), T1=(v00,v01,v11). */
            if (((int)z11 - (int)z00) * ((int)x10 - (int)x00) -
                ((int)x11 - (int)x00) * ((int)z10 - (int)z00) > 0) {
                idx[n++] = i00; idx[n++] = i11; idx[n++] = i10;
            }
            if (((int)z01 - (int)z00) * ((int)x11 - (int)x00) -
                ((int)x01 - (int)x00) * ((int)z11 - (int)z00) > 0) {
                idx[n++] = i00; idx[n++] = i01; idx[n++] = i11;
            }
        }
    }
    *outIndices = idx;
    *outCount = n;
}

