#include "Creature.h"
#include "Limb.h"
/** @file benchmark_animation.c
 * @brief Auditoría de asignaciones, reconstrucciones y coste real de animación.
 * El enlazador intercepta llamadas; solo cuenta la sección de fotograma.
 */
#include "AnimatedVisual.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <string.h>

static bool measuring;
static size_t allocations,sdfBuilds,meshBuilds;
void* __real_malloc(size_t n);
void* __real_calloc(size_t n,size_t size);
void* __real_realloc(void* p,size_t n);
void* __wrap_malloc(size_t n) { if(measuring)++allocations; return __real_malloc(n); }
void* __wrap_calloc(size_t n,size_t size) { if(measuring)++allocations; return __real_calloc(n,size); }
void* __wrap_realloc(void* p,size_t n) { if(measuring)++allocations; return __real_realloc(p,n); }
bool __real_MonsterSDF_Build(MonsterSDF* sdf,const Monster* m,MonsterSDFConfig cfg);
bool __wrap_MonsterSDF_Build(MonsterSDF* sdf,const Monster* m,MonsterSDFConfig cfg) {
    if(measuring)++sdfBuilds;
    return __real_MonsterSDF_Build(sdf,m,cfg);
}
bool __real_SDFMesher_GenerateMesh(SDFMesher* mesher,const SDFField* field,Mesh* mesh);
bool __wrap_SDFMesher_GenerateMesh(SDFMesher* mesher,const SDFField* field,Mesh* mesh) {
    if(measuring)++meshBuilds;
    return __real_SDFMesher_GenerateMesh(mesher,field,mesh);
}
bool __real_SDFMesher_GenerateMeshDetailed(SDFMesher* mesher,const SDFField* field,const SDFDetailRegion* regions,size_t count,Mesh* mesh);
bool __wrap_SDFMesher_GenerateMeshDetailed(SDFMesher* mesher,const SDFField* field,const SDFDetailRegion* regions,size_t count,Mesh* mesh) {
    if(measuring)++meshBuilds;
    return __real_SDFMesher_GenerateMeshDetailed(mesher,field,regions,count,mesh);
}
static double Time(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec+t.tv_nsec*1e-9; }
int main(int argc,char** argv) {
    bool grow=argc>1 && !strcmp(argv[1],"--morphology");
    Monster m=Monster_Create(); AnimatedVisual binding={0}; int status=1;
    MonsterVisual* v=calloc(1,sizeof(*v)); if(!v)return 1;
    CreaturePhenotype phenotype=grow?CreatureRecipes_Lizard()->juvenile:CreatureRecipes_Lizard()->adult;
    if(!Creature_BuildMonster(&m,CreatureRecipes_Lizard(),&phenotype))goto done;
    SDFMesherConfig config=SDFMesher_DefaultConfig(); config.voxelSize=.075f; config.maxCells=1800000;
    *v=MonsterVisual_Create(config);
    if(!MonsterVisual_RebuildNow(v,&m,MonsterSDF_DefaultConfig()) || !AnimatedVisual_Bind(&binding,v,&m))goto done;
    double poseTime=0,deformTime=0; float maxError=0;
    uint64_t fp=AnatomyGraph_Fingerprint(&m.anatomyGraph); size_t vertexCount=v->mesh.vertexCount;
    MeshIndex* indexBuffer=v->mesh.indices;
    uint64_t generation=v->rebuildGeneration;
    for(int frame=0;frame<180;++frame) {
        if(grow && frame==90) {
            float phase=m.animation->animator.locomotion.phase;
            phenotype=CreatureRecipes_Lizard()->adult;
            if(!Creature_BuildMonster(&m,CreatureRecipes_Lizard(),&phenotype) || AnimatedVisual_Deform(&binding,v,&m) ||
                AnimatedVisual_Bind(&binding,v,&m))goto done;
            if(fabsf(phase-m.animation->animator.locomotion.phase)>.00001f)goto done;
            if(!MonsterVisual_RebuildNow(v,&m,MonsterSDF_DefaultConfig()) || !AnimatedVisual_Bind(&binding,v,&m))goto done;
            fp=AnatomyGraph_Fingerprint(&m.anatomyGraph); generation=v->rebuildGeneration; indexBuffer=v->mesh.indices;
            vertexCount=v->mesh.vertexCount;
        }
        m.animation->animator.desiredVelocity=Vec3_Create(0,0,grow && frame<90?.20f:.38f);
        m.animation->animator.mouthOpen=.4f+.3f*sinf(frame*.03f);
        m.animation->animator.lookDirection=Vec3_Create(.1f*sinf(frame*.02f),.04f,1);
        measuring=true; double start=Time();
        bool ok=MonsterAnimation_Update(&m,1.f/60);
        double middle=Time();
        ok=ok && AnimatedVisual_Deform(&binding,v,&m);
        double end=Time(); measuring=false;
        if(!ok)goto done;
        poseTime+=middle-start; deformTime+=end-middle;
        for(size_t i=0;i<m.animation->rig.limbCount;++i)maxError=fmaxf(maxError,m.animation->animator.limbResults[i].error);
        if(v->rebuildGeneration!=generation || v->mesh.indices!=indexBuffer || vertexCount!=v->mesh.vertexCount ||
            fp!=AnatomyGraph_Fingerprint(&m.anatomyGraph) || !MonsterVisual_MatchesGeometry(v,&m))goto done;
    }
    printf("vértices=%zu frames=180 pose_ms=%.3f deformación_ms=%.3f error_IK_max=%.6f\n",vertexCount,poseTime*1000/180,deformTime*1000/180,maxError);
    printf("malloc/calloc/realloc=%zu SDF_Build=%zu SDFMesher=%zu invalidaciones_huella=0 generaciones_morfológicas=%llu\n",allocations,sdfBuilds,meshBuilds,(unsigned long long)generation);
    status=allocations || sdfBuilds || meshBuilds || maxError>.005f;
done:
    measuring=false; AnimatedVisual_Free(&binding); MonsterVisual_Free(v);free(v);Monster_Free(&m);
    if(status)fprintf(stderr,"Falló la auditoría de animación\n");
    return status;
}
