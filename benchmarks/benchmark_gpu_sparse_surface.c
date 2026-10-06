/** @file benchmark_gpu_sparse_surface.c
 * @brief Mide control y residencia sin evaluar SDF ni fabricar una malla CPU.
 */
#include "Creature.h"
#include "CreatureMorph.h"
#include "CreatureRecipes.h"
#include "GpuSDFSurfaceCache.h"
#include "Monster.h"
#include <stdio.h>
#include <time.h>
static double NowMs(void) {struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1000.+t.tv_nsec*1e-6;}
int main(void) {
    Monster dog=Monster_Create(),lizard=Monster_Create(),current=Monster_Create();
    const CreatureRecipe* a=CreatureRecipes_Dog();const CreatureRecipe* b=CreatureRecipes_Lizard();
    if(!Creature_BuildMonster(&dog,a,&a->adult)||!Creature_BuildMonster(&lizard,b,&b->adult))return 2;
    MonsterSDF sdf=MonsterSDF_Create();AABB3D envelope=AABB_Empty();
    for(unsigned i=0;i<=32;++i) {
        if(!CreatureMorph_Interpolate(&dog,&lizard,i/32.f,&current)||!MonsterSDF_Build(&sdf,&current,MonsterSDF_DefaultConfig()))return 2;
        AABB_ExpandPoint(&envelope,sdf.bounds.start);AABB_ExpandPoint(&envelope,sdf.bounds.end);
    }
    AABB_Pad(&envelope,.035f);GpuSDFSurfaceGrid grid={0};
    if(!GpuSDFSurfaceGrid_Configure(&grid,envelope,.0175f,2))return 2;
    for(unsigned size=8;size<=16;size*=2) {
        GpuSDFSurfaceCache* cache=GpuSDFSurfaceCache_Create(&grid,size);if(!cache)return 2;
        const float ages[]={0,.5f,1,.5f,0,0};
        for(unsigned i=0;i<6;++i) {
            if(!CreatureMorph_Interpolate(&dog,&lizard,ages[i],&current)||!MonsterSDF_Build(&sdf,&current,MonsterSDF_DefaultConfig()))return 2;
            double begin=NowMs();if(!GpuSDFSurfaceCache_Update(cache,&sdf))return 2;
            size_t side=size+3;
            printf("brick=%u age=%.2f cpu=%.3fms resident=%zu dirty=%zu new=%zu retired=%zu sample_bytes=%zu reused=%zu\n",size,ages[i],NowMs()-begin,
                cache->residentCount,cache->dirtyCount,cache->newCount,cache->retiredCount,(size_t)cache->nextSlot*side*side*side*sizeof(float),cache->reusedSamples);
            GpuSDFSurfaceCache_Commit(cache,cache->generation);
        }
        GpuSDFSurfaceCache_Destroy(cache);
    }
    MonsterSDF_Free(&sdf);Monster_Free(&current);Monster_Free(&dog);Monster_Free(&lizard);return 0;
}
