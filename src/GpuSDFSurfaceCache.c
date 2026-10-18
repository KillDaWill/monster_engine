/** @file GpuSDFSurfaceCache.c
 * @brief Validación y planificación pura de la retícula de extracción.
 */
#include "GpuSDFSurfaceCache.h"
#include <math.h>
#include <string.h>
static bool ValidBounds(AABB3D b) {
    return isfinite(b.start.x)&&isfinite(b.start.y)&&isfinite(b.start.z)&&
        isfinite(b.end.x)&&isfinite(b.end.y)&&isfinite(b.end.z)&&
        b.start.x<b.end.x&&b.start.y<b.end.y&&b.start.z<b.end.z;
}
bool GpuSDFSurfaceGrid_Configure(GpuSDFSurfaceGrid* grid,AABB3D bounds,float step,unsigned refinement) {
    if(!grid||!ValidBounds(bounds)||!isfinite(step)||step<=0||refinement>3)return false;
    GpuSDFSurfaceGrid next={.voxelSize=step,.refinementSteps=refinement};
    float lo[3]={bounds.start.x,bounds.start.y,bounds.start.z};
    float hi[3]={bounds.end.x,bounds.end.y,bounds.end.z},origin[3];
    uint64_t corners=1,cells=1;
    for(int axis=0;axis<3;++axis) {
        origin[axis]=floorf(lo[axis]/step)*step;
        double count=ceil(((double)hi[axis]-origin[axis])/step);
        if(!isfinite(origin[axis])||count<1||count>UINT32_MAX-1)return false;
        next.cells[axis]=(uint32_t)count;
        if(corners>UINT32_MAX/(next.cells[axis]+1u)||cells>UINT32_MAX/next.cells[axis])return false;
        corners*=next.cells[axis]+1u;cells*=next.cells[axis];
    }
    next.origin=Vec3_Create(origin[0],origin[1],origin[2]);
    next.sampleCount=(size_t)corners;next.cellCount=(size_t)cells;
    *grid=next;return true;
}
bool GpuSDFSurfaceGrid_Contains(const GpuSDFSurfaceGrid* g,AABB3D b) {
    if(!g||!ValidBounds(b)||g->voxelSize<=0)return false;
    Vector3 end=Vec3_Add(g->origin,Vec3_Create(g->cells[0]*g->voxelSize,
        g->cells[1]*g->voxelSize,g->cells[2]*g->voxelSize));
    return b.start.x>=g->origin.x&&b.start.y>=g->origin.y&&b.start.z>=g->origin.z&&
        b.end.x<=end.x&&b.end.y<=end.y&&b.end.z<=end.z;
}

bool GpuSDFSurfaceGrid_PlanDense(const GpuSDFSurfaceGrid* g,size_t sampleLimit,
    size_t cellLimit,unsigned requestedDepth,GpuSDFSurfacePlan* plan) {
    if(!g||!plan||!g->cells[0]||!g->cells[1]||!g->cells[2]||!requestedDepth)return false;
    uint64_t corners=1,cells=1;
    for(unsigned axis=0;axis<3;++axis) {
        if(g->cells[axis]==UINT32_MAX||corners>UINT32_MAX/(g->cells[axis]+1u)||
           cells>UINT32_MAX/g->cells[axis])return false;
        corners*=g->cells[axis]+1u;cells*=g->cells[axis];
    }
    size_t cornersXY=(size_t)(g->cells[0]+1)*(g->cells[1]+1);
    size_t cellsXY=(size_t)g->cells[0]*g->cells[1];
    if(cornersXY>sampleLimit/2||cellsXY>cellLimit)return false;
    size_t depth=sampleLimit/cornersXY-1;
    if(depth>cellLimit/cellsXY)depth=cellLimit/cellsXY;
    if(depth>requestedDepth)depth=requestedDepth;
    if(depth>g->cells[2])depth=g->cells[2];
    if(!depth)return false;
    size_t slabs=((size_t)g->cells[2]+depth-1)/depth;
    if(slabs>4096)return false;
    GpuSDFSurfacePlan next={(uint32_t)depth,(uint32_t)slabs,
        cornersXY*(depth+1),cellsXY*depth,cornersXY*(g->cells[2]+slabs)};
    *plan=next;return true;
}

#include <stdlib.h>
static bool GrowCacheArray(void** array,size_t* capacity,size_t count,size_t size) {
    if(count<=*capacity)return true;
    size_t next=*capacity?*capacity:256;
    while(next<count) {if(next>SIZE_MAX/2)return false;next*=2;}
    if(next>SIZE_MAX/size)return false;
    void* data=realloc(*array,next*size);if(!data)return false;
    *array=data;*capacity=next;return true;
}
GpuSDFSurfaceCache* GpuSDFSurfaceCache_Create(const GpuSDFSurfaceGrid* grid,unsigned brickCells) {
    if(!grid||(brickCells!=8&&brickCells!=16)||!grid->cellCount)return NULL;
    GpuSDFSurfaceCache* cache=calloc(1,sizeof(*cache));if(!cache)return NULL;
    cache->grid=*grid;cache->brickCells=brickCells;
    cache->spatial=MorphSpatialCache_Create(grid->voxelSize*brickCells);
    if(!cache->spatial||!MorphSpatialCache_SetGridOrigin(cache->spatial,grid->origin)) {
        GpuSDFSurfaceCache_Destroy(cache);return NULL;
    }
    return cache;
}
void GpuSDFSurfaceCache_Destroy(GpuSDFSurfaceCache* cache) {
    if(!cache)return;
    MorphSpatialCache_Destroy(cache->spatial);free(cache->features);free(cache->featureScratch);free(cache->dirty);
    free(cache->retired);free(cache->freeSlots);free(cache);
}
static bool TouchFeatureBox(GpuSDFSurfaceCache* cache,AABB3D box,bool dirty,bool seen) {
    const GpuSDFSurfaceGrid* g=&cache->grid;
    float lo[3]={box.start.x,box.start.y,box.start.z},hi[3]={box.end.x,box.end.y,box.end.z};
    float origin[3]={g->origin.x,g->origin.y,g->origin.z};int first[3],last[3];
    for(unsigned axis=0;axis<3;++axis) {
        if(!isfinite(lo[axis])||!isfinite(hi[axis])||lo[axis]>hi[axis])return false;
        unsigned count=(g->cells[axis]+cache->brickCells-1)/cache->brickCells;
        double a=floor(((double)lo[axis]-origin[axis])/(g->voxelSize*cache->brickCells));
        double b=floor(((double)hi[axis]-origin[axis])/(g->voxelSize*cache->brickCells));
        if(b<0||a>=count)return true;
        first[axis]=(int)fmax(0,a);last[axis]=(int)fmin(count-1,b);
    }
    for(int z=first[2];z<=last[2];++z)for(int y=first[1];y<=last[1];++y)for(int x=first[0];x<=last[0];++x) {
        MorphSpatialBrick* brick=MorphSpatialCache_GetBrick(cache->spatial,x,y,z,true);
        if(!brick)return false;
        if(seen)brick->lastSeenGeneration=cache->generation;
        if(dirty||!brick->resident) {
            if(!brick->isDirty){brick->isDirty=true;++cache->spatial->dirtyCount;}
            brick->requestedGeneration=cache->generation;brick->fieldState=MORPH_BRICK_DIRTY;
        }
    }
    return true;
}
static int CompareFeatureIds(const void* a,const void* b) {
    uint64_t x=((const MonsterSDFSurfaceFeature*)a)->id,y=((const MonsterSDFSurfaceFeature*)b)->id;
    return x==y?0:x<y?-1:1;
}
bool GpuSDFSurfaceCache_Update(GpuSDFSurfaceCache* cache,const MonsterSDF* sdf) {
    if(!cache||!sdf||!GpuSDFSurfaceGrid_Contains(&cache->grid,sdf->bounds))return false;
    size_t count=MonsterSDF_CollectSurfaceFeatures(sdf,NULL,0,0);
    if(count==SIZE_MAX||count>SIZE_MAX/sizeof(MonsterSDFSurfaceFeature))return false;
    if(!GrowCacheArray((void**)&cache->featureScratch,&cache->featureScratchCapacity,count,
        sizeof(*cache->featureScratch)))return false;
    MonsterSDFSurfaceFeature* current=cache->featureScratch;
    float band=cache->grid.voxelSize*(3+sqrtf(3.f));
    if(MonsterSDF_CollectSurfaceFeatures(sdf,current,count,band)!=count)return false;
    if(count>1)qsort(current,count,sizeof(*current),CompareFeatureIds);
    // Rechazar identidades ambiguas antes de modificar residencia o versiones.
    for(size_t i=1;i<count;++i)if(current[i-1].id==current[i].id)return false;
    ++cache->generation;cache->dirtyCount=cache->retiredCount=cache->newCount=cache->residentCount=0;
    size_t previous=0;
    for(size_t i=0;i<count;++i) {
        const MonsterSDFSurfaceFeature* old=NULL;
        while(previous<cache->featureCount&&cache->features[previous].id<current[i].id) {
            if(!TouchFeatureBox(cache,cache->features[previous].bounds,true,false))goto fail;
            ++previous;
        }
        if(previous<cache->featureCount&&current[i].id==cache->features[previous].id)
            old=&cache->features[previous++];
        bool changed=!old||old->fingerprint!=current[i].fingerprint;
        if(!TouchFeatureBox(cache,current[i].bounds,changed,true))goto fail;
        if(changed&&old&&!TouchFeatureBox(cache,old->bounds,true,false))goto fail;
    }
    while(previous<cache->featureCount)
        if(!TouchFeatureBox(cache,cache->features[previous++].bounds,true,false))goto fail;
    for(size_t i=0;i<cache->spatial->brickCount;++i) {
        MorphSpatialBrick* brick=&cache->spatial->bricks[i];
        if(brick->resident&&cache->generation-brick->lastSeenGeneration>3) {
            if(!GrowCacheArray((void**)&cache->retired,&cache->retiredCapacity,cache->retiredCount+1,sizeof(*cache->retired))||
               !GrowCacheArray((void**)&cache->freeSlots,&cache->freeCapacity,cache->freeCount+1,sizeof(*cache->freeSlots)))goto fail;
            cache->retired[cache->retiredCount++]=brick->gpuSlot;cache->freeSlots[cache->freeCount++]=brick->gpuSlot;
            brick->resident=false;brick->fieldState=MORPH_BRICK_RETIRED;
            if(brick->isDirty){brick->isDirty=false;--cache->spatial->dirtyCount;}
        }
        if(!brick->resident&&brick->lastSeenGeneration!=cache->generation)continue;
        if(!brick->resident) {
            brick->gpuSlot=cache->freeCount?cache->freeSlots[--cache->freeCount]:cache->nextSlot++;
            brick->resident=true;brick->isDirty=true;brick->requestedGeneration=cache->generation;++cache->newCount;
        }
        ++cache->residentCount;
        if(brick->isDirty) {
            if(!GrowCacheArray((void**)&cache->dirty,&cache->dirtyCapacity,cache->dirtyCount+1,sizeof(*cache->dirty)))goto fail;
            cache->dirty[cache->dirtyCount++]=(GpuSDFBrickJob){(uint32_t)brick->bx*cache->brickCells,(uint32_t)brick->by*cache->brickCells,(uint32_t)brick->bz*cache->brickCells,brick->gpuSlot};
            brick->fieldState=MORPH_BRICK_SCHEDULED;
        }
    }
    size_t side=cache->brickCells+3;cache->reusedSamples=(cache->residentCount-cache->dirtyCount)*side*side*side;
    cache->featureScratch=cache->features;cache->features=current;cache->featureCount=count;
    size_t capacity=cache->featureCapacity;cache->featureCapacity=cache->featureScratchCapacity;
    cache->featureScratchCapacity=capacity;return true;
fail:
    return false;
}
void GpuSDFSurfaceCache_Submit(GpuSDFSurfaceCache* cache,uint64_t generation) {
    if(!cache||generation!=cache->generation)return;
    cache->submittedGeneration=generation;
    for(size_t i=0;i<cache->spatial->brickCount;++i) {
        MorphSpatialBrick* brick=&cache->spatial->bricks[i];
        if(!brick->resident||!brick->isDirty||brick->requestedGeneration>generation)continue;
        brick->fieldState=MORPH_BRICK_SCHEDULED;
        brick->isDirty=false;if(cache->spatial->dirtyCount)--cache->spatial->dirtyCount;
    }
}
void GpuSDFSurfaceCache_Commit(GpuSDFSurfaceCache* cache,uint64_t generation) {
    if(!cache||generation>cache->generation||generation<cache->completedGeneration)return;
    cache->completedGeneration=generation;
    for(size_t i=0;i<cache->spatial->brickCount;++i) {
        MorphSpatialBrick* brick=&cache->spatial->bricks[i];
        if(!brick->resident||brick->requestedGeneration!=generation)continue;
        brick->sampleGeneration=brick->meshGeneration=generation;brick->fieldState=MORPH_BRICK_CLEAN;
        if(brick->isDirty){brick->isDirty=false;if(cache->spatial->dirtyCount)--cache->spatial->dirtyCount;}
    }
}
