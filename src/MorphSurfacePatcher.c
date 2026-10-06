/** @file MorphSurfacePatcher.c
 * @brief Selección semántica y recorte común de parches SDF locales.
 */
#include "MorphSurfacePatcher.h"
#include "CreatureMorph.h"
#include "SurfaceMapper.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double PatchNowMs(void) {
    struct timespec now;clock_gettime(CLOCK_MONOTONIC,&now);
    return (double)now.tv_sec*1000.0+(double)now.tv_nsec/1000000.0;
}

static uint32_t VertexModule(const AnatomyDeformer* binding,
    const AnatomyGraph* graph,MeshIndex vertex) {
    if(vertex>=binding->vertexCount)return UINT32_MAX;
    uint16_t connection=binding->bindings[vertex].connIndex[0];
    return connection<graph->connectionCount?
        graph->connections[connection].moduleInstanceId:UINT32_MAX;
}

static size_t CountOpposed(const Mesh* mesh) {
    size_t count=0;
    for(size_t t=0;t+2<mesh->indexCount;t+=3) {
        MeshIndex ia=mesh->indices[t],ib=mesh->indices[t+1],ic=mesh->indices[t+2];
        Vector3 a=mesh->vertices[ia].position,b=mesh->vertices[ib].position,
            c=mesh->vertices[ic].position;
        Vector3 area=Vec3_Cross(Vec3_Sub(b,a),Vec3_Sub(c,a));
        Vector3 normal=Vec3_Add(mesh->vertices[ia].normal,
            Vec3_Add(mesh->vertices[ib].normal,mesh->vertices[ic].normal));
        count+=Vec3_Dot(area,normal)<-1e-8f;
    }
    return count;
}

uint32_t MorphSurfacePatcher_SelectModule(const Mesh* mesh,
    const AnatomyDeformer* binding,const MorphSurfaceTracker* tracker,
    const CreatureMorphTopologyEvent* events,size_t eventCount,
    size_t* problemTriangles) {
    if(problemTriangles)*problemTriangles=0;
    if(!mesh||!binding||!tracker||!events||!eventCount||
       !tracker->problemTriangles||binding->vertexCount!=mesh->vertexCount)
        return UINT32_MAX;
    const AnatomyGraph* graph=&binding->refGraph;
    uint32_t selected=UINT32_MAX;size_t best=0;
    for(size_t event=0;event<eventCount;++event) {
        uint32_t module=events[event].moduleInstanceId;
        size_t bad=0;
        for(size_t t=0;t<mesh->indexCount/3;++t) {
            if(!tracker->problemTriangles[t])continue;
            int matched=0;
            for(int k=0;k<3;++k)
                matched+=VertexModule(binding,graph,
                    mesh->indices[t*3+k])==module;
            bad+=matched>=2;
        }
        if(bad>best){best=bad;selected=module;}
    }
    if(problemTriangles)*problemTriangles=best;
    return best>=100?selected:UINT32_MAX;
}

bool MorphSurfacePatcher_TryModule(const Mesh* source,
    const AnatomyDeformer* binding,const Monster* target,
    const MonsterSDF* sdf,SDFMesher* mesher,uint32_t module,
    Mesh* result,MeshPatchTransfer* transfer,MorphSurfacePatchStats* output) {
    if(!source||!binding||!target||!sdf||!mesher||!result||!transfer||
       !target->hasAnatomyGraph||binding->vertexCount!=source->vertexCount||
       module==UINT32_MAX)return false;
    MorphSurfacePatchStats stats={.moduleInstanceId=module};
    stats.opposedBefore=CountOpposed(source);
    AABB3D bounds=AABB_Empty();size_t points=0;
    Vector3 digitCenter=Vec3_Zero(),proximalCenter=Vec3_Zero();
    size_t proximalPoints=0;
    for(size_t i=0;i<source->vertexCount;++i) {
        if(VertexModule(binding,&binding->refGraph,(MeshIndex)i)!=module)continue;
        int region=(int)lrintf(source->vertices[i].surface.region);
        if(region==SURFACE_REGION_DIGIT) {
            AABB_ExpandPoint(&bounds,source->vertices[i].position);points++;
            digitCenter=Vec3_Add(digitCenter,source->vertices[i].position);
        } else if(region==SURFACE_REGION_FORELIMB||region==SURFACE_REGION_HINDLIMB) {
            proximalCenter=Vec3_Add(proximalCenter,source->vertices[i].position);
            proximalPoints++;
        }
    }
    if(points<32)return false;
    Vector3 extent=AABB_Size(bounds);
    float desired=fmaxf(extent.x,fmaxf(extent.y,extent.z))+.20f;
    float side=desired;
    if(side<.1f||side>=16.0f)return false;
    Vector3 center=Vec3_Scale(Vec3_Add(bounds.start,bounds.end),.5f);
    Vector3 half=Vec3_Create(side*.5f,side*.5f,side*.5f);
    AABB3D box=AABB_FromMinMax(Vec3_Sub(center,half),Vec3_Add(center,half));
    int axis=-1,sign=0;
    if(proximalPoints) {
        Vector3 toRoot=Vec3_Sub(Vec3_Scale(proximalCenter,1.0f/proximalPoints),
            Vec3_Scale(digitCenter,1.0f/points));
        float components[3]={toRoot.x,toRoot.y,toRoot.z};
        axis=0;
        if(fabsf(components[1])>fabsf(components[axis]))axis=1;
        if(fabsf(components[2])>fabsf(components[axis]))axis=2;
        if(fabsf(components[axis])<.05f)axis=-1;
        else sign=components[axis]>0?1:-1;
    }
    const float extensions[3]={0.0f,.10f,.20f};
    AABB3D meshingBox=box;
    if(axis>=0) {
        float* coordinate=sign>0?
            (axis==0?&meshingBox.end.x:axis==1?&meshingBox.end.y:&meshingBox.end.z):
            (axis==0?&meshingBox.start.x:axis==1?&meshingBox.start.y:&meshingBox.start.z);
        *coordinate+=sign*extensions[2];
    }

    Mesh cut=Mesh_Create(),raw=Mesh_Create(),patch=Mesh_Create();
    uint8_t* remove=NULL;size_t removed=0;
    double start=PatchNowMs();
    bool ok=true;
    if(ok) {
        SDFMesherConfig previous=mesher->config;
        mesher->config.useAutoBounds=false;
        mesher->config.adaptiveDetail=true;
        mesher->config.bounds=meshingBox;
        SDFField field=MonsterSDF_GetField(sdf);
        SDFDetailRegion regions[MONSTER_SDF_DETAIL_REGION_CAPACITY];
        size_t count=MonsterSDF_GetDetailRegions(sdf,1.6f,regions,
            MONSTER_SDF_DETAIL_REGION_CAPACITY);
        for(size_t i=0;i<count;++i)
            if(regions[i].targetVoxelSize<.004f)
                regions[i].targetVoxelSize=.004f;
        start=PatchNowMs();
        ok=SDFMesher_GenerateMeshDetailed(mesher,&field,regions,count,&raw);
        stats.cells=mesher->lastStats.cellCount;
        stats.distanceEvaluations=mesher->lastStats.distanceEvaluationCount;
        stats.meshingMs=(float)(PatchNowMs()-start);
        mesher->config=previous;
        if(ok) {
            ok=false;
            for(size_t attempt=0;attempt<(axis>=0?3u:1u);++attempt) {
                AABB3D candidateBox=box;
                if(axis>=0) {
                    float* coordinate=sign>0?
                        (axis==0?&candidateBox.end.x:axis==1?&candidateBox.end.y:&candidateBox.end.z):
                        (axis==0?&candidateBox.start.x:axis==1?&candidateBox.start.y:&candidateBox.start.z);
                    *coordinate+=sign*extensions[attempt];
                }
                Mesh_Free(&cut);cut=Mesh_Create();
                Mesh_Free(&patch);patch=Mesh_Create();
                free(remove);remove=NULL;removed=0;
                start=PatchNowMs();
                bool candidateOk=MeshPatch_CutBox(source,candidateBox,&cut,&remove,&removed);
                if(candidateOk)candidateOk=MeshPatch_RegularizeSelection(&cut,remove,NULL);
                stats.cutMs+=(float)(PatchNowMs()-start);
                if(!candidateOk)continue;
                start=PatchNowMs();
                candidateOk=MeshPatch_ExtractInsideBox(&raw,candidateBox,&patch);
                if(candidateOk)SurfaceMapper_MapMesh(&patch,&target->anatomyGraph,
                    &target->surfaceMapping);
                stats.meshingMs+=(float)(PatchNowMs()-start);
                if(!candidateOk)continue;
                MeshPatchValidation validation={.field=&field,.maximumResidual=.02f};
                start=PatchNowMs();
                candidateOk=MeshPatch_ReplaceWithTransfer(&cut,remove,&patch,
                    side*.5f+extensions[attempt],&validation,&stats.seam,transfer);
                stats.stitchMs+=(float)(PatchNowMs()-start);
                if(candidateOk) {
                    stats.opposedAfter=CountOpposed(&cut);
                    candidateOk=stats.opposedAfter<stats.opposedBefore;
                    if(!candidateOk)MeshPatchTransfer_Free(transfer);
                }
                if(candidateOk){ok=true;break;}
            }
        }
    }
    if(ok) {
        for(size_t i=0;i<transfer->vertexCount;++i)
            if(transfer->oldVertexOfNew[i]>=source->vertexCount)
                transfer->oldVertexOfNew[i]=UINT32_MAX;
        stats.removedTriangles=removed;
        for(size_t i=0;i<transfer->vertexCount;++i)
            stats.newVertices+=transfer->oldVertexOfNew[i]==UINT32_MAX;
        Mesh previous=*result;*result=cut;Mesh_Free(&previous);
        cut=Mesh_Create();
    }
    Mesh_Free(&cut);Mesh_Free(&raw);Mesh_Free(&patch);free(remove);
    if(output)*output=stats;
    return ok;
}
