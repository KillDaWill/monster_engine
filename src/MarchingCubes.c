#include "MarchingCubes.h"
#include <stddef.h>

uint16_t MarchingCubes_GetEdgeMask(int cubeIndex) {
    if (cubeIndex < 0 || cubeIndex > 255) return 0;

    uint16_t mask = 0;
    for (int edge = 0; edge < MARCHING_CUBES_EDGE_COUNT; ++edge) {
        int c1 = MARCHING_CUBES_EDGE_ENDPOINTS[edge][0];
        int c2 = MARCHING_CUBES_EDGE_ENDPOINTS[edge][1];
        int inside1 = (cubeIndex >> c1) & 1;
        int inside2 = (cubeIndex >> c2) & 1;
        if (inside1 != inside2) {
            mask |= (uint16_t)(1u << edge);
        }
    }
    return mask;
}

const int* MarchingCubes_GetTriangleRow(int cubeIndex) {
    if (cubeIndex < 0 || cubeIndex > 255) return NULL;
    return MARCHING_CUBES_TRI_TABLE[cubeIndex];
}

int MarchingCubes_GetTriangleCount(int cubeIndex) {
    const int* row = MarchingCubes_GetTriangleRow(cubeIndex);
    if (!row) return -1;

    int count = 0;
    for (int i = 0; row[i] != -1; ++i) {
        ++count;
    }
    return count / 3;
}

bool MarchingCubes_GetEdgeEndpoints(int edgeIndex, int* outCornerA, int* outCornerB) {
    if (edgeIndex < 0 || edgeIndex >= MARCHING_CUBES_EDGE_COUNT) return false;
    if (!outCornerA || !outCornerB) return false;

    *outCornerA = MARCHING_CUBES_EDGE_ENDPOINTS[edgeIndex][0];
    *outCornerB = MARCHING_CUBES_EDGE_ENDPOINTS[edgeIndex][1];
    return true;
}
