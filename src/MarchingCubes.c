#include "MarchingCubes.h"
#include "MarchingCubesTables.h"
#include "MathUtils.h"
#include <math.h>

static const int EDGE_CONNECTIONS[12][2] = {
    {0, 1}, {1, 2}, {2, 3}, {3, 0},
    {4, 5}, {5, 6}, {6, 7}, {7, 4},
    {0, 4}, {1, 5}, {2, 6}, {3, 7}
};

static MeshVertex InterpolateEdgeVertex(
    Vector3 p1, float d1, Color c1,
    Vector3 p2, float d2, Color c2,
    float isolevel,
    SDFEvaluateFn evalFn,
    const void* context,
    float normalEps
) {
    float t = 0.5f;
    float denom = d2 - d1;
    if (fabsf(denom) > 1e-6f) {
        t = (isolevel - d1) / denom;
    }
    t = Math_Clamp01(t);

    Vector3 pos = Vec3_Lerp(p1, p2, t);
    Color col = Color_Lerp(c1, c2, t);
    Vector3 norm = SDF_EstimateNormal(evalFn, context, pos, normalEps);

    return (MeshVertex){
        .position = pos,
        .normal = norm,
        .color = col
    };
}

void MarchingCubes_PolygonizeCell(
    const MarchingCubesCell* cell,
    float isolevel,
    Mesh* mesh,
    SDFEvaluateFn evalFn,
    const void* context,
    float normalEps
) {
    if (!cell || !mesh) return;

    /* 1. Calcular el índice de configuración de la celda */
    int cubeIndex = 0;
    for (int i = 0; i < 8; ++i) {
        if (cell->samples[i].distance < isolevel) {
            cubeIndex |= (1 << i);
        }
    }

    /* 2. Si está completamente dentro o completamente fuera, no hay intersección */
    int edgeFlags = MARCHING_CUBES_EDGE_TABLE[cubeIndex];
    if (edgeFlags == 0) return;

    /* 3. Calcular vértices interpolados para cada arista cortada */
    MeshVertex edgeVertices[12];

    for (int i = 0; i < 12; ++i) {
        if (edgeFlags & (1 << i)) {
            int c1 = EDGE_CONNECTIONS[i][0];
            int c2 = EDGE_CONNECTIONS[i][1];

            edgeVertices[i] = InterpolateEdgeVertex(
                cell->corners[c1], cell->samples[c1].distance, cell->samples[c1].color,
                cell->corners[c2], cell->samples[c2].distance, cell->samples[c2].color,
                isolevel, evalFn, context, normalEps
            );
        }
    }

    /* 4. Emitir triángulos según triTable */
    for (int i = 0; MARCHING_CUBES_TRI_TABLE[cubeIndex][i] != -1; i += 3) {
        int e0 = MARCHING_CUBES_TRI_TABLE[cubeIndex][i];
        int e1 = MARCHING_CUBES_TRI_TABLE[cubeIndex][i + 1];
        int e2 = MARCHING_CUBES_TRI_TABLE[cubeIndex][i + 2];

        unsigned int idx0 = (unsigned int)Mesh_AddVertex(mesh, edgeVertices[e0]);
        unsigned int idx1 = (unsigned int)Mesh_AddVertex(mesh, edgeVertices[e1]);
        unsigned int idx2 = (unsigned int)Mesh_AddVertex(mesh, edgeVertices[e2]);

        Mesh_AddTriangle(mesh, idx0, idx1, idx2);
    }
}
