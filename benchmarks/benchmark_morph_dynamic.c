/** @file benchmark_morph_dynamic.c
 * @brief Referencia exacta y error de predicción anatómica perro-lagarto.
 */
#include "Creature.h"
#include "CreatureMorph.h"
#include "MonsterAger.h"
#include "MonsterVisualAsync.h"
#include "MonsterSDF.h"
#include "SDFSamplingPool.h"
#include "AnatomyDeformer.h"
#include "HeadMorph.h"
#include "MorphSurfaceTracker.h"
#include "MeshPatch.h"
#include "SurfaceMapper.h"
#include "Limb.h"
#include "MathUtils.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static double NowMs(void) {
    struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);
    return (double)t.tv_sec*1000.0+(double)t.tv_nsec/1000000.0;
}
static int Compare(const void* a,const void* b) {
    float x=*(const float*)a,y=*(const float*)b;
    return (x>y)-(x<y);
}
typedef struct PresentedSample {
    Vector3* positions;
    Vector3* normals;
    size_t count;
    float age;
} PresentedSample;
static bool BuildExact(const Monster* monster,MonsterSDF* sdf,SDFMesher* mesher,Mesh* mesh) {
    if(!MonsterSDF_Build(sdf,monster,MonsterSDF_DefaultConfig()))return false;
    SDFField field=MonsterSDF_GetField(sdf);
    SDFDetailRegion regions[MONSTER_SDF_DETAIL_REGION_CAPACITY];
    size_t count=MonsterSDF_GetDetailRegions(sdf,1.6f,regions,MONSTER_SDF_DETAIL_REGION_CAPACITY);
    for(size_t i=0;i<count;++i)if(regions[i].targetVoxelSize<.004f)regions[i].targetVoxelSize=.004f;
    return SDFMesher_GenerateMeshDetailed(mesher,&field,regions,count,mesh);
}
static void AuditTriangles(const Mesh* mesh,size_t* thin,size_t* opposed) {
    *thin=*opposed=0;
    for(size_t triangle=0;triangle+2<mesh->indexCount;triangle+=3) {
        MeshIndex ia=mesh->indices[triangle],ib=mesh->indices[triangle+1],ic=mesh->indices[triangle+2];
        Vector3 a=mesh->vertices[ia].position,b=mesh->vertices[ib].position,
            c=mesh->vertices[ic].position;
        Vector3 ab=Vec3_Sub(b,a),bc=Vec3_Sub(c,b),ca=Vec3_Sub(a,c);
        Vector3 area=Vec3_Cross(ab,Vec3_Sub(c,a));
        float longest=fmaxf(Vec3_Dot(ab,ab),fmaxf(Vec3_Dot(bc,bc),Vec3_Dot(ca,ca)));
        if(longest>1e-12f && Vec3_Length(area)/longest<.02f)(*thin)++;
        Vector3 normals=Vec3_Add(mesh->vertices[ia].normal,
            Vec3_Add(mesh->vertices[ib].normal,mesh->vertices[ic].normal));
        if(Vec3_Dot(area,normals)<-1e-8f)(*opposed)++;
    }
}
static size_t AuditSDFOrientation(const Mesh* mesh,const SDFField* field);
static SurfaceRegion TriangleRegion(const Mesh* mesh,MeshIndex ia,MeshIndex ib,MeshIndex ic) {
    int a=(int)lrintf(mesh->vertices[ia].surface.region);
    int b=(int)lrintf(mesh->vertices[ib].surface.region);
    int c=(int)lrintf(mesh->vertices[ic].surface.region);
    int region=(a==b||a==c)?a:(b==c?b:a);
    return region>=0&&region<SURFACE_REGION_COUNT?(SurfaceRegion)region:SURFACE_REGION_UNKNOWN;
}
static void AuditRegions(const Mesh* mesh,const AnatomyDeformer* morph,
    const Monster* target,const MorphSurfaceTracker* tracker,const char* stage,float age) {
    size_t vertices[SURFACE_REGION_COUNT]={0},collapsed[SURFACE_REGION_COUNT]={0};
    size_t triangles[SURFACE_REGION_COUNT]={0},thin[SURFACE_REGION_COUNT]={0},
        opposed[SURFACE_REGION_COUNT]={0},problem[SURFACE_REGION_COUNT]={0};
    for(size_t i=0;i<mesh->vertexCount;++i) {
        int region=(int)lrintf(mesh->vertices[i].surface.region);
        if(region<0||region>=SURFACE_REGION_COUNT)region=SURFACE_REGION_UNKNOWN;
        vertices[region]++;
        size_t connection=morph->bindings[i].connIndex[0];
        if(connection<target->anatomyGraph.connectionCount &&
           target->anatomyGraph.connections[connection].development<.10f)
            collapsed[region]++;
    }
    for(size_t t=0;t+2<mesh->indexCount;t+=3) {
        MeshIndex ia=mesh->indices[t],ib=mesh->indices[t+1],ic=mesh->indices[t+2];
        SurfaceRegion region=TriangleRegion(mesh,ia,ib,ic);
        Vector3 a=mesh->vertices[ia].position,b=mesh->vertices[ib].position,
            c=mesh->vertices[ic].position;
        Vector3 ab=Vec3_Sub(b,a),bc=Vec3_Sub(c,b),ca=Vec3_Sub(a,c);
        Vector3 area=Vec3_Cross(ab,Vec3_Sub(c,a));
        float longest=fmaxf(Vec3_Dot(ab,ab),fmaxf(Vec3_Dot(bc,bc),Vec3_Dot(ca,ca)));
        Vector3 normal=Vec3_Add(mesh->vertices[ia].normal,
            Vec3_Add(mesh->vertices[ib].normal,mesh->vertices[ic].normal));
        triangles[region]++;
        thin[region]+=longest>1e-12f && Vec3_Length(area)/longest<.02f;
        opposed[region]+=Vec3_Dot(area,normal)<-1e-8f;
        if(tracker)problem[region]+=tracker->problemTriangles[t/3]!=0;
    }
    for(int region=0;region<SURFACE_REGION_COUNT;++region)
        if(triangles[region]||vertices[region])
            printf("region stage=%s age=%.2f id=%d vertices=%zu collapsed=%zu triangles=%zu thin=%zu opposed=%zu problem=%zu\n",
                stage,age,region,vertices[region],collapsed[region],triangles[region],
                thin[region],opposed[region],problem[region]);
}
static int DigitGroup(const AnatomyGraph* graph,size_t connection) {
    if(connection>=graph->connectionCount ||
       graph->connections[connection].kind!=BODY_CONNECTION_DIGIT_SEGMENT)return -1;
    const AnatomyNode* node=AnatomyGraph_FindNode(graph,graph->connections[connection].toId);
    unsigned base=Limb_DigitLocalId(0,0);
    return node&&node->localNodeId>=base &&
        node->localNodeId<base+CREATURE_MAX_DIGITS*8?
        (int)((node->localNodeId-base)/8):-1;
}
static size_t FilterBranchBindings(AnatomyDeformer* morph) {
    const AnatomyGraph* graph=&morph->refGraph;
    size_t changed=0;
    for(size_t i=0;i<morph->vertexCount;++i) {
        AnatomyDeformerBinding* binding=&morph->bindings[i];
        const BodyConnection* main=&graph->connections[binding->connIndex[0]];
        bool limb=main->kind==BODY_CONNECTION_LIMB_SEGMENT ||
            main->kind==BODY_CONNECTION_DIGIT_SEGMENT;
        if(!limb)continue;
        int digit=DigitGroup(graph,binding->connIndex[0]);
        float sum=0;
        for(int k=0;k<ANATOMY_MORPH_INFLUENCES;++k) {
            const BodyConnection* other=&graph->connections[binding->connIndex[k]];
            bool keep=other->moduleInstanceId==main->moduleInstanceId &&
                DigitGroup(graph,binding->connIndex[k])==digit;
            if(!keep && binding->weight[k]>0)changed++;
            if(!keep)binding->weight[k]=0;
            sum+=binding->weight[k];
        }
        if(sum>0)for(int k=0;k<ANATOMY_MORPH_INFLUENCES;++k)
            binding->weight[k]/=sum;
    }
    return changed;
}
static void ProbeRadialFootProjection(Mesh* mesh,const AnatomyDeformer* morph,
    const Monster* target,const SDFField* field) {
    size_t accepted=0,unbracketed=0,centerOutside=0,skippedNear=0,evaluations=0;
    for(size_t i=0;i<mesh->vertexCount;++i) {
        int region=(int)lrintf(mesh->vertices[i].surface.region);
        if(region!=SURFACE_REGION_DIGIT && region!=SURFACE_REGION_HINDLIMB &&
           region!=SURFACE_REGION_FORELIMB)continue;
        const AnatomyDeformerBinding* binding=&morph->bindings[i];
        size_t index=binding->connIndex[0];
        if(index>=target->anatomyGraph.connectionCount)continue;
        const BodyConnection* connection=&target->anatomyGraph.connections[index];
        const AnatomyNode* from=AnatomyGraph_FindNode(&target->anatomyGraph,connection->fromId);
        const AnatomyNode* to=AnatomyGraph_FindNode(&target->anatomyGraph,connection->toId);
        if(!from||!to)continue;
        Vector3 center=Vec3_Lerp(from->center,to->center,binding->projT[0]);
        Vector3 p=mesh->vertices[i].position;
        float atPoint=field->evaluateDistance(field->context,p);evaluations++;
        if(isfinite(atPoint)&&fabsf(atPoint)<.003f){skippedNear++;continue;}
        Vector3 radial=Vec3_Sub(p,center);
        float radius=Vec3_Length(radial);
        if(radius<1e-6f)continue;
        radial=Vec3_Scale(radial,1.0f/radius);
        float inside=field->evaluateDistance(field->context,center);evaluations++;
        if(!isfinite(inside)||inside>=-.0001f){centerOutside++;continue;}
        float outer=fmaxf(radius,.01f);
        Vector3 endpoint=Vec3_Add(center,Vec3_Scale(radial,outer));
        float outside=outer==radius?atPoint:
            field->evaluateDistance(field->context,endpoint);
        if(outer!=radius)evaluations++;
        for(int expand=0;expand<3 && outside<0 && outer<.5f;++expand) {
            outer*=1.7f;
            endpoint=Vec3_Add(center,Vec3_Scale(radial,outer));
            outside=field->evaluateDistance(field->context,endpoint);evaluations++;
        }
        if(!isfinite(outside)||outside<0){unbracketed++;continue;}
        float lo=0,hi=outer,loValue=inside,hiValue=outside;
        for(int step=0;step<4;++step) {
            float alpha=Math_Clamp(-loValue/(hiValue-loValue),.12f,.88f);
            float mid=Math_Lerp(lo,hi,alpha);
            float value=field->evaluateDistance(field->context,
                Vec3_Add(center,Vec3_Scale(radial,mid)));
            evaluations++;
            if(value<0){lo=mid;loValue=value;}
            else {hi=mid;hiValue=value;}
        }
        float root=Math_Lerp(lo,hi,Math_Clamp01(-loValue/(hiValue-loValue)));
        mesh->vertices[i].position=Vec3_Add(center,Vec3_Scale(radial,root));
        accepted++;
    }
    size_t thin=0,opposed=0;AuditTriangles(mesh,&thin,&opposed);
    printf("radial accepted=%zu skippedNear=%zu outsideCenters=%zu unbracketed=%zu evals=%zu thin=%zu opposed=%zu sdfOpposedSample=%zu\n",
        accepted,skippedNear,centerOutside,unbracketed,evaluations,thin,opposed,
        AuditSDFOrientation(mesh,field));
}
static size_t AuditSDFOrientation(const Mesh* mesh,const SDFField* field) {
    size_t opposed=0;
    const float h=.003f;
    for(size_t triangle=0;triangle+2<mesh->indexCount;triangle+=30) {
        MeshIndex ia=mesh->indices[triangle],ib=mesh->indices[triangle+1],
            ic=mesh->indices[triangle+2];
        Vector3 a=mesh->vertices[ia].position,b=mesh->vertices[ib].position,
            c=mesh->vertices[ic].position;
        Vector3 center=Vec3_Scale(Vec3_Add(a,Vec3_Add(b,c)),1.0f/3);
        Vector3 area=Vec3_Cross(Vec3_Sub(b,a),Vec3_Sub(c,a));
        Vector3 gradient=Vec3_Create(0,0,0);
        for(int axis=0;axis<3;++axis) {
            Vector3 step=Vec3_Create(axis==0?h:0,axis==1?h:0,axis==2?h:0);
            float derivative=(field->evaluateDistance(field->context,Vec3_Add(center,step))-
                field->evaluateDistance(field->context,Vec3_Sub(center,step)))/(2*h);
            if(axis==0)gradient.x=derivative;
            if(axis==1)gradient.y=derivative;
            if(axis==2)gradient.z=derivative;
        }
        if(Vec3_Dot(area,gradient)<0)opposed++;
    }
    return opposed;
}
static void ProbeSmooth(const MorphSurfaceTracker* tracker,Mesh* mesh,
    const SDFField* field,const Vector3* predicted) {
    size_t count=mesh->vertexCount;
    Vector3* projected=malloc(count*sizeof(*projected));
    Vector3* delta=malloc(count*sizeof(*delta));
    Vector3* next=malloc(count*sizeof(*next));
    float* errors=malloc(count*sizeof(*errors));
    if(!projected||!delta||!next||!errors)exit(18);
    for(size_t i=0;i<count;++i)projected[i]=mesh->vertices[i].position;
    const float alphas[]={.25f,.50f,.75f,.9f,1.0f,1.0f};
    const int iterations[]={2,2,2,4,8,16};
    for(size_t variant=0;variant<sizeof(alphas)/sizeof(alphas[0]);++variant) {
        for(size_t i=0;i<count;++i)delta[i]=Vec3_Sub(projected[i],predicted[i]);
        for(int iteration=0;iteration<iterations[variant];++iteration) {
            for(size_t i=0;i<count;++i) {
                Vector3 average=Vec3_Create(0,0,0);size_t neighbors=0;
                for(size_t at=tracker->triangleOffsets[i];at<tracker->triangleOffsets[i+1];++at) {
                    size_t triangle=tracker->incidentTriangles[at];
                    for(int corner=0;corner<3;++corner) {
                        MeshIndex other=mesh->indices[triangle+corner];
                        if(other==i)continue;
                        average=Vec3_Add(average,delta[other]);neighbors++;
                    }
                }
                if(neighbors)average=Vec3_Scale(average,1.0f/neighbors);
                Vector3 original=Vec3_Sub(projected[i],predicted[i]);
                next[i]=Vec3_Lerp(original,average,alphas[variant]);
            }
            Vector3* swap=delta;delta=next;next=swap;
        }
        float maximum=0;double mean=0;
        for(size_t i=0;i<count;++i) {
            mesh->vertices[i].position=Vec3_Add(predicted[i],delta[i]);
            errors[i]=fabsf(field->evaluateDistance(field->context,mesh->vertices[i].position));
            mean+=errors[i];maximum=fmaxf(maximum,errors[i]);
        }
        size_t thin=0,opposed=0;AuditTriangles(mesh,&thin,&opposed);
        qsort(errors,count,sizeof(*errors),Compare);
        printf("smooth alpha=%.2f iterations=%d thin=%zu opposed=%zu sdfOpposedSample=%zu mean=%.6f p95=%.6f max=%.6f\n",
            alphas[variant],iterations[variant],thin,opposed,AuditSDFOrientation(mesh,field),
            mean/count,errors[(size_t)(count*.95f)],maximum);
    }
    free(projected);free(delta);free(next);free(errors);
}
static void ProbeOrientation(Mesh* mesh,const SDFField* field,const Vector3* predicted) {
    size_t n=mesh->vertexCount;
    Vector3* corrected=malloc(n*sizeof(*corrected));
    float* alpha=malloc(n*sizeof(*alpha));
    float* errors=malloc(n*sizeof(*errors));
    if(!corrected||!alpha||!errors)exit(20);
    for(size_t i=0;i<n;++i) {corrected[i]=mesh->vertices[i].position;alpha[i]=1;}
    for(int pass=0;pass<12;++pass) {
        size_t reduced=0;
        for(size_t t=0;t+2<mesh->indexCount;t+=3) {
            MeshIndex ia=mesh->indices[t],ib=mesh->indices[t+1],ic=mesh->indices[t+2];
            Vector3 pa=predicted[ia],pb=predicted[ib],pc=predicted[ic];
            Vector3 ref=Vec3_Cross(Vec3_Sub(pb,pa),Vec3_Sub(pc,pa));
            float ref2=Vec3_Dot(ref,ref);
            if(ref2<1e-12f)continue;
            Vector3 a=Vec3_Lerp(pa,corrected[ia],alpha[ia]);
            Vector3 b=Vec3_Lerp(pb,corrected[ib],alpha[ib]);
            Vector3 c=Vec3_Lerp(pc,corrected[ic],alpha[ic]);
            Vector3 area=Vec3_Cross(Vec3_Sub(b,a),Vec3_Sub(c,a));
            if(Vec3_Dot(area,ref)<ref2*.05f) {
                alpha[ia]*=.5f;alpha[ib]*=.5f;alpha[ic]*=.5f;
                reduced++;
            }
        }
        printf("orientation pass=%d reduced=%zu\n",pass,reduced);
        if(!reduced)break;
    }
    double mean=0;float maximum=0;
    for(size_t i=0;i<n;++i) {
        mesh->vertices[i].position=Vec3_Lerp(predicted[i],corrected[i],alpha[i]);
        errors[i]=fabsf(field->evaluateDistance(field->context,mesh->vertices[i].position));
        mean+=errors[i];maximum=fmaxf(maximum,errors[i]);
    }
    size_t thin=0,opposed=0;AuditTriangles(mesh,&thin,&opposed);
    qsort(errors,n,sizeof(*errors),Compare);
    printf("orientation thin=%zu opposed=%zu sdfOpposedSample=%zu mean=%.6f p95=%.6f max=%.6f\n",
        thin,opposed,AuditSDFOrientation(mesh,field),mean/n,errors[(size_t)(n*.95f)],maximum);
    free(corrected);free(alpha);free(errors);
}
static size_t ProbeFoldGuard(Mesh* mesh,MorphSurfaceTracker* tracker,const Vector3* raw) {
    uint8_t* revert=calloc(mesh->vertexCount,1);
    if(!revert)exit(22);
    size_t rescued=0;
    for(size_t triangle=0;triangle+2<mesh->indexCount;triangle+=3) {
        MeshIndex ia=mesh->indices[triangle],ib=mesh->indices[triangle+1],ic=mesh->indices[triangle+2];
        Vector3 pa=raw[ia],pb=raw[ib],pc=raw[ic];
        Vector3 qa=mesh->vertices[ia].position,qb=mesh->vertices[ib].position,qc=mesh->vertices[ic].position;
        Vector3 ref=Vec3_Cross(Vec3_Sub(pb,pa),Vec3_Sub(pc,pa));
        Vector3 moved=Vec3_Cross(Vec3_Sub(qb,qa),Vec3_Sub(qc,qa));
        if(Vec3_Dot(ref,ref)<1e-10f)continue;
        if(Vec3_Dot(moved,ref)<.05f*Vec3_Dot(ref,ref)) {
            revert[ia]=revert[ib]=revert[ic]=1;rescued++;
        }
    }
    size_t reverted=0;
    for(size_t i=0;i<mesh->vertexCount;++i)if(revert[i]) {
        mesh->vertices[i].position=raw[i];
        tracker->corrections[i]=Vec3_Sub(raw[i],tracker->rawPositions[i]);
        reverted++;
    }
    size_t thin=0,opposed=0;AuditTriangles(mesh,&thin,&opposed);
    printf("foldGuard rescued=%zu reverted=%zu thin=%zu opposed=%zu\n",
        rescued,reverted,thin,opposed);
    free(revert);return reverted;
}
typedef struct ProbeEdgeFace { MeshIndex lo,hi,from,to,opposite;size_t triangle; } ProbeEdgeFace;
static int CompareProbeEdgeFace(const void* left,const void* right) {
    const ProbeEdgeFace* a=left;const ProbeEdgeFace* b=right;
    if(a->lo!=b->lo)return (a->lo>b->lo)-(a->lo<b->lo);
    if(a->hi!=b->hi)return (a->hi>b->hi)-(a->hi<b->hi);
    return (a->triangle>b->triangle)-(a->triangle<b->triangle);
}
static float ProbeFaceScore(const Mesh* mesh,MeshIndex a,MeshIndex b,MeshIndex c) {
    Vector3 pa=mesh->vertices[a].position,pb=mesh->vertices[b].position,
        pc=mesh->vertices[c].position;
    Vector3 ab=Vec3_Sub(pb,pa),ac=Vec3_Sub(pc,pa);
    Vector3 area=Vec3_Cross(ab,ac);
    float areaLength=Vec3_Length(area);
    float longest=fmaxf(Vec3_Dot(ab,ab),fmaxf(Vec3_Dot(ac,ac),
        Vec3_LengthSq(Vec3_Sub(pb,pc))));
    Vector3 normal=Vec3_Normalize(Vec3_Add(mesh->vertices[a].normal,
        Vec3_Add(mesh->vertices[b].normal,mesh->vertices[c].normal)));
    float orientation=Vec3_Dot(area,normal);
    float penalty=orientation<0?10.0f:0.0f;
    return penalty+fminf(1.0f,longest/fmaxf(areaLength,1e-8f))*.05f;
}
static void ProbeEdgeFlips(Mesh* mesh) {
    size_t triangles=mesh->indexCount/3,edgeCount=triangles*3;
    ProbeEdgeFace* edges=malloc(edgeCount*sizeof(*edges));
    uint8_t* touched=calloc(triangles,1);
    if(!edges||!touched)exit(23);
    for(size_t t=0;t<triangles;++t)for(int e=0;e<3;++e) {
        MeshIndex from=mesh->indices[t*3+e],to=mesh->indices[t*3+(e+1)%3],
            opposite=mesh->indices[t*3+(e+2)%3];
        edges[t*3+e]=(ProbeEdgeFace){.lo=from<to?from:to,
            .hi=from<to?to:from,.from=from,.to=to,.opposite=opposite,.triangle=t};
    }
    qsort(edges,edgeCount,sizeof(*edges),CompareProbeEdgeFace);
    size_t flips=0,candidates=0;
    for(size_t i=0;i+1<edgeCount;) {
        size_t end=i+1;
        while(end<edgeCount && edges[end].lo==edges[i].lo && edges[end].hi==edges[i].hi)end++;
        if(end-i==2) {
            ProbeEdgeFace a=edges[i],b=edges[i+1];
            if(a.from==b.to && a.to==b.from && !touched[a.triangle] &&
               !touched[b.triangle] && a.opposite!=b.opposite) {
                candidates++;
                float oldScore=ProbeFaceScore(mesh,a.from,a.to,a.opposite)+
                    ProbeFaceScore(mesh,b.from,b.to,b.opposite);
                float newScore=ProbeFaceScore(mesh,a.opposite,b.opposite,a.to)+
                    ProbeFaceScore(mesh,b.opposite,a.opposite,a.from);
                if(newScore+.01f<oldScore && newScore<10.0f) {
                    MeshIndex* first=&mesh->indices[a.triangle*3];
                    MeshIndex* second=&mesh->indices[b.triangle*3];
                    first[0]=a.opposite;first[1]=b.opposite;first[2]=a.to;
                    second[0]=b.opposite;second[1]=a.opposite;second[2]=a.from;
                    touched[a.triangle]=touched[b.triangle]=1;flips++;
                }
            }
        }
        i=end;
    }
    size_t thin=0,opposed=0;AuditTriangles(mesh,&thin,&opposed);
    MeshValidationResult valid=Mesh_Validate(mesh);
    printf("edgeFlip candidates=%zu flips=%zu thin=%zu opposed=%zu valid=%d watertight=%d nonmanifold=%zu\n",
        candidates,flips,thin,opposed,valid.valid,valid.watertight,
        valid.nonManifoldEdgeCount);
    free(edges);free(touched);
}
static size_t SelectModuleFaces(const Mesh* mesh,const AnatomyDeformer* binding,
    const Monster* target,uint32_t module,uint8_t* selection) {
    size_t count=0;
    for(size_t t=0;t<mesh->indexCount/3;++t) {
        int matching=0;
        for(int k=0;k<3;++k) {
            MeshIndex id=mesh->indices[t*3+k];
            uint16_t connection=binding->bindings[id].connIndex[0];
            matching+=connection<target->anatomyGraph.connectionCount&&
                target->anatomyGraph.connections[connection].moduleInstanceId==module;
        }
        selection[t]=matching>=2;
        count+=selection[t]!=0;
    }
    return count;
}

static bool ProbeLocalPatch(Mesh* mesh,const AnatomyDeformer* morph,
    const Monster* target,MonsterSDF* sdf,const SDFMesherConfig* referenceConfig,
    uint32_t forcedModule,MeshPatchTransfer* transfer) {
    const char* moduleOverride=getenv("MORPH_PATCH_MODULE");
    if(moduleOverride)forcedModule=(uint32_t)strtoul(moduleOverride,NULL,10);
    uint32_t chosen=UINT32_MAX;size_t chosenBad=0;
    for(size_t connection=0;connection<target->anatomyGraph.connectionCount;++connection) {
        uint32_t module=target->anatomyGraph.connections[connection].moduleInstanceId;
        size_t bad=0;
        for(size_t t=0;t<mesh->indexCount/3;++t) {
            MeshIndex a=mesh->indices[t*3];
            if((int)lrintf(mesh->vertices[a].surface.region)!=SURFACE_REGION_DIGIT ||
               morph->bindings[a].connIndex[0]>=target->anatomyGraph.connectionCount ||
               target->anatomyGraph.connections[morph->bindings[a].connIndex[0]].moduleInstanceId!=module)
                continue;
            MeshIndex b=mesh->indices[t*3+1],c=mesh->indices[t*3+2];
            Vector3 pa=mesh->vertices[a].position,pb=mesh->vertices[b].position,
                pc=mesh->vertices[c].position;
            Vector3 area=Vec3_Cross(Vec3_Sub(pb,pa),Vec3_Sub(pc,pa));
            Vector3 normal=Vec3_Add(mesh->vertices[a].normal,
                Vec3_Add(mesh->vertices[b].normal,mesh->vertices[c].normal));
            bad+=Vec3_Dot(area,normal)<-1e-8f;
        }
        if((forcedModule==UINT32_MAX&&bad>chosenBad)||
           (forcedModule!=UINT32_MAX&&module==forcedModule&&bad>chosenBad)) {
            chosenBad=bad;chosen=module;
        }
    }
    if(chosen==UINT32_MAX)return false;
    AABB3D bounds=AABB_Empty();size_t points=0;
    for(size_t i=0;i<mesh->vertexCount;++i) {
        if((int)lrintf(mesh->vertices[i].surface.region)!=SURFACE_REGION_DIGIT ||
           morph->bindings[i].connIndex[0]>=target->anatomyGraph.connectionCount ||
           target->anatomyGraph.connections[morph->bindings[i].connIndex[0]].moduleInstanceId!=chosen)
            continue;
        AABB_ExpandPoint(&bounds,mesh->vertices[i].position);points++;
    }
    if(getenv("MORPH_PATCH_TARGET_BOUNDS"))for(size_t i=0;i<sdf->connectorCount;++i) {
        const MonsterSDFConnector* connector=&sdf->connectors[i];
        if(connector->moduleInstanceId!=chosen)continue;
        AABB_ExpandPoint(&bounds,connector->bounds.start);
        AABB_ExpandPoint(&bounds,connector->bounds.end);
    }
    Vector3 extent=AABB_Size(bounds),center=Vec3_Scale(Vec3_Add(bounds.start,bounds.end),.5f);
    const char* shiftX=getenv("MORPH_PATCH_SHIFT_X");
    const char* shiftY=getenv("MORPH_PATCH_SHIFT_Y");
    static unsigned patchAttempt=0;
    const char* laterShiftY=getenv("MORPH_PATCH_SHIFT_LATER_Y");
    bool laterAttempt=patchAttempt++!=0;
    if(laterAttempt && laterShiftY)shiftY=laterShiftY;
    const char* shiftZ=getenv("MORPH_PATCH_SHIFT_Z");
    if(shiftX)center.x+=strtof(shiftX,NULL);
    if(shiftY)center.y+=strtof(shiftY,NULL);
    if(shiftZ)center.z+=strtof(shiftZ,NULL);
    float desired=fmaxf(extent.x,fmaxf(extent.y,extent.z))+.20f;
    float side=.004f;while(side<desired)side*=2;
    if(getenv("MORPH_PATCH_CONTINUOUS_SIDE"))side=desired;
    if(getenv("MORPH_PATCH_DOUBLE"))side*=2;
    Vector3 half=Vec3_Create(side*.5f,side*.5f,side*.5f);
    AABB3D cube=AABB_FromMinMax(Vec3_Sub(center,half),Vec3_Add(center,half));
    const char* topExtension=getenv("MORPH_PATCH_TOP_EXTENSION");
    const char* laterTop=getenv("MORPH_PATCH_TOP_AFTER_TWO");
    if(patchAttempt>2&&laterTop)topExtension=laterTop;
    const char* finalTop=getenv("MORPH_PATCH_TOP_AFTER_THREE");
    if(patchAttempt>3&&finalTop)topExtension=finalTop;
    if(topExtension)cube.end.y+=strtof(topExtension,NULL);
    printf("patch probe module=%u bad=%zu points=%zu side=%.3f bounds=(%.3f,%.3f,%.3f)-(%.3f,%.3f,%.3f)\n",
        chosen,chosenBad,points,side,cube.start.x,cube.start.y,cube.start.z,
        cube.end.x,cube.end.y,cube.end.z);
    size_t originalVertexCount=mesh->vertexCount;
    uint8_t* remove=NULL;
    size_t removed=0;
    Mesh cut=Mesh_Create();
    Mesh* candidate=mesh;
    bool semantic=getenv("MORPH_PATCH_SEMANTIC")!=NULL;
    if(getenv("MORPH_PATCH_CUT") && !semantic) {
        double cutStart=NowMs();
        bool cutOk=MeshPatch_CutBox(mesh,cube,&cut,&remove,&removed);
        printf("patch cut=%d ms=%.3f vertices=%zu triangles=%zu removed=%zu\n",
            cutOk,NowMs()-cutStart,cut.vertexCount,cut.indexCount/3,removed);
        if(!cutOk){Mesh_Free(&cut);return false;}
        candidate=&cut;
    } else {
        remove=calloc(mesh->indexCount/3,1);
        if(!remove)exit(24);
        if(semantic)removed=SelectModuleFaces(mesh,morph,target,chosen,remove);
        else for(size_t t=0;t<mesh->indexCount/3;++t) {
            Vector3 centroid=Vec3_Zero();
            bool any=false;
            for(int k=0;k<3;++k) {
                Vector3 point=mesh->vertices[mesh->indices[t*3+k]].position;
                centroid=Vec3_Add(centroid,point);
                any|=AABB_ContainsPoint(cube,point);
            }
            centroid=Vec3_Scale(centroid,1.0f/3);
            remove[t]=getenv("MORPH_PATCH_ANY")?any:AABB_ContainsPoint(cube,centroid);
            removed+=remove[t]!=0;
        }
    }
    size_t expanded=0;
    bool regular=MeshPatch_RegularizeSelection(candidate,remove,&expanded);
    printf("patch selection regular=%d expanded=%zu\n",regular,expanded);
    SDFMesherConfig config=*referenceConfig;
    config.useAutoBounds=false;config.adaptiveDetail=true;config.bounds=cube;
    SDFMesher local=SDFMesher_Create(config);Mesh patch=Mesh_Create();
    SDFField field=MonsterSDF_GetField(sdf);
    SDFDetailRegion regions[MONSTER_SDF_DETAIL_REGION_CAPACITY];
    size_t count=MonsterSDF_GetDetailRegions(sdf,1.6f,regions,MONSTER_SDF_DETAIL_REGION_CAPACITY);
    for(size_t i=0;i<count;++i)if(regions[i].targetVoxelSize<.004f)
        regions[i].targetVoxelSize=.004f;
    double start=NowMs();
    bool generated=SDFMesher_GenerateMeshDetailed(&local,&field,regions,count,&patch);
    if(generated&&getenv("MORPH_PATCH_SHARED_BOX")) {
        Mesh clipped=Mesh_Create();
        generated=MeshPatch_ExtractInsideBox(&patch,cube,&clipped);
        if(generated){Mesh_Free(&patch);patch=clipped;}
        else Mesh_Free(&clipped);
    }
    if(generated)SurfaceMapper_MapMesh(&patch,&target->anatomyGraph,
        &target->surfaceMapping);
    if(generated&&semantic) {
        AnatomyDeformer* patchBinding=AnatomyDeformer_Create();
        generated=patchBinding&&AnatomyDeformer_Bind(patchBinding,&patch,
            &target->anatomyGraph);
        if(generated) {
            uint8_t* patchSelection=calloc(patch.indexCount/3,1);
            generated=patchSelection!=NULL;
            if(generated) {
                size_t selected=SelectModuleFaces(&patch,patchBinding,target,chosen,
                    patchSelection);
                Mesh partial=Mesh_Create();
                generated=Mesh_ReserveVertices(&partial,patch.vertexCount)&&
                    Mesh_ReserveIndices(&partial,selected*3);
                if(generated) {
                    memcpy(partial.vertices,patch.vertices,
                        patch.vertexCount*sizeof(*patch.vertices));
                    partial.vertexCount=patch.vertexCount;
                    partial.surfaceRecipe=patch.surfaceRecipe;
                    partial.hasSurface=patch.hasSurface;
                    for(size_t t=0;t<patch.indexCount/3;++t)if(patchSelection[t] &&
                        !Mesh_AddTriangle(&partial,patch.indices[t*3],
                            patch.indices[t*3+1],patch.indices[t*3+2])) {
                        generated=false;break;
                    }
                    if(generated) {
                        printf("patch semantic oldFaces=%zu newFaces=%zu\n",removed,selected);
                        Mesh_Free(&patch);patch=partial;
                    } else Mesh_Free(&partial);
                } else Mesh_Free(&partial);
                free(patchSelection);
            }
        }
        AnatomyDeformer_Free(patchBinding);
    }
    size_t components=0,largest=0,second=0;
    if(generated)Mesh_ComponentStatistics(&patch,&components,&largest,&second);
    printf("patch extracted=%d ms=%.3f removed=%zu newVertices=%zu newTriangles=%zu cells=%zu distanceEvals=%zu fullSamples=%zu components=%zu largest=%zu second=%zu\n",
        generated,NowMs()-start,removed,patch.vertexCount,patch.indexCount/3,
        local.lastStats.cellCount,local.lastStats.distanceEvaluationCount,
        local.lastStats.fullSampleEvaluationCount,components,largest,second);
    if(generated) {
        size_t patchThin=0,patchOpposed=0;
        AuditTriangles(&patch,&patchThin,&patchOpposed);
        printf("patch extracted quality thin=%zu opposed=%zu\n",patchThin,patchOpposed);
    }
    bool stitched=false;
    if(generated) {
        MeshPatchStats stats={0};start=NowMs();
        MeshPatchValidation validation={.field=&field,.maximumResidual=.02f};
        validation.angularSeam=getenv("MORPH_PATCH_ANGLE")!=NULL ||
            (patchAttempt>3&&getenv("MORPH_PATCH_ANGLE_LATER")!=NULL);
        const char* phaseShift=getenv("MORPH_PATCH_PHASE");
        const char* laterPhase=getenv("MORPH_PATCH_PHASE_LATER");
        if(laterAttempt&&laterPhase)phaseShift=laterPhase;
        if(phaseShift)validation.loopPhaseShift=atoi(phaseShift);
        const char* residualLimit=getenv("MORPH_PATCH_MAX_RESIDUAL");
        if(residualLimit)validation.maximumResidual=strtof(residualLimit,NULL);
        const char* blend=getenv("MORPH_PATCH_BLEND");
        if(blend) {
            validation.hasBoundaryBlend=true;
            validation.boundaryBlend=strtof(blend,NULL);
        }
        stitched=MeshPatch_ReplaceWithTransfer(candidate,remove,&patch,side*.5f,
            &validation,&stats,transfer);
        if(stitched&&transfer&&candidate==&cut)
            for(size_t i=0;i<transfer->vertexCount;++i)
                if(transfer->oldVertexOfNew[i]>=originalVertexCount)
                    transfer->oldVertexOfNew[i]=UINT32_MAX;
        printf("patch stitched=%d ms=%.3f oldLoops=%zu newLoops=%zu oldEdges=%zu newEdges=%zu oldError=%d newError=%d seamTriangles=%zu gap=%.4f residual=%.5f boundary=(%.5f,%.5f) opposed=%zu oldOpposed=%zu newOpposed=%zu evals=%zu\n",
            stitched,NowMs()-start,stats.oldBoundaryLoops,stats.newBoundaryLoops,
            stats.oldBoundaryEdges,stats.newBoundaryEdges,
            stats.oldBoundaryError,stats.newBoundaryError,
            stats.seamTriangles,stats.maximumSeamDistance,
            stats.maximumSeamResidual,stats.oldBoundaryMaximumResidual,
            stats.newBoundaryMaximumResidual,stats.seamOpposedTriangles,
            stats.oldSeamOpposedTriangles,stats.newSeamOpposedTriangles,
            stats.seamDistanceEvaluations);
        if(stats.seamOpposedTriangles)
            printf("patch firstOpposed orientation=%.9f area=%.9f center=(%.4f,%.4f,%.4f)\n",
                stats.firstOpposedOrientation,stats.firstOpposedArea,
                stats.firstOpposedCenter.x,stats.firstOpposedCenter.y,
                stats.firstOpposedCenter.z);
        for(size_t i=0;i<stats.oldBoundaryLoops&&i<8;++i)
            printf("patch oldLoop %zu center=(%.3f,%.3f,%.3f) normalDot=%.3f\n",i,
                stats.oldLoopCenters[i].x,stats.oldLoopCenters[i].y,stats.oldLoopCenters[i].z,
                stats.loopNormalDot[i]);
        for(size_t i=0;i<stats.newBoundaryLoops&&i<8;++i)
            printf("patch newLoop %zu center=(%.3f,%.3f,%.3f)\n",i,
                stats.newLoopCenters[i].x,stats.newLoopCenters[i].y,stats.newLoopCenters[i].z);
        if(stitched) {
            size_t thin=0,opposed=0;AuditTriangles(candidate,&thin,&opposed);
            size_t sdfOpposed=AuditSDFOrientation(candidate,&field);
            printf("patch result vertices=%zu triangles=%zu thin=%zu opposed=%zu sdfOpposedSample=%zu\n",
                candidate->vertexCount,candidate->indexCount/3,thin,opposed,sdfOpposed);
        }
    }
    if(stitched&&candidate==&cut) {
        Mesh_Free(mesh);*mesh=cut;
    } else Mesh_Free(&cut);
    free(remove);Mesh_Free(&patch);SDFMesher_Free(&local);
    return stitched;
}
int main(int argc,char** argv) {
    bool full=argc>1 && strcmp(argv[1],"--full")==0;
    bool regionAudit=argc>1 && strcmp(argv[1],"--region-audit")==0;
    bool branchCycle=argc>1 && strcmp(argv[1],"--branch-cycle")==0;
    bool branchRadialCycle=argc>1 && strcmp(argv[1],"--branch-radial-cycle")==0;
    bool branchGuardCycle=argc>1 && strcmp(argv[1],"--branch-guard-cycle")==0;
    bool lizardAnchor=argc>1 && strcmp(argv[1],"--lizard-anchor")==0;
    bool middleAnchor=argc>1 && strcmp(argv[1],"--middle-anchor")==0;
    bool flipLizard=argc>1 && strcmp(argv[1],"--flip-lizard")==0;
    bool patchLizard=argc>1 && strcmp(argv[1],"--patch-lizard")==0;
    bool patchNext=argc>1 && strcmp(argv[1],"--patch-next")==0;
    bool patchSequence=argc>1 && strcmp(argv[1],"--patch-sequence")==0;
    const char* sequenceLimit=getenv("MORPH_PATCH_SEQUENCE_STEPS");
    int lastSequenceStep=sequenceLimit?atoi(sequenceLimit):14;
    if(lastSequenceStep<0)lastSequenceStep=0;
    if(lastSequenceStep>14)lastSequenceStep=14;
    bool project=argc>1 && (strcmp(argv[1],"--project")==0||regionAudit);
    project=project||branchCycle||branchRadialCycle||branchGuardCycle||lizardAnchor||middleAnchor;
    bool smoothProbe=argc>1 && strcmp(argv[1],"--smooth-probe")==0;
    bool orientationProbe=argc>1 && strcmp(argv[1],"--orientation-probe")==0;
    bool singleInfluence=argc>1 && strcmp(argv[1],"--single-influence")==0;
    bool singleLizard=argc>1 && strcmp(argv[1],"--single-lizard")==0;
    bool branchLizard=argc>1 && strcmp(argv[1],"--branch-lizard")==0;
    bool radialLizard=argc>1 && strcmp(argv[1],"--radial-lizard")==0;
    bool branchRadialLizard=argc>1 && strcmp(argv[1],"--branch-radial-lizard")==0;
    radialLizard=radialLizard||branchRadialLizard;
    bool direct=(argc>1 && strcmp(argv[1],"--direct")==0)||smoothProbe;
    bool directLizard=argc>1 && strcmp(argv[1],"--direct-lizard")==0;
    directLizard=directLizard||singleLizard||branchLizard||radialLizard||flipLizard||patchLizard;
    direct=direct||singleInfluence||orientationProbe||directLizard||patchNext;
    bool asyncCycle=argc>1 && strcmp(argv[1],"--async-cycle")==0;
    bool workerDelays=argc>1 && strcmp(argv[1],"--worker-delays")==0;
    bool scrubStress=argc>1 && strcmp(argv[1],"--scrub-stress")==0;
    bool fullAges=argc>1 && strcmp(argv[1],"--full-ages")==0;
    int correctionThreads=argc>2?atoi(argv[2]):0;
    Monster dog=Monster_Create(),lizard=Monster_Create();
    CreaturePhenotype dogP=CreatureRecipes_Dog()->adult,lizardP=CreatureRecipes_Lizard()->adult;
    if(!Creature_BuildMonster(&dog,CreatureRecipes_Dog(),&dogP)||
       !Creature_BuildMonster(&lizard,CreatureRecipes_Lizard(),&lizardP))return 1;
    Monster_SetHeadOpenFactor(&dog,0);Monster_SetHeadOpenFactor(&lizard,.08f);
    if(patchLizard||patchNext||patchSequence) {
        CreatureMorphTopologyEvent events[ANATOMY_MAX_NODES];
        size_t count=CreatureMorph_CollectTopologyEvents(&dog,&lizard,
            events,ANATOMY_MAX_NODES);
        printf("topology events=%zu\n",count);
        for(size_t i=0;i<count&&i<ANATOMY_MAX_NODES;++i)
            printf("topology module=%u nodes=%zu/%zu connections=%zu/%zu\n",
                events[i].moduleInstanceId,events[i].nodesAtStart,
                events[i].nodesAtEnd,events[i].connectionsAtStart,
                events[i].connectionsAtEnd);
    }
    if(asyncCycle) {
        MonsterAger cycle=MonsterAger_Create(&dog,&lizard,0);
        MonsterAger_SetGeometrySteps(&cycle,20);
        MonsterVisualAsyncConfig liveCfg=MonsterVisualAsync_DefaultConfig();
        liveCfg.morphMesherConfig.voxelSize=.070f;
        MonsterVisualAsync* visual=MonsterVisualAsync_Create(liveCfg);
        if(!visual||!MonsterVisualAsync_SetMorphEndpoints(visual,&dog,&lizard))return 11;
        MonsterVisualAsync_SetMorphMode(visual,true);
        MonsterVisualAsync_SetContinuousMotion(visual,true);
        MonsterVisualAsync_UpdateWithAppearance(visual,MonsterAger_GetGeometryResultConst(&cycle),
            MonsterAger_GetResultConst(&cycle),0);
        MonsterVisualAsync_Flush(visual);
        MonsterVisualAsyncStats initial=MonsterVisualAsync_GetStats(visual);
        printf("initial mesh vertices=%zu build=%.3f projection=%.3f residual95=%.6f residualMax=%.6f fullFallbacks=%zu\n",
            visual->displayMesh.vertexCount,initial.lastBuildDurationMs,initial.dynamicSurface.projectionMs,
            initial.dynamicSurface.p95Residual,initial.dynamicSurface.maximumResidual,
            initial.dynamicFullFallbackCount);
        if(!visual->displayMesh.vertexCount)return 12;
        size_t indexCount=visual->displayMesh.indexCount;
        MeshIndex* referenceIndices=malloc(indexCount*sizeof(*referenceIndices));
        if(!referenceIndices)return 17;
        memcpy(referenceIndices,visual->displayMesh.indices,indexCount*sizeof(*referenceIndices));
        Vector3 previousPositions[2048];
        for(size_t v=0;v<2048;++v)
            previousPositions[v]=visual->displayMesh.vertices[v*visual->displayMesh.vertexCount/2048].position;
        uint64_t previousGeneration=visual->displayGeneration;
        float times[1100],maxLag=0;size_t frames=1000,fullFallbacks=0;
        PresentedSample presented[10]={0};size_t presentedCount=0;
        double startCycle=NowMs();
        for(size_t frame=0;frame<frames;++frame) {
            float age=frame<500?frame/500.0f:(1000-frame)/500.0f;
            MonsterAger_SetPerc(&cycle,age);
            double startFrame=NowMs();
            MonsterVisualAsync_UpdateWithAppearance(visual,
                MonsterAger_GetGeometryResultConst(&cycle),MonsterAger_GetResultConst(&cycle),1.0f/60.0f);
            MonsterVisualAsyncStats stats=MonsterVisualAsync_GetStats(visual);
            times[frame]=(float)(NowMs()-startFrame);
            if(isfinite(stats.presentedMorphAge))
                maxLag=fmaxf(maxLag,fabsf(stats.presentedMorphAge-age));
            fullFallbacks=stats.dynamicFullFallbackCount;
            const Mesh* current=MonsterVisualAsync_GetDisplayMesh(visual);
            float maximumJump=0;
            for(size_t v=0;v<2048;++v) {
                Vector3 currentPosition=current->vertices[v*current->vertexCount/2048].position;
                maximumJump=fmaxf(maximumJump,Vec3_Distance(currentPosition,previousPositions[v]));
                previousPositions[v]=currentPosition;
            }
            if(visual->displayGeneration!=previousGeneration) {
                printf("publication frame=%zu age=%.3f generation=%llu maxSampledJump=%.6f\n",
                    frame,age,(unsigned long long)visual->displayGeneration,maximumJump);
                previousGeneration=visual->displayGeneration;
            }
            if(frame%120==0) {
                PresentedSample* sample=&presented[presentedCount++];
                sample->age=age;sample->count=current->vertexCount;
                sample->positions=malloc(sample->count*sizeof(*sample->positions));
                sample->normals=malloc(sample->count*sizeof(*sample->normals));
                if(!sample->positions||!sample->normals)return 14;
                for(size_t v=0;v<sample->count;++v) {
                    sample->positions[v]=current->vertices[v].position;
                    sample->normals[v]=current->vertices[v].normal;
                }
                printf("cycle frame=%zu age=%.3f presented=%.3f visualAge=%.3f certAge=%.3f predAge=%.3f frameMs=%.3f presMs=%.3f workerMs=%.3f evals=%zu p95=%.6f fallbacks=%zu clones=%zu validations=%zu bytesCopied=%zu\n",
                frame,age,stats.presentedMorphAge,stats.visualAge,stats.certifiedGeometryAge,stats.predictedGeometryAge,
                times[frame],stats.presentationCPUms,stats.workerCPUms,
                stats.sdfEvaluations,stats.dynamicSurface.p95Residual,
                stats.topologyFallbackCount,stats.fullMeshClones,stats.fullMeshValidations,stats.geometryBytesCopied);
            }
            double deadline=startCycle+(frame+1)*(1000.0/60.0);
            double now=NowMs();if(deadline>now)usleep((useconds_t)((deadline-now)*1000));
        }
        qsort(times,frames,sizeof(float),Compare);
        MonsterVisualAsyncStats endStats=MonsterVisualAsync_GetStats(visual);
        printf("cycle frames=%zu p50=%.3f p95=%.3f max=%.3f presentedMaxLag=%.6f fullFallbacks=%zu totalClones=%zu totalValidations=%zu totalBytesCopied=%zu totalFallbackSeconds=%.3f\n",
            frames,times[frames/2],times[(size_t)(frames*.95f)],times[frames-1],maxLag,fullFallbacks,
            endStats.fullMeshClones,endStats.fullMeshValidations,endStats.geometryBytesCopied,endStats.topologyFallbackDuration);
        MonsterVisualAsync_Free(visual);
        MonsterSDF check=MonsterSDF_Create();
        for(size_t i=0;i<presentedCount;++i) {
            PresentedSample* sample=&presented[i];
            MonsterAger_SetPerc(&cycle,sample->age);
            if(!MonsterSDF_Build(&check,MonsterAger_GetResultConst(&cycle),
                MonsterSDF_DefaultConfig()))return 15;
            float* errors=malloc(sample->count*sizeof(*errors));
            if(!errors)return 16;
            double sum=0;float maximum=0;
            for(size_t v=0;v<sample->count;++v) {
                float error=fabsf(MonsterSDF_EvaluateDistance(&check,sample->positions[v]));
                errors[v]=error;sum+=error;maximum=fmaxf(maximum,error);
            }
            qsort(errors,sample->count,sizeof(*errors),Compare);
            size_t thin=0,inverted=0;
            for(size_t triangle=0;triangle+2<indexCount;triangle+=3) {
                MeshIndex ia=referenceIndices[triangle],ib=referenceIndices[triangle+1],
                    ic=referenceIndices[triangle+2];
                Vector3 a=sample->positions[ia],b=sample->positions[ib],c=sample->positions[ic];
                Vector3 ab=Vec3_Sub(b,a),bc=Vec3_Sub(c,b),ca=Vec3_Sub(a,c);
                Vector3 area=Vec3_Cross(ab,Vec3_Sub(c,a));
                float longest=fmaxf(Vec3_Dot(ab,ab),fmaxf(Vec3_Dot(bc,bc),Vec3_Dot(ca,ca)));
                if(longest>1e-12f && Vec3_Length(area)/longest<.02f)thin++;
                Vector3 normalSum=Vec3_Add(sample->normals[ia],
                    Vec3_Add(sample->normals[ib],sample->normals[ic]));
                if(Vec3_Dot(area,normalSum)<-1e-8f)inverted++;
            }
            printf("presented age=%.3f vertices=%zu mean=%.6f p95=%.6f p99=%.6f max=%.6f thin=%zu inverted=%zu\n",
                sample->age,sample->count,sum/sample->count,
                errors[(size_t)(sample->count*.95f)],errors[(size_t)(sample->count*.99f)],maximum,
                thin,inverted);
            free(errors);free(sample->positions);free(sample->normals);
        }
        free(referenceIndices);
        MonsterSDF_Free(&check);MonsterAger_Free(&cycle);
        Monster_Free(&dog);Monster_Free(&lizard);
        return 0;
    }
    if(workerDelays) {
        printf("=================================================================\n");
        printf(" BENCHMARK: Resistencia a Latencia Extrema del Worker (0-3000 ms)\n");
        printf("=================================================================\n");
        const unsigned testDelays[] = {0, 20, 50, 100, 250, 500, 1000, 3000};
        for(size_t di = 0; di < sizeof(testDelays)/sizeof(testDelays[0]); ++di) {
            unsigned delay = testDelays[di];
            MonsterAger cycle = MonsterAger_Create(&dog, &lizard, 0.0f);
            MonsterAger_SetGeometrySteps(&cycle, 20);
            MonsterVisualAsyncConfig cfg = MonsterVisualAsync_DefaultConfig();
            cfg.morphMesherConfig.voxelSize = .070f;
            cfg.workerDelayMs = delay;
            MonsterVisualAsync* visual = MonsterVisualAsync_Create(cfg);
            if(!visual || !MonsterVisualAsync_SetMorphEndpoints(visual, &dog, &lizard)) return 31;
            MonsterVisualAsync_SetMorphMode(visual, true);
            MonsterVisualAsync_SetContinuousMotion(visual, true);
            MonsterVisualAsync_UpdateWithAppearance(visual, MonsterAger_GetGeometryResultConst(&cycle),
                MonsterAger_GetResultConst(&cycle), 0.016f);
            MonsterVisualAsync_Flush(visual);

            double start = NowMs();
            size_t testFrames = 60;
            for(size_t f = 0; f < testFrames; ++f) {
                float age = (float)f / (float)testFrames;
                MonsterAger_SetPerc(&cycle, age);
                MonsterVisualAsync_UpdateWithAppearance(visual,
                    MonsterAger_GetGeometryResultConst(&cycle),
                    MonsterAger_GetResultConst(&cycle), 0.016f);
            }
            MonsterVisualAsync_Flush(visual);
            double totalMs = NowMs() - start;
            MonsterVisualAsyncStats stats = MonsterVisualAsync_GetStats(visual);
            const Mesh* mesh = MonsterVisualAsync_GetDisplayMesh(visual);
            size_t thin = 0, opposed = 0;
            if(mesh && mesh->vertexCount) AuditTriangles(mesh, &thin, &opposed);
            printf("workerDelay=%4u ms | completed=%3llu coalesced=%3llu staleDiscarded=%3llu | clones=%3zu validations=%3zu bytesCopied=%9zu | fallbacks=%2zu duration=%.3f s | thin=%zu opposed=%zu | totalMs=%.1f\n",
                delay,
                (unsigned long long)stats.completedBuildCount,
                (unsigned long long)stats.coalescedCount,
                (unsigned long long)stats.staleResultsDiscarded,
                stats.fullMeshClones,
                stats.fullMeshValidations,
                stats.geometryBytesCopied,
                stats.topologyFallbackCount,
                stats.topologyFallbackDuration,
                thin, opposed, totalMs);
            MonsterVisualAsync_Free(visual);
            MonsterAger_Free(&cycle);
        }
        Monster_Free(&dog); Monster_Free(&lizard);
        return 0;
    }
    if(scrubStress) {
        printf("=================================================================\n");
        printf(" BENCHMARK: Estrés por Scrubbing Rápido y Fuera de Orden         \n");
        printf("=================================================================\n");
        MonsterAger cycle = MonsterAger_Create(&dog, &lizard, 0.0f);
        MonsterAger_SetGeometrySteps(&cycle, 20);
        MonsterVisualAsyncConfig cfg = MonsterVisualAsync_DefaultConfig();
        cfg.morphMesherConfig.voxelSize = .070f;
        cfg.workerDelayMs = 25;
        MonsterVisualAsync* visual = MonsterVisualAsync_Create(cfg);
        if(!visual || !MonsterVisualAsync_SetMorphEndpoints(visual, &dog, &lizard)) return 32;
        MonsterVisualAsync_SetMorphMode(visual, true);
        MonsterVisualAsync_SetContinuousMotion(visual, true);
        MonsterVisualAsync_UpdateWithAppearance(visual, MonsterAger_GetGeometryResultConst(&cycle),
            MonsterAger_GetResultConst(&cycle), 0.016f);
        MonsterVisualAsync_Flush(visual);

        const float scrubAges[] = {0.0f, 0.70f, 0.20f, 0.95f, 0.10f, 0.85f, 0.35f, 0.65f, 0.05f, 1.0f, 0.50f, 0.0f};
        for(size_t s = 0; s < sizeof(scrubAges)/sizeof(scrubAges[0]); ++s) {
            float target = scrubAges[s];
            MonsterAger_SetPerc(&cycle, target);
            double t0 = NowMs();
            MonsterVisualAsync_UpdateWithAppearance(visual,
                MonsterAger_GetGeometryResultConst(&cycle),
                MonsterAger_GetResultConst(&cycle), 0.016f);
            double stepMs = NowMs() - t0;
            MonsterVisualAsyncStats stats = MonsterVisualAsync_GetStats(visual);
            printf("scrub step=%2zu targetAge=%.2f visualAge=%.2f certAge=%.2f predAge=%.2f updateMs=%.3f presMs=%.3f workerBusy=%d coalesced=%llu stale=%llu clones=%zu\n",
                s, target, stats.visualAge, stats.certifiedGeometryAge, stats.predictedGeometryAge,
                stepMs, stats.presentationCPUms, stats.isWorkerBusy ? 1 : 0,
                (unsigned long long)stats.coalescedCount,
                (unsigned long long)stats.staleResultsDiscarded,
                stats.fullMeshClones);
            usleep(15000);
        }
        MonsterVisualAsync_Flush(visual);
        MonsterVisualAsyncStats finalStats = MonsterVisualAsync_GetStats(visual);
        const Mesh* finalMesh = MonsterVisualAsync_GetDisplayMesh(visual);
        size_t thin = 0, opposed = 0;
        if(finalMesh && finalMesh->vertexCount) AuditTriangles(finalMesh, &thin, &opposed);
        printf("scrub complete: builds=%llu coalesced=%llu staleDiscarded=%llu fullClones=%zu fullValidations=%zu bytesCopied=%zu thin=%zu opposed=%zu\n",
            (unsigned long long)finalStats.completedBuildCount,
            (unsigned long long)finalStats.coalescedCount,
            (unsigned long long)finalStats.staleResultsDiscarded,
            finalStats.fullMeshClones,
            finalStats.fullMeshValidations,
            finalStats.geometryBytesCopied,
            thin, opposed);
        MonsterVisualAsync_Free(visual);
        MonsterAger_Free(&cycle);
        Monster_Free(&dog); Monster_Free(&lizard);
        return 0;
    }
    MonsterAger ager=MonsterAger_Create(&dog,&lizard,.25f);
    if(middleAnchor)MonsterAger_SetPerc(&ager,.5f);
    MonsterAger_SetGeometrySteps(&ager,20);
    MonsterVisualAsyncConfig cfg=MonsterVisualAsync_DefaultConfig();
    cfg.morphMesherConfig.voxelSize=.070f;
    SDFMesherConfig meshCfg=MonsterVisualAsync_ResolveBodyConfig(&cfg,MONSTER_VISUAL_QUALITY_MORPH,true);
    SDFMesher mesher=SDFMesher_Create(meshCfg);
    SDFSamplingPool* pool=SDFSamplingPool_Create(correctionThreads);
    SDFMesher_SetSamplingPool(&mesher,pool);
    MonsterSDF sdf=MonsterSDF_Create();Mesh mesh=Mesh_Create();
    double start=NowMs();
    if(!BuildExact(MonsterAger_GetResultConst(&ager),&sdf,&mesher,&mesh))return 2;
    SDFMesherStats bs=*SDFMesher_GetLastStats(&mesher);
    printf("exact age=.25 total=%.3f classify=%.3f contour=%.3f attributes=%.3f cells=%zu points=%zu distanceEvals=%zu fullSamples=%zu vertices=%zu triangles=%zu ranges=%zu regionTests=%zu candidates=%zu exactConnectors=%zu\n",
        NowMs()-start,bs.adaptiveClassificationMs,bs.adaptiveContourMs,bs.adaptiveAttributesMs,
        bs.cellCount,bs.gridPointCount,bs.distanceEvaluationCount,bs.fullSampleEvaluationCount,
        mesh.vertexCount,mesh.indexCount/3,bs.cellRangeQueryCount,
        bs.detailRegionTestCount,bs.connectorCandidateCount,bs.connectorExactEvaluationCount);
    size_t anchorThin=0,anchorOpposed=0;
    AuditTriangles(&mesh,&anchorThin,&anchorOpposed);
    printf("exact age=.25 thin=%zu opposed=%zu\n",anchorThin,anchorOpposed);
    SDFField anchorAuditField=MonsterSDF_GetField(&sdf);
    printf("exact age=.25 sdfOpposedSample=%zu\n",
        AuditSDFOrientation(&mesh,&anchorAuditField));
    if(lizardAnchor) {
        MonsterAger_SetPerc(&ager,1.0f);
        double lizardStart=NowMs();
        if(!BuildExact(MonsterAger_GetResultConst(&ager),&sdf,&mesher,&mesh))return 21;
        printf("lizard anchor build=%.3f vertices=%zu triangles=%zu\n",
            NowMs()-lizardStart,mesh.vertexCount,mesh.indexCount/3);
        AuditTriangles(&mesh,&anchorThin,&anchorOpposed);
        printf("lizard anchor thin=%zu opposed=%zu\n",anchorThin,anchorOpposed);
    }
    if(fullAges) {
        const float ages[]={0,.05f,.10f,.15f,.25f,.50f,.75f,1};
        for(size_t i=0;i<sizeof(ages)/sizeof(ages[0]);++i) {
            MonsterAger_SetPerc(&ager,ages[i]);
            double t=NowMs();
            if(!BuildExact(MonsterAger_GetResultConst(&ager),&sdf,&mesher,&mesh))return 13;
            const SDFMesherStats* s=SDFMesher_GetLastStats(&mesher);
            printf("full age=%.2f total=%.3f classify=%.3f contour=%.3f attributes=%.3f cells=%zu points=%zu distanceEvals=%zu fullSamples=%zu vertices=%zu triangles=%zu\n",
                ages[i],NowMs()-t,s->adaptiveClassificationMs,s->adaptiveContourMs,
                s->adaptiveAttributesMs,s->cellCount,s->gridPointCount,
                s->distanceEvaluationCount,s->fullSampleEvaluationCount,
                mesh.vertexCount,mesh.indexCount/3);
        }
        Mesh_Free(&mesh);MonsterSDF_Free(&sdf);SDFMesher_Free(&mesher);
        SDFSamplingPool_Free(pool);Monster_Free(&dog);Monster_Free(&lizard);
        MonsterAger_Free(&ager);return 0;
    }
    AnatomyDeformer* morph=AnatomyDeformer_Create();HeadMorph* head=HeadMorph_Create();
    const Monster* anchor=MonsterAger_GetResultConst(&ager);
    if(regionAudit||radialLizard||branchRadialCycle||lizardAnchor||patchLizard||patchNext||patchSequence) {
        SurfaceMapperStats mapped={0};
        SurfaceMapper_MapMeshWithStats(&mesh,&anchor->anatomyGraph,
            &anchor->surfaceMapping,&mapped);
        printf("region anchorMappingMs=%.3f candidateTests=%zu\n",
            mapped.durationMs,mapped.candidateTests);
    }
    if(!AnatomyDeformer_Bind(morph,&mesh,&anchor->anatomyGraph))return 3;
    if(singleInfluence||singleLizard)for(size_t i=0;i<morph->vertexCount;++i) {
        morph->bindings[i].weight[0]=1.0f;
        for(int k=1;k<ANATOMY_MORPH_INFLUENCES;++k)
            morph->bindings[i].weight[k]=0.0f;
    }
    if(branchLizard||branchCycle||branchRadialLizard||branchRadialCycle||branchGuardCycle)
        printf("branch filteredInfluences=%zu\n",FilterBranchBindings(morph));
    if(anchor->hasHead && anchor->head.anatomy.attachmentBodyPartIndex<anchor->bodyPartCount)
        HeadMorph_Bind(head,&mesh,&anchor->head.anatomy,
            anchor->bodyParts[anchor->head.anatomy.attachmentBodyPartIndex].positionRender);
    if(project||direct||patchSequence) {
        MorphSurfaceTracker tracker={0};
        if(!MorphSurfaceTracker_Bind(&tracker,&mesh,lizardAnchor?1.0f:middleAnchor?.5f:.25f))return 7;
        MorphSurfaceTracker_SetPool(&tracker,pool);
        for(int step=0;step<=(patchSequence?lastSequenceStep:direct?0:lizardAnchor?30:middleAnchor?50:45);++step) {
            float age=patchSequence?.30f+step*.05f:patchNext?.30f:
                lizardAnchor?(step<=15?1.0f-step*.05f:.25f+(step-15)*.05f):
                middleAnchor?(step<=10?.5f-step*.05f:step<=30?(step-10)*.05f:1.0f-(step-30)*.05f):
                direct?(directLizard?1.0f:0.0f):step<=5?.25f-.05f*step:
                step<=25?.05f*(step-5):1.0f-.05f*(step-25);
            MonsterAger_SetPerc(&ager,age);
            const Monster* target=MonsterAger_GetResultConst(&ager);
            if(!MorphSurfaceTracker_Predict(&tracker,morph,head,target,&mesh))return 8;
            Vector3* predicted=(direct||branchGuardCycle)?malloc(mesh.vertexCount*sizeof(Vector3)):NULL;
            if((direct||branchGuardCycle) && !predicted)return 19;
            if(predicted)for(size_t v=0;v<mesh.vertexCount;++v)
                predicted[v]=mesh.vertices[v].position;
            size_t predictedThin=0,predictedOpposed=0;
            AuditTriangles(&mesh,&predictedThin,&predictedOpposed);
            printf("predicted age=%.2f thin=%zu opposed=%zu\n",age,predictedThin,predictedOpposed);
            if(regionAudit && step%5==0)
                AuditRegions(&mesh,morph,target,NULL,"predicted",age);
            if(!MonsterSDF_Build(&sdf,target,MonsterSDF_DefaultConfig()))return 9;
            SDFField field=MonsterSDF_GetField(&sdf);
            if(radialLizard||(branchRadialCycle && age>=.5f))
                ProbeRadialFootProjection(&mesh,morph,target,&field);
            if(!MorphSurfaceTracker_Correct(&tracker,&field,&mesh,age))return 10;
            if(branchGuardCycle)ProbeFoldGuard(&mesh,&tracker,predicted);
            if(flipLizard)ProbeEdgeFlips(&mesh);
            MorphSurfaceTrackerStats stepStats=tracker.stats;
            if(patchLizard||patchNext)ProbeLocalPatch(&mesh,morph,target,&sdf,&meshCfg,
                UINT32_MAX,NULL);
            if(patchSequence && (step%2==0||getenv("MORPH_PATCH_EVERY_STEP"))) {
                CreatureMorphTopologyEvent events[ANATOMY_MAX_NODES];
                size_t attempts=step==0&&!getenv("MORPH_PATCH_MODULE")?
                    CreatureMorph_CollectTopologyEvents(&dog,&lizard,
                        events,ANATOMY_MAX_NODES):1;
                if(attempts>ANATOMY_MAX_NODES)attempts=ANATOMY_MAX_NODES;
                for(size_t attempt=0;attempt<attempts;++attempt) {
                    uint32_t module=step==0&&!getenv("MORPH_PATCH_MODULE")?
                        events[attempt].moduleInstanceId:UINT32_MAX;
                    MeshPatchTransfer transfer={0};
                    if(!ProbeLocalPatch(&mesh,morph,target,&sdf,&meshCfg,module,
                        &transfer))continue;
                    double bindingStart=NowMs();
                    size_t newlyBound=0;
                    if(!AnatomyDeformer_Reanchor(morph,&mesh,&target->anatomyGraph,
                        transfer.oldVertexOfNew,&newlyBound))return 25;
                    double bindMs=NowMs()-bindingStart;
                    MeshPatchTransfer_Free(&transfer);
                    bindingStart=NowMs();
                    if(target->hasHead &&
                       target->head.anatomy.attachmentBodyPartIndex<target->bodyPartCount &&
                       !HeadMorph_Bind(head,&mesh,&target->head.anatomy,
                           target->bodyParts[target->head.anatomy.attachmentBodyPartIndex].positionRender))return 26;
                    double headBindMs=NowMs()-bindingStart;
                    if(!MorphSurfaceTracker_Bind(&tracker,&mesh,age))return 27;
                    MorphSurfaceTracker_SetPool(&tracker,pool);
                    printf("patch reanchor age=%.2f module=%u vertices=%zu newBindings=%zu bindMs=%.3f headBindMs=%.3f\n",
                        age,module,mesh.vertexCount,newlyBound,bindMs,headBindMs);
                }
            }
            size_t correctedThin=0,correctedOpposed=0;
            AuditTriangles(&mesh,&correctedThin,&correctedOpposed);
            MorphSurfaceTrackerStats s=stepStats;
            Vector3 worst=mesh.vertices[s.maximumResidualVertex].position;
            printf("project age=%.2f deform=%.3f project=%.3f normalMs=%.3f qualityMs=%.3f bricks=%zu qualityBricks=%zu thin=%zu opposed=%zu brickMs=%.3f candidatesMs=%.3f evals=%zu connectorCandidates=%zu gradients=%zu moved=%zu normals=%zu rejected=%zu mean=%.6f p95=%.6f max=%.6f worst=(%.3f,%.3f,%.3f) valid=%d watertight=%d\n",
                age,s.deformationMs,s.projectionMs,s.normalCorrectionMs,
                s.qualityScanMs,s.processedBricks,s.qualityBricks,s.thinTriangles,
                s.opposedTriangles,s.brickUpdateMs,
                s.candidateCompileMs,s.distanceEvaluations,s.connectorCandidates,s.gradientEvaluations,
                s.projectedVertices,s.correctedNormals,s.rejectedProjections,s.meanResidual,s.p95Residual,
                s.maximumResidual,worst.x,worst.y,worst.z,s.valid,s.watertight);
            printf("corrected age=%.2f thin=%zu opposed=%zu\n",age,correctedThin,correctedOpposed);
            if(regionAudit && step%5==0)
                AuditRegions(&mesh,morph,target,&tracker,"corrected",age);
            if(direct||patchSequence)printf("corrected age=%.2f sdfOpposedSample=%zu\n",age,
                AuditSDFOrientation(&mesh,&field));
            if(smoothProbe)ProbeSmooth(&tracker,&mesh,&field,predicted);
            if(orientationProbe)ProbeOrientation(&mesh,&field,predicted);
            free(predicted);
        }
        MorphSurfaceTracker_Free(&tracker);
        HeadMorph_Free(head);AnatomyDeformer_Free(morph);
        Mesh_Free(&mesh);MonsterSDF_Free(&sdf);SDFMesher_Free(&mesher);SDFSamplingPool_Free(pool);
        Monster_Free(&dog);Monster_Free(&lizard);MonsterAger_Free(&ager);
        return 0;
    }
    float* residual=malloc(mesh.vertexCount*sizeof(float));if(!residual)return 4;
    const float ages[]={0,.05f,.10f,.15f,.25f,.50f,.75f,1,.75f,.50f,.25f,.15f,.10f,.05f,0};
    for(size_t ai=0;ai<sizeof(ages)/sizeof(ages[0]);++ai) {
        MonsterAger_SetPerc(&ager,ages[ai]);const Monster* target=MonsterAger_GetResultConst(&ager);
        double d0=NowMs();
        if(!AnatomyDeformer_Deform(morph,&target->anatomyGraph,&mesh))return 5;
        if(target->hasHead && target->head.anatomy.attachmentBodyPartIndex<target->bodyPartCount)
            HeadMorph_Deform(head,&mesh,&target->head.anatomy,
                target->bodyParts[target->head.anatomy.attachmentBodyPartIndex].positionRender);
        double deform=NowMs()-d0;
        MonsterSDF_Build(&sdf,target,MonsterSDF_DefaultConfig());
        double eval0=NowMs();double mean=0;float max=0;
        for(size_t i=0;i<mesh.vertexCount;++i) {
            float e=fabsf(MonsterSDF_EvaluateDistance(&sdf,mesh.vertices[i].position));
            residual[i]=e;mean+=e;if(e>max)max=e;
        }
        double evaluate=NowMs()-eval0;
        qsort(residual,mesh.vertexCount,sizeof(float),Compare);
        MeshValidationResult v=Mesh_Validate(&mesh);
        printf("deform age=%.2f ms=%.3f sdfMs=%.3f evals=%zu mean=%.6f p95=%.6f p99=%.6f max=%.6f valid=%d boundary=%zu zeroArea=%zu\n",
            ages[ai],deform,evaluate,mesh.vertexCount,mean/mesh.vertexCount,
            residual[(size_t)(mesh.vertexCount*.95f)],residual[(size_t)(mesh.vertexCount*.99f)],max,
            v.valid,v.boundaryEdgeCount,v.zeroAreaTriangleCount);
        if(full) {
            double t=NowMs();
            if(!BuildExact(target,&sdf,&mesher,&mesh))return 6;
            const SDFMesherStats* s=SDFMesher_GetLastStats(&mesher);
            printf("full age=%.2f total=%.3f classify=%.3f contour=%.3f attributes=%.3f cells=%zu points=%zu vertices=%zu\n",
                ages[ai],NowMs()-t,s->adaptiveClassificationMs,s->adaptiveContourMs,
                s->adaptiveAttributesMs,s->cellCount,s->gridPointCount,mesh.vertexCount);
            break; /* La referencia cambia la conectividad y anula este vínculo. */
        }
    }
    free(residual);HeadMorph_Free(head);AnatomyDeformer_Free(morph);
    Mesh_Free(&mesh);MonsterSDF_Free(&sdf);SDFMesher_Free(&mesher);SDFSamplingPool_Free(pool);
    Monster_Free(&dog);Monster_Free(&lizard);MonsterAger_Free(&ager);
    return 0;
}
