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
    return (SDFMesher){
        .config = config
    };
}

static inline size_t GridIndex(int x, int y, int z, int dimY, int dimZ) {
    return (size_t)x * (size_t)dimY * (size_t)dimZ + (size_t)y * (size_t)dimZ + (size_t)z;
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

    /* Determinar resolución */
    Vector3 size = Vec3_Sub(cfg.bounds.end, cfg.bounds.start);
    size.x = Math_Max(size.x, 0.001f);
    size.y = Math_Max(size.y, 0.001f);
    size.z = Math_Max(size.z, 0.001f);

    int maxRes = (cfg.maxResolution > 0) ? cfg.maxResolution : 128;
    size_t maxCellsLimit = (cfg.maxCells > 0) ? cfg.maxCells : 500000;

    int resX = cfg.resolutionX;
    int resY = cfg.resolutionY;
    int resZ = cfg.resolutionZ;

    if (cfg.voxelSize > 0.0001f) {
        resX = (int)ceilf(size.x / cfg.voxelSize);
        resY = (int)ceilf(size.y / cfg.voxelSize);
        resZ = (int)ceilf(size.z / cfg.voxelSize);
    }

    resX = Math_Clamp(resX, 2, maxRes);
    resY = Math_Clamp(resY, 2, maxRes);
    resZ = Math_Clamp(resZ, 2, maxRes);

    if ((size_t)resX * (size_t)resY * (size_t)resZ > maxCellsLimit) {
        return false;
    }

    Vector3 step = Vec3_Create(
        size.x / (float)resX,
        size.y / (float)resY,
        size.z / (float)resZ
    );

    /* normalEps automático: proporcional al paso de vóxel mínimo */
    float normalEps = cfg.normalEps;
    if (normalEps <= 0.0f) {
        float minStep = Math_Min(Math_Min(step.x, step.y), step.z);
        normalEps = Math_Clamp(minStep * 0.25f, 1e-5f, 1.0f);
    }

    int numGridX = resX + 1;
    int numGridY = resY + 1;
    int numGridZ = resZ + 1;

    size_t totalGridPoints = (size_t)numGridX * (size_t)numGridY * (size_t)numGridZ;

    /* 1. Evaluar el campo escalar en todos los nodos de la rejilla (Grid Caching) */
    SDFSample* gridSamples = (SDFSample*)malloc(totalGridPoints * sizeof(SDFSample));
    if (!gridSamples) return false;

    for (int ix = 0; ix < numGridX; ++ix) {
        float x = cfg.bounds.start.x + (float)ix * step.x;
        for (int iy = 0; iy < numGridY; ++iy) {
            float y = cfg.bounds.start.y + (float)iy * step.y;
            for (int iz = 0; iz < numGridZ; ++iz) {
                float z = cfg.bounds.start.z + (float)iz * step.z;
                size_t gIdx = GridIndex(ix, iy, iz, numGridY, numGridZ);
                gridSamples[gIdx] = field->evaluate(field->context, Vec3_Create(x, y, z));
            }
        }
    }

    /* 2. Asignar tablas de caché de vértices de aristas */
    size_t numXEdges = (size_t)resX * (size_t)numGridY * (size_t)numGridZ;
    size_t numYEdges = (size_t)numGridX * (size_t)resY * (size_t)numGridZ;
    size_t numZEdges = (size_t)numGridX * (size_t)numGridY * (size_t)resZ;

    MeshIndex* xEdges = (MeshIndex*)malloc(numXEdges * sizeof(MeshIndex));
    MeshIndex* yEdges = (MeshIndex*)malloc(numYEdges * sizeof(MeshIndex));
    MeshIndex* zEdges = (MeshIndex*)malloc(numZEdges * sizeof(MeshIndex));

    if (!xEdges || !yEdges || !zEdges) {
        if (gridSamples) free(gridSamples);
        if (xEdges) free(xEdges);
        if (yEdges) free(yEdges);
        if (zEdges) free(zEdges);
        return false;
    }

    memset(xEdges, 0xFF, numXEdges * sizeof(MeshIndex));
    memset(yEdges, 0xFF, numYEdges * sizeof(MeshIndex));
    memset(zEdges, 0xFF, numZEdges * sizeof(MeshIndex));

    /* Reserva aproximada para la malla */
    Mesh_ReserveVertices(outMesh, totalGridPoints / 4);
    Mesh_ReserveIndices(outMesh, totalGridPoints / 2);

    bool success = true;

    /* 3. Recorrer celdas y poligonizar */
    for (int ix = 0; ix < resX && success; ++ix) {
        float x0 = cfg.bounds.start.x + (float)ix * step.x;

        for (int iy = 0; iy < resY && success; ++iy) {
            float y0 = cfg.bounds.start.y + (float)iy * step.y;

            for (int iz = 0; iz < resZ && success; ++iz) {
                float z0 = cfg.bounds.start.z + (float)iz * step.z;

                /* Obtener las 8 esquinas de la celda desde Marching Cubes */
                Vector3 corners[8];
                SDFSample samples[8];

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

                    size_t gIdx = GridIndex(gX, gY, gZ, numGridY, numGridZ);
                    samples[c] = gridSamples[gIdx];

                    if (samples[c].distance < cfg.isolevel) {
                        cubeIndex |= (1 << c);
                    }
                }

                uint16_t edgeFlags = MarchingCubes_GetEdgeMask(cubeIndex);
                if (edgeFlags == 0) continue;

                /* Obtener o crear los vértices de las 12 aristas cortadas.
                   Todo índice no generado permanece en UINT32_MAX: una referencia
                   inválida a estas aristas en la fila de triángulos es un error
                   duro de topología. */
                MeshIndex edgeVertIndices[MARCHING_CUBES_EDGE_COUNT];
                for (int e = 0; e < MARCHING_CUBES_EDGE_COUNT; ++e) {
                    edgeVertIndices[e] = UINT32_MAX;
                }

                for (int e = 0; e < 12; ++e) {
                    if (!(edgeFlags & (1 << e))) continue;

                    /* Identificar puntero a caché de la arista mediante topología canónica */
                    MeshIndex* cachePtr = NULL;
                    int axis = -1;
                    int localBase[3] = {0, 0, 0};
                    if (MarchingCubes_GetEdgeCacheInfo(e, &axis, localBase)) {
                        int gX = ix + localBase[0];
                        int gY = iy + localBase[1];
                        int gZ = iz + localBase[2];

                        if (axis == 0) {
                            cachePtr = &xEdges[GridIndex(gX, gY, gZ, numGridY, numGridZ)];
                        } else if (axis == 1) {
                            cachePtr = &yEdges[GridIndex(gX, gY, gZ, resY, numGridZ)];
                        } else if (axis == 2) {
                            cachePtr = &zEdges[GridIndex(gX, gY, gZ, numGridY, resZ)];
                        }
                    }

                    if (cachePtr && *cachePtr != UINT32_MAX) {
                        edgeVertIndices[e] = *cachePtr;
                    } else {
                        /* Interpolar y crear nuevo vértice */
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
                        Vector3 norm = SDF_EstimateNormal(field->evaluate, field->context, pos, normalEps);

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

                /* Emitir triángulos desde la fila canónica con validación dura:
                   cualquier arista no generada (topología inválida) aborta la
                   generación con diagnóstico. */
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

    /* Limpieza */
    free(gridSamples);
    free(xEdges);
    free(yEdges);
    free(zEdges);

    if (!success) {
        Mesh_Clear(outMesh);
        return false;
    }

    return true;
}
