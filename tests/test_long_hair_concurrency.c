/** @file test_long_hair_concurrency.c
 * @brief Paridad determinista y opt-in explícito del constructor paralelo. */
#include "LongHair.h"
#include "LongHairScheduler.h"
#include "test_utils.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static AnatomyGraph ConcurrencyGraph(void){AnatomyGraph graph={0};
    for(unsigned i=0;i<3;++i){AnatomyNode node={.id=Anatomy_MakeId(1,i+1),.moduleInstanceId=1,.localNodeId=i+1,
        .center={0,0,(float)i},.widthRadius=.4f,.heightRadius=.3f,.envelopeRadii={.4f,.3f,.5f},.development=1,.region=ANATOMY_REGION_NECK};
        TEST_ASSERT(AnatomyGraph_AddNode(&graph,node),"crear estación para cabello concurrente");}return graph;}
static float SafeField(const void* context,Vector3 p){const float* floor=context;return p.y-*floor;}
static bool EqualGuide(const LongHairGuide* a,const LongHairGuide* b){
#define EQ(field) if(memcmp(&a->field,&b->field,sizeof(a->field)))return false
    if(a->root.groupId!=b->root.groupId||a->root.moduleId!=b->root.moduleId||a->root.stableRootId!=b->root.stableRootId||
       a->root.fromId!=b->root.fromId||a->root.toId!=b->root.toId||memcmp(&a->root.restPosition,&b->root.restPosition,sizeof(Vector3))||
       memcmp(&a->root.restNormal,&b->root.restNormal,sizeof(Vector3))||memcmp(&a->root.restDirection,&b->root.restDirection,sizeof(Vector3))||
       a->root.rootParameter!=b->root.rootParameter||a->root.segmentParameter!=b->root.segmentParameter)return false;
    EQ(restPoints);EQ(points);EQ(restSide);EQ(posedSide);EQ(posedNormal);EQ(corrections);EQ(clearance);EQ(restClearance);EQ(restPenetration);EQ(rootClearance);EQ(restRootClearance);EQ(length);EQ(contactPenetration);EQ(visibility);EQ(attachmentJoint);EQ(groupIndex);EQ(visible);EQ(rootValid);EQ(follicles);
#undef EQ
    return true;}
void run_long_hair_concurrency_tests(void){
    AnatomyGraph graph=ConcurrencyGraph();LongHairGroup group=LongHairGroup_Default();group.domain.hostModuleId=1;group.domain.anchorCount=3;
    for(unsigned i=0;i<3;++i)group.domain.anchors[i]=Anatomy_MakeId(1,i+1);
    group.guideCount=72;group.childrenPerGuide=24;group.rootFullness=.9f;float floor=-.05f;
    LongHairSet serial={0};TEST_ASSERT(LongHairSet_Build(&serial,&group,1,&graph,1,SafeField,&floor),"construcción legacy sigue funcionando");
    for(int threads=1;threads<=8;threads*=2){LongHairSchedulerConfig config=LongHairScheduler_DefaultConfig();config.threadCount=threads;config.maxThreads=threads;
        LongHairScheduler* scheduler=LongHairScheduler_Create(&config);TEST_ASSERT(scheduler,"crear scheduler privado de prueba");LongHairSet parallel={0};
        TEST_ASSERT(LongHairSet_BuildParallel(&parallel,&group,1,&graph,1,SafeField,&floor,scheduler),"construcción opt-in completa");
        TEST_ASSERT(parallel.count==serial.count&&parallel.groupCount==serial.groupCount&&parallel.scale==serial.scale,"dimensiones y escala exactas");
        for(size_t i=0;i<serial.count;++i)TEST_ASSERT(EqualGuide(&serial.guides[i],&parallel.guides[i]),"descriptor completo por guía idéntico al serial");
        float (*left)[4]=calloc(serial.count*LONG_HAIR_GPU_ROWS,sizeof(*left)),(*right)[4]=calloc(parallel.count*LONG_HAIR_GPU_ROWS,sizeof(*right));
        TEST_ASSERT(left&&right,"asignar pack GPU");size_t l=LongHairSet_Pack(&serial,left,serial.count*LONG_HAIR_GPU_ROWS),r=LongHairSet_Pack(&parallel,right,parallel.count*LONG_HAIR_GPU_ROWS);
        TEST_ASSERT(l==r&&memcmp(left,right,l*sizeof(*left))==0,"pack GPU bit a bit idéntico");free(left);free(right);
        LongHairSet_Free(&parallel);LongHairScheduler_Destroy(scheduler);
    }
    LongHairSet_Free(&serial);
}
