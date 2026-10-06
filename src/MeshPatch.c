/** @file MeshPatch.c
 * @brief Extracción de lazos y costura determinista de superficies locales.
 */
#include "MeshPatch.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct PatchEdge {
    MeshIndex lo,hi,from,to;
} PatchEdge;
typedef struct PatchLoopSet {
    MeshIndex* vertices;
    size_t* offsets;
    size_t count,vertexCount;
    size_t boundaryEdges;
    int error;
} PatchLoopSet;
typedef struct PatchFaceEdge {
    MeshIndex lo,hi;
    size_t triangle;
} PatchFaceEdge;

typedef struct PatchCutEdge {
    MeshIndex lo,hi,intersection;
} PatchCutEdge;

static int Patch_CompareCutEdge(const void* left,const void* right) {
    const PatchCutEdge* a=left;const PatchCutEdge* b=right;
    if(a->lo!=b->lo)return (a->lo>b->lo)-(a->lo<b->lo);
    return (a->hi>b->hi)-(a->hi<b->hi);
}

static float Patch_Axis(Vector3 p,int axis) {
    return axis==0?p.x:(axis==1?p.y:p.z);
}

static Vector3 Patch_InterpolateVector(Vector3 a,Vector3 b,float t) {
    return Vec3_Add(Vec3_Scale(a,1-t),Vec3_Scale(b,t));
}

static MeshVertex Patch_InterpolateVertex(MeshVertex a,MeshVertex b,float t) {
    MeshVertex out=a;
    out.position=Patch_InterpolateVector(a.position,b.position,t);
    out.normal=Vec3_Normalize(Patch_InterpolateVector(a.normal,b.normal,t));
    out.color.r=(uint8_t)lrintf(a.color.r+(b.color.r-a.color.r)*t);
    out.color.g=(uint8_t)lrintf(a.color.g+(b.color.g-a.color.g)*t);
    out.color.b=(uint8_t)lrintf(a.color.b+(b.color.b-a.color.b)*t);
    out.color.a=(uint8_t)lrintf(a.color.a+(b.color.a-a.color.a)*t);
    out.material=t<.5f?a.material:b.material;
    out.surface.position=Patch_InterpolateVector(a.surface.position,b.surface.position,t);
    out.surface.normal=Vec3_Normalize(Patch_InterpolateVector(a.surface.normal,b.surface.normal,t));
    out.surface.flowDirection=Vec3_Normalize(Patch_InterpolateVector(
        a.surface.flowDirection,b.surface.flowDirection,t));
    out.surface.region=a.surface.region+(b.surface.region-a.surface.region)*t;
    out.surface.secondaryRegion=a.surface.secondaryRegion+
        (b.surface.secondaryRegion-a.surface.secondaryRegion)*t;
    out.surface.blend=a.surface.blend+(b.surface.blend-a.surface.blend)*t;
    out.surface.ventral=a.surface.ventral+(b.surface.ventral-a.surface.ventral)*t;
    out.surface.integumentMask=a.surface.integumentMask+
        (b.surface.integumentMask-a.surface.integumentMask)*t;
    return out;
}

static MeshIndex Patch_FindCutVertex(const PatchCutEdge* edges,size_t count,
    MeshIndex a,MeshIndex b) {
    PatchCutEdge key={a<b?a:b,a<b?b:a,0};
    const PatchCutEdge* found=bsearch(&key,edges,count,sizeof(*edges),Patch_CompareCutEdge);
    return found?found->intersection:UINT32_MAX;
}

static bool Patch_AddPolygon(Mesh* mesh,const MeshIndex* polygon,size_t count) {
    if(count<3)return true;
    for(size_t i=1;i+1<count;++i) {
        MeshIndex a=polygon[0],b=polygon[i],c=polygon[i+1];
        if(a==b||b==c||a==c)continue;
        if(!Mesh_TriangleHasArea(mesh->vertices[a].position,
            mesh->vertices[b].position,mesh->vertices[c].position))return false;
        if(!Mesh_AddTriangle(mesh,a,b,c))return false;
    }
    return true;
}

static bool Patch_CutPlane(const Mesh* source,int axis,float plane,Mesh* output) {
    const float epsilon=1e-7f;
    size_t triangleCount=source->indexCount/3,edgeCount=0;
    PatchCutEdge* edges=malloc(source->indexCount*sizeof(*edges));
    float* distance=malloc(source->vertexCount*sizeof(*distance));
    if(!edges||!distance){free(edges);free(distance);return false;}
    for(size_t i=0;i<source->vertexCount;++i) {
        distance[i]=Patch_Axis(source->vertices[i].position,axis)-plane;
        if(fabsf(distance[i])<=epsilon)distance[i]=0;
    }
    for(size_t t=0;t<triangleCount;++t)for(int e=0;e<3;++e) {
        MeshIndex a=source->indices[t*3+e],b=source->indices[t*3+(e+1)%3];
        if(a>=source->vertexCount||b>=source->vertexCount) {
            free(edges);free(distance);return false;
        }
        if(distance[a]*distance[b]<0)
            edges[edgeCount++]=(PatchCutEdge){a<b?a:b,a<b?b:a,0};
    }
    qsort(edges,edgeCount,sizeof(*edges),Patch_CompareCutEdge);
    size_t unique=0;
    for(size_t i=0;i<edgeCount;++i)
        if(!unique||Patch_CompareCutEdge(&edges[i],&edges[unique-1]))
            edges[unique++]=edges[i];
    *output=Mesh_Create();
    output->surfaceRecipe=source->surfaceRecipe;
    output->hasSurface=source->hasSurface;
    bool ok=source->vertexCount<=UINT32_MAX-unique&&
        source->indexCount<=SIZE_MAX/3&&
        Mesh_ReserveVertices(output,source->vertexCount+unique)&&
        Mesh_ReserveIndices(output,source->indexCount*3);
    if(ok) {
        memcpy(output->vertices,source->vertices,
            source->vertexCount*sizeof(*source->vertices));
        output->vertexCount=source->vertexCount;
    }
    for(size_t i=0;ok&&i<unique;++i) {
        MeshIndex a=edges[i].lo,b=edges[i].hi;
        float t=distance[a]/(distance[a]-distance[b]);
        MeshVertex vertex=Patch_InterpolateVertex(source->vertices[a],
            source->vertices[b],t);
        if(axis==0)vertex.position.x=plane;
        else if(axis==1)vertex.position.y=plane;
        else vertex.position.z=plane;
        ok=Mesh_AddVertex(output,vertex,&edges[i].intersection);
    }
    for(size_t t=0;ok&&t<triangleCount;++t) {
        MeshIndex face[3]={source->indices[t*3],source->indices[t*3+1],
            source->indices[t*3+2]};
        bool positive=false,negative=false;
        for(int j=0;j<3;++j) {
            positive|=distance[face[j]]>0;
            negative|=distance[face[j]]<0;
        }
        if(!positive||!negative) {
            ok=Mesh_AddTriangle(output,face[0],face[1],face[2]);
            continue;
        }
        MeshIndex lower[5],upper[5];size_t lowCount=0,upCount=0;
        for(int j=0;j<3;++j) {
            MeshIndex a=face[j],b=face[(j+1)%3];
            float da=distance[a],db=distance[b];
            if(da<=0)lower[lowCount++]=a;
            if(da>=0)upper[upCount++]=a;
            MeshIndex crossing=UINT32_MAX;
            if(da*db<0)crossing=Patch_FindCutVertex(edges,unique,a,b);
            if(crossing!=UINT32_MAX) {
                lower[lowCount++]=crossing;
                upper[upCount++]=crossing;
            } else if(da*db<0) {ok=false;break;}
        }
        if(ok)ok=Patch_AddPolygon(output,lower,lowCount)&&
            Patch_AddPolygon(output,upper,upCount);
    }
    free(edges);free(distance);
    if(!ok)Mesh_Free(output);
    return ok;
}

static bool Patch_CutBoxInternal(const Mesh* source,AABB3D box,Mesh* cut,
    uint8_t** removeTriangle,size_t* removedCount,bool requireWatertight) {
    if(!source||!cut||source==cut||!removeTriangle||!source->vertices||!source->indices||
       source->indexCount%3||!isfinite(box.start.x)||!isfinite(box.start.y)||
       !isfinite(box.start.z)||!isfinite(box.end.x)||!isfinite(box.end.y)||
       !isfinite(box.end.z)||box.start.x>=box.end.x||box.start.y>=box.end.y||
       box.start.z>=box.end.z)return false;
    *removeTriangle=NULL;if(removedCount)*removedCount=0;
    Mesh current=Mesh_Create();
    bool ok=Mesh_ReserveVertices(&current,source->vertexCount)&&
        Mesh_ReserveIndices(&current,source->indexCount);
    if(ok) {
        memcpy(current.vertices,source->vertices,source->vertexCount*sizeof(*source->vertices));
        memcpy(current.indices,source->indices,source->indexCount*sizeof(*source->indices));
        current.vertexCount=source->vertexCount;current.indexCount=source->indexCount;
        current.surfaceRecipe=source->surfaceRecipe;current.hasSurface=source->hasSurface;
    }
    const float planes[6]={box.start.x,box.end.x,box.start.y,
        box.end.y,box.start.z,box.end.z};
    for(int p=0;ok&&p<6;++p) {
        Mesh next=Mesh_Create();
        ok=Patch_CutPlane(&current,p/2,planes[p],&next);
        Mesh_Free(&current);
        current=next;
    }
    if(ok) {
        size_t count=current.indexCount/3,selected=0;
        uint8_t* mask=calloc(count,1);
        ok=mask!=NULL;
        for(size_t t=0;ok&&t<count;++t) {
            Vector3 center=Vec3_Zero();
            for(int j=0;j<3;++j)center=Vec3_Add(center,
                current.vertices[current.indices[t*3+j]].position);
            center=Vec3_Scale(center,1.0f/3);
            mask[t]=AABB_ContainsPoint(box,center);
            selected+=mask[t]!=0;
        }
        if(!selected)ok=false;
        if(ok&&requireWatertight) {
            MeshValidationResult validation=Mesh_Validate(&current);
            ok=validation.valid&&validation.watertight;
        }
        if(ok) {
            Mesh previous=*cut;*cut=current;Mesh_Free(&previous);
            *removeTriangle=mask;
            if(removedCount)*removedCount=selected;
        } else free(mask);
    }
    if(!ok)Mesh_Free(&current);
    return ok;
}

bool MeshPatch_CutBox(const Mesh* source,AABB3D box,Mesh* cut,
    uint8_t** removeTriangle,size_t* removedCount) {
    return Patch_CutBoxInternal(source,box,cut,removeTriangle,removedCount,true);
}

static int Patch_CompareFaceEdge(const void* left,const void* right) {
    const PatchFaceEdge* a=left;const PatchFaceEdge* b=right;
    if(a->lo!=b->lo)return (a->lo>b->lo)-(a->lo<b->lo);
    return (a->hi>b->hi)-(a->hi<b->hi);
}
bool MeshPatch_RegularizeSelection(const Mesh* mesh,uint8_t* selection,
    size_t* expandedTriangles) {
    if(!mesh||!selection||mesh->indexCount%3||!mesh->vertices||!mesh->indices)return false;
    size_t count=mesh->indexCount/3,edgeCount=mesh->indexCount,expanded=0;
    PatchFaceEdge* edges=malloc(edgeCount*sizeof(*edges));
    size_t* degree=calloc(mesh->vertexCount,sizeof(*degree));
    if(!edges||!degree){free(edges);free(degree);return false;}
    for(size_t t=0;t<count;++t)for(int e=0;e<3;++e) {
        MeshIndex a=mesh->indices[t*3+e],b=mesh->indices[t*3+(e+1)%3];
        if(a>=mesh->vertexCount||b>=mesh->vertexCount){free(edges);free(degree);return false;}
        edges[t*3+e]=(PatchFaceEdge){a<b?a:b,a<b?b:a,t};
    }
    qsort(edges,edgeCount,sizeof(*edges),Patch_CompareFaceEdge);
    bool finished=false;
    for(int pass=0;pass<12;++pass) {
        memset(degree,0,mesh->vertexCount*sizeof(*degree));
        for(size_t i=0;i<edgeCount;) {
            size_t end=i+1;
            while(end<edgeCount&&edges[i].lo==edges[end].lo&&
                  edges[i].hi==edges[end].hi)end++;
            if(end-i!=2){free(edges);free(degree);return false;}
            if((selection[edges[i].triangle]!=0)!=(selection[edges[i+1].triangle]!=0)) {
                degree[edges[i].lo]++;degree[edges[i].hi]++;
            }
            i=end;
        }
        size_t changed=0;
        for(size_t t=0;t<count;++t)if(!selection[t]) {
            MeshIndex a=mesh->indices[t*3],b=mesh->indices[t*3+1],c=mesh->indices[t*3+2];
            if(degree[a]>2||degree[b]>2||degree[c]>2) {
                selection[t]=1;changed++;
            }
        }
        expanded+=changed;
        if(!changed){finished=true;break;}
    }
    free(edges);free(degree);
    if(expandedTriangles)*expandedTriangles=expanded;
    return finished;
}

static int Patch_CompareEdge(const void* left,const void* right) {
    const PatchEdge* a=left;const PatchEdge* b=right;
    if(a->lo!=b->lo)return (a->lo>b->lo)-(a->lo<b->lo);
    return (a->hi>b->hi)-(a->hi<b->hi);
}
static bool Patch_CollectBoundary(const Mesh* mesh,const uint8_t* selection,
    PatchLoopSet* loops) {
    size_t selected=0;
    for(size_t t=0;t<mesh->indexCount/3;++t)
        selected+=!selection||selection[t]!=0;
    if(!selected || selected>SIZE_MAX/3){loops->error=1;return false;}
    size_t edgeCount=selected*3,cursor=0;
    PatchEdge* edges=malloc(edgeCount*sizeof(*edges));
    if(!edges){loops->error=2;return false;}
    for(size_t t=0;t<mesh->indexCount/3;++t) {
        if(selection&&!selection[t])continue;
        for(int e=0;e<3;++e) {
            MeshIndex from=mesh->indices[t*3+e],to=mesh->indices[t*3+(e+1)%3];
            if(from>=mesh->vertexCount||to>=mesh->vertexCount) {
                loops->error=3;free(edges);return false;
            }
            edges[cursor++]=(PatchEdge){from<to?from:to,from<to?to:from,from,to};
        }
    }
    qsort(edges,edgeCount,sizeof(*edges),Patch_CompareEdge);
    MeshIndex* next=malloc(mesh->vertexCount*sizeof(*next));
    uint8_t* seen=calloc(mesh->vertexCount,1);
    loops->vertices=malloc(edgeCount*sizeof(*loops->vertices));
    loops->offsets=malloc((edgeCount+1)*sizeof(*loops->offsets));
    if(!next||!seen||!loops->vertices||!loops->offsets) {
        loops->error=2;free(edges);free(next);free(seen);return false;
    }
    for(size_t i=0;i<mesh->vertexCount;++i)next[i]=UINT32_MAX;
    size_t boundaryCount=0;
    for(size_t i=0;i<edgeCount;) {
        size_t end=i+1;
        while(end<edgeCount && edges[i].lo==edges[end].lo &&
              edges[i].hi==edges[end].hi)end++;
        if(end-i>2) {loops->error=4;free(edges);free(next);free(seen);return false;}
        if(end-i==1) {
            MeshIndex from=edges[i].from,to=edges[i].to;
            if(next[from]!=UINT32_MAX){loops->error=5;free(edges);free(next);free(seen);return false;}
            next[from]=to;boundaryCount++;
        }
        i=end;
    }
    loops->boundaryEdges=boundaryCount;
    for(size_t start=0;start<mesh->vertexCount;++start) {
        if(next[start]==UINT32_MAX||seen[start])continue;
        loops->offsets[loops->count++]=loops->vertexCount;
        MeshIndex at=(MeshIndex)start;
        do {
            if(at>=mesh->vertexCount||next[at]==UINT32_MAX||seen[at]||
               loops->vertexCount>=boundaryCount) {
                loops->error=6;free(edges);free(next);free(seen);return false;
            }
            seen[at]=1;loops->vertices[loops->vertexCount++]=at;
            at=next[at];
        } while(at!=start);
        if(loops->vertexCount-loops->offsets[loops->count-1]<3) {
            loops->error=7;free(edges);free(next);free(seen);return false;
        }
    }
    loops->offsets[loops->count]=loops->vertexCount;
    bool valid=loops->vertexCount==boundaryCount;
    if(!valid)loops->error=8;
    free(edges);free(next);free(seen);
    return valid;
}
static void Patch_FreeLoops(PatchLoopSet* loops) {
    free(loops->vertices);free(loops->offsets);memset(loops,0,sizeof(*loops));
}
static Vector3 Patch_Centroid(const Mesh* mesh,const PatchLoopSet* loops,size_t index) {
    Vector3 sum=Vec3_Zero();size_t begin=loops->offsets[index],end=loops->offsets[index+1];
    for(size_t i=begin;i<end;++i)sum=Vec3_Add(sum,mesh->vertices[loops->vertices[i]].position);
    return Vec3_Scale(sum,1.0f/(end-begin));
}
static Vector3 Patch_LoopNormal(const Mesh* mesh,const MeshIndex* ids,size_t count) {
    Vector3 center=Vec3_Zero(),normal=Vec3_Zero();
    for(size_t i=0;i<count;++i)center=Vec3_Add(center,mesh->vertices[ids[i]].position);
    center=Vec3_Scale(center,1.0f/count);
    for(size_t i=0;i<count;++i) {
        Vector3 a=Vec3_Sub(mesh->vertices[ids[i]].position,center);
        Vector3 b=Vec3_Sub(mesh->vertices[ids[(i+1)%count]].position,center);
        normal=Vec3_Add(normal,Vec3_Cross(a,b));
    }
    return Vec3_Normalize(normal);
}
static bool Patch_AppendTriangle(Mesh* mesh,MeshIndex a,MeshIndex b,MeshIndex c) {
    if(!Mesh_TriangleHasArea(mesh->vertices[a].position,mesh->vertices[b].position,
        mesh->vertices[c].position))return false;
    return Mesh_AddTriangle(mesh,a,b,c);
}
typedef struct PatchWeldMap {
    MeshIndex* vertex;
    MeshIndex* next;
    MeshIndex* ringStart;
    MeshIndex* ringLength;
} PatchWeldMap;
typedef struct PatchSeamList {
    size_t* triangles;
    size_t count,capacity;
} PatchSeamList;

static bool Patch_RecordSeam(PatchSeamList* seam,size_t triangle) {
    if(seam->count==seam->capacity) {
        size_t capacity=seam->capacity?seam->capacity*2:256;
        size_t* resized=realloc(seam->triangles,capacity*sizeof(*resized));
        if(!resized)return false;
        seam->triangles=resized;seam->capacity=capacity;
    }
    seam->triangles[seam->count++]=triangle;
    return true;
}

static void Patch_FreeWeldMap(PatchWeldMap* map) {
    free(map->vertex);free(map->next);free(map->ringStart);free(map->ringLength);
    memset(map,0,sizeof(*map));
}

static bool Patch_InitWeldMap(PatchWeldMap* map,size_t count) {
    map->vertex=malloc(count*sizeof(*map->vertex));
    map->next=malloc(count*sizeof(*map->next));
    map->ringStart=malloc(count*sizeof(*map->ringStart));
    map->ringLength=malloc(count*sizeof(*map->ringLength));
    if(!map->vertex||!map->next||!map->ringStart||!map->ringLength)return false;
    for(size_t i=0;i<count;++i) {
        map->vertex[i]=(MeshIndex)i;
        map->next[i]=map->ringStart[i]=map->ringLength[i]=UINT32_MAX;
    }
    return true;
}

static Vector3 Patch_LerpVector(Vector3 a,Vector3 b,float t) {
    return Vec3_Add(Vec3_Scale(a,1-t),Vec3_Scale(b,t));
}

static float Patch_Distance(const SDFField* field,Vector3 point,size_t* calls) {
    (*calls)++;
    return field->evaluateDistance?field->evaluateDistance(field->context,point):
        field->evaluate(field->context,point).distance;
}

static bool Patch_AngularArc(const Mesh* mesh,const MeshIndex* ids,
    size_t count,size_t start,Vector3 center,int plane,float* arc) {
    float previous=0,total=0,orientation=0;
    arc[0]=0;
    for(size_t i=0;i<=count;++i) {
        Vector3 p=Vec3_Sub(mesh->vertices[ids[(start+i)%count]].position,center);
        float angle=plane==0?atan2f(p.z,p.y):
            plane==1?atan2f(p.z,p.x):atan2f(p.y,p.x);
        if(i) {
            float delta=angle-previous;
            while(delta>3.1415927f)delta-=6.2831853f;
            while(delta<-3.1415927f)delta+=6.2831853f;
            if(i==1)orientation=delta>=0?1.0f:-1.0f;
            delta*=orientation;
            if(delta<=1e-5f||delta>3.0f)return false;
            total+=delta;arc[i]=total;
        }
        previous=angle;
    }
    if(total<5.0f||total>7.5f)return false;
    for(size_t i=1;i<=count;++i)arc[i]/=total;
    return true;
}

/* Cada frontera se parametriza por longitud de arco. La unión de sus muestras
 * define un solo anillo, compartido exactamente por ambos lados. */
static bool Patch_WeldLoop(Mesh* result,const Mesh* original,const Mesh* patch,
    const MeshIndex* oldIds,size_t oldCount,const MeshIndex* newIds,size_t newCount,
    float maximumGap,float blend,const MeshPatchValidation* check,
    PatchWeldMap* oldMap,PatchWeldMap* newMap,MeshPatchStats* stats) {
    float* oldArc=malloc((oldCount+1)*sizeof(*oldArc));
    float* newArc=malloc((newCount+1)*sizeof(*newArc));
    if(!oldArc||!newArc){free(oldArc);free(newArc);return false;}
    oldArc[0]=0;
    for(size_t i=0;i<oldCount;++i)oldArc[i+1]=oldArc[i]+Vec3_Distance(
        original->vertices[oldIds[i]].position,
        original->vertices[oldIds[(i+1)%oldCount]].position);
    if(oldArc[oldCount]<=1e-8f) {
        free(oldArc);free(newArc);return false;
    }
    for(size_t i=1;i<=oldCount;++i)oldArc[i]/=oldArc[oldCount];
    /* Elegir la fase con el menor error de TODO el lazo. Emparejar sólo el
     * primer vértice podía desplazar una arista completa y plegar una cara. */
    size_t nearestIndex=0;
    float bestMaximum=INFINITY,bestError=INFINITY;
    for(size_t start=0;start<newCount;++start) {
        newArc[0]=0;
        for(size_t j=0;j<newCount;++j)newArc[j+1]=newArc[j]+Vec3_Distance(
            patch->vertices[newIds[(start+j)%newCount]].position,
            patch->vertices[newIds[(start+j+1)%newCount]].position);
        if(newArc[newCount]<=1e-8f)continue;
        for(size_t j=1;j<=newCount;++j)newArc[j]/=newArc[newCount];
        float maximum=0,error=0;
        size_t j=0;
        for(size_t i=0;i<oldCount;++i) {
            float s=oldArc[i];
            while(j+1<newCount&&newArc[j+1]<=s)j++;
            float width=newArc[j+1]-newArc[j];
            if(width<=1e-8f){maximum=INFINITY;break;}
            float t=(s-newArc[j])/width;
            Vector3 a=patch->vertices[newIds[(start+j)%newCount]].position;
            Vector3 b=patch->vertices[newIds[(start+j+1)%newCount]].position;
            float distance=Vec3_Distance(original->vertices[oldIds[i]].position,
                Patch_LerpVector(a,b,t));
            maximum=fmaxf(maximum,distance);error+=distance*distance;
        }
        if(maximum<bestMaximum-1e-6f ||
           (fabsf(maximum-bestMaximum)<=1e-6f&&error<bestError)) {
            nearestIndex=start;bestMaximum=maximum;bestError=error;
        }
    }
    if(bestMaximum>maximumGap){free(oldArc);free(newArc);return false;}
    if(check&&check->loopPhaseShift) {
        int shifted=((int)nearestIndex+check->loopPhaseShift)%(int)newCount;
        if(shifted<0)shifted+=(int)newCount;
        nearestIndex=(size_t)shifted;
    }
    newArc[0]=0;
    for(size_t j=0;j<newCount;++j)newArc[j+1]=newArc[j]+Vec3_Distance(
        patch->vertices[newIds[(nearestIndex+j)%newCount]].position,
        patch->vertices[newIds[(nearestIndex+j+1)%newCount]].position);
    for(size_t j=1;j<=newCount;++j)newArc[j]/=newArc[newCount];
    if(check&&check->angularSeam) {
        Vector3 center=Vec3_Scale(Vec3_Add(
            Patch_LoopNormal(original,oldIds,oldCount),
            Patch_LoopNormal(patch,newIds,newCount)),.5f);
        int plane=fabsf(center.x)>fabsf(center.y)?0:1;
        if(fabsf(center.z)>fabsf(plane==0?center.x:center.y))plane=2;
        center=Vec3_Scale(Vec3_Add(Patch_Centroid(original,
            &(PatchLoopSet){.vertices=(MeshIndex*)oldIds,
                .offsets=(size_t[]){0,oldCount}},0),
            Patch_Centroid(patch,&(PatchLoopSet){.vertices=(MeshIndex*)newIds,
                .offsets=(size_t[]){0,newCount}},0)),.5f);
        float* candidateOld=malloc((oldCount+1)*sizeof(*candidateOld));
        float* candidateNew=malloc((newCount+1)*sizeof(*candidateNew));
        if(candidateOld&&candidateNew&&
           Patch_AngularArc(original,oldIds,oldCount,0,center,plane,candidateOld)&&
           Patch_AngularArc(patch,newIds,newCount,nearestIndex,center,plane,candidateNew)) {
            memcpy(oldArc,candidateOld,(oldCount+1)*sizeof(*oldArc));
            memcpy(newArc,candidateNew,(newCount+1)*sizeof(*newArc));
        }
        free(candidateOld);free(candidateNew);
    }
    size_t i=0,j=0,ringCount=0;
    MeshIndex ringStart=(MeshIndex)result->vertexCount;
    bool ok=true;
    while(i<oldCount||j<newCount) {
        float oldS=i<oldCount?oldArc[i]:INFINITY;
        float newS=j<newCount?newArc[j]:INFINITY;
        float s=fminf(oldS,newS);
        bool oldEvent=oldS<=s+1e-6f,newEvent=newS<=s+1e-6f;
        size_t oi=0,nj=0;
        while(oi+1<oldCount&&oldArc[oi+1]<=s)oi++;
        while(nj+1<newCount&&newArc[nj+1]<=s)nj++;
        float ot=(s-oldArc[oi])/(oldArc[oi+1]-oldArc[oi]);
        float nt=(s-newArc[nj])/(newArc[nj+1]-newArc[nj]);
        if(!isfinite(ot)||!isfinite(nt)){ok=false;break;}
        Vector3 oa=original->vertices[oldIds[oi]].position;
        Vector3 ob=original->vertices[oldIds[(oi+1)%oldCount]].position;
        Vector3 na=patch->vertices[newIds[(nearestIndex+nj)%newCount]].position;
        Vector3 nb=patch->vertices[newIds[(nearestIndex+nj+1)%newCount]].position;
        Vector3 oldPoint=Patch_LerpVector(oa,ob,ot);
        Vector3 newPoint=Patch_LerpVector(na,nb,nt);
        float gap=Vec3_Distance(oldPoint,newPoint);
        stats->maximumSeamDistance=fmaxf(stats->maximumSeamDistance,gap);
        if(gap>maximumGap){ok=false;break;}
        MeshVertex vertex=original->vertices[oldIds[oi]];
        vertex.position=Patch_LerpVector(oldPoint,newPoint,blend);
        vertex.normal=Vec3_Normalize(Patch_LerpVector(
            original->vertices[oldIds[oi]].normal,
            patch->vertices[newIds[(nearestIndex+nj)%newCount]].normal,blend));
        if(check&&check->field&&check->field->evaluate&&
           check->maximumResidual>0) {
            float distance=Patch_Distance(check->field,vertex.position,
                &stats->seamDistanceEvaluations);
            if(!isfinite(distance)||fabsf(distance)>check->maximumResidual) {
                stats->maximumSeamResidual=fmaxf(stats->maximumSeamResidual,fabsf(distance));
                ok=false;break;
            }
            stats->maximumSeamResidual=fmaxf(stats->maximumSeamResidual,fabsf(distance));
        }
        MeshIndex shared;
        if(!Mesh_AddVertex(result,vertex,&shared)){ok=false;break;}
        ringCount++;
        if(oldEvent) {
            MeshIndex id=oldIds[i];
            oldMap->vertex[id]=shared;
            oldMap->next[id]=oldIds[(i+1)%oldCount];
            oldMap->ringStart[id]=ringStart;
            i++;
        }
        if(newEvent) {
            MeshIndex id=newIds[(nearestIndex+j)%newCount];
            newMap->vertex[id]=shared;
            newMap->next[id]=newIds[(nearestIndex+j+1)%newCount];
            newMap->ringStart[id]=ringStart;
            j++;
        }
    }
    if(ok&&ringCount<3)ok=false;
    if(ok) {
        for(size_t k=0;k<oldCount;++k)oldMap->ringLength[oldIds[k]]=(MeshIndex)ringCount;
        for(size_t k=0;k<newCount;++k)newMap->ringLength[newIds[k]]=(MeshIndex)ringCount;
    }
    free(oldArc);free(newArc);
    return ok;
}

static bool Patch_ExpandedFace(Mesh* result,const MeshIndex face[3],
    const PatchWeldMap* map,MeshIndex offset,bool reverse,
    const MeshPatchValidation* check,MeshPatchStats* stats,PatchSeamList* seam) {
    MeshIndex* polygon=NULL;size_t count=0,capacity=0;
    bool ok=true,nearSeam=false;
    size_t boundaryEdges=0;MeshIndex opposite=UINT32_MAX;
    for(size_t edge=0;edge<3&&ok;++edge) {
        MeshIndex a=face[edge],b=face[(edge+1)%3];
        nearSeam|=map->ringStart[a]!=UINT32_MAX;
        MeshIndex mapped=map->vertex[a];
        if(count==capacity) {
            size_t grown=capacity?capacity*2:8;
            MeshIndex* replacement=realloc(polygon,grown*sizeof(*replacement));
            if(!replacement){ok=false;break;}polygon=replacement;capacity=grown;
        }
        polygon[count++]=mapped==a?a+offset:mapped;
        bool boundary=reverse?map->next[b]==a:map->next[a]==b;
        if(!boundary)continue;
        boundaryEdges++;
        opposite=face[(edge+2)%3];
        MeshIndex start=map->ringStart[a],length=map->ringLength[a];
        if(start==UINT32_MAX||length==UINT32_MAX||!length||
           map->ringStart[b]!=start){ok=false;break;}
        MeshIndex from=map->vertex[a],to=map->vertex[b],at=from;
        for(size_t step=0;step<length;++step) {
            at=reverse?start+(at-start+length-1)%length:
                start+(at-start+1)%length;
            if(at==to)break;
            if(count==capacity) {
                size_t grown=capacity*2;
                MeshIndex* replacement=realloc(polygon,grown*sizeof(*replacement));
                if(!replacement){ok=false;break;}polygon=replacement;capacity=grown;
            }
            polygon[count++]=at;
        }
        if(at!=to)ok=false;
    }
    MeshIndex center=UINT32_MAX;
    size_t fanStart=0;
    if(ok&&count>3&&boundaryEdges==1) {
        MeshIndex mapped=map->vertex[opposite];
        MeshIndex anchor=mapped==opposite?opposite+offset:mapped;
        while(fanStart<count&&polygon[fanStart]!=anchor)fanStart++;
        if(fanStart==count)ok=false;
    } else if(ok&&count>3) {
        MeshIndex ids[3];
        for(int i=0;i<3;++i) {
            MeshIndex mapped=map->vertex[face[i]];
            ids[i]=mapped==face[i]?face[i]+offset:mapped;
        }
        MeshVertex vertex=Patch_InterpolateVertex(result->vertices[ids[0]],
            result->vertices[ids[1]],.5f);
        vertex=Patch_InterpolateVertex(vertex,result->vertices[ids[2]],1.0f/3.0f);
        ok=Mesh_AddVertex(result,vertex,&center);
    }
    size_t triangles=center==UINT32_MAX?count-2:count;
    for(size_t k=0;ok&&k<triangles;++k) {
        MeshIndex a=center==UINT32_MAX?polygon[fanStart]:center;
        MeshIndex b=center==UINT32_MAX?polygon[(fanStart+k+1)%count]:polygon[k];
        MeshIndex c=center==UINT32_MAX?polygon[(fanStart+k+2)%count]:polygon[(k+1)%count];
        size_t triangle=result->indexCount/3;
        ok=Patch_AppendTriangle(result,a,b,c);
        if(!ok||!nearSeam)continue;
        ok=Patch_RecordSeam(seam,triangle);
        if(!ok)break;
        stats->seamTriangles++;
        if(!check||!check->field||!check->field->evaluate||
           check->maximumResidual<=0)continue;
        const SDFField* field=check->field;
        Vector3 pa=result->vertices[a].position,pb=result->vertices[b].position,
            pc=result->vertices[c].position;
        Vector3 center=Vec3_Scale(Vec3_Add(pa,Vec3_Add(pb,pc)),1.0f/3);
        float f=Patch_Distance(field,center,&stats->seamDistanceEvaluations);
        stats->maximumSeamResidual=fmaxf(stats->maximumSeamResidual,fabsf(f));
        if(!isfinite(f)||fabsf(f)>check->maximumResidual){ok=false;break;}
        Vector3 gradient=Vec3_Zero();const float h=.002f;
        for(int axis=0;axis<3;++axis) {
            Vector3 d=Vec3_Create(axis==0?h:0,axis==1?h:0,axis==2?h:0);
            float plus=Patch_Distance(field,Vec3_Add(center,d),
                &stats->seamDistanceEvaluations);
            float minus=Patch_Distance(field,Vec3_Sub(center,d),
                &stats->seamDistanceEvaluations);
            float value=(plus-minus)/(2*h);
            if(axis==0)gradient.x=value;
            if(axis==1)gradient.y=value;
            if(axis==2)gradient.z=value;
        }
        float orientation=Vec3_Dot(Vec3_Cross(Vec3_Sub(pb,pa),Vec3_Sub(pc,pa)),gradient);
        if(!isfinite(orientation)||orientation<-1e-8f) {
            if(!stats->seamOpposedTriangles) {
                stats->firstOpposedOrientation=orientation;
                stats->firstOpposedArea=Vec3_Length(Vec3_Cross(
                    Vec3_Sub(pb,pa),Vec3_Sub(pc,pa)))*.5f;
                stats->firstOpposedCenter=center;
            }
            stats->seamOpposedTriangles++;
            if(reverse)stats->oldSeamOpposedTriangles++;
            else stats->newSeamOpposedTriangles++;
        }
    }
    free(polygon);
    return ok;
}

static float Patch_Orientation(const Mesh* mesh,size_t triangle,
    const SDFField* field,size_t* evaluations,Vector3* gradientOut) {
    MeshIndex ia=mesh->indices[triangle*3],ib=mesh->indices[triangle*3+1],
        ic=mesh->indices[triangle*3+2];
    Vector3 a=mesh->vertices[ia].position,b=mesh->vertices[ib].position,
        c=mesh->vertices[ic].position;
    Vector3 center=Vec3_Scale(Vec3_Add(a,Vec3_Add(b,c)),1.0f/3);
    Vector3 gradient=Vec3_Zero();const float h=.002f;
    for(int axis=0;axis<3;++axis) {
        Vector3 delta=Vec3_Create(axis==0?h:0,axis==1?h:0,axis==2?h:0);
        float plus=Patch_Distance(field,Vec3_Add(center,delta),evaluations);
        float minus=Patch_Distance(field,Vec3_Sub(center,delta),evaluations);
        float value=(plus-minus)/(2*h);
        if(axis==0)gradient.x=value;
        if(axis==1)gradient.y=value;
        if(axis==2)gradient.z=value;
    }
    if(gradientOut)*gradientOut=gradient;
    return Vec3_Dot(Vec3_Cross(Vec3_Sub(b,a),Vec3_Sub(c,a)),gradient);
}

static bool Patch_RepairSeam(Mesh* mesh,const PatchSeamList* seam,
    const MeshPatchValidation* check,MeshPatchStats* stats,size_t oldVertexCount,
    size_t initialVertexCount) {
    if(!check||!check->field||!check->field->evaluate||!seam->count)return true;
    size_t triangleCount=mesh->indexCount/3,n=mesh->vertexCount;
    size_t* offsets=calloc(n+1,sizeof(*offsets));
    size_t* incident=malloc(mesh->indexCount*sizeof(*incident));
    uint8_t* isSeam=calloc(triangleCount,1);
    if(!offsets||!incident||!isSeam) {
        free(offsets);free(incident);free(isSeam);return false;
    }
    for(size_t i=0;i<mesh->indexCount;++i)offsets[mesh->indices[i]+1]++;
    for(size_t i=1;i<=n;++i)offsets[i]+=offsets[i-1];
    size_t* cursor=malloc(n*sizeof(*cursor));
    if(!cursor){free(offsets);free(incident);free(isSeam);return false;}
    memcpy(cursor,offsets,n*sizeof(*cursor));
    for(size_t t=0;t<triangleCount;++t)for(int k=0;k<3;++k) {
        MeshIndex id=mesh->indices[t*3+k];incident[cursor[id]++]=t;
    }
    free(cursor);
    for(size_t i=0;i<seam->count;++i)isSeam[seam->triangles[i]]=1;
    for(int pass=0;pass<4;++pass) {
        size_t changed=0;
        for(size_t i=0;i<seam->count;++i) {
            size_t t=seam->triangles[i];
            Vector3 gradient=Vec3_Zero();
            float orientation=Patch_Orientation(mesh,t,check->field,
                &stats->seamDistanceEvaluations,&gradient);
            if(orientation>=-1e-8f)continue;
            MeshIndex old[3]={mesh->indices[t*3],mesh->indices[t*3+1],
                mesh->indices[t*3+2]};
            bool fixed=false;
            for(int k=0;k<3&&!fixed;++k) {
                MeshIndex vertex=old[k];
                if(vertex<oldVertexCount||vertex>=initialVertexCount)continue;
                Vector3 a=mesh->vertices[old[(k+1)%3]].position;
                Vector3 b=mesh->vertices[old[(k+2)%3]].position;
                Vector3 direction=Vec3_Cross(gradient,Vec3_Sub(b,a));
                float lengthSq=Vec3_LengthSq(direction);
                if(lengthSq<1e-14f)continue;
                float edge=Vec3_Distance(mesh->vertices[vertex].position,a);
                edge=fminf(edge,Vec3_Distance(mesh->vertices[vertex].position,b));
                float step=fminf((-orientation+1e-7f)/lengthSq,edge*.4f/sqrtf(lengthSq));
                if(!isfinite(step)||step<=0)continue;
                Vector3 previous=mesh->vertices[vertex].position;
                Vector3 proposed=Vec3_Add(previous,Vec3_Scale(direction,step));
                float residual=Patch_Distance(check->field,proposed,
                    &stats->seamDistanceEvaluations);
                if(!isfinite(residual))continue;
                proposed=Vec3_Sub(proposed,Vec3_Scale(gradient,
                    residual/fmaxf(Vec3_LengthSq(gradient),1e-8f)));
                mesh->vertices[vertex].position=proposed;
                size_t worsened=0,improved=0;
                for(size_t at=offsets[vertex];at<offsets[vertex+1];++at) {
                    size_t face=incident[at];
                    float current=Patch_Orientation(mesh,face,check->field,
                        &stats->seamDistanceEvaluations,NULL);
                    mesh->vertices[vertex].position=previous;
                    float prior=Patch_Orientation(mesh,face,check->field,
                        &stats->seamDistanceEvaluations,NULL);
                    mesh->vertices[vertex].position=proposed;
                    if(current<-1e-8f&&prior>=-1e-8f)worsened++;
                    if(current>=-1e-8f&&prior<-1e-8f)improved++;
                }
                if(!worsened&&improved) {
                    float corrected=fabsf(Patch_Distance(check->field,proposed,
                        &stats->seamDistanceEvaluations));
                    if(isfinite(corrected)&&corrected<=check->maximumResidual) {
                        stats->maximumSeamResidual=fmaxf(stats->maximumSeamResidual,corrected);
                        fixed=true;changed++;
                    }
                }
                if(!fixed)mesh->vertices[vertex].position=previous;
            }
            if(fixed)continue;
            for(int edge=0;edge<3&&!fixed;++edge) {
                MeshIndex a=old[edge],b=old[(edge+1)%3],c=old[(edge+2)%3];
                for(size_t at=offsets[a];at<offsets[a+1]&&!fixed;++at) {
                    size_t neighbor=incident[at];
                    if(neighbor==t||!isSeam[neighbor])continue;
                    MeshIndex other[3]={mesh->indices[neighbor*3],
                        mesh->indices[neighbor*3+1],mesh->indices[neighbor*3+2]};
                    MeshIndex d=UINT32_MAX;
                    for(int k=0;k<3;++k)
                        if(other[k]==b&&other[(k+1)%3]==a)
                            d=other[(k+2)%3];
                    if(d==UINT32_MAX||d==c||
                       !Mesh_TriangleHasArea(mesh->vertices[c].position,
                           mesh->vertices[a].position,mesh->vertices[d].position)||
                       !Mesh_TriangleHasArea(mesh->vertices[d].position,
                           mesh->vertices[b].position,mesh->vertices[c].position))continue;
                    MeshIndex first[3]={c,a,d},second[3]={d,b,c};
                    memcpy(mesh->indices+t*3,first,sizeof(first));
                    memcpy(mesh->indices+neighbor*3,second,sizeof(second));
                    float x=Patch_Orientation(mesh,t,check->field,
                        &stats->seamDistanceEvaluations,NULL);
                    float y=Patch_Orientation(mesh,neighbor,check->field,
                        &stats->seamDistanceEvaluations,NULL);
                    Vector3 centerA=Vec3_Scale(Vec3_Add(mesh->vertices[c].position,
                        Vec3_Add(mesh->vertices[a].position,mesh->vertices[d].position)),1.0f/3);
                    Vector3 centerB=Vec3_Scale(Vec3_Add(mesh->vertices[d].position,
                        Vec3_Add(mesh->vertices[b].position,mesh->vertices[c].position)),1.0f/3);
                    float fa=fabsf(Patch_Distance(check->field,centerA,
                        &stats->seamDistanceEvaluations));
                    float fb=fabsf(Patch_Distance(check->field,centerB,
                        &stats->seamDistanceEvaluations));
                    if(isfinite(x)&&isfinite(y)&&x>=-1e-8f&&y>=-1e-8f&&
                       isfinite(fa)&&isfinite(fb)&&fa<=check->maximumResidual&&
                       fb<=check->maximumResidual) {
                        stats->maximumSeamResidual=fmaxf(stats->maximumSeamResidual,
                            fmaxf(fa,fb));
                        fixed=true;changed++;
                    } else {
                        memcpy(mesh->indices+t*3,old,sizeof(old));
                        memcpy(mesh->indices+neighbor*3,other,sizeof(other));
                    }
                }
            }
        }
        if(!changed)break;
    }
    size_t opposed=0;
    for(size_t i=0;i<seam->count;++i)
        opposed+=Patch_Orientation(mesh,seam->triangles[i],check->field,
            &stats->seamDistanceEvaluations,NULL)<-1e-8f;
    stats->seamOpposedTriangles=opposed;
    free(offsets);free(incident);free(isSeam);
    return opposed==0;
}

void MeshPatchTransfer_Free(MeshPatchTransfer* transfer) {
    if(!transfer)return;
    free(transfer->oldVertexOfNew);
    transfer->oldVertexOfNew=NULL;
    transfer->vertexCount=0;
}

static bool Patch_CompactReferenced(Mesh* mesh,size_t oldVertexCount,
    MeshPatchTransfer* transfer) {
    MeshIndex* remap=malloc(mesh->vertexCount*sizeof(*remap));
    if(!remap)return false;
    for(size_t i=0;i<mesh->vertexCount;++i)remap[i]=UINT32_MAX;
    size_t used=0;
    for(size_t i=0;i<mesh->indexCount;++i) {
        MeshIndex old=mesh->indices[i];
        if(old>=mesh->vertexCount){free(remap);return false;}
        if(remap[old]==UINT32_MAX)remap[old]=(MeshIndex)used++;
    }
    MeshVertex* compact=malloc(used*sizeof(*compact));
    uint32_t* origin=transfer?malloc(used*sizeof(*origin)):NULL;
    if(!compact||(transfer&&!origin)){free(remap);free(compact);free(origin);return false;}
    for(size_t old=0;old<mesh->vertexCount;++old)
        if(remap[old]!=UINT32_MAX) {
            compact[remap[old]]=mesh->vertices[old];
            if(origin)origin[remap[old]]=old<oldVertexCount?(uint32_t)old:UINT32_MAX;
        }
    for(size_t i=0;i<mesh->indexCount;++i)mesh->indices[i]=remap[mesh->indices[i]];
    free(mesh->vertices);mesh->vertices=compact;
    mesh->vertexCount=mesh->vertexCapacity=used;
    if(transfer) {
        MeshPatchTransfer_Free(transfer);
        transfer->oldVertexOfNew=origin;
        transfer->vertexCount=used;
    }
    free(remap);return true;
}

bool MeshPatch_ExtractInsideBox(const Mesh* source,AABB3D box,Mesh* inside) {
    if(!inside||inside==source)return false;
    Mesh cut=Mesh_Create(),result=Mesh_Create();
    uint8_t* selected=NULL;size_t count=0;
    bool ok=Patch_CutBoxInternal(source,box,&cut,&selected,&count,false)&&
        Mesh_ReserveVertices(&result,cut.vertexCount)&&
        Mesh_ReserveIndices(&result,count*3);
    if(ok) {
        memcpy(result.vertices,cut.vertices,cut.vertexCount*sizeof(*cut.vertices));
        result.vertexCount=cut.vertexCount;
        result.surfaceRecipe=cut.surfaceRecipe;result.hasSurface=cut.hasSurface;
        for(size_t t=0;t<cut.indexCount/3;++t)if(selected[t]&&
            !Mesh_AddTriangle(&result,cut.indices[t*3],cut.indices[t*3+1],
                cut.indices[t*3+2])){ok=false;break;}
    }
    if(ok)ok=Patch_CompactReferenced(&result,0,NULL);
    if(ok) {
        Mesh previous=*inside;*inside=result;Mesh_Free(&previous);
    } else Mesh_Free(&result);
    free(selected);Mesh_Free(&cut);
    return ok;
}

bool MeshPatch_ReplaceWithTransfer(Mesh* mesh,const uint8_t* removeTriangle,
    const Mesh* patch,float maximumGap,const MeshPatchValidation* check,
    MeshPatchStats* output,MeshPatchTransfer* transfer) {
    if(!mesh||!removeTriangle||!patch||!mesh->vertexCount||!patch->vertexCount||
       mesh->indexCount%3||patch->indexCount%3||!isfinite(maximumGap)||maximumGap<=0||
       (check&&check->field&&(!check->field->evaluate||
           !isfinite(check->maximumResidual)||check->maximumResidual<=0))||
       (check&&check->hasBoundaryBlend&&
           (!isfinite(check->boundaryBlend)||check->boundaryBlend<0||
            check->boundaryBlend>1))||
       mesh->vertexCount>UINT32_MAX-patch->vertexCount)return false;
    MeshPatchStats stats={0};PatchLoopSet oldLoops={0},newLoops={0};
    bool ok=Patch_CollectBoundary(mesh,removeTriangle,&oldLoops)&&
        Patch_CollectBoundary(patch,NULL,&newLoops);
    stats.oldBoundaryLoops=oldLoops.count;stats.newBoundaryLoops=newLoops.count;
    stats.oldBoundaryEdges=oldLoops.boundaryEdges;
    stats.newBoundaryEdges=newLoops.boundaryEdges;
    stats.oldBoundaryError=oldLoops.error;stats.newBoundaryError=newLoops.error;
    if(!ok||!oldLoops.count||oldLoops.count!=newLoops.count){ok=false;goto done;}
    stats.boundaryLoops=oldLoops.count;
    for(size_t i=0;i<oldLoops.count&&i<8;++i)
        stats.oldLoopCenters[i]=Patch_Centroid(mesh,&oldLoops,i);
    for(size_t i=0;i<newLoops.count&&i<8;++i)
        stats.newLoopCenters[i]=Patch_Centroid(patch,&newLoops,i);
    if(check&&check->field&&check->field->evaluate&&check->maximumResidual>0) {
        for(size_t i=0;i<oldLoops.vertexCount;++i) {
            float residual=fabsf(Patch_Distance(check->field,
                mesh->vertices[oldLoops.vertices[i]].position,
                &stats.seamDistanceEvaluations));
            if(!isfinite(residual)){ok=false;goto done;}
            stats.oldBoundaryMaximumResidual=fmaxf(stats.oldBoundaryMaximumResidual,residual);
        }
        for(size_t i=0;i<newLoops.vertexCount;++i) {
            float residual=fabsf(Patch_Distance(check->field,
                patch->vertices[newLoops.vertices[i]].position,
                &stats.seamDistanceEvaluations));
            if(!isfinite(residual)){ok=false;goto done;}
            stats.newBoundaryMaximumResidual=fmaxf(stats.newBoundaryMaximumResidual,residual);
        }
        if(stats.oldBoundaryMaximumResidual>check->maximumResidual||
           stats.newBoundaryMaximumResidual>check->maximumResidual) {
            ok=false;goto done;
        }
    }
    Mesh result=Mesh_Create();
    PatchSeamList seam={0};
    result.surfaceRecipe=mesh->surfaceRecipe;result.hasSurface=mesh->hasSurface;
    PatchWeldMap oldMap={0},newMap={0};
    size_t initial=mesh->vertexCount+patch->vertexCount;
    ok=Patch_InitWeldMap(&oldMap,mesh->vertexCount)&&
        Patch_InitWeldMap(&newMap,patch->vertexCount)&&
        Mesh_ReserveVertices(&result,initial+
            oldLoops.vertexCount+newLoops.vertexCount);
    if(ok) {
        memcpy(result.vertices,mesh->vertices,mesh->vertexCount*sizeof(*mesh->vertices));
        memcpy(result.vertices+mesh->vertexCount,patch->vertices,
            patch->vertexCount*sizeof(*patch->vertices));
        result.vertexCount=initial;
    }
    uint8_t* used=calloc(newLoops.count,1);
    if(!used)ok=false;
    /* El borde publicado permanece fijo salvo petición explícita: desplazarlo
     * puede invertir caras sanas del lado que no se sustituye. */
    float blend=check&&check->hasBoundaryBlend?check->boundaryBlend:0.0f;
    for(size_t loop=0;ok&&loop<oldLoops.count;++loop) {
        Vector3 center=Patch_Centroid(mesh,&oldLoops,loop);
        size_t match=SIZE_MAX;float nearest=INFINITY;
        for(size_t j=0;j<newLoops.count;++j)if(!used[j]) {
            float distance=Vec3_Distance(center,Patch_Centroid(patch,&newLoops,j));
            if(distance<nearest){nearest=distance;match=j;}
        }
        if(match==SIZE_MAX||nearest>maximumGap*2){ok=false;break;}
        used[match]=1;
        if(loop<8)stats.loopNormalDot[loop]=Vec3_Dot(
            Patch_LoopNormal(mesh,oldLoops.vertices+oldLoops.offsets[loop],
                oldLoops.offsets[loop+1]-oldLoops.offsets[loop]),
            Patch_LoopNormal(patch,newLoops.vertices+newLoops.offsets[match],
                newLoops.offsets[match+1]-newLoops.offsets[match]));
        const MeshIndex* oldIds=oldLoops.vertices+oldLoops.offsets[loop];
        size_t oldCount=oldLoops.offsets[loop+1]-oldLoops.offsets[loop];
        const MeshIndex* newIds=newLoops.vertices+newLoops.offsets[match];
        size_t newCount=newLoops.offsets[match+1]-newLoops.offsets[match];
        ok=Patch_WeldLoop(&result,mesh,patch,oldIds,oldCount,newIds,newCount,
            maximumGap,blend,check,&oldMap,&newMap,&stats);
    }
    free(used);
    if(ok)for(size_t t=0;t<mesh->indexCount/3;++t) {
        if(removeTriangle[t]){stats.removedTriangles++;continue;}
        ok=Patch_ExpandedFace(&result,mesh->indices+t*3,&oldMap,0,true,check,&stats,&seam);
        if(!ok)break;
    }
    if(ok)for(size_t t=0;t<patch->indexCount/3;++t) {
        ok=Patch_ExpandedFace(&result,patch->indices+t*3,&newMap,
            (MeshIndex)mesh->vertexCount,false,check,&stats,&seam);
        if(!ok)break;
        stats.insertedTriangles++;
    }
    if(ok)ok=Patch_RepairSeam(&result,&seam,check,&stats,
        mesh->vertexCount,initial);
    if(ok) {
        MeshValidationResult valid=Mesh_Validate(&result);
        ok=valid.valid&&valid.watertight;
    }
    if(ok)ok=Patch_CompactReferenced(&result,mesh->vertexCount,transfer);
    if(ok) {
        Mesh previous=*mesh;*mesh=result;Mesh_Free(&previous);
        Mesh_MarkSurfaceChanged(mesh);
    } else Mesh_Free(&result);
    Patch_FreeWeldMap(&oldMap);Patch_FreeWeldMap(&newMap);
    free(seam.triangles);
done:
    Patch_FreeLoops(&oldLoops);Patch_FreeLoops(&newLoops);
    if(output)*output=stats;
    return ok;
}

bool MeshPatch_Replace(Mesh* mesh,const uint8_t* removeTriangle,
    const Mesh* patch,float maximumGap,const MeshPatchValidation* check,
    MeshPatchStats* output) {
    return MeshPatch_ReplaceWithTransfer(mesh,removeTriangle,patch,
        maximumGap,check,output,NULL);
}
