#include "Creature.h"
#include "Limb.h"
/** @file benchmark_growth.c
 * @brief Barrido larval real: conectividad, costes y complejidad del worker.
 */
#include "MonsterVisualAsync.h"
#include "MonsterAger.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
static size_t Root(size_t* parent,size_t i){while(parent[i]!=i){parent[i]=parent[parent[i]];i=parent[i];}return i;}
static size_t Components(const Mesh* mesh) {
    size_t* parent=malloc(mesh->vertexCount*sizeof(*parent));if(!parent)return 0;
    for(size_t i=0;i<mesh->vertexCount;++i)parent[i]=i;
    for(size_t i=0;i<mesh->indexCount;i+=3)for(unsigned k=1;k<3;++k)
        parent[Root(parent,mesh->indices[i+k])]=Root(parent,mesh->indices[i]);
    size_t count=0;for(size_t i=0;i<mesh->vertexCount;++i)count+=Root(parent,i)==i;
    if(count>1)for(size_t root=0;root<mesh->vertexCount;++root)if(Root(parent,root)==root) {
        size_t vertices=0;Vector3 lo={1e6f,1e6f,1e6f},hi={-1e6f,-1e6f,-1e6f};
        for(size_t i=0;i<mesh->vertexCount;++i)if(Root(parent,i)==root) {
            Vector3 p=mesh->vertices[i].position;++vertices;
            lo.x=fminf(lo.x,p.x);lo.y=fminf(lo.y,p.y);lo.z=fminf(lo.z,p.z);
            hi.x=fmaxf(hi.x,p.x);hi.y=fmaxf(hi.y,p.y);hi.z=fmaxf(hi.z,p.z);
        }
        fprintf(stderr,"componente %zu vértices: (%g,%g,%g)-(%g,%g,%g)\n",vertices,lo.x,lo.y,lo.z,hi.x,hi.y,hi.z);
    }
    free(parent);return count;
}
int main(int argc,char** argv) {
    Monster larva=Monster_Create(),adult=Monster_Create();
    CreaturePhenotype a=CreatureRecipes_Lizard()->larva,b=CreatureRecipes_Lizard()->adult;
    if(!Creature_BuildMonster(&larva,CreatureRecipes_Lizard(),&a)||!Creature_BuildMonster(&adult,CreatureRecipes_Lizard(),&b))return 1;
    MonsterAger ager=MonsterAger_Create(&larva,&adult,0);
    MonsterVisualAsync* visual=MonsterVisualAsync_Create(MonsterVisualAsync_DefaultConfig());
    if(!visual)return 1;
    MonsterVisualAsync_SetMorphMode(visual,true);MonsterVisualAsync_SetContinuousMotion(visual,true);
    puts("age,development,vertices,triangles,components,valid,watertight,worker_ms,sdf_ms,meshing_ms,surface_ms,bind_ms,head_bind_ms,eye_ms,mouth_ms,candidates,exact,pruned");
    int failed=0;
    for(unsigned i=0;i<=100;++i) {
        float age=argc>1?strtof(argv[1],NULL):i*.01f;
        MonsterAger_SetPerc(&ager,age);const Monster* m=MonsterAger_GetResultConst(&ager);
        MonsterVisualAsync_Update(visual,m,0);MonsterVisualAsync_Flush(visual);
        MonsterVisualAsyncStats s=MonsterVisualAsync_GetStats(visual);
        const Mesh* mesh=MonsterVisualAsync_GetDisplayMesh(visual);MeshValidationResult v=Mesh_Validate(mesh);
        size_t components=Components(mesh);failed|=!v.valid||!v.watertight||components!=1;
        printf("%.3f,%.5f,%zu,%zu,%zu,%d,%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%zu,%zu,%zu\n",
            age,m->phenotype.development.appendages,mesh->vertexCount,mesh->indexCount/3,components,v.valid,v.watertight,
            s.lastBuildDurationMs,s.sdfBuildMs,s.bodyMeshMs,s.surfaceMappingMs,s.morphBindingMs,s.headBindingMs,s.eyeBuildMs,s.mouthBuildMs,
            s.bodyMesher.connectorCandidateCount,s.bodyMesher.connectorExactEvaluationCount,s.bodyMesher.connectorPrunedCount);
        fflush(stdout);
        if(argc>1)break;
    }
    MonsterVisualAsync_Free(visual);MonsterAger_Free(&ager);Monster_Free(&larva);Monster_Free(&adult);return failed;
}
