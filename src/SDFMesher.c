#include "SDFMesher.h"
#include "MarchingCubes.h"
#include "MathUtils.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>

SDFMesherConfig SDFMesher_DefaultConfig(void) {
    return (SDFMesherConfig){
        .resolutionX = 32,
        .resolutionY = 32,
        .resolutionZ = 32,
        .voxelSize = 0.0f,
        .maxResolution = 128,
        .maxCells = 500000,
        .isolevel = 0.0f,
        .normalEps = 0.01f,
        .bounds = {
            .start = Vec3_Create(-2.0f, -2.0f, -2.0f),
            .end   = Vec3_Create(2.0f, 2.0f, 2.0f)
        },
        .useAutoBounds = true
    };
}

SDFMesher SDFMesher_Create(SDFMesherConfig config) {
    SDFMesher mesher;
    memset(&mesher, 0, sizeof(SDFMesher));
    mesher.config = config;
    return mesher;
}

void SDFMesher_Free(SDFMesher* mesher) {
    if (!mesher) return;

    if (mesher->gridSamples) free(mesher->gridSamples);
    if (mesher->gridGradients) free(mesher->gridGradients);
    if (mesher->xEdges) free(mesher->xEdges);
    if (mesher->yEdges) free(mesher->yEdges);
    if (mesher->zEdges) free(mesher->zEdges);

    mesher->gridSamples = NULL;
    mesher->gridSampleCapacity = 0;
    mesher->gridGradients = NULL;
    mesher->gridGradientCapacity = 0;
    mesher->xEdges = NULL;
    mesher->xEdgeCapacity = 0;
    mesher->yEdges = NULL;
    mesher->yEdgeCapacity = 0;
    mesher->zEdges = NULL;
    mesher->zEdgeCapacity = 0;

    memset(&mesher->lastStats, 0, sizeof(SDFMesherStats));
}

const SDFMesherStats* SDFMesher_GetLastStats(const SDFMesher* mesher) {
    return mesher ? &mesher->lastStats : NULL;
}

static inline size_t GridIndex(int x, int y, int z, int dimY, int dimZ) {
    return (size_t)x * (size_t)dimY * (size_t)dimZ + (size_t)y * (size_t)dimZ + (size_t)z;
}

typedef struct SDFResolvedGrid {
    int resX;
    int resY;
    int resZ;
    int numGridX;
    int numGridY;
    int numGridZ;
    Vector3 step;
    float effectiveVoxelSize;
    size_t cellCount;
    size_t gridPointCount;
    bool budgetAdjusted;
} SDFResolvedGrid;

static bool SDFMesher_ResolveGrid(
    const SDFMesherConfig* config,
    AABB3D bounds,
    SDFResolvedGrid* out
) {
    if (!config || !out) return false;

    Vector3 size = Vec3_Sub(bounds.end, bounds.start);
    size.x = Math_Max(size.x, 0.001f);
    size.y = Math_Max(size.y, 0.001f);
    size.z = Math_Max(size.z, 0.001f);

    int maxRes = (config->maxResolution > 0) ? config->maxResolution : 128;
    size_t maxCellsLimit = (config->maxCells > 0) ? config->maxCells : 500000;

    float effectiveVoxel = config->voxelSize;
    bool adjusted = false;

    int resX = 0, resY = 0, resZ = 0;

    if (config->voxelSize > 0.0001f) {
        resX = (int)ceilf(size.x / effectiveVoxel);
        resY = (int)ceilf(size.y / effectiveVoxel);
        resZ = (int)ceilf(size.z / effectiveVoxel);

        resX = Math_Clamp(resX, 2, maxRes);
        resY = Math_Clamp(resY, 2, maxRes);
        resZ = Math_Clamp(resZ, 2, maxRes);

        size_t cellCount = 0;
        bool multOk = Math_MulSize((size_t)resX, (size_t)resY, &cellCount) &&
                       Math_MulSize(cellCount, (size_t)resZ, &cellCount);

        if (!multOk || cellCount > maxCellsLimit) {
            adjusted = true;
            float rawCells = (size.x / effectiveVoxel) * (size.y / effectiveVoxel) * (size.z / effectiveVoxel);
            float scale = cbrtf(rawCells / (float)maxCellsLimit);
            if (scale < 1.01f) scale = 1.01f;
            effectiveVoxel *= scale;

            while (1) {
                resX = Math_Clamp((int)ceilf(size.x / effectiveVoxel), 2, maxRes);
                resY = Math_Clamp((int)ceilf(size.y / effectiveVoxel), 2, maxRes);
                resZ = Math_Clamp((int)ceilf(size.z / effectiveVoxel), 2, maxRes);

                if (Math_MulSize((size_t)resX, (size_t)resY, &cellCount) &&
                    Math_MulSize(cellCount, (size_t)resZ, &cellCount) &&
                    cellCount <= maxCellsLimit) {
                    break;
                }
                effectiveVoxel *= 1.02f;
            }
        }
    } else {
        resX = Math_Clamp(config->resolutionX, 2, maxRes);
        resY = Math_Clamp(config->resolutionY, 2, maxRes);
        resZ = Math_Clamp(config->resolutionZ, 2, maxRes);

        size_t cellCount = 0;
        bool multOk = Math_MulSize((size_t)resX, (size_t)resY, &cellCount) &&
                       Math_MulSize(cellCount, (size_t)resZ, &cellCount);

        if (!multOk || cellCount > maxCellsLimit) {
            adjusted = true;
            float scale = cbrtf((float)cellCount / (float)maxCellsLimit);
            float factor = 1.0f / scale;
            resX = Math_Clamp((int)floorf((float)resX * factor), 2, maxRes);
            resY = Math_Clamp((int)floorf((float)resY * factor), 2, maxRes);
            resZ = Math_Clamp((int)floorf((float)resZ * factor), 2, maxRes);

            while (1) {
                if (Math_MulSize((size_t)resX, (size_t)resY, &cellCount) &&
                    Math_MulSize(cellCount, (size_t)resZ, &cellCount) &&
                    cellCount <= maxCellsLimit) {
                    break;
                }
                if (resX >= resY && resX >= resZ && resX > 2) resX--;
                else if (resY >= resX && resY >= resZ && resY > 2) resY--;
                else if (resZ > 2) resZ--;
                else break;
            }
        }
    }

    size_t finalCellCount = 0;
    if (!Math_MulSize((size_t)resX, (size_t)resY, &finalCellCount) ||
        !Math_MulSize(finalCellCount, (size_t)resZ, &finalCellCount)) {
        return false;
    }

    int numGridX = resX + 1;
    int numGridY = resY + 1;
    int numGridZ = resZ + 1;

    size_t gridPointCount = 0;
    if (!Math_MulSize((size_t)numGridX, (size_t)numGridY, &gridPointCount) ||
        !Math_MulSize(gridPointCount, (size_t)numGridZ, &gridPointCount)) {
        return false;
    }

    out->resX = resX;
    out->resY = resY;
    out->resZ = resZ;
    out->numGridX = numGridX;
    out->numGridY = numGridY;
    out->numGridZ = numGridZ;
    out->step = Vec3_Create(
        size.x / (float)resX,
        size.y / (float)resY,
        size.z / (float)resZ
    );
    out->effectiveVoxelSize = (config->voxelSize > 0.0001f) ? effectiveVoxel : 0.0f;
    out->cellCount = finalCellCount;
    out->gridPointCount = gridPointCount;
    out->budgetAdjusted = adjusted;

    return true;
}

static bool EnsureBufferCapacity(void** buffer, size_t* currentCapacity, size_t requiredCapacity, size_t elementSize) {
    if (!buffer || !currentCapacity) return false;
    if (*currentCapacity >= requiredCapacity && *buffer != NULL) return true;

    size_t newCap = 0;
    if (!Math_GrowCapacity(*currentCapacity, requiredCapacity, elementSize, &newCap)) {
        return false;
    }

    void* newBuf = realloc(*buffer, newCap * elementSize);
    if (!newBuf) return false;

    *buffer = newBuf;
    *currentCapacity = newCap;
    return true;
}

bool SDFMesher_GenerateMesh(
    SDFMesher* mesher,
    const SDFField* field,
    Mesh* outMesh
) {
    if (!mesher || !field || !field->evaluate || !outMesh) return false;

    Mesh_Clear(outMesh);

    SDFMesherConfig cfg = mesher->config;

    /* Si se requiere autobounds y el campo provee getBounds */
    if (cfg.useAutoBounds && field->getBounds) {
        cfg.bounds = field->getBounds(field->context);
    }

    SDFResolvedGrid grid;
    if (!SDFMesher_ResolveGrid(&cfg, cfg.bounds, &grid)) {
        return false;
    }

    Vector3 step = grid.step;
    float normalEps = cfg.normalEps;
    if (normalEps <= 0.0f) {
        float minStep = Math_Min(Math_Min(step.x, step.y), step.z);
        normalEps = Math_Clamp(minStep * 0.25f, 1e-5f, 1.0f);
    }

    /* Asegurar capacidad de workspace buffers */
    if (!EnsureBufferCapacity((void**)&mesher->gridSamples, &mesher->gridSampleCapacity, grid.gridPointCount, sizeof(SDFSample)) ||
        !EnsureBufferCapacity((void**)&mesher->gridGradients, &mesher->gridGradientCapacity, grid.gridPointCount, sizeof(Vector3))) {
        return false;
    }

    size_t numXEdges = 0, numYEdges = 0, numZEdges = 0;
    size_t tmp = 0;
    if (!Math_MulSize((size_t)grid.resX, (size_t)grid.numGridY, &tmp) || !Math_MulSize(tmp, (size_t)grid.numGridZ, &numXEdges) ||
        !Math_MulSize((size_t)grid.numGridX, (size_t)grid.resY, &tmp) || !Math_MulSize(tmp, (size_t)grid.numGridZ, &numYEdges) ||
        !Math_MulSize((size_t)grid.numGridX, (size_t)grid.numGridY, &tmp) || !Math_MulSize(tmp, (size_t)grid.resZ, &numZEdges)) {
        return false;
    }

    if (!EnsureBufferCapacity((void**)&mesher->xEdges, &mesher->xEdgeCapacity, numXEdges, sizeof(MeshIndex)) ||
        !EnsureBufferCapacity((void**)&mesher->yEdges, &mesher->yEdgeCapacity, numYEdges, sizeof(MeshIndex)) ||
        !EnsureBufferCapacity((void**)&mesher->zEdges, &mesher->zEdgeCapacity, numZEdges, sizeof(MeshIndex))) {
        return false;
    }

    memset(mesher->xEdges, 0xFF, numXEdges * sizeof(MeshIndex));
    memset(mesher->yEdges, 0xFF, numYEdges * sizeof(MeshIndex));
    memset(mesher->zEdges, 0xFF, numZEdges * sizeof(MeshIndex));

    size_t fieldEvalCount = 0;

    /* 1. Evaluar el campo escalar en todos los nodos de la rejilla */
    for (int ix = 0; ix < grid.numGridX; ++ix) {
        float x = cfg.bounds.start.x + (float)ix * step.x;
        for (int iy = 0; iy < grid.numGridY; ++iy) {
            float y = cfg.bounds.start.y + (float)iy * step.y;
            for (int iz = 0; iz < grid.numGridZ; ++iz) {
                float z = cfg.bounds.start.z + (float)iz * step.z;
                size_t gIdx = GridIndex(ix, iy, iz, grid.numGridY, grid.numGridZ);
                mesher->gridSamples[gIdx] = field->evaluate(field->context, Vec3_Create(x, y, z));
                fieldEvalCount++;
            }
        }
    }

    /* 2. Calcular gradientes numéricos cacheados para la rejilla completa */
    for (int ix = 0; ix < grid.numGridX; ++ix) {
        for (int iy = 0; iy < grid.numGridY; ++iy) {
            for (int iz = 0; iz < grid.numGridZ; ++iz) {
                size_t gIdx = GridIndex(ix, iy, iz, grid.numGridY, grid.numGridZ);

                float dx = 0.0f, dy = 0.0f, dz = 0.0f;

                if (ix > 0 && ix < grid.numGridX - 1) {
                    float dNext = mesher->gridSamples[GridIndex(ix + 1, iy, iz, grid.numGridY, grid.numGridZ)].distance;
                    float dPrev = mesher->gridSamples[GridIndex(ix - 1, iy, iz, grid.numGridY, grid.numGridZ)].distance;
                    dx = (dNext - dPrev) / (2.0f * step.x);
                } else if (ix == 0) {
                    float dNext = mesher->gridSamples[GridIndex(1, iy, iz, grid.numGridY, grid.numGridZ)].distance;
                    float dCurr = mesher->gridSamples[GridIndex(0, iy, iz, grid.numGridY, grid.numGridZ)].distance;
                    dx = (dNext - dCurr) / step.x;
                } else {
                    float dCurr = mesher->gridSamples[GridIndex(ix, iy, iz, grid.numGridY, grid.numGridZ)].distance;
                    float dPrev = mesher->gridSamples[GridIndex(ix - 1, iy, iz, grid.numGridY, grid.numGridZ)].distance;
                    dx = (dCurr - dPrev) / step.x;
                }

                if (iy > 0 && iy < grid.numGridY - 1) {
                    float dNext = mesher->gridSamples[GridIndex(ix, iy + 1, iz, grid.numGridY, grid.numGridZ)].distance;
                    float dPrev = mesher->gridSamples[GridIndex(ix, iy - 1, iz, grid.numGridY, grid.numGridZ)].distance;
                    dy = (dNext - dPrev) / (2.0f * step.y);
                } else if (iy == 0) {
                    float dNext = mesher->gridSamples[GridIndex(ix, 1, iz, grid.numGridY, grid.numGridZ)].distance;
                    float dCurr = mesher->gridSamples[GridIndex(ix, 0, iz, grid.numGridY, grid.numGridZ)].distance;
                    dy = (dNext - dCurr) / step.y;
                } else {
                    float dCurr = mesher->gridSamples[GridIndex(ix, iy, iz, grid.numGridY, grid.numGridZ)].distance;
                    float dPrev = mesher->gridSamples[GridIndex(ix, iy - 1, iz, grid.numGridY, grid.numGridZ)].distance;
                    dy = (dCurr - dPrev) / step.y;
                }

                if (iz > 0 && iz < grid.numGridZ - 1) {
                    float dNext = mesher->gridSamples[GridIndex(ix, iy, iz + 1, grid.numGridY, grid.numGridZ)].distance;
                    float dPrev = mesher->gridSamples[GridIndex(ix, iy, iz - 1, grid.numGridY, grid.numGridZ)].distance;
                    dz = (dNext - dPrev) / (2.0f * step.z);
                } else if (iz == 0) {
                    float dNext = mesher->gridSamples[GridIndex(ix, iy, 1, grid.numGridY, grid.numGridZ)].distance;
                    float dCurr = mesher->gridSamples[GridIndex(ix, iy, 0, grid.numGridY, grid.numGridZ)].distance;
                    dz = (dNext - dCurr) / step.z;
                } else {
                    float dCurr = mesher->gridSamples[GridIndex(ix, iy, iz, grid.numGridY, grid.numGridZ)].distance;
                    float dPrev = mesher->gridSamples[GridIndex(ix, iy, iz - 1, grid.numGridY, grid.numGridZ)].distance;
                    dz = (dCurr - dPrev) / step.z;
                }

                mesher->gridGradients[gIdx] = Vec3_Create(dx, dy, dz);
            }
        }
    }

    Mesh_ReserveVertices(outMesh, grid.gridPointCount / 4);
    Mesh_ReserveIndices(outMesh, grid.gridPointCount / 2);

    bool success = true;

    /* 3. Recorrer celdas y poligonizar */
    for (int ix = 0; ix < grid.resX && success; ++ix) {
        float x0 = cfg.bounds.start.x + (float)ix * step.x;

        for (int iy = 0; iy < grid.resY && success; ++iy) {
            float y0 = cfg.bounds.start.y + (float)iy * step.y;

            for (int iz = 0; iz < grid.resZ && success; ++iz) {
                float z0 = cfg.bounds.start.z + (float)iz * step.z;

                Vector3 corners[8];
                SDFSample samples[8];
                size_t cornerIndices[8];

                int cubeIndex = 0;
                for (int c = 0; c < 8; ++c) {
                    int gX = ix + MARCHING_CUBES_CORNER_OFFSETS[c][0];
                    int gY = iy + MARCHING_CUBES_CORNER_OFFSETS[c][1];
                    int gZ = iz + MARCHING_CUBES_CORNER_OFFSETS[c][2];

                    corners[c] = Vec3_Create(
                        x0 + (float)MARCHING_CUBES_CORNER_OFFSETS[c][0] * step.x,
                        y0 + (float)MARCHING_CUBES_CORNER_OFFSETS[c][1] * step.y,
                        z0 + (float)MARCHING_CUBES_CORNER_OFFSETS[c][2] * step.z
                    );

                    size_t gIdx = GridIndex(gX, gY, gZ, grid.numGridY, grid.numGridZ);
                    cornerIndices[c] = gIdx;
                    samples[c] = mesher->gridSamples[gIdx];

                    if (samples[c].distance < cfg.isolevel) {
                        cubeIndex |= (1 << c);
                    }
                }

                uint16_t edgeFlags = MarchingCubes_GetEdgeMask(cubeIndex);
                if (edgeFlags == 0) continue;

                MeshIndex edgeVertIndices[MARCHING_CUBES_EDGE_COUNT];
                for (int e = 0; e < MARCHING_CUBES_EDGE_COUNT; ++e) {
                    edgeVertIndices[e] = UINT32_MAX;
                }

                for (int e = 0; e < 12; ++e) {
                    if (!(edgeFlags & (1 << e))) continue;

                    MeshIndex* cachePtr = NULL;
                    int axis = -1;
                    int localBase[3] = {0, 0, 0};
                    if (MarchingCubes_GetEdgeCacheInfo(e, &axis, localBase)) {
                        int gX = ix + localBase[0];
                        int gY = iy + localBase[1];
                        int gZ = iz + localBase[2];

                        if (axis == 0 && gX >= 0 && gX < grid.resX && gY >= 0 && gY < grid.numGridY && gZ >= 0 && gZ < grid.numGridZ) {
                            cachePtr = &mesher->xEdges[GridIndex(gX, gY, gZ, grid.numGridY, grid.numGridZ)];
                        } else if (axis == 1 && gX >= 0 && gX < grid.numGridX && gY >= 0 && gY < grid.resY && gZ >= 0 && gZ < grid.numGridZ) {
                            cachePtr = &mesher->yEdges[GridIndex(gX, gY, gZ, grid.resY, grid.numGridZ)];
                        } else if (axis == 2 && gX >= 0 && gX < grid.numGridX && gY >= 0 && gY < grid.numGridY && gZ >= 0 && gZ < grid.resZ) {
                            cachePtr = &mesher->zEdges[GridIndex(gX, gY, gZ, grid.numGridY, grid.resZ)];
                        }
                    }

                    if (cachePtr && *cachePtr != UINT32_MAX) {
                        edgeVertIndices[e] = *cachePtr;
                    } else {
                        int c1 = 0, c2 = 0;
                        if (!MarchingCubes_GetEdgeEndpoints(e, &c1, &c2)) {
                            fprintf(stderr, "SDFMesher: índice de arista inválido %d\n", e);
                            success = false;
                            break;
                        }

                        Vector3 p1 = corners[c1];
                        Vector3 p2 = corners[c2];
                        float d1 = samples[c1].distance;
                        float d2 = samples[c2].distance;
                        Color col1 = samples[c1].color;
                        Color col2 = samples[c2].color;

                        float t = 0.5f;
                        float denom = d2 - d1;
                        if (fabsf(denom) > 1e-6f) {
                            t = (cfg.isolevel - d1) / denom;
                        }
                        t = Math_Clamp01(t);

                        Vector3 pos = Vec3_Lerp(p1, p2, t);
                        Color col = Color_Lerp(col1, col2, t);

                        Vector3 grad1 = mesher->gridGradients[cornerIndices[c1]];
                        Vector3 grad2 = mesher->gridGradients[cornerIndices[c2]];
                        Vector3 interpGrad = Vec3_Lerp(grad1, grad2, t);

                        Vector3 norm;
                        float gradLenSq = Vec3_Dot(interpGrad, interpGrad);
                        if (gradLenSq > 1e-12f) {
                            norm = Vec3_Normalize(interpGrad);
                        } else {
                            norm = SDF_EstimateNormal(field->evaluate, field->context, pos, normalEps);
                            fieldEvalCount += 6;
                        }

                        MeshVertex vert = {
                            .position = pos,
                            .normal = norm,
                            .color = col
                        };

                        MeshIndex newIdx = 0;
                        if (!Mesh_AddVertex(outMesh, vert, &newIdx)) {
                            success = false;
                            break;
                        }

                        if (cachePtr) *cachePtr = newIdx;
                        edgeVertIndices[e] = newIdx;
                    }
                }

                if (!success) break;

                const int* triRow = MarchingCubes_GetTriangleRow(cubeIndex);
                if (!triRow) {
                    fprintf(stderr, "SDFMesher: índice de celda inválido %d\n", cubeIndex);
                    success = false;
                    break;
                }

                for (int i = 0; triRow[i] != -1; i += 3) {
                    int e0 = triRow[i];
                    int e1 = triRow[i + 1];
                    int e2 = triRow[i + 2];

                    if (e0 < 0 || e0 >= MARCHING_CUBES_EDGE_COUNT ||
                        e1 < 0 || e1 >= MARCHING_CUBES_EDGE_COUNT ||
                        e2 < 0 || e2 >= MARCHING_CUBES_EDGE_COUNT ||
                        edgeVertIndices[e0] == UINT32_MAX ||
                        edgeVertIndices[e1] == UINT32_MAX ||
                        edgeVertIndices[e2] == UINT32_MAX) {
                        fprintf(stderr,
                            "SDFMesher: fila de triángulos inválida (cubeIndex=%d, offset=%d, edges=%d,%d,%d)\n",
                            cubeIndex, i, e0, e1, e2);
                        success = false;
                        break;
                    }

                    MeshIndex idx0 = edgeVertIndices[e0];
                    MeshIndex idx1 = edgeVertIndices[e1];
                    MeshIndex idx2 = edgeVertIndices[e2];

                    if (idx0 == idx1 || idx1 == idx2 || idx0 == idx2) continue;

                    Vector3 pos0 = outMesh->vertices[idx0].position;
                    Vector3 pos1 = outMesh->vertices[idx1].position;
                    Vector3 pos2 = outMesh->vertices[idx2].position;

                    Vector3 ab = Vec3_Sub(pos1, pos0);
                    Vector3 ac = Vec3_Sub(pos2, pos0);
                    Vector3 cross = Vec3_Cross(ab, ac);
                    if (Vec3_Dot(cross, cross) <= 1e-16f) {
                        continue;
                    }

                    if (!Mesh_AddTriangle(outMesh, idx0, idx1, idx2)) {
                        success = false;
                        break;
                    }
                }
            }
        }
    }

    if (!success) {
        Mesh_Clear(outMesh);
        return false;
    }

    mesher->lastStats.resolutionX = grid.resX;
    mesher->lastStats.resolutionY = grid.resY;
    mesher->lastStats.resolutionZ = grid.resZ;
    mesher->lastStats.voxelStep = grid.step;
    mesher->lastStats.cellCount = grid.cellCount;
    mesher->lastStats.gridPointCount = grid.gridPointCount;
    mesher->lastStats.fieldEvaluationCount = fieldEvalCount;
    mesher->lastStats.generatedVertexCount = outMesh->vertexCount;
    mesher->lastStats.generatedTriangleCount = outMesh->indexCount / 3;
    mesher->lastStats.requestedVoxelSize = cfg.voxelSize;
    mesher->lastStats.effectiveVoxelSize = grid.effectiveVoxelSize;
    mesher->lastStats.cellBudgetAdjusted = grid.budgetAdjusted;

    return true;
}
