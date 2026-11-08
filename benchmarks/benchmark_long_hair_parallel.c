/** @file benchmark_long_hair_parallel.c
 * @brief Comparación serial/paralela controlada de construcción de pelo largo. */
#define _POSIX_C_SOURCE 200809L
#include "LongHair.h"
#include "LongHairScheduler.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static double Now(void){struct timespec ts;clock_gettime(CLOCK_MONOTONIC,&ts);return ts.tv_sec+ts.tv_nsec*1e-9;}
static float Field(const void* context,Vector3 p){return p.y-*(const float*)context;}
static int MakeGraph(AnatomyGraph* graph){for(unsigned i=0;i<3;++i){AnatomyNode n={.id=Anatomy_MakeId(1,i+1),.moduleInstanceId=1,.localNodeId=i+1,
    .center={0,0,(float)i},.widthRadius=.4f,.heightRadius=.3f,.envelopeRadii={.4f,.3f,.5f},.development=1,.region=ANATOMY_REGION_NECK};
    if(!AnatomyGraph_AddNode(graph,n))return 0;}return 1;}
int main(int argc,char** argv){int warmups=argc>1?atoi(argv[1]):3,samples=argc>2?atoi(argv[2]):5;if(warmups<0||samples<1)return 2;
    AnatomyGraph graph={0};if(!MakeGraph(&graph))return 1;LongHairGroup group=LongHairGroup_Default();group.domain.hostModuleId=1;group.domain.anchorCount=3;
    for(unsigned i=0;i<3;++i)group.domain.anchors[i]=Anatomy_MakeId(1,i+1);group.guideCount=72;group.childrenPerGuide=24;group.rootFullness=.9f;float floor=-.05f;
    puts("threads,mode,sample,elapsed_ms,guides");
    for(int threads=1;threads<=8;threads*=2){LongHairScheduler* scheduler=NULL;if(threads>1){LongHairSchedulerConfig c=LongHairScheduler_DefaultConfig();c.threadCount=c.maxThreads=threads;scheduler=LongHairScheduler_Create(&c);if(!scheduler)return 3;}
        LongHairSet set={0};for(int sample=-warmups;sample<samples;++sample){double begin=Now();int ok=threads==1?LongHairSet_Build(&set,&group,1,&graph,1,Field,&floor):LongHairSet_BuildParallel(&set,&group,1,&graph,1,Field,&floor,scheduler);double ms=(Now()-begin)*1000;
            if(!ok){fprintf(stderr,"construction failed at thread count %d\n",threads);return 4;}if(sample>=0)printf("%d,%s,%d,%.6f,%zu\n",threads,threads==1?"legacy-serial":"parallel",sample,ms,set.count);}
        LongHairSet_Free(&set);LongHairScheduler_Destroy(scheduler);}
    return 0;}
