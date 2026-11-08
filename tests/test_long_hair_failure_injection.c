/** Deterministic linker-wrapped failure injection; no production allocator hooks. */
#define _POSIX_C_SOURCE 200809L
#include "LongHair.h"
#include "LongHairScheduler.h"
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void* __real_malloc(size_t);
void* __real_calloc(size_t,size_t);
void __real_free(void*);
int __real_pthread_create(pthread_t*,const pthread_attr_t*,void*(*)(void*),void*);
static long allocationFailAt=-1,allocationCalls;
static long threadFailAt=-1,threadCalls;
void* __wrap_malloc(size_t size){long call=allocationCalls++;if(call==allocationFailAt)return NULL;return __real_malloc(size);}
void* __wrap_calloc(size_t count,size_t size){long call=allocationCalls++;if(call==allocationFailAt)return NULL;return __real_calloc(count,size);}
void __wrap_free(void* p){__real_free(p);}
int __wrap_pthread_create(pthread_t* t,const pthread_attr_t* a,void*(*fn)(void*),void* arg){long call=threadCalls++;if(call==threadFailAt)return EAGAIN;return __real_pthread_create(t,a,fn,arg);}

#define CHECK(c,m) do{if(!(c)){fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,m);exit(1);}}while(0)
static unsigned attachedFrees;
static void CountAttachedFree(void* p){CHECK(p==&attachedFrees,"attached context identity");attachedFrees++;}
static float Field(const void* c,Vector3 p);
typedef struct NestedWork {LongHairScheduler* scheduler;const LongHairGroup* group;const AnatomyGraph* graph;LongHairSet* set;bool buildResult;} NestedWork;
static bool NestedBuilderWork(void* opaque,int start,int end,int index){NestedWork* n=opaque;(void)start;(void)end;(void)index;
    n->buildResult=LongHairSet_BuildParallel(n->set,n->group,1,n->graph,1,Field,NULL,n->scheduler);return true;}
typedef struct BusyWork {pthread_mutex_t mutex;pthread_cond_t cond;bool entered,release;} BusyWork;
static bool BlockingWork(void* opaque,int start,int end,int index){BusyWork* b=opaque;(void)start;(void)end;(void)index;
    pthread_mutex_lock(&b->mutex);b->entered=true;pthread_cond_broadcast(&b->cond);while(!b->release)pthread_cond_wait(&b->cond,&b->mutex);
    pthread_mutex_unlock(&b->mutex);return true;}
typedef struct SubmitWork {LongHairScheduler* scheduler;BusyWork* busy;LongHairSchedulerStatus status;} SubmitWork;
static void* SubmitBlockingWork(void* opaque){SubmitWork* s=opaque;s->status=LongHairScheduler_ParallelFor(s->scheduler,1,BlockingWork,s->busy);return NULL;}
static AnatomyGraph MakeGraph(void){AnatomyGraph g={0};for(unsigned i=0;i<3;++i){AnatomyNode n={.id=Anatomy_MakeId(1,i+1),.moduleInstanceId=1,
    .localNodeId=i+1,.center={0,0,(float)i},.widthRadius=.4f,.heightRadius=.3f,.envelopeRadii={.4f,.3f,.5f},
    .development=1,.region=ANATOMY_REGION_NECK};CHECK(AnatomyGraph_AddNode(&g,n),"build graph");}return g;}
static LongHairGroup MakeGroup(void){LongHairGroup g=LongHairGroup_Default();g.domain.hostModuleId=1;g.domain.anchorCount=3;
    for(unsigned i=0;i<3;++i)g.domain.anchors[i]=Anatomy_MakeId(1,i+1);
    g.guideCount=16;g.childrenPerGuide=8;return g;}
static float Field(const void* c,Vector3 p){(void)c;return p.y+.1f;}
static bool PackEqual(const LongHairSet* s,const float* expected,size_t count){float (*rows)[4]=calloc(s->count*LONG_HAIR_GPU_ROWS,sizeof(*rows));
    if(!rows)return false;
    size_t n=LongHairSet_Pack(s,rows,s->count*LONG_HAIR_GPU_ROWS);bool equal=n==count*LONG_HAIR_GPU_ROWS&&
        !memcmp(rows,expected,n*sizeof(*rows));free(rows);return equal;}
static float* PackCopy(const LongHairSet* s,size_t* rowsOut){size_t n=s->count*LONG_HAIR_GPU_ROWS;float (*rows)[4]=calloc(n,sizeof(*rows));
    CHECK(rows,"baseline pack allocation");CHECK(LongHairSet_Pack(s,rows,n)==n,"baseline pack");*rowsOut=n;return (float*)rows;}

int main(void){AnatomyGraph graph=MakeGraph();LongHairGroup group=MakeGroup();LongHairSet set={0};
    CHECK(LongHairSet_Build(&set,&group,1,&graph,1,Field,NULL),"initial build");set.attachedContext=&attachedFrees;
    set.freeAttachedContext=CountAttachedFree;set.attachedContextBytes=sizeof(attachedFrees);
    size_t packedRows=0;float* expected=PackCopy(&set,&packedRows);LongHairGuide* originalGuides=set.guides;
    uint64_t originalGeneration=set.generation;unsigned originalCount=(unsigned)set.count;
    bool observedFailure=false,observedSuccess=false;
    for(long nth=0;nth<128;++nth){
        allocationFailAt=-1;allocationCalls=0;CHECK(LongHairSet_Build(&set,&group,1,&graph,1,Field,NULL),"refresh baseline before injection");
        set.attachedContext=&attachedFrees;set.freeAttachedContext=CountAttachedFree;set.attachedContextBytes=sizeof(attachedFrees);
        originalGuides=set.guides;originalGeneration=set.generation;originalCount=(unsigned)set.count;free(expected);expected=PackCopy(&set,&packedRows);
        unsigned freeCount=attachedFrees;allocationCalls=0;allocationFailAt=nth;
        bool built=LongHairSet_Build(&set,&group,1,&graph,1,Field,NULL);allocationFailAt=-1;
        if(!built){observedFailure=true;CHECK(set.guides==originalGuides&&set.generation==originalGeneration&&set.count==originalCount,
            "allocation rollback preserves runtime identity and generation");
            CHECK(set.attachedContext==&attachedFrees&&attachedFrees==freeCount,"failed candidate neither loses nor frees attached context");
            CHECK(PackEqual(&set,expected,packedRows/LONG_HAIR_GPU_ROWS),"failed candidate preserves packed output");
            continue;}
        observedSuccess=true;CHECK(allocationCalls<=nth,"success follows final injectable allocation");
        CHECK(attachedFrees==freeCount+1,"successful publication frees replaced attached context exactly once");break;
    }
    CHECK(observedFailure&&observedSuccess,"all deterministic allocation points exercised through first non-failing index");
    free(expected);set.attachedContext=&attachedFrees;set.freeAttachedContext=CountAttachedFree;
    unsigned finalFreeCount=attachedFrees;LongHairSet_Free(&set);
    CHECK(attachedFrees==finalFreeCount+1,"final runtime frees attached context exactly once");

    LongHairSchedulerConfig config=LongHairScheduler_DefaultConfig();config.threadCount=4;config.maxThreads=4;
    threadCalls=0;threadFailAt=0;LongHairScheduler* zero=LongHairScheduler_Create(&config);threadFailAt=-1;
    CHECK(zero&&LongHairScheduler_GetThreadCount(zero)==1,"zero-worker creation failure falls back to serial");
    CHECK(LongHairSet_BuildParallel(&set,&group,1,&graph,1,Field,NULL,zero),"zero-worker scheduler remains usable serially");
    LongHairSet_Free(&set);LongHairScheduler_Destroy(zero);
    threadCalls=0;threadFailAt=1;LongHairScheduler* partial=LongHairScheduler_Create(&config);threadFailAt=-1;
    CHECK(partial&&LongHairScheduler_GetThreadCount(partial)==2,"partial worker creation reduces pool and remains usable");
    CHECK(LongHairSet_BuildParallel(&set,&group,1,&graph,1,Field,NULL,partial),"partial pool completes exact build");
    LongHairSet_Free(&set);LongHairScheduler_Destroy(partial);
    config.requireParallel=true;threadCalls=0;threadFailAt=0;LongHairScheduler* rejected=LongHairScheduler_Create(&config);threadFailAt=-1;
    CHECK(!rejected,"requireParallel rejects zero-worker fallback");

    /* A nested builder invoked from an admitted scheduler callback must reject
     * cleanly and leave the pre-published nested destination intact. */
    config.requireParallel=false;threadCalls=0;LongHairScheduler* nestedScheduler=LongHairScheduler_Create(&config);
    CHECK(nestedScheduler&&LongHairScheduler_GetThreadCount(nestedScheduler)==4,"nested rollback scheduler");
    LongHairSet nestedSet={0};CHECK(LongHairSet_Build(&nestedSet,&group,1,&graph,1,Field,NULL),"nested rollback seed");
    nestedSet.attachedContext=&attachedFrees;nestedSet.freeAttachedContext=CountAttachedFree;
    LongHairGuide* nestedGuides=nestedSet.guides;uint64_t nestedGeneration=nestedSet.generation;NestedWork nested={
        .scheduler=nestedScheduler,.group=&group,.graph=&graph,.set=&nestedSet};
    CHECK(LongHairScheduler_ParallelFor(nestedScheduler,1,NestedBuilderWork,&nested)==LONG_HAIR_SCHEDULER_OK,
        "outer scheduler callback completes");
    CHECK(!nested.buildResult&&nestedSet.guides==nestedGuides&&nestedSet.generation==nestedGeneration&&
        nestedSet.attachedContext==&attachedFrees,"nested builder rejection rolls back the old runtime");

    /* An occupied-scheduler rejection is exercised through the real builder,
     * not only through the scheduler facade. */
    LongHairSet occupiedSet={0};CHECK(LongHairSet_Build(&occupiedSet,&group,1,&graph,1,Field,NULL),"occupied rollback seed");
    occupiedSet.attachedContext=&attachedFrees;occupiedSet.freeAttachedContext=CountAttachedFree;
    LongHairGuide* occupiedGuides=occupiedSet.guides;uint64_t occupiedGeneration=occupiedSet.generation;
    BusyWork busy={.mutex=PTHREAD_MUTEX_INITIALIZER,.cond=PTHREAD_COND_INITIALIZER};SubmitWork submit={
        .scheduler=nestedScheduler,.busy=&busy};pthread_t submitter;
    CHECK(pthread_create(&submitter,NULL,SubmitBlockingWork,&submit)==0,"start occupied scheduler job");
    pthread_mutex_lock(&busy.mutex);while(!busy.entered)pthread_cond_wait(&busy.cond,&busy.mutex);pthread_mutex_unlock(&busy.mutex);
    CHECK(!LongHairSet_BuildParallel(&occupiedSet,&group,1,&graph,1,Field,NULL,nestedScheduler),
        "builder rejects occupied scheduler");
    CHECK(occupiedSet.guides==occupiedGuides&&occupiedSet.generation==occupiedGeneration&&
        occupiedSet.attachedContext==&attachedFrees,"occupied-scheduler rejection preserves prior runtime");
    pthread_mutex_lock(&busy.mutex);busy.release=true;pthread_cond_broadcast(&busy.cond);pthread_mutex_unlock(&busy.mutex);
    pthread_join(submitter,NULL);CHECK(submit.status==LONG_HAIR_SCHEDULER_OK,"occupied scheduler owner completes normally");
    pthread_cond_destroy(&busy.cond);pthread_mutex_destroy(&busy.mutex);
    LongHairSet_Free(&nestedSet);LongHairSet_Free(&occupiedSet);LongHairScheduler_Destroy(nestedScheduler);
    puts("PASS linker-wrapped malloc/calloc/pthread_create failure injection");return 0;
}
