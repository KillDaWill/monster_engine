/** @file test_long_hair_concurrency_oracle.c
 * @brief Differential contra LongHair.c extraído directamente del BASE_SHA fijado. */
#define _POSIX_C_SOURCE 200809L
#include "Creature.h"
#include "CreatureRecipeIO.h"
#include "LongHair.h"
#include "LongHairScheduler.h"
#include "MonsterAger.h"
#include "MonsterAnimation.h"
#include "MonsterSDF.h"
#include "MonsterVisual.h"
#include "Transform3D.h"
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool Original_LongHairGroup_Default(void);
void Original_LongHairGroup_Normalize(LongHairGroup*);
uint64_t Original_LongHairGroups_Fingerprint(const LongHairGroup*,size_t);
float Original_LongHairGroup_Envelope(const LongHairGroup*,float,float);
void Original_LongHairSet_Free(LongHairSet*);
void Original_LongHairSet_Collide(LongHairSet*,LongHairDistance,const void*);
bool Original_LongHairSet_Build(LongHairSet*,const LongHairGroup*,size_t,const AnatomyGraph*,float,LongHairDistance,const void*);
bool Original_LongHairSet_Pose(LongHairSet*,const Skeleton*,const SkeletonPose*);
Vector3 Original_LongHairGuide_Evaluate(const LongHairGuide*,float);
size_t Original_LongHairSet_Pack(const LongHairSet*,float (*)[4],size_t);

#define REQUIRE(x,msg) do { if(!(x)){fprintf(stderr,"ORACLE FAIL %s:%d: %s\n",__FILE__,__LINE__,msg);exit(2);} } while(0)

typedef struct OracleField {
    const MonsterSDF* body;
    MonsterSDFMouth mouth;
    Vector3 center,x,y,z,pivot;
    AABB3D bounds;
    float jawScale,bodyScale;
} OracleField;

static float CombinedDistance(const void* opaque,Vector3 p){OracleField* f=(OracleField*)opaque;
    float body=MonsterSDF_EvaluateDistance(f->body,p);if(f->jawScale<=.001f)return body;
    Vector3 rel=Vec3_Sub(p,f->center),local=Vec3_Create(Vec3_Dot(rel,f->x),Vec3_Dot(rel,f->y),Vec3_Dot(rel,f->z));
    local=Vec3_Add(f->pivot,Vec3_Scale(Vec3_Sub(local,f->pivot),1.f/f->jawScale));
    Vector3 delta=Vec3_Create(fmaxf(f->bounds.start.x-local.x,fmaxf(0,local.x-f->bounds.end.x)),
        fmaxf(f->bounds.start.y-local.y,fmaxf(0,local.y-f->bounds.end.y)),
        fmaxf(f->bounds.start.z-local.z,fmaxf(0,local.z-f->bounds.end.z)));
    float lower=Vec3_Length(delta)*f->jawScale;
    if(lower>.4f*f->bodyScale)return fminf(body,lower);
    return fminf(body,MonsterSDF_EvaluateJawSnapshotDistance(&f->mouth,local)*f->jawScale);
}
static float SnapshotJawDistance(const void* opaque,Vector3 p){OracleField* f=(OracleField*)opaque;
    if(f->jawScale<=.001f)return 1e6f;
    Vector3 rel=Vec3_Sub(p,f->center),local=Vec3_Create(Vec3_Dot(rel,f->x),Vec3_Dot(rel,f->y),Vec3_Dot(rel,f->z));
    local=Vec3_Add(f->pivot,Vec3_Scale(Vec3_Sub(local,f->pivot),1.f/f->jawScale));
    Vector3 delta=Vec3_Create(fmaxf(f->bounds.start.x-local.x,fmaxf(0,local.x-f->bounds.end.x)),
        fmaxf(f->bounds.start.y-local.y,fmaxf(0,local.y-f->bounds.end.y)),
        fmaxf(f->bounds.start.z-local.z,fmaxf(0,local.z-f->bounds.end.z)));
    float lower=Vec3_Length(delta)*f->jawScale;
    if(lower>.4f*f->bodyScale)return lower;
    return MonsterSDF_EvaluateJawSnapshotDistance(&f->mouth,local)*f->jawScale;
}

static OracleField MakeField(const MonsterSDF* sdf,const Monster* m){OracleField f={.body=sdf,
    .jawScale=m->phenotype.development.cephalic,.bodyScale=m->phenotype.axial.totalScale};
    if(sdf->mouthCount&&m->mouthCount){MonsterSDFJawField context;SDFField jaw=MonsterSDF_GetJawField((MonsterSDF*)sdf,0,&context);
        f.mouth=sdf->mouths[0];f.center=sdf->mouths[0].center;Vector3 rotation=m->mouths[0].rotation;
        f.x=Transform3D_RotateVector(rotation,Vec3_Create(1,0,0));f.y=Transform3D_RotateVector(rotation,Vec3_Create(0,1,0));
        f.z=Transform3D_RotateVector(rotation,Vec3_Create(0,0,1));f.pivot=m->mouths[0].jawPivot;f.bounds=jaw.getBounds(jaw.context);}
    return f;}

static bool FloatSame(float a,float b){return memcmp(&a,&b,sizeof(a))==0;}
static bool VecSame(Vector3 a,Vector3 b){return FloatSame(a.x,b.x)&&FloatSame(a.y,b.y)&&FloatSame(a.z,b.z);}
static bool GuideSame(const LongHairGuide* a,const LongHairGuide* b){
    if(a->root.groupId!=b->root.groupId||a->root.moduleId!=b->root.moduleId||a->root.stableRootId!=b->root.stableRootId||
       a->root.fromId!=b->root.fromId||a->root.toId!=b->root.toId||!VecSame(a->root.restPosition,b->root.restPosition)||
       !VecSame(a->root.restNormal,b->root.restNormal)||!VecSame(a->root.restDirection,b->root.restDirection)||
       !FloatSame(a->root.rootParameter,b->root.rootParameter)||!FloatSame(a->root.segmentParameter,b->root.segmentParameter))return false;
    for(unsigned i=0;i<LONG_HAIR_POINTS;++i)if(!VecSame(a->restPoints[i],b->restPoints[i])||!VecSame(a->points[i],b->points[i])||
       !VecSame(a->corrections[i],b->corrections[i])||!FloatSame(a->clearance[i],b->clearance[i])||
       !FloatSame(a->restClearance[i],b->restClearance[i]))return false;
    if(!VecSame(a->restSide,b->restSide)||!VecSame(a->posedSide,b->posedSide)||!VecSame(a->posedNormal,b->posedNormal)||
       !FloatSame(a->restPenetration,b->restPenetration)||!FloatSame(a->rootClearance,b->rootClearance)||
       !FloatSame(a->restRootClearance,b->restRootClearance)||!FloatSame(a->length,b->length)||
       !FloatSame(a->contactPenetration,b->contactPenetration)||!FloatSame(a->visibility,b->visibility)||
       a->attachmentJoint!=b->attachmentJoint||a->groupIndex!=b->groupIndex||a->visible!=b->visible||a->rootValid!=b->rootValid)return false;
    for(unsigned i=0;i<64;++i)for(unsigned j=0;j<4;++j)if(!FloatSame(a->follicles[i][j],b->follicles[i][j]))return false;
    return true;
}
static bool ColliderSame(const LongHairCollider* a,const LongHairCollider* b){return a->id==b->id&&a->endId==b->endId&&
    VecSame(a->center,b->center)&&VecSame(a->radii,b->radii)&&VecSame(a->end,b->end)&&VecSame(a->endRadii,b->endRadii)&&
    FloatSame(a->inverseRotation.x,b->inverseRotation.x)&&FloatSame(a->inverseRotation.y,b->inverseRotation.y)&&
    FloatSame(a->inverseRotation.z,b->inverseRotation.z)&&FloatSame(a->inverseRotation.w,b->inverseRotation.w)&&
    FloatSame(a->surfaceOffset,b->surfaceOffset);}

static void EqualSets(const LongHairSet* original,const LongHairSet* actual,const char* label){
    REQUIRE(original->count==actual->count&&original->groupCount==actual->groupCount&&
        original->colliderCount==actual->colliderCount,"counts differ");
    REQUIRE(FloatSame(original->scale,actual->scale)&&original->generation==actual->generation&&
        original->fingerprint==actual->fingerprint,"set identity differs");
    REQUIRE(Original_LongHairGroups_Fingerprint(original->groups,original->groupCount)==
        Original_LongHairGroups_Fingerprint(actual->groups,actual->groupCount),"normalized descriptors differ");
    for(size_t i=0;i<original->count;++i)if(!GuideSame(&original->guides[i],&actual->guides[i])){
        fprintf(stderr,"guide mismatch %s index=%zu\n",label,i);exit(3);}
    for(size_t i=0;i<original->colliderCount;++i)if(!ColliderSame(&original->colliders[i],&actual->colliders[i])){
        fprintf(stderr,"collider mismatch %s index=%zu\n",label,i);exit(4);}
    float (*a)[4]=calloc(original->count*LONG_HAIR_GPU_ROWS,sizeof(*a));
    float (*b)[4]=calloc(actual->count*LONG_HAIR_GPU_ROWS,sizeof(*b));REQUIRE(a&&b,"pack buffers allocated");
    size_t na=Original_LongHairSet_Pack(original,a,original->count*LONG_HAIR_GPU_ROWS);
    size_t nb=LongHairSet_Pack(actual,b,actual->count*LONG_HAIR_GPU_ROWS);
    REQUIRE(na==nb&&!memcmp(a,b,na*sizeof(*a)),"full GPU pack differs");free(a);free(b);
}

static void BuildMonster(const CreatureRecipeDocument* doc,Monster* m,const CreaturePhenotype* phenotype){
    *m=Monster_Create();REQUIRE(Creature_BuildMonster(m,&doc->recipe,phenotype),"creature build");Monster_SetHeadOpenFactor(m,0);
}

static void CompareOne(const Monster* m,LongHairScheduler** schedulers,size_t schedulerCount,const char* label){
    MonsterSDF sdf=MonsterSDF_Create();REQUIRE(MonsterSDF_Build(&sdf,m,MonsterSDF_DefaultConfig()),"body/jaw SDF build");
    OracleField field=MakeField(&sdf,m);LongHairSet original={0},serial={0},parallel[4]={{0}};
    REQUIRE(Original_LongHairSet_Build(&original,m->surface.longHair,m->surface.longHairCount,&m->anatomyGraph,
        m->phenotype.axial.totalScale,CombinedDistance,&field),"frozen source builder");
    REQUIRE(LongHairSet_Build(&serial,m->surface.longHair,m->surface.longHairCount,&m->anatomyGraph,
        m->phenotype.axial.totalScale,CombinedDistance,&field),"current serial builder");EqualSets(&original,&serial,label);
    for(size_t i=0;i<schedulerCount;++i){char detail[256];snprintf(detail,sizeof(detail),"%s workers=%d",label,LongHairScheduler_GetThreadCount(schedulers[i]));
        REQUIRE(LongHairSet_BuildParallel(&parallel[i],m->surface.longHair,m->surface.longHairCount,&m->anatomyGraph,
            m->phenotype.axial.totalScale,CombinedDistance,&field,schedulers[i]),"opt-in builder");EqualSets(&original,&parallel[i],detail);}
    printf("PASS %s guides=%zu colliders=%zu\n",label,original.count,original.colliderCount);
    Original_LongHairSet_Free(&original);LongHairSet_Free(&serial);for(unsigned i=0;i<4;++i)LongHairSet_Free(&parallel[i]);MonsterSDF_Free(&sdf);
}

static void ApplyProductionHairPose(LongHairSet* hair,const Monster* m,OracleField* field){
    if(field&&hair->count){hair->attachedDistance=SnapshotJawDistance;hair->attachedContext=field;
        hair->collisionJoint=m->animation&&m->animation->rig.jawJoint>=0?
            m->animation->rig.skeleton.joints[m->animation->rig.jawJoint].id:0;
        for(size_t i=0;i<hair->count;++i){LongHairGuide* g=&hair->guides[i];
            if(!g->visible||g->root.moduleId!=field->mouth.moduleInstanceId||Vec3_Dot(g->root.restNormal,field->y)>-.15f)continue;
            float d=SnapshotJawDistance(field,g->root.restPosition);
            if(isfinite(d)&&fabsf(d)<.01f*hair->scale)g->attachmentJoint=hair->collisionJoint;}}
    if(m->animation)REQUIRE(Original_LongHairSet_Pose(hair,&m->animation->rig.skeleton,&m->animation->pose),
        "frozen source pose matches production update path");
}

static int RunFullMatrix(const char* horsePath,const char* dogPath){
    const char* paths[]={horsePath,dogPath};const int threadCounts[]={1,2,4,8};LongHairScheduler* schedulers[4];
    for(unsigned i=0;i<4;++i){LongHairSchedulerConfig config=LongHairScheduler_DefaultConfig();
        config.threadCount=threadCounts[i];config.maxThreads=threadCounts[i];schedulers[i]=LongHairScheduler_Create(&config);
        REQUIRE(schedulers[i],"private scheduler create");REQUIRE(LongHairScheduler_GetThreadCount(schedulers[i])==threadCounts[i],"requested pool size unavailable");}
    for(unsigned species=0;species<2;++species){CreatureRecipeDocument doc={0};char error[256]={0};
        REQUIRE(CreatureRecipeIO_LoadFile(&doc,paths[species],error,sizeof(error)),error);
        for(unsigned stage=0;stage<4;++stage){Monster monster;BuildMonster(&doc,&monster,CreatureRecipe_GetStage(&doc.recipe,(CreatureStage)stage));
            char label[128];snprintf(label,sizeof(label),"species=%u stage=%u",species,stage);CompareOne(&monster,schedulers,4,label);Monster_Free(&monster);}
        Monster juvenile,adult;BuildMonster(&doc,&juvenile,&doc.recipe.juvenile);BuildMonster(&doc,&adult,&doc.recipe.adult);
        MonsterAger ager=MonsterAger_Create(&juvenile,&adult,0);MonsterAger_SetGeometrySnapshotEnabled(&ager,false);
        for(int step=0;step<=20;++step){MonsterAger_SetPerc(&ager,step/20.f);char label[128];snprintf(label,sizeof(label),"species=%u growth-forward=%d",species,step);
            CompareOne(MonsterAger_GetResultConst(&ager),schedulers,4,label);}
        for(int step=20;step>=0;--step){MonsterAger_SetPerc(&ager,step/20.f);char label[128];snprintf(label,sizeof(label),"species=%u growth-reverse=%d",species,step);
            CompareOne(MonsterAger_GetResultConst(&ager),schedulers,4,label);}
        unsigned state=0x12345678u;for(unsigned i=0;i<12;++i){state=state*1664525u+1013904223u;float age=(state>>8)*(1.f/16777216.f);
            MonsterAger_SetPerc(&ager,age);char label[128];snprintf(label,sizeof(label),"species=%u random=%u age=%.8f",species,i,age);
            CompareOne(MonsterAger_GetResultConst(&ager),schedulers,4,label);}
        MonsterAger_Free(&ager);Monster_Free(&juvenile);Monster_Free(&adult);
    }
    for(unsigned i=0;i<4;++i)LongHairScheduler_Destroy(schedulers[i]);
    puts("ALL 116 FROZEN-SOURCE DIFFERENTIAL SCENARIOS PASS");return 0;
}

typedef struct VisualJob {pthread_barrier_t* gate;Monster* monster;LongHairSet* set;bool ok;} VisualJob;
static void* RunVisualJob(void* opaque){VisualJob* job=opaque;pthread_barrier_wait(job->gate);job->ok=MonsterVisual_UpdateLongHair(job->set,job->monster);return NULL;}

static void CompareProductionVisual(const LongHairSet* expected,const LongHairSet* actual,const char* label){
    REQUIRE(expected->count==actual->count&&expected->groupCount==actual->groupCount&&
        expected->colliderCount==actual->colliderCount,"production visual counts differ");
    for(size_t i=0;i<expected->count;++i){const LongHairGuide* a=&expected->guides[i],*b=&actual->guides[i];
        if(!(a->root.stableRootId==b->root.stableRootId&&VecSame(a->root.restPosition,b->root.restPosition)&&
            VecSame(a->root.restNormal,b->root.restNormal)&&VecSame(a->root.restDirection,b->root.restDirection)&&
            a->visible==b->visible&&a->rootValid==b->rootValid)){
            fprintf(stderr,"visual mismatch %s guide=%zu root=%u/%u pos=(%.8g,%.8g,%.8g)/(%.8g,%.8g,%.8g) visible=%d/%d valid=%d/%d\n",
                label,i,a->root.stableRootId,b->root.stableRootId,a->root.restPosition.x,a->root.restPosition.y,a->root.restPosition.z,
                b->root.restPosition.x,b->root.restPosition.y,b->root.restPosition.z,a->visible,b->visible,a->rootValid,b->rootValid);exit(5);}
        for(unsigned p=0;p<LONG_HAIR_POINTS;++p)if(!VecSame(a->points[p],b->points[p])||!FloatSame(a->clearance[p],b->clearance[p])){
            fprintf(stderr,"visual mismatch %s guide=%zu point=%u position=(%.8g,%.8g,%.8g)/(%.8g,%.8g,%.8g) clearance=%.8g/%.8g\n",
                label,i,p,a->points[p].x,a->points[p].y,a->points[p].z,b->points[p].x,b->points[p].y,b->points[p].z,a->clearance[p],b->clearance[p]);exit(6);}
        for(unsigned c=0;c<64;++c)for(unsigned v=0;v<4;++v)if(!FloatSame(a->follicles[c][v],b->follicles[c][v])){
            fprintf(stderr,"visual mismatch %s guide=%zu follicle=%u component=%u %.8g/%.8g\n",label,i,c,v,a->follicles[c][v],b->follicles[c][v]);exit(7);}}
    float (*left)[4]=calloc(expected->count*LONG_HAIR_GPU_ROWS,sizeof(*left));
    float (*right)[4]=calloc(actual->count*LONG_HAIR_GPU_ROWS,sizeof(*right));REQUIRE(left&&right,"production pack allocations");
    size_t n=LongHairSet_Pack(expected,left,expected->count*LONG_HAIR_GPU_ROWS);
    REQUIRE(n==LongHairSet_Pack(actual,right,actual->count*LONG_HAIR_GPU_ROWS),"production pack row count differs");
    for(size_t i=0;i<n;++i)for(unsigned c=0;c<4;++c)if(!FloatSame(left[i][c],right[i][c])){
        fprintf(stderr,"visual pack mismatch %s row=%zu component=%u %.9g/%.9g\n",label,i,c,left[i][c],right[i][c]);exit(8);}
    free(left);free(right);
}

static void BuildAdult(const CreatureRecipeDocument* doc,Monster* m){*m=Monster_Create();
    REQUIRE(Creature_BuildMonster(m,&doc->recipe,&doc->recipe.adult),"adult horse build");Monster_SetHeadOpenFactor(m,0);}

static int RunBoundedHorse(const char* recipePath){
    CreatureRecipeDocument doc={0};char error[256]={0};REQUIRE(CreatureRecipeIO_LoadFile(&doc,recipePath,error,sizeof(error)),error);
    const int threadCounts[]={1,4,8};
    for(unsigned threadIndex=0;threadIndex<sizeof(threadCounts)/sizeof(threadCounts[0]);++threadIndex){
        int threads=threadCounts[threadIndex];
        fprintf(stderr,"START bounded adult horse threads=%d owners=2\n",threads);
        Monster monsters[2];BuildAdult(&doc,&monsters[0]);BuildAdult(&doc,&monsters[1]);
        LongHairSet oracle[2]={{0}},actual[2]={{0}};MonsterSDF sdf[2]={MonsterSDF_Create(),MonsterSDF_Create()};
        OracleField fields[2];for(int owner=0;owner<2;++owner){REQUIRE(MonsterSDF_Build(&sdf[owner],&monsters[owner],MonsterSDF_DefaultConfig()),"horse exact body/jaw SDF");
            fields[owner]=MakeField(&sdf[owner],&monsters[owner]);REQUIRE(Original_LongHairSet_Build(&oracle[owner],
                monsters[owner].surface.longHair,monsters[owner].surface.longHairCount,&monsters[owner].anatomyGraph,
                monsters[owner].phenotype.axial.totalScale,CombinedDistance,&fields[owner]),"frozen adult horse oracle");
            ApplyProductionHairPose(&oracle[owner],&monsters[owner],&fields[owner]);}
        char countText[16];snprintf(countText,sizeof(countText),"%d",threads);REQUIRE(setenv("MONSTER_LONG_HAIR_THREADS",countText,1)==0,"set test thread count");
        pthread_barrier_t gate;REQUIRE(pthread_barrier_init(&gate,NULL,3)==0,"two-owner barrier init");
        VisualJob jobs[2]={{.gate=&gate,.monster=&monsters[0],.set=&actual[0]},
                           {.gate=&gate,.monster=&monsters[1],.set=&actual[1]}};pthread_t owners[2];
        REQUIRE(pthread_create(&owners[0],NULL,RunVisualJob,&jobs[0])==0&&pthread_create(&owners[1],NULL,RunVisualJob,&jobs[1])==0,
            "two independent visual owners start");pthread_barrier_wait(&gate);pthread_join(owners[0],NULL);pthread_join(owners[1],NULL);
        pthread_barrier_destroy(&gate);
        for(int owner=0;owner<2;++owner){REQUIRE(jobs[owner].ok,"production visual update completed");
            if(threads>1)REQUIRE(actual[owner].scheduler&&LongHairScheduler_GetThreadCount(actual[owner].scheduler)==threads,
                "production visual used the requested private pool");
            CompareProductionVisual(&oracle[owner],&actual[owner],"adult horse body+jaw result differs");}
        printf("PASS bounded adult horse combined-field threads=%d owners=2 guides=%zu children=9216\n",threads,actual[0].count);
        for(int owner=0;owner<2;++owner){Original_LongHairSet_Free(&oracle[owner]);LongHairSet_Free(&actual[owner]);MonsterSDF_Free(&sdf[owner]);Monster_Free(&monsters[owner]);}
    }
    unsetenv("MONSTER_LONG_HAIR_THREADS");return 0;
}

int main(int argc,char** argv){
    if(argc==3&&!strcmp(argv[1],"--bounded-horse"))return RunBoundedHorse(argv[2]);
    REQUIRE(argc==3,"usage: oracle HORSE_RECIPE DOG_RECIPE | --bounded-horse HORSE_RECIPE");
    return RunFullMatrix(argv[1],argv[2]);
}
