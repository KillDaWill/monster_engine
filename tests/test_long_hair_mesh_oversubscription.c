/** Bounded dual-owner stress: private hair schedulers plus real SDFMesher pools. */
#include "LongHair.h"
#include "LongHairScheduler.h"
#include "SDFMesher.h"
#include "SDFSamplingPool.h"
#include "test_utils.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

typedef struct Gate Gate;
typedef struct Owner {
    AnatomyGraph graph;LongHairGroup group;LongHairSet hair;LongHairScheduler* hairPool;
    SDFSamplingPool* meshPool;SDFMesher mesher;Mesh mesh;Gate* gate;bool ok;
} Owner;
struct Gate {pthread_mutex_t mutex;pthread_cond_t cond;unsigned arrived;bool go;};
static float HairField(const void* c,Vector3 p){(void)c;return p.y+.1f;}
static float SphereDistance(const void* c,Vector3 p){float r=*(const float*)c;return Vec3_Length(p)-r;}
static SDFSample SphereSample(const void* c,Vector3 p){return (SDFSample){.distance=SphereDistance(c,p),.color=(Color){180,80,40,255}};}
static AABB3D SphereBounds(const void* c){float r=*(const float*)c;return (AABB3D){.start={-r,-r,-r},.end={r,r,r}};}
static void* OwnerRun(void* opaque){Owner* o=opaque;Gate* gate=o->gate;
    pthread_mutex_lock(&gate->mutex);gate->arrived++;pthread_cond_broadcast(&gate->cond);while(!gate->go)pthread_cond_wait(&gate->cond,&gate->mutex);pthread_mutex_unlock(&gate->mutex);
    o->ok=LongHairSet_BuildParallel(&o->hair,&o->group,1,&o->graph,1,HairField,NULL,o->hairPool);
    float radius=.8f;SDFField field={.evaluate=SphereSample,.evaluateDistance=SphereDistance,.getBounds=SphereBounds,.context=&radius};
    if(o->ok)o->ok=SDFMesher_GenerateMeshDetailed(&o->mesher,&field,NULL,0,&o->mesh);
    return NULL;}
static void SetupOwner(Owner* o,Gate* gate){memset(o,0,sizeof(*o));o->gate=gate;
    for(unsigned i=0;i<3;++i){AnatomyNode n={.id=Anatomy_MakeId(1,i+1),.moduleInstanceId=1,.localNodeId=i+1,
        .center={0,0,(float)i},.widthRadius=.4f,.heightRadius=.3f,.envelopeRadii={.4f,.3f,.5f},.development=1,.region=ANATOMY_REGION_NECK};
        TEST_ASSERT(AnatomyGraph_AddNode(&o->graph,n),"dual-load anatomy node");}
    o->group=LongHairGroup_Default();o->group.domain.hostModuleId=1;o->group.domain.anchorCount=3;o->group.guideCount=48;o->group.childrenPerGuide=12;o->group.rootFullness=.8f;
    for(unsigned i=0;i<3;++i)o->group.domain.anchors[i]=Anatomy_MakeId(1,i+1);
    LongHairSchedulerConfig hc=LongHairScheduler_DefaultConfig();hc.threadCount=2;hc.maxThreads=2;o->hairPool=LongHairScheduler_Create(&hc);
    o->meshPool=SDFSamplingPool_Create(2);SDFMesherConfig cfg=SDFMesher_DefaultConfig();cfg.resolutionX=24;cfg.resolutionY=24;cfg.resolutionZ=24;
    cfg.maxResolution=32;cfg.maxCells=20000;cfg.samplingThreadCount=2;o->mesher=SDFMesher_Create(cfg);SDFMesher_SetSamplingPool(&o->mesher,o->meshPool);
    o->mesh=Mesh_Create();
    TEST_ASSERT(o->hairPool&&LongHairScheduler_GetThreadCount(o->hairPool)==2&&o->meshPool,"independent private pools created");}
static void FreeOwner(Owner* o){LongHairSet_Free(&o->hair);LongHairScheduler_Destroy(o->hairPool);SDFMesher_Free(&o->mesher);SDFSamplingPool_Free(o->meshPool);Mesh_Free(&o->mesh);}
static bool SameMesh(const Mesh* a,const Mesh* b){if(a->vertexCount!=b->vertexCount||a->indexCount!=b->indexCount)return false;
    if(a->indexCount&&memcmp(a->indices,b->indices,a->indexCount*sizeof(*a->indices)))return false;
    for(size_t i=0;i<a->vertexCount;++i){if(memcmp(&a->vertices[i].position,&b->vertices[i].position,sizeof(Vector3))||
        memcmp(&a->vertices[i].normal,&b->vertices[i].normal,sizeof(Vector3)))return false;}
    return true;}
static bool SameHair(const LongHairSet* a,const LongHairSet* b){if(a->count!=b->count)return false;
    float (*x)[4]=calloc(a->count*LONG_HAIR_GPU_ROWS,sizeof(*x)),(*y)[4]=calloc(b->count*LONG_HAIR_GPU_ROWS,sizeof(*y));if(!x||!y){free(x);free(y);return false;}
    size_t nx=LongHairSet_Pack(a,x,a->count*LONG_HAIR_GPU_ROWS),ny=LongHairSet_Pack(b,y,b->count*LONG_HAIR_GPU_ROWS);
    bool same=nx==ny&&!memcmp(x,y,nx*sizeof(*x));free(x);free(y);return same;}
void run_long_hair_mesh_oversubscription_tests(void){
    Gate gate={0};pthread_mutex_init(&gate.mutex,NULL);pthread_cond_init(&gate.cond,NULL);
    Owner a,b;SetupOwner(&a,&gate);SetupOwner(&b,&gate);pthread_t ta,tb;
    TEST_ASSERT(pthread_create(&ta,NULL,OwnerRun,&a)==0&&pthread_create(&tb,NULL,OwnerRun,&b)==0,"launch two mixed-load owners");
    pthread_mutex_lock(&gate.mutex);while(gate.arrived!=2)pthread_cond_wait(&gate.cond,&gate.mutex);gate.go=true;
    pthread_cond_broadcast(&gate.cond);pthread_mutex_unlock(&gate.mutex);pthread_join(ta,NULL);pthread_join(tb,NULL);
    TEST_ASSERT(a.ok&&b.ok,"hair and real SDFMesher jobs finish under simultaneous load");
    TEST_ASSERT(a.mesh.vertexCount&&b.mesh.vertexCount&&a.mesh.indexCount==b.mesh.indexCount,"both mesh worker pools produced bounded sphere meshes");
    LongHairSet serial={0};TEST_ASSERT(LongHairSet_Build(&serial,&a.group,1,&a.graph,1,HairField,NULL),"serial hair reference");
    TEST_ASSERT(SameHair(&serial,&a.hair)&&SameHair(&serial,&b.hair),"two private hair owners preserve serial output");
    SDFMesherConfig cfg=SDFMesher_DefaultConfig();cfg.resolutionX=24;cfg.resolutionY=24;cfg.resolutionZ=24;cfg.maxResolution=32;cfg.maxCells=20000;cfg.samplingThreadCount=1;
    SDFMesher referenceMesher=SDFMesher_Create(cfg);Mesh reference=Mesh_Create();float radius=.8f;
    SDFField field={.evaluate=SphereSample,.evaluateDistance=SphereDistance,.getBounds=SphereBounds,.context=&radius};
    TEST_ASSERT(SDFMesher_GenerateMeshDetailed(&referenceMesher,&field,NULL,0,&reference),"serial SDFMesher reference");
    TEST_ASSERT(SameMesh(&reference,&a.mesh)&&SameMesh(&reference,&b.mesh),"parallel mesh output exactly equals serial reference");
    LongHairSet_Free(&serial);FreeOwner(&a);FreeOwner(&b);SDFMesher_Free(&referenceMesher);Mesh_Free(&reference);
    pthread_cond_destroy(&gate.cond);pthread_mutex_destroy(&gate.mutex);
}
#ifdef LONG_HAIR_MESH_STRESS_STANDALONE
int main(void){run_long_hair_mesh_oversubscription_tests();return 0;}
#endif
