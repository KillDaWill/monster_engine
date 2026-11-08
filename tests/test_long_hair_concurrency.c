/** @file test_long_hair_concurrency.c
 * @brief Paridad determinista y opt-in explícito del constructor paralelo. */
#include "LongHair.h"
#include "LongHairScheduler.h"
#include "test_utils.h"
#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

static AnatomyGraph ConcurrencyGraph(void){AnatomyGraph graph={0};
    for(unsigned i=0;i<3;++i){AnatomyNode node={.id=Anatomy_MakeId(1,i+1),.moduleInstanceId=1,.localNodeId=i+1,
        .center={0,0,(float)i},.widthRadius=.4f,.heightRadius=.3f,.envelopeRadii={.4f,.3f,.5f},.development=1,.region=ANATOMY_REGION_NECK};
        TEST_ASSERT(AnatomyGraph_AddNode(&graph,node),"crear estación para cabello concurrente");}return graph;}
static float SafeField(const void* context,Vector3 p){const float* floor=context;return p.y-*floor;}
typedef struct WorkerCounts {float floor;uint64_t calls[LONG_HAIR_BUILD_MAX_THREADS];} WorkerCounts;
static float CountedField(const void* context,Vector3 p){WorkerCounts* counts=(WorkerCounts*)context;int index=LongHairScheduler_CurrentPartitionIndex();
    if(index<0||index>=LONG_HAIR_BUILD_MAX_THREADS)index=0;
    counts->calls[index]++;return p.y-counts->floor;}
static bool EqualGuide(const LongHairGuide* a,const LongHairGuide* b){
#define EQ(field) if(memcmp(&a->field,&b->field,sizeof(a->field)))return false
    if(a->root.groupId!=b->root.groupId||a->root.moduleId!=b->root.moduleId||a->root.stableRootId!=b->root.stableRootId||
       a->root.fromId!=b->root.fromId||a->root.toId!=b->root.toId||memcmp(&a->root.restPosition,&b->root.restPosition,sizeof(Vector3))||
       memcmp(&a->root.restNormal,&b->root.restNormal,sizeof(Vector3))||memcmp(&a->root.restDirection,&b->root.restDirection,sizeof(Vector3))||
       a->root.rootParameter!=b->root.rootParameter||a->root.segmentParameter!=b->root.segmentParameter)return false;
    EQ(restPoints);EQ(points);EQ(restSide);EQ(posedSide);EQ(posedNormal);EQ(corrections);EQ(clearance);EQ(restClearance);EQ(restPenetration);EQ(rootClearance);EQ(restRootClearance);EQ(length);EQ(contactPenetration);EQ(visibility);EQ(attachmentJoint);EQ(groupIndex);EQ(visible);EQ(rootValid);EQ(follicles);
#undef EQ
    return true;}
typedef struct ConcurrentBuild {LongHairSet set;LongHairScheduler* scheduler;const LongHairGroup* group;const AnatomyGraph* graph;const float* floor;bool ok;} ConcurrentBuild;
static void* BuildConcurrently(void* opaque){ConcurrentBuild* work=opaque;work->ok=LongHairSet_BuildParallel(&work->set,work->group,1,work->graph,1,SafeField,work->floor,work->scheduler);return NULL;}
void run_long_hair_concurrency_tests(void){
    AnatomyGraph graph=ConcurrencyGraph();LongHairGroup group=LongHairGroup_Default();group.domain.hostModuleId=1;group.domain.anchorCount=3;
    for(unsigned i=0;i<3;++i)group.domain.anchors[i]=Anatomy_MakeId(1,i+1);
    group.guideCount=72;group.childrenPerGuide=24;group.rootFullness=.9f;float floor=-.05f;
    LongHairSet serial={0};TEST_ASSERT(LongHairSet_Build(&serial,&group,1,&graph,1,SafeField,&floor),"construcción legacy sigue funcionando");
    for(int threads=1;threads<=8;threads*=2){LongHairSchedulerConfig config=LongHairScheduler_DefaultConfig();config.threadCount=threads;config.maxThreads=threads;
        LongHairScheduler* scheduler=LongHairScheduler_Create(&config);TEST_ASSERT(scheduler,"crear scheduler privado de prueba");LongHairSet parallel={0};
        WorkerCounts workerCounts={.floor=floor};LongHairBuildStats stats={0};bool built=threads==4?
            LongHairSet_BuildParallelProfiled(&parallel,&group,1,&graph,1,CountedField,&workerCounts,scheduler,&stats):
            LongHairSet_BuildParallel(&parallel,&group,1,&graph,1,CountedField,&workerCounts,scheduler);
        TEST_ASSERT(built,"construcción opt-in completa");
        uint64_t callbackCalls=0;for(int worker=0;worker<LONG_HAIR_BUILD_MAX_THREADS;++worker)callbackCalls+=workerCounts.calls[worker];
        TEST_ASSERT(callbackCalls>0,"callback contabiliza consultas en ranuras por partición");
        if(threads==4){TEST_ASSERT(stats.participants==4,"perfil informa participantes efectivos");
            for(int phase=0;phase<LONG_HAIR_BUILD_PHASES;++phase)TEST_ASSERT(stats.phaseMilliseconds[phase]>=0,"duración por fase presente");
            uint64_t guideCalls=0;for(int worker=0;worker<stats.participants;++worker)guideCalls+=stats.distanceCalls[0][worker];
            TEST_ASSERT(guideCalls>0,"consultas seriales de raíz contabilizadas");}
        TEST_ASSERT(parallel.count==serial.count&&parallel.groupCount==serial.groupCount&&parallel.scale==serial.scale,"dimensiones y escala exactas");
        for(size_t i=0;i<serial.count;++i)TEST_ASSERT(EqualGuide(&serial.guides[i],&parallel.guides[i]),"descriptor completo por guía idéntico al serial");
        float (*left)[4]=calloc(serial.count*LONG_HAIR_GPU_ROWS,sizeof(*left)),(*right)[4]=calloc(parallel.count*LONG_HAIR_GPU_ROWS,sizeof(*right));
        TEST_ASSERT(left&&right,"asignar pack GPU");size_t l=LongHairSet_Pack(&serial,left,serial.count*LONG_HAIR_GPU_ROWS),r=LongHairSet_Pack(&parallel,right,parallel.count*LONG_HAIR_GPU_ROWS);
        TEST_ASSERT(l==r&&memcmp(left,right,l*sizeof(*left))==0,"pack GPU bit a bit idéntico");free(left);free(right);
        LongHairSet_Free(&parallel);LongHairScheduler_Destroy(scheduler);
    }
    const LongHairGuide* preserved=serial.guides;uint64_t generation=serial.generation;LongHairGroup invalid=group;invalid.id=0;
    LongHairSchedulerConfig serialConfig=LongHairScheduler_DefaultConfig();serialConfig.threadCount=1;LongHairScheduler* rollbackScheduler=LongHairScheduler_Create(&serialConfig);
    TEST_ASSERT(rollbackScheduler,"scheduler para rollback");
    TEST_ASSERT(!LongHairSet_BuildParallel(&serial,&invalid,1,&graph,1,SafeField,&floor,rollbackScheduler),"entrada inválida aborta el candidato");
    TEST_ASSERT(serial.guides==preserved&&serial.generation==generation,"fallo conserva íntegro el runtime previamente publicado");
    LongHairScheduler_Destroy(rollbackScheduler);

    LongHairSchedulerConfig ca=LongHairScheduler_DefaultConfig(),cb=LongHairScheduler_DefaultConfig();ca.threadCount=2;ca.maxThreads=2;cb.threadCount=4;cb.maxThreads=4;
    ConcurrentBuild a={.scheduler=LongHairScheduler_Create(&ca),.group=&group,.graph=&graph,.floor=&floor},b={.scheduler=LongHairScheduler_Create(&cb),.group=&group,.graph=&graph,.floor=&floor};
    TEST_ASSERT(a.scheduler&&b.scheduler,"dos dueños scheduler independientes");pthread_t ta,tb;
    TEST_ASSERT(pthread_create(&ta,NULL,BuildConcurrently,&a)==0&&pthread_create(&tb,NULL,BuildConcurrently,&b)==0,"crear submitters concurrentes independientes");
    pthread_join(ta,NULL);pthread_join(tb,NULL);TEST_ASSERT(a.ok&&b.ok,"ambos dueños terminan sin interferencia");
    TEST_ASSERT(a.set.count==serial.count&&b.set.count==serial.count,"conteos estables entre dueños concurrentes");
    for(size_t i=0;i<serial.count;++i)TEST_ASSERT(EqualGuide(&serial.guides[i],&a.set.guides[i])&&EqualGuide(&serial.guides[i],&b.set.guides[i]),"geometría exacta bajo trabajos concurrentes independientes");
    LongHairSet_Free(&a.set);LongHairSet_Free(&b.set);LongHairScheduler_Destroy(a.scheduler);LongHairScheduler_Destroy(b.scheduler);
    LongHairSet_Free(&serial);
}
