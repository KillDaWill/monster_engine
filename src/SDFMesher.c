#include "SDFMesher.h"
#include "SDFSamplingPool.h"
#include "MonsterSDF.h"
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
        .useAutoBounds = true,
        .samplingThreadCount = 0
    };
}

SDFMesher SDFMesher_Create(SDFMesherConfig config) {
    SDFMesher mesher;
    memset(&mesher, 0, sizeof(SDFMesher));
    mesher.config = config;
    mesher.currentGradientGeneration = 1;
    return mesher;
}

void SDFMesher_Free(SDFMesher* mesher) {
    if (!mesher) return;

    if (mesher->ownedPool) {
        SDFSamplingPool_Free(mesher->ownedPool);
        mesher->ownedPool = NULL;
    }
    mesher->borrowedPool = NULL;

    for(int axis=0;axis<3;++axis) { free(mesher->coordinates[axis]); mesher->coordinates[axis]=NULL; mesher->coordinateCapacity[axis]=0; }
    free(mesher->cornerVertices);mesher->cornerVertices=NULL;mesher->cornerVertexCapacity=0;
    if (mesher->cellCandidateMask) free(mesher->cellCandidateMask);
    if (mesher->nodeCandidateMask) free(mesher->nodeCandidateMask);
    mesher->cellCandidateMask = NULL;
    mesher->cellCandidateCapacity = 0;
    mesher->nodeCandidateMask = NULL;
    mesher->nodeCandidateCapacity = 0;
    if (mesher->gridDistances) free(mesher->gridDistances);
    if (mesher->gridGradients) free(mesher->gridGradients);
    if (mesher->gradientStamp) free(mesher->gradientStamp);
    if (mesher->xEdges) free(mesher->xEdges);
    if (mesher->yEdges) free(mesher->yEdges);
    if (mesher->zEdges) free(mesher->zEdges);

    mesher->gridDistances = NULL;
    mesher->gridDistanceCapacity = 0;
    mesher->gridGradients = NULL;
    mesher->gridGradientCapacity = 0;
    mesher->gradientStamp = NULL;
    mesher->gradientStampCapacity = 0;
    mesher->currentGradientGeneration = 0;
    mesher->xEdges = NULL;
    mesher->xEdgeCapacity = 0;
    mesher->yEdges = NULL;
    mesher->yEdgeCapacity = 0;
    mesher->zEdges = NULL;
    mesher->zEdgeCapacity = 0;

    memset(&mesher->lastStats, 0, sizeof(SDFMesherStats));
}

void SDFMesher_SetSamplingPool(SDFMesher* mesher, SDFSamplingPool* pool) {
    if (!mesher) return;
    mesher->borrowedPool = pool;
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
    if(maxRes<2 || (config->maxCells>0 && config->maxCells<8)) return false;
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

    size_t oldCap = *currentCapacity;
    size_t newCap = 0;
    if (!Math_GrowCapacity(oldCap, requiredCapacity, elementSize, &newCap)) {
        return false;
    }

    void* newBuf = realloc(*buffer, newCap * elementSize);
    if (!newBuf) return false;

    if (newCap > oldCap) {
        memset((char*)newBuf + oldCap * elementSize, 0, (newCap - oldCap) * elementSize);
    }

    *buffer = newBuf;
    *currentCapacity = newCap;
    return true;
}

static Vector3 SDFMesher_GetGridGradient(
    SDFMesher* mesher,
    const SDFResolvedGrid* grid,
    int ix, int iy, int iz
) {
    size_t gIdx = GridIndex(ix, iy, iz, grid->numGridY, grid->numGridZ);
    if (mesher->gradientStamp && mesher->gradientStamp[gIdx] == mesher->currentGradientGeneration) {
        return mesher->gridGradients[gIdx];
    }

    float derivative[3];
    int pos[3]={ix,iy,iz},dims[3]={grid->numGridX,grid->numGridY,grid->numGridZ};
    for(int axis=0;axis<3;++axis) {
        int lo[3]={ix,iy,iz},hi[3]={ix,iy,iz};
        if(pos[axis]>0) --lo[axis];
        if(pos[axis]+1<dims[axis]) ++hi[axis];
        float dl=mesher->gridDistances[GridIndex(lo[0],lo[1],lo[2],grid->numGridY,grid->numGridZ)];
        float dr=mesher->gridDistances[GridIndex(hi[0],hi[1],hi[2],grid->numGridY,grid->numGridZ)];
        float dc=mesher->gridDistances[gIdx];
        const float* c=mesher->coordinates[axis];
        float hl=c[pos[axis]]-c[lo[axis]],hr=c[hi[axis]]-c[pos[axis]];
        derivative[axis]=hl>0 && hr>0 ?
            (hr*(dc-dl)/hl+hl*(dr-dc)/hr)/(hl+hr) : (dr-dl)/(hl+hr);
    }
    float dx=derivative[0],dy=derivative[1],dz=derivative[2];

    Vector3 grad = Vec3_Create(dx, dy, dz);
    mesher->gridGradients[gIdx] = grad;
    if (mesher->gradientStamp) {
        mesher->gradientStamp[gIdx] = mesher->currentGradientGeneration;
    }
    mesher->lastStats.gradientEvaluationCount++;
    return grad;
}

static float SDF_Axis(Vector3 v,int axis) { return axis==0?v.x:axis==1?v.y:v.z; }

/* La extensión de planos de muestreo evita caras incompatibles: cada vecino
 * comparte exactamente cuatro esquinas y los mismos vértices de arista. */
static int SDFMesher_AxisCoordinates(float* c,int limit,float lo,float hi,float base,
    const SDFDetailRegion* regions,size_t count,int axis,float detailScale) {
    c[0]=lo;
    int n=0;
    while(c[n]<hi) {
        float step=base;
        for(size_t i=0;i<count;++i) {
            float a=SDF_Axis(regions[i].bounds.start,axis),b=SDF_Axis(regions[i].bounds.end,axis);
            float target=regions[i].targetVoxelSize*detailScale;
            if(!isfinite(target)||target<=0 || !isfinite(a)||!isfinite(b)||a>b) continue;
            float distance=Math_Max(a-c[n],Math_Max(c[n]-b,0));
            step=Math_Min(step,target+distance*.30f);
            if(a>c[n] && c[n]+step>a)step=Math_Min(step,target);
        }
        if(n>=limit || !isfinite(step) || step<1e-7f) return 0;
        float next=Math_Min(c[n]+step,hi);
        if(next<=c[n]) return 0;
        c[++n]=next;
    }
    return n;
}

bool SDFMesher_GenerateMesh(SDFMesher* mesher,const SDFField* field,Mesh* mesh) {
    return SDFMesher_GenerateMeshDetailed(mesher,field,NULL,0,mesh);
}

typedef struct SamplingContext {
    SDFMesher* mesher;
    const SDFField* field;
    SDFResolvedGrid grid;
    SDFDistanceFn distFn;
    SDFEvaluateFn evalFn;
    const uint8_t* nodeCandidateMask;
    bool hasCandidateCulling;
    bool hasNonFinite;
    size_t threadDistanceCount[SDF_SAMPLING_POOL_MAX_THREADS];
    size_t threadSkippedNodeCount[SDF_SAMPLING_POOL_MAX_THREADS];
    size_t threadCandidateCount[SDF_SAMPLING_POOL_MAX_THREADS];
    size_t threadExactCount[SDF_SAMPLING_POOL_MAX_THREADS];
    size_t threadPrunedCount[SDF_SAMPLING_POOL_MAX_THREADS];
} SamplingContext;

static void SDFMesher_SamplingSliceWork(void* context, int startIndex, int endIndex, int threadIndex) {
    SamplingContext* ctx = (SamplingContext*)context;
    MonsterSDF_EnableThreadStats(true);
    MonsterSDF_ResetThreadStats();
    size_t localDistEvals = 0;
    size_t localSkippedNodes = 0;

    for (int ix = startIndex; ix < endIndex; ++ix) {
        float x = ctx->mesher->coordinates[0][ix];
        for (int iy = 0; iy < ctx->grid.numGridY; ++iy) {
            float y = ctx->mesher->coordinates[1][iy];
            for (int iz = 0; iz < ctx->grid.numGridZ; ++iz) {
                size_t gIdx = GridIndex(ix, iy, iz, ctx->grid.numGridY, ctx->grid.numGridZ);

                if (ctx->hasCandidateCulling && !ctx->nodeCandidateMask[gIdx]) {
                    ctx->mesher->gridDistances[gIdx] = 1e6f;
                    localSkippedNodes++;
                    continue;
                }

                float z = ctx->mesher->coordinates[2][iz];
                Vector3 p = Vec3_Create(x, y, z);
                float dist;
                if (ctx->distFn) {
                    dist = ctx->distFn(ctx->field->context, p);
                } else {
                    dist = ctx->evalFn(ctx->field->context, p).distance;
                }
                if (!isfinite(dist)) {
                    ctx->hasNonFinite = true;
                }
                ctx->mesher->gridDistances[gIdx] = dist;
                localDistEvals++;
            }
        }
    }

    size_t cCand = 0, cExact = 0, cPruned = 0;
    MonsterSDF_GetThreadStats(&cCand, &cExact, &cPruned);
    if (threadIndex >= 0 && threadIndex < SDF_SAMPLING_POOL_MAX_THREADS) {
        ctx->threadDistanceCount[threadIndex] = localDistEvals;
        ctx->threadSkippedNodeCount[threadIndex] = localSkippedNodes;
        ctx->threadCandidateCount[threadIndex] = cCand;
        ctx->threadExactCount[threadIndex] = cExact;
        ctx->threadPrunedCount[threadIndex] = cPruned;
    }
}

bool SDFMesher_GenerateMeshDetailed(
    SDFMesher* mesher,
    const SDFField* field,
    const SDFDetailRegion* regions, size_t regionCount,
    Mesh* outMesh
) {
    if (!mesher || !field || !field->evaluate || !outMesh) return false;

    Mesh_Clear(outMesh);
    memset(&mesher->lastStats,0,sizeof(mesher->lastStats));

    SDFMesherConfig cfg = mesher->config;

    /* Si se requiere autobounds y el campo provee getBounds */
    if (cfg.useAutoBounds && field->getBounds) {
        cfg.bounds = field->getBounds(field->context);
    }

    if(!isfinite(cfg.bounds.start.x)||!isfinite(cfg.bounds.start.y)||!isfinite(cfg.bounds.start.z)||
       !isfinite(cfg.bounds.end.x)||!isfinite(cfg.bounds.end.y)||!isfinite(cfg.bounds.end.z)||
       cfg.bounds.end.x<=cfg.bounds.start.x||cfg.bounds.end.y<=cfg.bounds.start.y||cfg.bounds.end.z<=cfg.bounds.start.z||
       !isfinite(cfg.voxelSize)||!isfinite(cfg.isolevel)||!isfinite(cfg.normalEps))return false;
    SDFResolvedGrid grid;
    if (!SDFMesher_ResolveGrid(&cfg, cfg.bounds, &grid)) {
        return false;
    }

    bool detailAdjusted=false;
    float minSpacing=1e6f,maxSpacing=0;
    int limit=cfg.maxResolution>0?cfg.maxResolution:128;
    for(int axis=0;axis<3;++axis)
        if(!EnsureBufferCapacity((void**)&mesher->coordinates[axis],&mesher->coordinateCapacity[axis],
                                 (size_t)limit+1,sizeof(float))) return false;
    int dims[3]={grid.resX,grid.resY,grid.resZ};
    if(regions && regionCount && cfg.voxelSize>0) {
        float base=cfg.voxelSize,detailScale=1;
        size_t budget=cfg.maxCells?cfg.maxCells:500000;
        bool resolved=false;
        for(int attempt=0;attempt<160;++attempt) {
            for(int axis=0;axis<3;++axis)
                dims[axis]=SDFMesher_AxisCoordinates(mesher->coordinates[axis],limit,
                    SDF_Axis(cfg.bounds.start,axis),SDF_Axis(cfg.bounds.end,axis),base,
                    regions,regionCount,axis,detailScale);
            size_t cells=0;
            if(dims[0]>=2 && dims[1]>=2 && dims[2]>=2 &&
               Math_MulSize(dims[0],dims[1],&cells) && Math_MulSize(cells,dims[2],&cells) && cells<=budget) {
                resolved=true;break;
            }
            grid.budgetAdjusted=true;
            if(attempt<8) base*=1.2f;
            else {detailScale*=1.12f;base*=1.04f;detailAdjusted=true;}
        }
        if(!resolved) return false;
        grid.resX=dims[0];grid.resY=dims[1];grid.resZ=dims[2];
        grid.numGridX=dims[0]+1;grid.numGridY=dims[1]+1;grid.numGridZ=dims[2]+1;
        grid.cellCount=(size_t)dims[0]*dims[1]*dims[2];
        grid.gridPointCount=(size_t)(dims[0]+1)*(dims[1]+1)*(dims[2]+1);
    } else {
        for(int axis=0;axis<3;++axis)
            for(int i=0;i<=dims[axis];++i)
                mesher->coordinates[axis][i]=SDF_Axis(cfg.bounds.start,axis)+(float)i*SDF_Axis(grid.step,axis);
    }
    float axisMax[3]={0};
    for(int axis=0;axis<3;++axis) for(int i=0;i<dims[axis];++i) {
        float spacing=mesher->coordinates[axis][i+1]-mesher->coordinates[axis][i];
        minSpacing=Math_Min(minSpacing,spacing);maxSpacing=Math_Max(maxSpacing,spacing);
        axisMax[axis]=Math_Max(axisMax[axis],spacing);
    }
    grid.step=Vec3_Create(axisMax[0],axisMax[1],axisMax[2]);
    grid.effectiveVoxelSize=maxSpacing;
    Vector3 step = grid.step;
    float normalEps = cfg.normalEps;
    if (normalEps <= 0.0f) {
        float minStep = Math_Min(Math_Min(step.x, step.y), step.z);
        normalEps = Math_Clamp(minStep * 0.25f, 1e-5f, 1.0f);
    }

    /* Incrementar generación de gradiente para reuso de marcas */
    mesher->currentGradientGeneration++;
    if (mesher->currentGradientGeneration == 0) {
        mesher->currentGradientGeneration = 1;
        if (mesher->gradientStamp) {
            memset(mesher->gradientStamp, 0, mesher->gradientStampCapacity * sizeof(uint32_t));
        }
    }

    /* Asegurar capacidad de workspace buffers */
    if (!EnsureBufferCapacity((void**)&mesher->gridDistances, &mesher->gridDistanceCapacity, grid.gridPointCount, sizeof(float)) ||
        !EnsureBufferCapacity((void**)&mesher->gridGradients, &mesher->gridGradientCapacity, grid.gridPointCount, sizeof(Vector3)) ||
        !EnsureBufferCapacity((void**)&mesher->gradientStamp, &mesher->gradientStampCapacity, grid.gridPointCount, sizeof(uint32_t))) {
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

    if(!EnsureBufferCapacity((void**)&mesher->cornerVertices,&mesher->cornerVertexCapacity,grid.gridPointCount,sizeof(MeshIndex)))return false;
    memset(mesher->cornerVertices,0xFF,grid.gridPointCount*sizeof(MeshIndex));
    memset(mesher->xEdges, 0xFF, numXEdges * sizeof(MeshIndex));
    memset(mesher->yEdges, 0xFF, numYEdges * sizeof(MeshIndex));
    memset(mesher->zEdges, 0xFF, numZEdges * sizeof(MeshIndex));

    memset(&mesher->lastStats, 0, sizeof(SDFMesherStats));

    SDFDistanceFn distFn = field->evaluateDistance;
    SDFEvaluateFn evalFn = field->evaluate;

    /* Preparar cajas de influencia de componentes para poda espacial conservadora */
    AABB3D compBoxes[128];
    size_t compCount = 0;
    if (field->getComponentBounds) {
        compCount = field->getComponentBounds(field->context, compBoxes, 96);
    }
    for (size_t r = 0; r < regionCount && compCount < 128; ++r) {
        compBoxes[compCount++] = regions[r].bounds;
    }

    bool hasCandidateCulling = (compCount > 0);
    size_t candCellCount = 0;
    size_t candNodeCount = 0;

    if (hasCandidateCulling) {
        if (!EnsureBufferCapacity((void**)&mesher->cellCandidateMask, &mesher->cellCandidateCapacity, grid.cellCount, sizeof(uint8_t)) ||
            !EnsureBufferCapacity((void**)&mesher->nodeCandidateMask, &mesher->nodeCandidateCapacity, grid.gridPointCount, sizeof(uint8_t))) {
            return false;
        }
        memset(mesher->cellCandidateMask, 0, grid.cellCount * sizeof(uint8_t));
        memset(mesher->nodeCandidateMask, 0, grid.gridPointCount * sizeof(uint8_t));

        for (size_t b = 0; b < compCount; ++b) {
            AABB3D box = compBoxes[b];
            if (box.end.x < cfg.bounds.start.x || box.start.x > cfg.bounds.end.x ||
                box.end.y < cfg.bounds.start.y || box.start.y > cfg.bounds.end.y ||
                box.end.z < cfg.bounds.start.z || box.start.z > cfg.bounds.end.z) {
                continue;
            }

            int minCell[3] = {0, 0, 0};
            int maxCell[3] = {grid.resX - 1, grid.resY - 1, grid.resZ - 1};

            for (int axis = 0; axis < 3; ++axis) {
                float bStart = SDF_Axis(box.start, axis);
                float bEnd = SDF_Axis(box.end, axis);
                const float* coords = mesher->coordinates[axis];
                int res = dims[axis];

                int iMin = 0;
                while (iMin < res && coords[iMin + 1] < bStart) {
                    iMin++;
                }
                int iMax = res - 1;
                while (iMax >= 0 && coords[iMax] > bEnd) {
                    iMax--;
                }

                iMin = Math_Max(0, iMin - 1);
                iMax = Math_Min(res - 1, iMax + 1);

                minCell[axis] = iMin;
                maxCell[axis] = iMax;
            }

            if (minCell[0] <= maxCell[0] && minCell[1] <= maxCell[1] && minCell[2] <= maxCell[2]) {
                for (int cx = minCell[0]; cx <= maxCell[0]; ++cx) {
                    for (int cy = minCell[1]; cy <= maxCell[1]; ++cy) {
                        for (int cz = minCell[2]; cz <= maxCell[2]; ++cz) {
                            size_t cIdx = (size_t)cx * (size_t)grid.resY * (size_t)grid.resZ +
                                          (size_t)cy * (size_t)grid.resZ + (size_t)cz;
                            mesher->cellCandidateMask[cIdx] = 1;
                        }
                    }
                }
            }
        }

        for (int cx = 0; cx < grid.resX; ++cx) {
            for (int cy = 0; cy < grid.resY; ++cy) {
                for (int cz = 0; cz < grid.resZ; ++cz) {
                    size_t cIdx = (size_t)cx * (size_t)grid.resY * (size_t)grid.resZ +
                                  (size_t)cy * (size_t)grid.resZ + (size_t)cz;
                    if (mesher->cellCandidateMask[cIdx]) {
                        candCellCount++;
                        for (int dx = 0; dx <= 1; ++dx) {
                            for (int dy = 0; dy <= 1; ++dy) {
                                for (int dz = 0; dz <= 1; ++dz) {
                                    size_t nIdx = GridIndex(cx + dx, cy + dy, cz + dz,
                                                            grid.numGridY, grid.numGridZ);
                                    mesher->nodeCandidateMask[nIdx] = 1;
                                }
                            }
                        }
                    }
                }
            }
        }

        for (size_t n = 0; n < grid.gridPointCount; ++n) {
            if (mesher->nodeCandidateMask[n]) candNodeCount++;
        }
    }

    /* 1. Muestreo de sólo distancia escalar en todos los nodos de la rejilla */
    SDFSamplingPool* poolToUse = NULL;
    if (cfg.samplingThreadCount != 1) {
        if (mesher->borrowedPool) {
            poolToUse = mesher->borrowedPool;
        } else {
            if (!mesher->ownedPool) {
                mesher->ownedPool = SDFSamplingPool_Create(cfg.samplingThreadCount);
            }
            poolToUse = mesher->ownedPool;
        }
    }

    SamplingContext sctx;
    memset(&sctx, 0, sizeof(sctx));
    sctx.mesher = mesher;
    sctx.field = field;
    sctx.grid = grid;
    sctx.distFn = distFn;
    sctx.evalFn = evalFn;
    sctx.nodeCandidateMask = mesher->nodeCandidateMask;
    sctx.hasCandidateCulling = hasCandidateCulling;

    if (poolToUse) {
        SDFSamplingPool_ParallelFor(poolToUse, grid.numGridX, SDFMesher_SamplingSliceWork, &sctx);
        mesher->lastStats.threadsUsed = SDFSamplingPool_GetThreadCount(poolToUse);
    } else {
        SDFMesher_SamplingSliceWork(&sctx, 0, grid.numGridX, 0);
        mesher->lastStats.threadsUsed = 1;
    }

    if (sctx.hasNonFinite) return false;

    for (int t = 0; t < SDF_SAMPLING_POOL_MAX_THREADS; ++t) {
        mesher->lastStats.distanceEvaluationCount += sctx.threadDistanceCount[t];
        mesher->lastStats.connectorCandidateCount += sctx.threadCandidateCount[t];
        mesher->lastStats.connectorExactEvaluationCount += sctx.threadExactCount[t];
        mesher->lastStats.connectorPrunedCount += sctx.threadPrunedCount[t];
    }
    mesher->lastStats.fieldEvaluationCount = mesher->lastStats.distanceEvaluationCount;

    Mesh_ReserveVertices(outMesh, grid.gridPointCount / 4);
    Mesh_ReserveIndices(outMesh, grid.gridPointCount / 2);

    bool success = true;

    /* 2. Recorrer celdas y poligonizar */
    size_t skippedCells = 0;
    for (int ix = 0; ix < grid.resX && success; ++ix) {
        for (int iy = 0; iy < grid.resY && success; ++iy) {
            for (int iz = 0; iz < grid.resZ && success; ++iz) {
                size_t cellIndex = (size_t)ix * (size_t)grid.resY * (size_t)grid.resZ +
                                   (size_t)iy * (size_t)grid.resZ + (size_t)iz;

                if (hasCandidateCulling && !mesher->cellCandidateMask[cellIndex]) {
                    skippedCells++;
                    continue;
                }

                Vector3 corners[8];
                float cornerDistances[8];
                int cubeIndex = 0;

                for (int c = 0; c < 8; ++c) {
                    int gX = ix + MARCHING_CUBES_CORNER_OFFSETS[c][0];
                    int gY = iy + MARCHING_CUBES_CORNER_OFFSETS[c][1];
                    int gZ = iz + MARCHING_CUBES_CORNER_OFFSETS[c][2];

                    corners[c] = Vec3_Create(
                        mesher->coordinates[0][gX],
                        mesher->coordinates[1][gY],
                        mesher->coordinates[2][gZ]
                    );

                    size_t gIdx = GridIndex(gX, gY, gZ, grid.numGridY, grid.numGridZ);
                    cornerDistances[c] = mesher->gridDistances[gIdx];

                    if (cornerDistances[c] < cfg.isolevel) {
                        cubeIndex |= (1 << c);
                    }
                }

                uint16_t edgeFlags = MarchingCubes_GetEdgeMask(cubeIndex);
                if (regions && regionCount &&
                    (mesher->coordinates[0][ix+1]-mesher->coordinates[0][ix]<cfg.voxelSize*.95f ||
                     mesher->coordinates[1][iy+1]-mesher->coordinates[1][iy]<cfg.voxelSize*.95f ||
                     mesher->coordinates[2][iz+1]-mesher->coordinates[2][iz]<cfg.voxelSize*.95f))
                    mesher->lastStats.refinedCellCount++;
                if (edgeFlags == 0) continue;
                mesher->lastStats.activeCellCount++;

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
                        float d1 = cornerDistances[c1];
                        float d2 = cornerDistances[c2];

                        float t = 0.5f;
                        float denom = d2 - d1;
                        if (fabsf(denom) > 1e-6f) {
                            t = (cfg.isolevel - d1) / denom;
                        }
                        t = Math_Clamp01(t);

                        /* Colapsar al nodo las intersecciones indistinguibles en float.
                         * Sin caché nodal, eliminar triángulos nulos abre pequeños agujeros. */
                        MeshIndex* cornerCache=NULL;
                        int snapped=t<1e-5f?c1:t>1-1e-5f?c2:-1;
                        if(snapped>=0) {
                            t=snapped==c1?0:1;
                            int nx=ix+MARCHING_CUBES_CORNER_OFFSETS[snapped][0];
                            int ny=iy+MARCHING_CUBES_CORNER_OFFSETS[snapped][1];
                            int nz=iz+MARCHING_CUBES_CORNER_OFFSETS[snapped][2];
                            cornerCache=&mesher->cornerVertices[GridIndex(nx,ny,nz,grid.numGridY,grid.numGridZ)];
                            if(*cornerCache!=UINT32_MAX) {
                                edgeVertIndices[e]=*cornerCache;
                                if(cachePtr)*cachePtr=*cornerCache;
                                continue;
                            }
                        }
                        Vector3 pos = Vec3_Lerp(p1, p2, t);

                        /* Muestreo de atributos completos (color) ONCE en el punto de superficie exacto */
                        SDFSample surfaceSample = evalFn(field->context, pos);
                        Color col = surfaceSample.color;
                        mesher->lastStats.fullSampleEvaluationCount++;

                        /* Obtención perezosa de gradientes en los extremos de la arista */
                        int gX1 = ix + MARCHING_CUBES_CORNER_OFFSETS[c1][0];
                        int gY1 = iy + MARCHING_CUBES_CORNER_OFFSETS[c1][1];
                        int gZ1 = iz + MARCHING_CUBES_CORNER_OFFSETS[c1][2];
                        Vector3 grad1 = SDFMesher_GetGridGradient(mesher, &grid, gX1, gY1, gZ1);

                        int gX2 = ix + MARCHING_CUBES_CORNER_OFFSETS[c2][0];
                        int gY2 = iy + MARCHING_CUBES_CORNER_OFFSETS[c2][1];
                        int gZ2 = iz + MARCHING_CUBES_CORNER_OFFSETS[c2][2];
                        Vector3 grad2 = SDFMesher_GetGridGradient(mesher, &grid, gX2, gY2, gZ2);

                        Vector3 interpGrad = Vec3_Lerp(grad1, grad2, t);

                        Vector3 norm;
                        float gradLenSq = Vec3_Dot(interpGrad, interpGrad);
                        if (gradLenSq > 1e-10f) {
                            norm = Vec3_Normalize(interpGrad);
                        } else {
                            norm = SDF_EstimateNormal(evalFn, field->context, pos, normalEps);
                            mesher->lastStats.normalFallbackCount++;
                        }

                        MeshVertex vert = {
                            .position = pos,
                            .normal = norm,
                            .color = col,
                            .material = surfaceSample.material
                        };

                        MeshIndex newIdx = 0;
                        if (!Mesh_AddVertex(outMesh, vert, &newIdx)) {
                            success = false;
                            break;
                        }

                        if(cornerCache)*cornerCache=newIdx;
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

                    if (!Mesh_TriangleHasArea(pos0,pos1,pos2)) {
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

    float worstRatio=0;
    for(size_t r=0;regions && r<regionCount;++r) {
        if(!isfinite(regions[r].targetVoxelSize)||regions[r].targetVoxelSize<=0)continue;
        for(int axis=0;axis<3;++axis)for(int i=0;i<dims[axis];++i) {
            float lo=mesher->coordinates[axis][i],hi=mesher->coordinates[axis][i+1];
            if(hi>SDF_Axis(regions[r].bounds.start,axis)+1e-6f && lo<SDF_Axis(regions[r].bounds.end,axis)-1e-6f)
                worstRatio=Math_Max(worstRatio,(hi-lo)/regions[r].targetVoxelSize);
        }
    }
    mesher->lastStats.detailSpacingRatio=worstRatio;
    mesher->lastStats.minimumVoxelSize=minSpacing;
    mesher->lastStats.detailBudgetAdjusted=detailAdjusted;
    mesher->lastStats.resolutionX = grid.resX;
    mesher->lastStats.resolutionY = grid.resY;
    mesher->lastStats.resolutionZ = grid.resZ;
    mesher->lastStats.voxelStep = grid.step;
    mesher->lastStats.cellCount = grid.cellCount;
    mesher->lastStats.gridPointCount = grid.gridPointCount;
    mesher->lastStats.generatedVertexCount = outMesh->vertexCount;
    mesher->lastStats.generatedTriangleCount = outMesh->indexCount / 3;
    mesher->lastStats.requestedVoxelSize = cfg.voxelSize;
    mesher->lastStats.effectiveVoxelSize = grid.effectiveVoxelSize;
    mesher->lastStats.cellBudgetAdjusted = grid.budgetAdjusted;
    mesher->lastStats.candidateCellCount = hasCandidateCulling ? candCellCount : grid.cellCount;
    mesher->lastStats.candidateNodeCount = hasCandidateCulling ? candNodeCount : grid.gridPointCount;
    mesher->lastStats.skippedCellCount = hasCandidateCulling ? skippedCells : 0;
    for (int t = 0; t < SDF_SAMPLING_POOL_MAX_THREADS; ++t) {
        mesher->lastStats.skippedNodeCount += sctx.threadSkippedNodeCount[t];
    }

    return true;
}
