// Contrato GPU: esquinas globales estables y tablas canónicas del núcleo.
uniform uvec3 gridCells;
uniform vec3 gridOrigin;
uniform float gridStep;
uniform uint vertexCapacity;
uniform int refinementSteps;
uniform uint sampleZOffset,cellOffset,sampleCornerBase,slabCellBase,slabCellCount,sampleEndZ;
layout(std430,binding=1) buffer Samples {float samples[];};
layout(std430,binding=2) buffer Cases {uvec2 activeCases[];};
layout(std430,binding=3) readonly buffer Tables {int mcTables[];};
layout(std430,binding=4) buffer Command {uint drawCount,instances,firstVertex,baseInstance,overflow,activeCount,dispatchX,dispatchY,dispatchZ;};
struct SurfaceVertex {vec4 position;vec4 normal;vec4 material;};
layout(std430,binding=5) buffer Vertices {SurfaceVertex vertices[];};
uint cornerIndex(uvec3 p) {uvec3 size=gridCells+uvec3(1);return p.x+size.x*(p.y+size.y*p.z);}
uint sampleIndex(uvec3 p) {return cornerIndex(p)-sampleCornerBase;}
uvec3 cellCoord(uint id) {return uvec3(id%gridCells.x,(id/gridCells.x)%gridCells.y,id/(gridCells.x*gridCells.y));}
uvec3 cornerOffset(int c) {int base=4096+24+3*c;return uvec3(mcTables[base],mcTables[base+1],mcTables[base+2]);}
vec3 gridPoint(uvec3 p){return gridOrigin+vec3(p)*gridStep;}
