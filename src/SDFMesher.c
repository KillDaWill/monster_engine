#include "SDFMesher.h"
#include "MarchingCubes.h"
#include "MonsterSDF.h"
#include "MathUtils.h"
#include <stdlib.h>

static const Vector3 CORNER_OFFSETS[8] = {
    {0.0f, 0.0f, 0.0f}, /* 0 */
    {1.0f, 0.0f, 0.0f}, /* 1 */
    {1.0f, 0.0f, 1.0f}, /* 2 */
    {0.0f, 0.0f, 1.0f}, /* 3 */
    {0.0f, 1.0f, 0.0f}, /* 4 */
    {1.0f, 1.0f, 0.0f}, /* 5 */
    {1.0f, 1.0f, 1.0f}, /* 6 */
    {0.0f, 1.0f, 1.0f}  /* 7 */
};

SDFMesherConfig SDFMesher_DefaultConfig(void) {
    return (SDFMesherConfig){
        .resolutionX = 32,
        .resolutionY = 32,
        .resolutionZ = 32,
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

bool SDFMesher_GenerateMesh(
    SDFMesher* mesher,
    SDFEvaluateFn evalFn,
    const void* context,
    Mesh* outMesh
) {
    if (!mesher || !evalFn || !outMesh) return false;

    Mesh_Clear(outMesh);

    SDFMesherConfig cfg = mesher->config;

    /* Si se requiere autobounds y el contexto es un MonsterSDF, calcular bounds automáticamente */
    if (cfg.useAutoBounds && context) {
        /* Intentar obtener bounds si es un MonsterSDF */
        AABB3D bounds = MonsterSDF_GetBounds((const MonsterSDF*)context);
        cfg.bounds = bounds;
    }

    int resX = Math_Max(cfg.resolutionX, 2);
    int resY = Math_Max(cfg.resolutionY, 2);
    int resZ = Math_Max(cfg.resolutionZ, 2);

    Vector3 size = Vec3_Sub(cfg.bounds.end, cfg.bounds.start);
    Vector3 step = Vec3_Create(
        size.x / (float)resX,
        size.y / (float)resY,
        size.z / (float)resZ
    );

    /* Reserva de memoria inicial estimativa */
    Mesh_ReserveVertices(outMesh, (size_t)(resX * resY * resZ / 2));
    Mesh_ReserveIndices(outMesh, (size_t)(resX * resY * resZ));

    MarchingCubesCell cell;

    for (int ix = 0; ix < resX; ++ix) {
        float x0 = cfg.bounds.start.x + (float)ix * step.x;

        for (int iy = 0; iy < resY; ++iy) {
            float y0 = cfg.bounds.start.y + (float)iy * step.y;

            for (int iz = 0; iz < resZ; ++iz) {
                float z0 = cfg.bounds.start.z + (float)iz * step.z;

                /* Rellenar esquinas de la celda */
                for (int c = 0; c < 8; ++c) {
                    Vector3 cornerPos = Vec3_Create(
                        x0 + CORNER_OFFSETS[c].x * step.x,
                        y0 + CORNER_OFFSETS[c].y * step.y,
                        z0 + CORNER_OFFSETS[c].z * step.z
                    );

                    cell.corners[c] = cornerPos;
                    cell.samples[c] = evalFn(context, cornerPos);
                }

                /* Poligonizar la celda */
                MarchingCubes_PolygonizeCell(
                    &cell,
                    cfg.isolevel,
                    outMesh,
                    evalFn,
                    context,
                    cfg.normalEps
                );
            }
        }
    }

    return true;
}
