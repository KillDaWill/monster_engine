// Retícula global inmutable; halo de una celda en cada brick.
uniform uvec3 gridCells;
uniform vec3 gridOrigin;
uniform float gridStep;
uniform uint brickCells,jobCount,jobBase,pageCapacity,sampleSlotCapacity,sampleBankSize,generation;
uniform int refinementSteps,retireOnly,cachedNormals;
uniform uint edgeCapacity;
const uint PAGE_VERTICES=384u;
struct SurfaceVertex {vec4 position;vec4 normal;};
struct Brick {uint head,pages,count,sampleSlot,nearSurface,farAge,pad0,pad1;};
layout(std430,binding=1) buffer SampleBank0 {float stored0[];};
layout(std430,binding=12) buffer SampleBank1 {float stored1[];};
layout(std430,binding=2) buffer Cases {uint cases[];};
layout(std430,binding=3) readonly buffer Tables {int mcTables[];};
layout(std430,binding=4) buffer Command {uint drawCount,instances,firstVertex,baseInstance,overflow,unused0,unused1,unused2,unused3,exactNormalCount;};
layout(std430,binding=5) buffer Vertices {SurfaceVertex vertices[];};
layout(std430,binding=6) buffer Scratch {float scratch[];};
layout(std430,binding=7) buffer Bricks {Brick bricks[];};
layout(std430,binding=8) readonly buffer Jobs {uvec4 jobs[];};
struct Draw {uint count,instanceCount,first,baseVertex,baseInstance;};
layout(std430,binding=9) buffer Draws {Draw draws[];};
layout(std430,binding=13) buffer Indices {uint indices[];};
struct Edge {uint key,version;float d0,d1;vec4 g0,g1;uvec4 statistics;};
layout(std430,binding=14) buffer Edges {Edge edges[];};
layout(std430,binding=10) buffer Allocator {uint freePageCount,freeSampleCount,liveBricks,liveSamples;uint pool[];};
layout(std430,binding=11) buffer Links {uint nextPage[];};
uint side(){return brickCells+3u;}
uint sampleCount(){uint s=side();return s*s*s;}
uint cellCount(){return brickCells*brickCells*brickCells;}
uint localSample(uvec3 p){uint s=side();return p.x+s*(p.y+s*p.z);}
uint cornerIndex(uvec3 p){uvec3 s=gridCells+uvec3(1);return p.x+s.x*(p.y+s.y*p.z);}
uvec3 cornerOffset(int c){int b=4120+3*c;return uvec3(mcTables[b],mcTables[b+1],mcTables[b+2]);}
vec3 gridPoint(ivec3 p){return gridOrigin+vec3(p)*gridStep;}
uint invocation(){return (gl_WorkGroupID.y*gl_NumWorkGroups.x+gl_WorkGroupID.x)*gl_WorkGroupSize.x+gl_LocalInvocationID.x;}
float sampleValue(uint job,uvec3 p){return scratch[job*sampleCount()+localSample(p+uvec3(1))];}
void storeValue(uint index,float value){if(index<sampleBankSize)stored0[index]=value;else stored1[index-sampleBankSize]=value;}
void releasePages(uint slot){
    bricks[slot].pad1=generation;
    uint p=bricks[slot].head,expected=bricks[slot].pages;
    if(expected>pageCapacity){atomicOr(overflow,16u);return;}
    for(uint step=0u;step<expected;++step){
        if(p>=pageCapacity){atomicOr(overflow,16u);return;}
        uint n=nextPage[p],k=atomicAdd(freePageCount,1u);
        if(k>=pageCapacity){atomicOr(overflow,16u);return;}
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
    // La salida uniforme del bucle evita depender del progreso de lanes divergentes.
    for(uint attempt=0u;attempt<=jobCount;++attempt){
        if(result==0xffffffffu&&available>=count){
            uint before=atomicCompSwap(freePageCount,available,available-count);
            if(before==available)result=available-count;else available=before;
        }
    }
    if(result==0xffffffffu)atomicOr(overflow,1u);
    return result;
}
uint reserveSample(){
    uint available=atomicAdd(freeSampleCount,0u),result=0xffffffffu;
    for(uint attempt=0u;attempt<=jobCount;++attempt){
        if(result==0xffffffffu&&available>0u){
            uint before=atomicCompSwap(freeSampleCount,available,available-1u);
            if(before==available)result=available-1u;else available=before;
        }
    }
    if(result==0xffffffffu){atomicOr(overflow,2u);return result;}
    uint sampleSlot=pool[pageCapacity+result];
    if(sampleSlot>=sampleSlotCapacity){atomicOr(overflow,16u);return 0xffffffffu;}
    return sampleSlot;
}
