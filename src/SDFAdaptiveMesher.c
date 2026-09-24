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
typedef struct AdaptiveContour { MeshIndex a,b; Vector3 outward; bool used; } AdaptiveContour;

struct SDFAdaptiveWorkspace {
    AdaptiveCell* cells;
    size_t cellCapacity;
    AdaptivePoint* points;
    size_t pointCapacity;
    uint32_t* pointTable;
    size_t pointTableCapacity;
    AdaptiveEdge* edges;
    size_t edgeCapacity;
    AdaptiveContour* contours;
    size_t contourCapacity;
    MeshIndex* polygon;
    size_t polygonCapacity;
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
    free(ws->contours);
    free(ws->polygon);
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
    AdaptiveContour* contours;
    size_t contourCount,contourCapacity;
    MeshIndex* polygon;
    size_t polygonCapacity;
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
    /* El presupuesto limita divisiones, no la visita de octantes ya creados:
     * todos deben clasificarse para conservar la superficie completa. */
    if(c->failed || c->cancelled)return;
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
           Adaptive_Overlaps(box,c->regions[r].bounds)) {
            float local=c->regions[r].localTarget?c->regions[r].localTarget(c->regions[r].context,box,c->regions[r].targetVoxelSize):c->regions[r].targetVoxelSize;
            if(isfinite(local)&&local>0)target=fminf(target,local);
        }
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
    } else {
        c->leaves++;
        float step=cell.size*c->unit;
        c->stats->minimumVoxelSize=fminf(c->stats->minimumVoxelSize,step);
        c->stats->effectiveVoxelSize=fmaxf(c->stats->effectiveVoxelSize,step);
        if(target<c->base*(1-1e-5f))c->stats->refinedCellCount++;
        for(size_t r=0;r<c->regionCount;++r)
            if(c->regions[r].targetVoxelSize>0 && Adaptive_Overlaps(box,c->regions[r].bounds)) {
                float local=c->regions[r].localTarget?c->regions[r].localTarget(c->regions[r].context,box,c->regions[r].targetVoxelSize):c->regions[r].targetVoxelSize;
                if(isfinite(local)&&local>0)c->stats->detailSpacingRatio=fmaxf(c->stats->detailSpacingRatio,step/local);
            }
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
    if(!Mesh_AddVertex(c->mesh,(MeshVertex){.position=pos,.normal={0,0,0},.color=COLOR_WHITE,.material=SDF_MATERIAL_UNKNOWN},&vertex)) {
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
/* Los contornos de cada cara se comparten exactamente entre celdas. No se
 * crean cruces en diagonales interiores del cubo: cada bucle limita un parche. */
static void Adaptive_Contour(AdaptiveContext* c,MeshIndex a,MeshIndex b,Vector3 outward) {
    if(c->failed || a==b)return;
    if(!Adaptive_Grow((void**)&c->contours,&c->contourCapacity,c->contourCount+1,sizeof(*c->contours))) {
        c->failed=true;return;
    }
    c->contours[c->contourCount++]=(AdaptiveContour){a,b,outward,false};
}
static MeshIndex Adaptive_Crossing(AdaptiveContext* c,uint32_t a,uint32_t b,Vector3* outward) {
    Vector3 pa=Adaptive_World(c,c->points[a].p),pb=Adaptive_World(c,c->points[b].p);
    *outward=Vec3_Add(*outward,c->points[a].distance<0?Vec3_Sub(pb,pa):Vec3_Sub(pa,pb));
    return Adaptive_Vertex(c,a,b);
}
static void Adaptive_FaceTriangle(AdaptiveContext* c,uint32_t a,uint32_t b,uint32_t d) {
    uint32_t ids[3]={a,b,d};MeshIndex vertices[3];unsigned count=0;Vector3 outward={0};
    for(unsigned i=0;i<3;++i) {
        uint32_t u=ids[i],v=ids[(i+1)%3];
        if((c->points[u].distance<0)!=(c->points[v].distance<0))vertices[count++]=Adaptive_Crossing(c,u,v,&outward);
    }
    if(count==2)Adaptive_Contour(c,vertices[0],vertices[1],outward);
}
static void Adaptive_FaceEdge(AdaptiveContext* c,uint32_t faceCenter,
    const uint32_t a[3],const uint32_t b[3],int axis) {
    if(c->failed)return;
    uint32_t midpoint[3];for(int k=0;k<3;++k)midpoint[k]=(a[k]+b[k])/2;
    uint32_t size=a[axis]>b[axis]?a[axis]-b[axis]:b[axis]-a[axis];
    if(Adaptive_EdgeSplit(c,a,b,axis) && size>2) {
        Adaptive_FaceEdge(c,faceCenter,a,midpoint,axis);
        Adaptive_FaceEdge(c,faceCenter,midpoint,b,axis);
    } else {
        uint32_t ia=Adaptive_Sample(c,a),ib=Adaptive_Sample(c,b);
        if(!c->failed)Adaptive_FaceTriangle(c,faceCenter,ia,ib);
    }
}
static void Adaptive_Face(AdaptiveContext* c,const uint32_t origin[3],
    uint32_t size,int normalAxis,int direction) {
    if(c->failed)return;
    int u=(normalAxis+1)%3,v=(normalAxis+2)%3;
    uint32_t center[3]={origin[0],origin[1],origin[2]};center[u]+=size/2;center[v]+=size/2;
    int64_t opposite[3]={center[0],center[1],center[2]};opposite[normalAxis]+=direction;
    if(size>2 && Adaptive_LeafSize(c,opposite)<size) {
        for(unsigned i=0;i<4;++i) {
            uint32_t sub[3]={origin[0],origin[1],origin[2]};
            sub[u]+=(i&1)?size/2:0;sub[v]+=(i&2)?size/2:0;
            Adaptive_Face(c,sub,size/2,normalAxis,direction);
        }
        return;
    }
    uint32_t corners[4][3];
    for(int i=0;i<4;++i)memcpy(corners[i],origin,3*sizeof(uint32_t));
    corners[1][u]+=size;corners[2][u]+=size;corners[2][v]+=size;corners[3][v]+=size;
    bool split=false;
    for(int i=0;i<4;++i)split|=Adaptive_EdgeSplit(c,corners[i],corners[(i+1)%4],i%2?v:u);
    if(split) {
        uint32_t faceCenter=Adaptive_Sample(c,center);
        for(int i=0;i<4;++i)Adaptive_FaceEdge(c,faceCenter,corners[i],corners[(i+1)%4],i%2?v:u);
        return;
    }
    uint32_t ids[4];for(int i=0;i<4;++i)ids[i]=Adaptive_Sample(c,corners[i]);
    if(c->failed)return;
    MeshIndex crossing[4];unsigned count=0;Vector3 outward={0};
    for(int i=0;i<4;++i)if((c->points[ids[i]].distance<0)!=(c->points[ids[(i+1)%4]].distance<0))
        crossing[count++]=Adaptive_Crossing(c,ids[i],ids[(i+1)%4],&outward);
    if(count==2)Adaptive_Contour(c,crossing[0],crossing[1],outward);
    else if(count==4) {
        /* Decisor bilineal idéntico desde ambas celdas incidentes. */
        double determinant=(double)c->points[ids[0]].distance*c->points[ids[2]].distance-
            (double)c->points[ids[1]].distance*c->points[ids[3]].distance;
        if(determinant>=0) {
            Adaptive_Contour(c,crossing[0],crossing[1],outward);
            Adaptive_Contour(c,crossing[2],crossing[3],outward);
        } else {
            Adaptive_Contour(c,crossing[0],crossing[3],outward);
            Adaptive_Contour(c,crossing[1],crossing[2],outward);
        }
    }
}
static double Adaptive_Turn(Vector3 a,Vector3 b,Vector3 d,int axis) {
    double u[3]={a.x,a.y,a.z},v[3]={b.x,b.y,b.z},w[3]={d.x,d.y,d.z};
    int x=(axis+1)%3,y=(axis+2)%3;
    return (v[x]-u[x])*(w[y]-u[y])-(v[y]-u[y])*(w[x]-u[x]);
}
static bool Adaptive_BoundaryDiagonal(Vector3 a,Vector3 b,AABB3D box,float epsilon) {
    return (fabsf(a.x-box.start.x)<epsilon&&fabsf(b.x-box.start.x)<epsilon)||
        (fabsf(a.x-box.end.x)<epsilon&&fabsf(b.x-box.end.x)<epsilon)||
        (fabsf(a.y-box.start.y)<epsilon&&fabsf(b.y-box.start.y)<epsilon)||
        (fabsf(a.y-box.end.y)<epsilon&&fabsf(b.y-box.end.y)<epsilon)||
        (fabsf(a.z-box.start.z)<epsilon&&fabsf(b.z-box.start.z)<epsilon)||
        (fabsf(a.z-box.end.z)<epsilon&&fabsf(b.z-box.end.z)<epsilon);
}
static float Adaptive_Distance(AdaptiveContext* c,Vector3 p) {
    ++c->stats->distanceEvaluationCount;
    return (c->field->evaluateDistance?c->field->evaluateDistance(c->field->context,p):
        c->field->evaluate(c->field->context,p).distance)-c->iso;
}
static void Adaptive_InteriorFan(AdaptiveContext* c,size_t count,Vector3 outward,AABB3D box) {
    Vector3 center={0};
    for(size_t i=0;i<count;++i)center=Vec3_Add(center,c->mesh->vertices[c->polygon[i]].position);
    center=Vec3_Scale(center,1.0f/count);
    float size=box.end.x-box.start.x,epsilon=size*1e-4f,delta=size*.01f;
    for(int iteration=0;iteration<5;++iteration) {
        center.x=Math_Clamp(center.x,box.start.x+epsilon,box.end.x-epsilon);
        center.y=Math_Clamp(center.y,box.start.y+epsilon,box.end.y-epsilon);
        center.z=Math_Clamp(center.z,box.start.z+epsilon,box.end.z-epsilon);
        float d=Adaptive_Distance(c,center);
        Vector3 gradient=Vec3_Create(
            Adaptive_Distance(c,Vec3_Add(center,Vec3_Create(delta,0,0)))-Adaptive_Distance(c,Vec3_Add(center,Vec3_Create(-delta,0,0))),
            Adaptive_Distance(c,Vec3_Add(center,Vec3_Create(0,delta,0)))-Adaptive_Distance(c,Vec3_Add(center,Vec3_Create(0,-delta,0))),
            Adaptive_Distance(c,Vec3_Add(center,Vec3_Create(0,0,delta)))-Adaptive_Distance(c,Vec3_Add(center,Vec3_Create(0,0,-delta))));
        float length=Vec3_LengthSq(gradient);
        if(length<1e-16f)break;
        center=Vec3_Sub(center,Vec3_Scale(gradient,d*2*delta/length));
    }
    center.x=Math_Clamp(center.x,box.start.x+epsilon,box.end.x-epsilon);
    center.y=Math_Clamp(center.y,box.start.y+epsilon,box.end.y-epsilon);
    center.z=Math_Clamp(center.z,box.start.z+epsilon,box.end.z-epsilon);
    MeshIndex vertex;
    if(!Mesh_AddVertex(c->mesh,(MeshVertex){.position=center,.normal={0},.color=COLOR_WHITE,.material=SDF_MATERIAL_UNKNOWN},&vertex)) { c->failed=true;return; }
    for(size_t i=0;i<count;++i)Adaptive_Triangle(c,vertex,c->polygon[i],c->polygon[(i+1)%count],outward);
}
static void Adaptive_Polygon(AdaptiveContext* c,size_t count,Vector3 outward,AABB3D box) {
    if(count<3)return;
    MeshVertex* vertices=c->mesh->vertices;Vector3 normal={0};
    for(size_t i=0;i<count;++i) {
        Vector3 a=vertices[c->polygon[i]].position,b=vertices[c->polygon[(i+1)%count]].position;
        normal=Vec3_Add(normal,Vec3_Cross(a,b));
    }
    int axis=fabsf(normal.x)>fabsf(normal.y)?0:1;
    if(fabsf(normal.z)>(axis==0?fabsf(normal.x):fabsf(normal.y)))axis=2;
    double sign=(axis==0?normal.x:axis==1?normal.y:normal.z)>=0?1:-1;
    if(Vec3_LengthSq(outward)<1e-16f)outward=normal;
    while(count>3) {
        bool clipped=false;
        for(size_t i=0;i<count;++i) {
            size_t before=(i+count-1)%count,after=(i+1)%count;
            Vector3 a=vertices[c->polygon[before]].position,b=vertices[c->polygon[i]].position,d=vertices[c->polygon[after]].position;
            /* Una diagonal nueva nunca puede vivir en una cara compartida:
             * la celda vecina podría emitirla también y duplicar tejido. */
            if(Adaptive_BoundaryDiagonal(a,d,box,c->unit*.001f))continue;
            if(sign*Adaptive_Turn(a,b,d,axis)<=1e-18)continue;
            bool contains=false;
            for(size_t j=0;j<count&&!contains;++j)if(j!=before&&j!=i&&j!=after) {
                Vector3 p=vertices[c->polygon[j]].position;
                contains=sign*Adaptive_Turn(a,b,p,axis)>1e-18&&sign*Adaptive_Turn(b,d,p,axis)>1e-18&&sign*Adaptive_Turn(d,a,p,axis)>1e-18;
            }
            if(contains)continue;
            Adaptive_Triangle(c,c->polygon[before],c->polygon[i],c->polygon[after],outward);
            memmove(c->polygon+i,c->polygon+i+1,(count-i-1)*sizeof(*c->polygon));--count;clipped=true;break;
        }
        if(!clipped) {
            /* Sólo los bucles plegados requieren un vértice interior, proyectado
             * al campo y separado de las caras para conservar su incidencia. */
            Adaptive_InteriorFan(c,count,outward,box);
            return;
        }
    }
    Adaptive_Triangle(c,c->polygon[0],c->polygon[1],c->polygon[2],outward);
}
static void Adaptive_ContourCell(AdaptiveContext* c,const AdaptiveCell* cell) {
    c->contourCount=0;
    for(int axis=0;axis<3;++axis)for(int side=0;side<2;++side) {
        uint32_t origin[3]={cell->p[0],cell->p[1],cell->p[2]};origin[axis]+=side?cell->size:0;
        Adaptive_Face(c,origin,cell->size,axis,side?1:-1);
    }
    if(!Adaptive_Grow((void**)&c->polygon,&c->polygonCapacity,c->contourCount+1,sizeof(*c->polygon))) { c->failed=true;return; }
    for(size_t start=0;start<c->contourCount;++start)if(!c->contours[start].used) {
        AdaptiveContour* edge=&c->contours[start];edge->used=true;
        size_t count=1;c->polygon[0]=edge->a;MeshIndex current=edge->b;Vector3 outward=edge->outward;
        while(current!=c->polygon[0] && count<=c->contourCount) {
            c->polygon[count++]=current;bool found=false;
            for(size_t j=0;j<c->contourCount;++j) {
                AdaptiveContour* next=&c->contours[j];
                if(next->used || (next->a!=current&&next->b!=current))continue;
                current=next->a==current?next->b:next->a;next->used=true;outward=Vec3_Add(outward,next->outward);found=true;break;
            }
            if(!found) { c->failed=true;return; }
        }
        Adaptive_Polygon(c,count,outward,Adaptive_Box(c,cell));
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
    if(!mesher || !field || !field->evaluate || !mesh || (regionCount && !regions) ||
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
        c.contours=mesher->adaptiveWorkspace->contours;
        c.contourCapacity=mesher->adaptiveWorkspace->contourCapacity;
        c.polygon=mesher->adaptiveWorkspace->polygon;
        c.polygonCapacity=mesher->adaptiveWorkspace->polygonCapacity;
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
        /* Orden por anchura: se completa la retícula corporal antes de gastar
         * el presupuesto en detalles locales, sin privilegiar un octante. */
        for(size_t i=0;i<c.cellCount && !c.failed && !c.cancelled;++i)
            Adaptive_Build(&c,(uint32_t)i);
    }
    if(!c.failed && !c.cancelled)for(size_t i=0;i<c.cellCount && !c.failed;++i) {
        if((i & 0xff)==0 && c.shouldCancel && c.shouldCancel(c.cancelContext)) {
            c.cancelled=true;break;
        }
        AdaptiveCell cell=c.cells[i];if(cell.child || !cell.candidate)continue;
        size_t before=mesh->indexCount;
        Adaptive_ContourCell(&c,&cell);
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
        mesher->adaptiveWorkspace->contours=c.contours;
        mesher->adaptiveWorkspace->contourCapacity=c.contourCapacity;
        mesher->adaptiveWorkspace->polygon=c.polygon;
        mesher->adaptiveWorkspace->polygonCapacity=c.polygonCapacity;
    } else {
        free(c.cells);free(c.points);free(c.pointTable);free(c.edges);
        free(c.contours);free(c.polygon);
    }
    if(c.failed || c.cancelled){Mesh_Clear(mesh);return false;}
    return true;
}
