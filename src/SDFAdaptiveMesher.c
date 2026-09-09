#include "SDFAdaptiveMesher.h"
#include "SDFSamplingPool.h"
#include "MonsterSDF.h"
#include "MathUtils.h"
#include <math.h>
#include <stdint.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

/* La retícula entera reserva un bit para centros de caras y celdas. Las
 * interfaces comparten muestras y aristas incluso entre niveles distintos. */
typedef struct AdaptivePoint { uint32_t p[3]; float distance; MeshIndex vertex; } AdaptivePoint;
typedef struct AdaptiveCell { uint32_t p[3], size, child; bool candidate; } AdaptiveCell;
typedef struct AdaptiveEdge { uint64_t key; MeshIndex vertex; bool occupied; } AdaptiveEdge;

struct SDFAdaptiveWorkspace {
    AdaptiveCell* cells;
    size_t cellCapacity;
    AdaptivePoint* points;
    size_t pointCapacity;
    uint32_t* pointTable;
    size_t pointTableCapacity;
    AdaptiveEdge* edges;
    size_t edgeCapacity;
};

SDFAdaptiveWorkspace* SDFAdaptiveWorkspace_Create(void) {
    return (SDFAdaptiveWorkspace*)calloc(1, sizeof(SDFAdaptiveWorkspace));
}

void SDFAdaptiveWorkspace_Free(SDFAdaptiveWorkspace* ws) {
    if (!ws) return;
    free(ws->cells);
    free(ws->points);
    free(ws->pointTable);
    free(ws->edges);
    free(ws);
}

typedef struct AdaptiveContext {
    const SDFField* field;
    const SDFDetailRegion* regions;
    size_t regionCount;
    Mesh* mesh;
    SDFMesherStats* stats;
    AABB3D bounds, boxes[2 * ANATOMY_MAX_CONNECTIONS + 64];
    size_t boxCount, budget, leaves;
    Vector3 origin;
    float unit, base, iso;
    uint32_t rootSize;
    AdaptiveCell* cells;
    size_t cellCount, cellCapacity;
    AdaptivePoint* points;
    size_t pointCount, pointCapacity;
    uint32_t* pointTable;
    size_t pointTableCapacity;
    AdaptiveEdge* edges;
    size_t edgeCount, edgeCapacity;
    bool (*shouldCancel)(void* cancelContext);
    void* cancelContext;
    bool failed, exhausted, cancelled;
} AdaptiveContext;

static uint64_t Adaptive_Hash(uint64_t x) {
    x ^= x >> 30; x *= UINT64_C(0xbf58476d1ce4e5b9);
    x ^= x >> 27; x *= UINT64_C(0x94d049bb133111eb);
    return x ^ (x >> 31);
}
static uint64_t Adaptive_PointKey(const uint32_t p[3]) {
    return p[0] | ((uint64_t)p[1] << 21) | ((uint64_t)p[2] << 42);
}
static bool Adaptive_Grow(void** ptr, size_t* capacity, size_t needed, size_t item) {
    if (needed <= *capacity) return true;
    size_t n;
    if (!Math_GrowCapacity(*capacity, needed, item, &n)) return false;
    void* fresh = realloc(*ptr, n * item);
    if (!fresh) return false;
    *ptr = fresh; *capacity = n; return true;
}
static Vector3 Adaptive_World(const AdaptiveContext* c, const uint32_t p[3]) {
    return Vec3_Add(c->origin, Vec3_Create(p[0]*c->unit, p[1]*c->unit, p[2]*c->unit));
}
static bool Adaptive_Overlaps(AABB3D a, AABB3D b) {
    return a.start.x<=b.end.x && a.end.x>=b.start.x &&
           a.start.y<=b.end.y && a.end.y>=b.start.y &&
           a.start.z<=b.end.z && a.end.z>=b.start.z;
}
static AABB3D Adaptive_Box(const AdaptiveContext* c, const AdaptiveCell* cell) {
    Vector3 lo=Adaptive_World(c,cell->p);
    float size=cell->size*c->unit;
    return (AABB3D){lo,Vec3_Add(lo,Vec3_Create(size,size,size))};
}
static bool Adaptive_RehashPoints(AdaptiveContext* c, size_t size) {
    uint32_t* table=calloc(size,sizeof(*table));
    if (!table) return false;
    for (size_t i=0;i<c->pointCount;++i) {
        size_t slot=Adaptive_Hash(Adaptive_PointKey(c->points[i].p))&(size-1);
        while(table[slot])slot=(slot+1)&(size-1);
        table[slot]=(uint32_t)i+1;
    }
    free(c->pointTable);c->pointTable=table;c->pointTableCapacity=size;return true;
}
static uint32_t Adaptive_Sample(AdaptiveContext* c, const uint32_t p[3]) {
    if(c->failed)return 0;
    if(!c->pointTableCapacity || (c->pointCount+1)*10>c->pointTableCapacity*7)
        if(!Adaptive_RehashPoints(c,c->pointTableCapacity?c->pointTableCapacity*2:4096)) {
            c->failed=true;return 0;
        }
    uint64_t key=Adaptive_PointKey(p);
    size_t slot=Adaptive_Hash(key)&(c->pointTableCapacity-1);
    while(c->pointTable[slot]) {
        uint32_t index=c->pointTable[slot]-1;
        if(Adaptive_PointKey(c->points[index].p)==key)return index;
        slot=(slot+1)&(c->pointTableCapacity-1);
    }
    if(c->pointCount>=UINT32_MAX ||
       !Adaptive_Grow((void**)&c->points,&c->pointCapacity,c->pointCount+1,sizeof(*c->points))) {
        c->failed=true;return 0;
    }
    Vector3 world=Adaptive_World(c,p);
    float d=c->field->evaluateDistance?c->field->evaluateDistance(c->field->context,world):
        c->field->evaluate(c->field->context,world).distance;
    c->stats->distanceEvaluationCount++;
    if(!isfinite(d)){c->failed=true;return 0;}
    if(fabsf(d-c->iso)<c->unit*1e-4f)d=c->iso;
    uint32_t index=(uint32_t)c->pointCount++;
    c->points[index]=(AdaptivePoint){{p[0],p[1],p[2]},d-c->iso,UINT32_MAX};
    c->pointTable[slot]=index+1;return index;
}

static void Adaptive_Build(AdaptiveContext* c, uint32_t index) {
    if(c->failed || c->exhausted || c->cancelled)return;
    if(c->shouldCancel && (c->cellCount & 0x7f) == 0 && c->shouldCancel(c->cancelContext)) {
        c->cancelled = true;
        return;
    }
    AdaptiveCell cell=c->cells[index];
    AABB3D box=Adaptive_Box(c,&cell);
    bool candidate=Adaptive_Overlaps(box,c->bounds);
    if(candidate && c->boxCount) {
        candidate=false;
        for(size_t i=0;i<c->boxCount && !candidate;++i)
            candidate=Adaptive_Overlaps(box,c->boxes[i]);
    }
    c->cells[index].candidate=candidate;
    if(!candidate){c->stats->skippedCellCount++;return;}
    if(c->field->getCellRange) {
        float lo,hi;
        if(c->field->getCellRange(c->field->context,box,&lo,&hi) &&
           (lo>c->iso || hi<c->iso)) {
            c->cells[index].candidate=false;c->stats->skippedCellCount++;return;
        }
    }
    float target=c->base;
    for(size_t r=0;r<c->regionCount;++r)
        if(isfinite(c->regions[r].targetVoxelSize) && c->regions[r].targetVoxelSize>0 &&
           Adaptive_Overlaps(box,c->regions[r].bounds))
            target=fminf(target,c->regions[r].targetVoxelSize);
    if(cell.size>2 && cell.size*c->unit>target*(1+1e-5f)) {
        size_t totalLeaves=1+7*((c->cellCount-1)/8);
        if(totalLeaves+7>c->budget){
            c->exhausted=true;
            c->leaves++;
            float step=cell.size*c->unit;
            c->stats->minimumVoxelSize=fminf(c->stats->minimumVoxelSize,step);
            c->stats->effectiveVoxelSize=fmaxf(c->stats->effectiveVoxelSize,step);
            return;
        }
        if(c->cellCount>UINT32_MAX-8 ||
           !Adaptive_Grow((void**)&c->cells,&c->cellCapacity,c->cellCount+8,sizeof(*c->cells))) {
            c->failed=true;return;
        }
        uint32_t first=(uint32_t)c->cellCount;c->cellCount+=8;
        c->cells[index].child=first;
        for(unsigned k=0;k<8;++k) {
            uint32_t half=cell.size/2;
            c->cells[first+k]=(AdaptiveCell){{cell.p[0]+((k&1)?half:0),
                cell.p[1]+((k&2)?half:0),cell.p[2]+((k&4)?half:0)},half,0,false};
        }
        for(unsigned k=0;k<8;++k)Adaptive_Build(c,first+k);
    } else {
        c->leaves++;
        float step=cell.size*c->unit;
        c->stats->minimumVoxelSize=fminf(c->stats->minimumVoxelSize,step);
        c->stats->effectiveVoxelSize=fmaxf(c->stats->effectiveVoxelSize,step);
        if(target<c->base*(1-1e-5f))c->stats->refinedCellCount++;
        for(size_t r=0;r<c->regionCount;++r)
            if(c->regions[r].targetVoxelSize>0 && Adaptive_Overlaps(box,c->regions[r].bounds))
                c->stats->detailSpacingRatio=fmaxf(c->stats->detailSpacingRatio,step/c->regions[r].targetVoxelSize);
    }
}

static uint32_t Adaptive_LeafSize(const AdaptiveContext* c, const int64_t p[3]) {
    for(int a=0;a<3;++a)if(p[a]<0 || p[a]>=c->rootSize)return c->rootSize;
    uint32_t i=0;
    while(c->cells[i].child) {
        const AdaptiveCell* cell=&c->cells[i];unsigned octant=0;
        for(int a=0;a<3;++a)if(p[a]>=cell->p[a]+cell->size/2)octant|=1u<<a;
        i=cell->child+octant;
    }
    return c->cells[i].size;
}
static bool Adaptive_RehashEdges(AdaptiveContext* c, size_t size) {
    AdaptiveEdge* table=calloc(size,sizeof(*table));if(!table)return false;
    for(size_t i=0;i<c->edgeCapacity;++i)if(c->edges[i].occupied) {
        size_t slot=Adaptive_Hash(c->edges[i].key)&(size-1);
        while(table[slot].occupied)slot=(slot+1)&(size-1);
        table[slot]=c->edges[i];
    }
    free(c->edges);c->edges=table;c->edgeCapacity=size;return true;
}
static MeshIndex Adaptive_Vertex(AdaptiveContext* c, uint32_t a, uint32_t b) {
    if(c->failed)return 0;
    if(a>b){uint32_t t=a;a=b;b=t;}
    float da=c->points[a].distance, db=c->points[b].distance;
    float t=da/(da-db);
    uint32_t snap=t<=1e-5f?a:t>=1-1e-5f?b:UINT32_MAX;
    if(snap!=UINT32_MAX && c->points[snap].vertex!=UINT32_MAX)return c->points[snap].vertex;
    if(!c->edgeCapacity || (c->edgeCount+1)*10>c->edgeCapacity*7)
        if(!Adaptive_RehashEdges(c,c->edgeCapacity?c->edgeCapacity*2:4096)) {
            c->failed=true;return 0;
        }
    uint64_t key=((uint64_t)a<<32)|b;
    size_t slot=Adaptive_Hash(key)&(c->edgeCapacity-1);
    while(c->edges[slot].occupied) {
        if(c->edges[slot].key==key)return c->edges[slot].vertex;
        slot=(slot+1)&(c->edgeCapacity-1);
    }
    if(snap!=UINT32_MAX)t=snap==a?0.0f:1.0f;
    Vector3 pa=Adaptive_World(c,c->points[a].p),pb=Adaptive_World(c,c->points[b].p);
    Vector3 pos=Vec3_Add(pa,Vec3_Scale(Vec3_Sub(pb,pa),t));
    MeshIndex vertex;
    if(!Mesh_AddVertex(c->mesh,(MeshVertex){pos,{0,0,0},COLOR_WHITE,SDF_MATERIAL_UNKNOWN},&vertex)) {
        c->failed=true;return 0;
    }
    c->edges[slot]=(AdaptiveEdge){key,vertex,true};c->edgeCount++;
    if(snap!=UINT32_MAX)c->points[snap].vertex=vertex;
    return vertex;
}
static void Adaptive_Triangle(AdaptiveContext* c, MeshIndex a, MeshIndex b, MeshIndex d, Vector3 outward) {
    if(c->failed || a==b || b==d || a==d)return;
    MeshVertex* v=c->mesh->vertices;
    if(!Mesh_TriangleHasArea(v[a].position,v[b].position,v[d].position))return;
    Vector3 normal=Vec3_Cross(Vec3_Sub(v[b].position,v[a].position),Vec3_Sub(v[d].position,v[a].position));
    if(Vec3_Dot(normal,outward)<0){MeshIndex t=b;b=d;d=t;normal=Vec3_Scale(normal,-1);}
    if(!Mesh_AddTriangle(c->mesh,a,b,d)){c->failed=true;return;}
    v[a].normal=Vec3_Add(v[a].normal,normal);
    v[b].normal=Vec3_Add(v[b].normal,normal);
    v[d].normal=Vec3_Add(v[d].normal,normal);
}
static void Adaptive_Tetra(AdaptiveContext* c, const uint32_t ids[4]) {
    unsigned in[4],out[4],ni=0,no=0;
    for(unsigned i=0;i<4;++i)
        if(c->points[ids[i]].distance<0)in[ni++]=ids[i];else out[no++]=ids[i];
    if(ni==0 || ni==4)return;
    Vector3 outward=Vec3_Sub(Adaptive_World(c,c->points[out[0]].p),Adaptive_World(c,c->points[in[0]].p));
    if(ni==1 || ni==3) {
        uint32_t singleton=ni==1?in[0]:out[0];unsigned* others=ni==1?out:in;
        MeshIndex a=Adaptive_Vertex(c,singleton,others[0]);
        MeshIndex b=Adaptive_Vertex(c,singleton,others[1]);
        MeshIndex d=Adaptive_Vertex(c,singleton,others[2]);
        Adaptive_Triangle(c,a,b,d,outward);
    } else {
        MeshIndex ac=Adaptive_Vertex(c,in[0],out[0]),ad=Adaptive_Vertex(c,in[0],out[1]);
        MeshIndex bc=Adaptive_Vertex(c,in[1],out[0]),bd=Adaptive_Vertex(c,in[1],out[1]);
        Adaptive_Triangle(c,ac,ad,bc,outward);Adaptive_Triangle(c,ad,bd,bc,outward);
    }
}

/* Cada arista mira las cuatro celdas incidentes. Así, dos caras perpendiculares
 * usan exactamente la misma partición aun con un salto de varios niveles. */
static bool Adaptive_EdgeSplit(const AdaptiveContext* c, const uint32_t a[3],
    const uint32_t b[3], int axis) {
    uint32_t midpoint[3];for(int k=0;k<3;++k)midpoint[k]=(a[k]+b[k])/2;
    uint32_t size=a[axis]>b[axis]?a[axis]-b[axis]:b[axis]-a[axis];
    int u=(axis+1)%3,v=(axis+2)%3;
    for(int i=-1;i<=1;i+=2)for(int j=-1;j<=1;j+=2) {
        int64_t p[3]={midpoint[0],midpoint[1],midpoint[2]};p[u]+=i;p[v]+=j;
        if(Adaptive_LeafSize(c,p)<size)return size>2;
    }
    return false;
}
static void Adaptive_FaceEdge(AdaptiveContext* c, uint32_t cellCenter, uint32_t faceCenter,
    const uint32_t a[3], const uint32_t b[3], int axis) {
    if(c->failed)return;
    uint32_t midpoint[3];for(int k=0;k<3;++k)midpoint[k]=(a[k]+b[k])/2;
    uint32_t size=a[axis]>b[axis]?a[axis]-b[axis]:b[axis]-a[axis];
    if(Adaptive_EdgeSplit(c,a,b,axis) && size>2) {
        Adaptive_FaceEdge(c,cellCenter,faceCenter,a,midpoint,axis);
        Adaptive_FaceEdge(c,cellCenter,faceCenter,midpoint,b,axis);
    } else {
        uint32_t ia=Adaptive_Sample(c,a),ib=Adaptive_Sample(c,b);
        if(c->failed)return;
        uint32_t tet[4]={cellCenter,faceCenter,ia,ib};Adaptive_Tetra(c,tet);
    }
}
static void Adaptive_Face(AdaptiveContext* c, uint32_t cellCenter, const uint32_t origin[3],
    uint32_t size, int normalAxis, int direction) {
    if(c->failed)return;
    int u=(normalAxis+1)%3,v=(normalAxis+2)%3;
    uint32_t center[3]={origin[0],origin[1],origin[2]};center[u]+=size/2;center[v]+=size/2;
    int64_t opposite[3]={center[0],center[1],center[2]};opposite[normalAxis]+=direction;
    if(size>2 && Adaptive_LeafSize(c,opposite)<size) {
        for(unsigned i=0;i<4;++i) {
            uint32_t sub[3]={origin[0],origin[1],origin[2]};
            sub[u]+=(i&1)?size/2:0;sub[v]+=(i&2)?size/2:0;
            Adaptive_Face(c,cellCenter,sub,size/2,normalAxis,direction);
        }
        return;
    }
    uint32_t corners[4][3];
    for(int i=0;i<4;++i)memcpy(corners[i],origin,3*sizeof(uint32_t));
    corners[1][u]+=size;corners[2][u]+=size;corners[2][v]+=size;corners[3][v]+=size;
    bool split=false;
    for(int i=0;i<4;++i)split|=Adaptive_EdgeSplit(c,corners[i],corners[(i+1)%4],i%2?v:u);
    if(!split) {
        uint32_t ids[4];for(int i=0;i<4;++i)ids[i]=Adaptive_Sample(c,corners[i]);
        if(c->failed)return;
        uint32_t t0[4]={cellCenter,ids[0],ids[1],ids[2]};
        uint32_t t1[4]={cellCenter,ids[0],ids[2],ids[3]};
        Adaptive_Tetra(c,t0);Adaptive_Tetra(c,t1);
    } else {
        uint32_t faceCenter=Adaptive_Sample(c,center);
        for(int i=0;i<4;++i)Adaptive_FaceEdge(c,cellCenter,faceCenter,corners[i],corners[(i+1)%4],i%2?v:u);
    }
}

static bool Adaptive_Regular(const AdaptiveContext* c, const AdaptiveCell* cell) {
    for(int axis=0;axis<3;++axis)for(int side=0;side<2;++side) {
        int64_t p[3]={cell->p[0]+cell->size/2,cell->p[1]+cell->size/2,cell->p[2]+cell->size/2};
        p[axis]=(int64_t)cell->p[axis]+(side?(int64_t)cell->size+1:-1);
        if(Adaptive_LeafSize(c,p)<cell->size)return false;
    }
    for(int axis=0;axis<3;++axis)for(unsigned side=0;side<4;++side) {
        uint32_t a[3]={cell->p[0],cell->p[1],cell->p[2]},b[3];
        a[(axis+1)%3]+=(side&1)?cell->size:0;
        a[(axis+2)%3]+=(side&2)?cell->size:0;
        memcpy(b,a,sizeof(b));b[axis]+=cell->size;
        if(Adaptive_EdgeSplit(c,a,b,axis))return false;
    }
    return true;
}
static void Adaptive_RegularCell(AdaptiveContext* c, const AdaptiveCell* cell) {
    uint32_t ids[8];
    for(unsigned i=0;i<8;++i) {
        uint32_t p[3];for(int axis=0;axis<3;++axis)p[axis]=cell->p[axis]+((i&(1u<<axis))?cell->size:0);
        ids[i]=Adaptive_Sample(c,p);
    }
    if(c->failed)return;
    unsigned signs=0;
    for(unsigned i=0;i<8;++i)if(c->points[ids[i]].distance<0)signs|=1u<<i;
    if(signs==0 || signs==255)return;
    static const unsigned tetra[6][4]={{0,1,3,7},{0,1,5,7},{0,2,3,7},{0,2,6,7},{0,4,5,7},{0,4,6,7}};
    for(int t=0;t<6;++t) {
        uint32_t corners[4];for(int i=0;i<4;++i)corners[i]=ids[tetra[t][i]];
        Adaptive_Tetra(c,corners);
    }
}

/* La topología se construye determinísticamente; los atributos independientes
 * reutilizan el mismo pool prestado o persistente de la ruta rectilínea. */
typedef struct AdaptiveAttributes {
    AdaptiveContext* context;
    size_t candidates[SDF_SAMPLING_POOL_MAX_THREADS];
    size_t exact[SDF_SAMPLING_POOL_MAX_THREADS];
    size_t pruned[SDF_SAMPLING_POOL_MAX_THREADS];
    bool failed[SDF_SAMPLING_POOL_MAX_THREADS];
} AdaptiveAttributes;
static void Adaptive_AttributeSlice(void* context,int start,int end,int thread) {
    AdaptiveAttributes* attrs=context;AdaptiveContext* c=attrs->context;
    MonsterSDF_EnableThreadStats(true);MonsterSDF_ResetThreadStats();
    for(int i=start;i<end;++i) {
        MeshVertex* vertex=&c->mesh->vertices[i];
        SDFSample sample=c->field->evaluate(c->field->context,vertex->position);
        if(!isfinite(sample.distance))attrs->failed[thread]=true;
        vertex->color=sample.color;vertex->material=sample.material;
        Vector3 n=vertex->normal;
        double length=sqrt((double)n.x*n.x+(double)n.y*n.y+(double)n.z*n.z);
        vertex->normal=length>0?Vec3_Scale(n,(float)(1/length)):Vec3_Create(0,1,0);
    }
    MonsterSDF_GetThreadStats(&attrs->candidates[thread],&attrs->exact[thread],&attrs->pruned[thread]);
    MonsterSDF_EnableThreadStats(false);
}
static void Adaptive_FinalizeAttributes(AdaptiveContext* c,SDFMesher* mesher) {
    if(c->mesh->vertexCount>INT_MAX){c->failed=true;return;}
    SDFSamplingPool* pool=NULL;
    if(mesher->config.samplingThreadCount!=1) {
        if(mesher->borrowedPool)pool=mesher->borrowedPool;
        else {
            if(!mesher->ownedPool)mesher->ownedPool=SDFSamplingPool_Create(mesher->config.samplingThreadCount);
            pool=mesher->ownedPool;
        }
    }
    AdaptiveAttributes attrs={0};attrs.context=c;
    if(pool)SDFSamplingPool_ParallelFor(pool,(int)c->mesh->vertexCount,Adaptive_AttributeSlice,&attrs);
    else Adaptive_AttributeSlice(&attrs,0,(int)c->mesh->vertexCount,0);
    c->stats->threadsUsed=pool?SDFSamplingPool_GetThreadCount(pool):1;
    for(int i=0;i<SDF_SAMPLING_POOL_MAX_THREADS;++i) {
        c->failed|=attrs.failed[i];
        c->stats->connectorCandidateCount+=attrs.candidates[i];
        c->stats->connectorExactEvaluationCount+=attrs.exact[i];
        c->stats->connectorPrunedCount+=attrs.pruned[i];
    }
    c->stats->fullSampleEvaluationCount=c->mesh->vertexCount;
}

bool SDFAdaptiveMesher_Generate(SDFMesher* mesher, const SDFField* field,
    const SDFDetailRegion* regions, size_t regionCount, Mesh* mesh) {
    if(!mesher || !field || !field->evaluate || !mesh || !regions || !regionCount ||
       !isfinite(mesher->config.voxelSize) || mesher->config.voxelSize<=0)return false;
    Mesh_Clear(mesh);memset(&mesher->lastStats,0,sizeof(mesher->lastStats));
    AdaptiveContext c={0};c.field=field;c.regions=regions;c.regionCount=regionCount;c.mesh=mesh;c.stats=&mesher->lastStats;
    c.bounds=mesher->config.useAutoBounds&&field->getBounds?field->getBounds(field->context):mesher->config.bounds;
    Vector3 extent=AABB_Size(c.bounds);
    if(!isfinite(extent.x)||!isfinite(extent.y)||!isfinite(extent.z)||extent.x<=0||extent.y<=0||extent.z<=0)return false;
    c.origin=c.bounds.start;c.base=mesher->config.voxelSize;c.iso=mesher->config.isolevel;
    c.budget=mesher->config.maxCells?mesher->config.maxCells:500000;
    c.shouldCancel=mesher->config.shouldCancel;
    c.cancelContext=mesher->config.cancelContext;

    /* Reutilización de espacio de trabajo persistente */
    if(!mesher->adaptiveWorkspace) {
        mesher->adaptiveWorkspace=SDFAdaptiveWorkspace_Create();
    }
    if(mesher->adaptiveWorkspace) {
        c.cells=mesher->adaptiveWorkspace->cells;
        c.cellCapacity=mesher->adaptiveWorkspace->cellCapacity;
        c.points=mesher->adaptiveWorkspace->points;
        c.pointCapacity=mesher->adaptiveWorkspace->pointCapacity;
        c.pointTable=mesher->adaptiveWorkspace->pointTable;
        c.pointTableCapacity=mesher->adaptiveWorkspace->pointTableCapacity;
        if(c.pointTable && c.pointTableCapacity) {
            memset(c.pointTable,0,c.pointTableCapacity*sizeof(*c.pointTable));
        }
        c.edges=mesher->adaptiveWorkspace->edges;
        c.edgeCapacity=mesher->adaptiveWorkspace->edgeCapacity;
        if(c.edges && c.edgeCapacity) {
            memset(c.edges,0,c.edgeCapacity*sizeof(*c.edges));
        }
    }

    float minTarget=c.base,span=fmaxf(extent.x,fmaxf(extent.y,extent.z));
    for(size_t i=0;i<regionCount;++i)
        if(isfinite(regions[i].targetVoxelSize)&&regions[i].targetVoxelSize>0)minTarget=fminf(minTarget,regions[i].targetVoxelSize);
    unsigned depth=0;float step=minTarget;
    while(step<span && depth<19){step*=2.0f;depth++;}
    if(step<span)return false;
    c.rootSize=1u<<(depth+1);c.unit=minTarget*.5f;
    if(field->getComponentBounds) {
        size_t capacity=sizeof(c.boxes)/sizeof(c.boxes[0]);
        c.boxCount=field->getComponentBounds(field->context,c.boxes,capacity);
        if(c.boxCount>=capacity)c.boxCount=0;
    }
    c.stats->minimumVoxelSize=INFINITY;c.stats->requestedVoxelSize=c.base;
    MonsterSDF_EnableThreadStats(true);MonsterSDF_ResetThreadStats();
    if(!Adaptive_Grow((void**)&c.cells,&c.cellCapacity,1,sizeof(*c.cells)))c.failed=true;
    if(!c.failed) {
        c.cells[0]=(AdaptiveCell){{0,0,0},c.rootSize,0,false};c.cellCount=1;
        Adaptive_Build(&c,0);
    }
    if(!c.failed && !c.cancelled)for(size_t i=0;i<c.cellCount && !c.failed;++i) {
        if((i & 0xff)==0 && c.shouldCancel && c.shouldCancel(c.cancelContext)) {
            c.cancelled=true;break;
        }
        AdaptiveCell cell=c.cells[i];if(cell.child || !cell.candidate)continue;
        size_t before=mesh->indexCount;
        if(cell.size==2 || Adaptive_Regular(&c,&cell))Adaptive_RegularCell(&c,&cell);
        else {
            uint32_t center[3]={cell.p[0]+cell.size/2,cell.p[1]+cell.size/2,cell.p[2]+cell.size/2};
            uint32_t id=Adaptive_Sample(&c,center);if(c.failed)break;
            for(int axis=0;axis<3;++axis)for(int side=0;side<2;++side) {
                uint32_t origin[3]={cell.p[0],cell.p[1],cell.p[2]};origin[axis]+=side?cell.size:0;
                Adaptive_Face(&c,id,origin,cell.size,axis,side?1:-1);
            }
        }
        if(mesh->indexCount>before)c.stats->activeCellCount++;
    }
    MonsterSDF_GetThreadStats(&c.stats->connectorCandidateCount,&c.stats->connectorExactEvaluationCount,&c.stats->connectorPrunedCount);
    MonsterSDF_EnableThreadStats(false);
    if(!c.failed && !c.cancelled)Adaptive_FinalizeAttributes(&c,mesher);
    c.stats->cellCount=c.cellCount?1+7*((c.cellCount-1)/8):0;c.stats->candidateCellCount=c.leaves;c.stats->candidateNodeCount=c.pointCount;
    c.stats->gridPointCount=c.pointCount;c.stats->fieldEvaluationCount=c.stats->distanceEvaluationCount+c.stats->fullSampleEvaluationCount;
    c.stats->generatedVertexCount=mesh->vertexCount;c.stats->generatedTriangleCount=mesh->indexCount/3;
    c.stats->resolutionX=(int)ceilf(extent.x/c.stats->minimumVoxelSize);
    c.stats->resolutionY=(int)ceilf(extent.y/c.stats->minimumVoxelSize);
    c.stats->resolutionZ=(int)ceilf(extent.z/c.stats->minimumVoxelSize);
    c.stats->voxelStep=Vec3_Create(c.stats->effectiveVoxelSize,c.stats->effectiveVoxelSize,c.stats->effectiveVoxelSize);
    c.stats->detailBudgetAdjusted=c.exhausted;c.stats->cellBudgetAdjusted=c.exhausted;

    /* Actualizar punteros en workspace */
    if(mesher->adaptiveWorkspace) {
        mesher->adaptiveWorkspace->cells=c.cells;
        mesher->adaptiveWorkspace->cellCapacity=c.cellCapacity;
        mesher->adaptiveWorkspace->points=c.points;
        mesher->adaptiveWorkspace->pointCapacity=c.pointCapacity;
        mesher->adaptiveWorkspace->pointTable=c.pointTable;
        mesher->adaptiveWorkspace->pointTableCapacity=c.pointTableCapacity;
        mesher->adaptiveWorkspace->edges=c.edges;
        mesher->adaptiveWorkspace->edgeCapacity=c.edgeCapacity;
    } else {
        free(c.cells);free(c.points);free(c.pointTable);free(c.edges);
    }
    if(c.failed || c.cancelled){Mesh_Clear(mesh);return false;}
    return true;
}
