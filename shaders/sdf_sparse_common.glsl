// Retícula global inmutable; halo de una celda en cada brick.
uniform uvec3 gridCells;
uniform vec3 gridOrigin;
uniform float gridStep;
uniform uint brickCells,jobCount,jobBase,pageCapacity,sampleSlotCapacity,sampleBankSize,generation;
uniform int refinementSteps,retireOnly,cachedNormals;
uniform uint edgeCapacity;
const uint PAGE_VERTICES=384u;
struct SurfaceVertex {vec4 position;vec4 normal;vec4 material;};
struct Brick {uint head,pages,count,sampleSlot,nearSurface,farAge,pad0,pad1;};
layout(std430,binding=1) buffer SampleBank0 {float stored0[];};
layout(std430,binding=12) buffer SampleVersions {uint sampleVersions[];};
layout(std430,binding=16) readonly buffer PointPages {uint pointPages[];};
layout(std430,binding=2) buffer Cases {uint cases[];};
layout(std430,binding=3) readonly buffer Tables {int mcTables[];};
layout(std430,binding=4) buffer Command {uint drawCount,instances,firstVertex,baseInstance,overflow,unused0,unused1,unused2,unused3,exactNormalCount,sampledCount;};
layout(std430,binding=5) buffer Vertices {SurfaceVertex vertices[];};
layout(std430,binding=6) buffer Scratch {float scratch[];};
layout(std430,binding=7) buffer Bricks {Brick bricks[];};
layout(std430,binding=8) readonly buffer Jobs {uvec4 jobs[];};
struct Draw {uint count,instanceCount,first,baseVertex,baseInstance;};
layout(std430,binding=9) buffer Draws {Draw draws[];};
layout(std430,binding=13) buffer Indices {uint indices[];};
struct Edge {uint key,version;float d0,d1;vec4 g0,g1;uvec4 statistics;};
layout(std430,binding=14) buffer Edges {Edge edges[];};
// Cabeceras y colas comparten un SSBO para respetar el límite de bloques.
layout(std430,binding=15) buffer WorkQueues {uint workQueue[];};
#define rootCount workQueue[0]
#define rootGroupsX workQueue[1]
#define rootGroupsY workQueue[2]
#define rootGroupsZ workQueue[3]
#define RootEdge(i) workQueue[4u+(i)]
#define retiredCount workQueue[4u+edgeCapacity]
#define retiredGroupsX workQueue[5u+edgeCapacity]
#define retiredGroupsY workQueue[6u+edgeCapacity]
#define retiredGroupsZ workQueue[7u+edgeCapacity]
#define RetiredEdge(i) workQueue[8u+edgeCapacity+(i)]
#define uniqueSampleCount workQueue[8u+2u*edgeCapacity]
#define sampleGroupsX workQueue[9u+2u*edgeCapacity]
#define sampleGroupsY workQueue[10u+2u*edgeCapacity]
#define sampleGroupsZ workQueue[11u+2u*edgeCapacity]
#define SampleOrdinal(i) workQueue[12u+2u*edgeCapacity+(i)]
layout(std430,binding=10) buffer Allocator {uint freePageCount,freeSampleCount,liveBricks,liveSamples;uint pool[];};
layout(std430,binding=11) buffer Links {uint nextPage[];};
uint side(){return brickCells+3u;}
uint sampleCount(){uint s=side();return s*s*s;}
uint cellCount(){return brickCells*brickCells*brickCells;}
uint localSample(uvec3 p){uint s=side();return p.x+s*(p.y+s*p.z);}
uint pointAddress(uvec3 p){
    uint bits=brickCells==8u?3u:4u;uvec3 page=p>>bits,counts=(gridCells>>bits)+uvec3(1);
    uint slot=pointPages[page.x+counts.x*(page.y+counts.y*page.z)];
    uvec3 local=p&(brickCells-1u);return (slot<<(3u*bits))+local.x+brickCells*(local.y+brickCells*local.z);
}
uint cornerIndex(uvec3 p){uvec3 s=gridCells+uvec3(1);return p.x+s.x*(p.y+s.y*p.z);}
uvec3 cornerOffset(int c){int b=4120+3*c;return uvec3(mcTables[b],mcTables[b+1],mcTables[b+2]);}
vec3 gridPoint(ivec3 p){return gridOrigin+vec3(p)*gridStep;}
uint invocation(){return (gl_WorkGroupID.y*gl_NumWorkGroups.x+gl_WorkGroupID.x)*gl_WorkGroupSize.x+gl_LocalInvocationID.x;}
float sampleValue(uint job,uvec3 p){return scratch[job*sampleCount()+localSample(p+uvec3(1))];}

void releasePages(uint slot){
    bricks[slot].pad1=generation;
    uint p=bricks[slot].head,expected=bricks[slot].pages;
    if(expected>pageCapacity){atomicOr(overflow,16u);return;}
    for(uint step=0u;step<expected;++step){
        if(p>=pageCapacity){atomicOr(overflow,16u);return;}
        uint n=nextPage[p],k=atomicAdd(freePageCount,1u);
        if(k>=pageCapacity){atomicOr(overflow,16u);return;}
        for(uint v=0u;v<draws[p].count;++v) {
            uint edge=indices[p*PAGE_VERTICES+v];
            if(edge>=edgeCapacity){atomicOr(overflow,16u);return;}
            uint before=atomicAdd(edges[edge].statistics.y,0xffffffffu);
            if(before==0u){atomicOr(overflow,16u);return;}
            if(before==1u) {
                atomicAdd(unused3,0xffffffffu);
                uint queued=atomicAdd(retiredCount,1u);
                if(queued>=edgeCapacity){atomicOr(overflow,16u);return;}
                RetiredEdge(queued)=edge;
            }
        }
        draws[p]=Draw(0,1,p*PAGE_VERTICES,0,0);pool[k]=p;p=n;
    }
    if(p!=0xffffffffu){atomicOr(overflow,16u);return;}
    if(bricks[slot].count>0u)atomicAdd(liveBricks,0xffffffffu);
    bricks[slot].head=0xffffffffu;bricks[slot].pages=0u;bricks[slot].count=0u;bricks[slot].nearSurface=0u;
}
void releaseSamples(uint slot){
    uint s=bricks[slot].sampleSlot;if(s==0xffffffffu)return;
    if(s>=sampleSlotCapacity){atomicOr(overflow,16u);return;}
    uint k=atomicAdd(freeSampleCount,1u);
    if(k>=sampleSlotCapacity){atomicOr(overflow,16u);return;}
    pool[pageCapacity+k]=s;atomicAdd(liveSamples,0xffffffffu);
    bricks[slot].sampleSlot=0xffffffffu;
}
uint reservePages(uint count){
    uint available=atomicAdd(freePageCount,0u),result=0xffffffffu;
    // Cada CAS fallido implica otra reserva del lote; no hay liberaciones simultáneas.
    // No se retiene un lock ni se espera publicación de otra lane: una reserva
    // terminada puede abandonar inmediatamente el bucle de CAS.
    for(uint attempt=0u;attempt<=jobCount&&result==0xffffffffu&&available>=count;++attempt){
        uint before=atomicCompSwap(freePageCount,available,available-count);
        if(before==available)result=available-count;else available=before;
    }
    if(result==0xffffffffu)atomicOr(overflow,1u);
    return result;
}
uint reserveSample(){
    uint available=atomicAdd(freeSampleCount,0u),result=0xffffffffu;
    for(uint attempt=0u;attempt<=jobCount&&result==0xffffffffu&&available>0u;++attempt){
        uint before=atomicCompSwap(freeSampleCount,available,available-1u);
        if(before==available)result=available-1u;else available=before;
    }
    if(result==0xffffffffu){atomicOr(overflow,2u);return result;}
    uint sampleSlot=pool[pageCapacity+result];
    if(sampleSlot>=sampleSlotCapacity){atomicOr(overflow,16u);return 0xffffffffu;}
    return sampleSlot;
}
