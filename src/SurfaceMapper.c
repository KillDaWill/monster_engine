#include "SurfaceMapper.h"
#include <math.h>
#include <float.h>
#include <string.h>
#include <time.h>
static SurfaceRegion Region(const SurfaceMapping* map,AnatomyId id) {
    if(map)for(size_t i=0;i<map->tagCount && i<ANATOMY_MAX_NODES;++i)
        if(map->tags[i].node==id && map->tags[i].region<SURFACE_REGION_COUNT)return map->tags[i].region;
    return SURFACE_REGION_UNKNOWN;
}
typedef struct MapSegment { Vector3 a,ab; float inverseLength,ra,rb; SurfaceRegion aRegion,bRegion; } MapSegment;
typedef struct MapNode {
    Vector3 lo,hi;
    float maxRadius;
    float invMaxRadiusSq;
    int left,right;
    unsigned begin,count;
} MapNode;
typedef struct MapAcceleration {
    const MapSegment* segments;
    size_t count;
    unsigned order[ANATOMY_MAX_CONNECTIONS];
    MapNode nodes[ANATOMY_MAX_CONNECTIONS*2];
    unsigned nodeCount;
} MapAcceleration;

static double NowMs(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t);
    return (double)t.tv_sec*1000.0+(double)t.tv_nsec/1000000.0;
}
static size_t Compile(const AnatomyGraph* graph,const SurfaceMapping* map,MapSegment* segments) {
    size_t count=0;
    if(graph)for(size_t i=0;i<graph->connectionCount;++i) {
        if(graph->dormantConnections[i])continue;
        const BodyConnection* c=&graph->connections[i];
        const AnatomyNode* a=AnatomyGraph_FindNode(graph,c->fromId);
        const AnatomyNode* b=AnatomyGraph_FindNode(graph,c->toId);
        if(!a || !b)continue;
        Vector3 ab=Vec3_Sub(b->center,a->center); float len=Vec3_Dot(ab,ab);
        SurfaceRegion ar=Region(map,a->id),br=Region(map,b->id);
        if(c->kind==BODY_CONNECTION_DIGIT_SEGMENT)ar=br=SURFACE_REGION_DIGIT;
        segments[count++]=(MapSegment){a->center,ab,len>1e-10f?1/len:0,
            (a->widthRadius+a->heightRadius)*.5f,(b->widthRadius+b->heightRadius)*.5f,ar,br};
    }
    return count;
}

static float Axis(Vector3 p,int axis) { return axis==0?p.x:axis==1?p.y:p.z; }
static Vector3 End(const MapSegment* s) { return Vec3_Add(s->a,s->ab); }
static float CenterAxis(const MapSegment* s,int axis) {
    return .5f*(Axis(s->a,axis)+Axis(End(s),axis));
}
static int BuildNode(MapAcceleration* accel,unsigned begin,unsigned count) {
    int index=(int)accel->nodeCount++;
    MapNode* node=&accel->nodes[index];
    node->left=node->right=-1; node->begin=begin; node->count=count;
    Vector3 lo=Vec3_Create(FLT_MAX,FLT_MAX,FLT_MAX),hi=Vec3_Create(-FLT_MAX,-FLT_MAX,-FLT_MAX);
    float maxRadius=.001f;
    for(unsigned i=begin;i<begin+count;++i) {
        const MapSegment* s=&accel->segments[accel->order[i]]; Vector3 b=End(s);
        lo.x=fminf(lo.x,fminf(s->a.x,b.x));lo.y=fminf(lo.y,fminf(s->a.y,b.y));lo.z=fminf(lo.z,fminf(s->a.z,b.z));
        hi.x=fmaxf(hi.x,fmaxf(s->a.x,b.x));hi.y=fmaxf(hi.y,fmaxf(s->a.y,b.y));hi.z=fmaxf(hi.z,fmaxf(s->a.z,b.z));
        maxRadius=fmaxf(maxRadius,fmaxf(s->ra,s->rb));
    }
    node->lo=lo;node->hi=hi;node->maxRadius=maxRadius;
    node->invMaxRadiusSq=1.0f/(maxRadius*maxRadius);
    if(count<=4)return index;
    Vector3 extent=Vec3_Sub(hi,lo);int axis=extent.y>extent.x?1:0;if(Axis(extent,2)>Axis(extent,axis))axis=2;
    /* Sólo hay ~O(100) segmentos: inserción determinista evita estado global de qsort. */
    for(unsigned i=begin+1;i<begin+count;++i) {
        unsigned value=accel->order[i],j=i;float key=CenterAxis(&accel->segments[value],axis);
        while(j>begin && CenterAxis(&accel->segments[accel->order[j-1]],axis)>key) {
            accel->order[j]=accel->order[j-1];--j;
        }
        accel->order[j]=value;
    }
    unsigned leftCount=count/2;
    node->left=BuildNode(accel,begin,leftCount);
    node->right=BuildNode(accel,begin+leftCount,count-leftCount);
    node->count=0;
    return index;
}
static void BuildAcceleration(MapAcceleration* accel,const MapSegment* segments,size_t count) {
    memset(accel,0,sizeof(*accel));accel->segments=segments;accel->count=count;
    for(size_t i=0;i<count;++i)accel->order[i]=(unsigned)i;
    if(count)BuildNode(accel,0,(unsigned)count);
}
static inline float NodeLowerBound(const MapNode* node,Vector3 p) {
    float x=fmaxf(fmaxf(node->lo.x-p.x,p.x-node->hi.x),0);
    float y=fmaxf(fmaxf(node->lo.y-p.y,p.y-node->hi.y),0);
    float z=fmaxf(fmaxf(node->lo.z-p.z,p.z-node->hi.z),0);
    return (x*x+y*y+z*z)*node->invMaxRadiusSq;
}
static void ScoreSegment(Vector3 p,const MapSegment* s,float scores[SURFACE_REGION_COUNT]) {
    float x=p.x-s->a.x,y=p.y-s->a.y,z=p.z-s->a.z;
    float t=fmaxf(0,fminf(1,(x*s->ab.x+y*s->ab.y+z*s->ab.z)*s->inverseLength));
    x-=s->ab.x*t;y-=s->ab.y*t;z-=s->ab.z*t;
    float radius=fmaxf(s->ra+(s->rb-s->ra)*t,.001f);
    float score=(x*x+y*y+z*z)/(radius*radius);
    SurfaceRegion region=t<.5f?s->aRegion:s->bRegion;
    scores[region]=fminf(scores[region],score);
}
static float SecondBest(const float scores[SURFACE_REGION_COUNT]) {
    float first=FLT_MAX,second=FLT_MAX;
    for(int i=0;i<SURFACE_REGION_COUNT;++i) {
        float score=scores[i];if(score<first){second=first;first=score;}else if(score<second)second=score;
    }
    return second;
}
static SurfaceCoordinate Map(Vector3 p,Vector3 n,SDFMaterial material,
    const MapAcceleration* accel,const SurfaceMapping* map,size_t* candidateTests) {
    float unit=map && isfinite(map->unitScale) && map->unitScale>1e-6f?map->unitScale:1;
    SurfaceCoordinate out={.position=Vec3_Scale(Vec3_Sub(p,map?map->origin:Vec3_Zero()),1/unit),.normal=n};
    out.ventral=fmaxf(0,fminf(1,.5f-.7f*n.y));
    if(material!=SDF_MATERIAL_SKIN) { out.region=out.secondaryRegion=SURFACE_REGION_ORAL; return out; }
    if(!accel || !accel->count)return out;
    float scores[SURFACE_REGION_COUNT];
    for(int i=0;i<SURFACE_REGION_COUNT;++i)scores[i]=FLT_MAX;
    float secondBest=FLT_MAX;
    int stack[ANATOMY_MAX_CONNECTIONS*2];unsigned top=0;stack[top++]=0;
    while(top) {
        const MapNode* node=&accel->nodes[stack[--top]];
        if(NodeLowerBound(node,p)>secondBest)continue;
        if(node->count) {
            for(unsigned i=node->begin;i<node->begin+node->count;++i) {
                ScoreSegment(p,&accel->segments[accel->order[i]],scores);
                if(candidateTests)++*candidateTests;
            }
            secondBest=SecondBest(scores);
        } else {
            const MapNode* left=&accel->nodes[node->left],*right=&accel->nodes[node->right];
            float dl=NodeLowerBound(left,p),dr=NodeLowerBound(right,p);
            if(dl<dr){stack[top++]=node->right;stack[top++]=node->left;}
            else {stack[top++]=node->left;stack[top++]=node->right;}
        }
    }
    int first=0,second=0;
    for(int i=1;i<SURFACE_REGION_COUNT;++i)if(scores[i]<scores[first])first=i;
    second=first==0?1:0;
    for(int i=0;i<SURFACE_REGION_COUNT;++i)if(i!=first && scores[i]<scores[second])second=i;
    float gap=sqrtf(scores[second])-sqrtf(scores[first]);
    out.region=(float)first; out.secondaryRegion=(float)second;
    out.blend=isfinite(gap)?fmaxf(0,.5f-gap*2):0;
    return out;
}
SurfaceCoordinate SurfaceMapper_MapPoint(Vector3 p,Vector3 n,SDFMaterial material,
    const AnatomyGraph* graph,const SurfaceMapping* map) {
    MapSegment segments[ANATOMY_MAX_CONNECTIONS]; size_t count=Compile(graph,map,segments);
    MapAcceleration accel;BuildAcceleration(&accel,segments,count);
    return Map(p,n,material,&accel,map,NULL);
}
void SurfaceMapper_MapMesh(Mesh* mesh,const AnatomyGraph* graph,const SurfaceMapping* mapping) {
    SurfaceMapper_MapMeshWithStats(mesh,graph,mapping,NULL);
}
void SurfaceMapper_MapMeshWithStats(Mesh* mesh,const AnatomyGraph* graph,
    const SurfaceMapping* mapping,SurfaceMapperStats* stats) {
    if(!mesh)return;
    double start=NowMs();size_t tests=0;
    MapSegment segments[ANATOMY_MAX_CONNECTIONS]; size_t count=Compile(graph,mapping,segments);
    MapAcceleration accel;BuildAcceleration(&accel,segments,count);
    for(size_t i=0;i<mesh->vertexCount;++i) {
        MeshVertex* v=&mesh->vertices[i];
        v->surface=Map(v->position,v->normal,v->material,&accel,mapping,&tests);
    }
    Mesh_MarkSurfaceChanged(mesh);
    if(stats) {
        stats->verticesProcessed=mesh->vertexCount;stats->anatomySegmentCount=count;
        stats->candidateTests=tests;stats->bruteForceTests=mesh->vertexCount*count;
        stats->averageCandidatesPerVertex=mesh->vertexCount?(float)tests/(float)mesh->vertexCount:0;
        stats->durationMs=NowMs()-start;
    }
}


bool SurfaceMapper_MapBoundMesh(Mesh* mesh,const AnatomyDeformer* binder,
    const AnatomyGraph* canonical,const SurfaceMapping* mapping,SurfaceMapperStats* stats) {
    if(!mesh || !AnatomyDeformer_IsBound(binder) || !canonical ||
       binder->vertexCount!=mesh->vertexCount)return false;
    double start=NowMs();
    typedef struct MaterialTransform {Vector3 a,ab;Quaternion rotation;float ra,rb;SurfaceRegion region;} MaterialTransform;
    MaterialTransform transforms[ANATOMY_MAX_CONNECTIONS];
    for(size_t c=0;c<binder->refGraph.connectionCount;++c) {
        const BodyConnection* edge=&binder->refGraph.connections[c];
        const AnatomyNode* a=AnatomyGraph_FindNode(canonical,edge->fromId);
        const AnatomyNode* b=AnatomyGraph_FindNode(canonical,edge->toId);
        const AnatomyNode* ra=AnatomyGraph_FindNode(&binder->refGraph,edge->fromId);
        const AnatomyNode* rb=AnatomyGraph_FindNode(&binder->refGraph,edge->toId);
        if(!a || !b || !ra || !rb)return false;
        MaterialTransform* t=&transforms[c];t->a=a->center;t->ab=Vec3_Sub(b->center,a->center);
        t->rotation=Quat_FromTo(Vec3_Sub(rb->center,ra->center),t->ab);
        t->ra=(a->widthRadius+a->heightRadius)*.5f;t->rb=(b->widthRadius+b->heightRadius)*.5f;
        t->region=Region(mapping,edge->toId);
        if(edge->kind==BODY_CONNECTION_DIGIT_SEGMENT)t->region=SURFACE_REGION_DIGIT;
    }
    for(size_t v=0;v<mesh->vertexCount;++v) {
        const AnatomyDeformerBinding* b=&binder->bindings[v];
        Vector3 p=Vec3_Zero(),normal=Vec3_Zero();
        for(unsigned k=0;k<2;++k) {
            if(b->weight[k]<=0)continue;
            const MaterialTransform* t=&transforms[b->connIndex[k]];
            float radius=t->ra+(t->rb-t->ra)*b->projT[k];
            Vector3 offset=Quat_RotateVector(t->rotation,b->localOffset[k]);
            Vector3 q=Vec3_Add(Vec3_Add(t->a,Vec3_Scale(t->ab,b->projT[k])),
                Vec3_Scale(offset,radius/fmaxf(b->refRadius[k],1e-6f)));
            p=Vec3_Add(p,Vec3_Scale(q,b->weight[k]));
            normal=Vec3_Add(normal,Vec3_Scale(Quat_RotateVector(t->rotation,binder->baseNormals[v]),b->weight[k]));
        }
        SurfaceCoordinate* out=&mesh->vertices[v].surface;
        *out=(SurfaceCoordinate){.position=p,.normal=Vec3_Normalize(normal),
            .region=transforms[b->connIndex[0]].region,.secondaryRegion=transforms[b->connIndex[1]].region,
            .blend=b->weight[1]};
        out->ventral=fmaxf(0,fminf(1,.5f-.7f*out->normal.y));
        if(mesh->vertices[v].material!=SDF_MATERIAL_SKIN)out->region=out->secondaryRegion=SURFACE_REGION_ORAL;
    }
    Mesh_MarkSurfaceChanged(mesh);
    if(stats)*stats=(SurfaceMapperStats){.verticesProcessed=mesh->vertexCount,
        .anatomySegmentCount=binder->refGraph.connectionCount,.durationMs=NowMs()-start};
    return true;
}
