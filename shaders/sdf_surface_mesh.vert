struct SurfaceVertex {vec4 position;vec4 normal;};
layout(std430,binding=5) readonly buffer Vertices {SurfaceVertex vertices[];};
layout(std430,binding=4) readonly buffer Command {uint drawCount,instances,firstVertex,baseInstance,overflow;};
out vec3 surfacePosition,surfaceNormal;
flat out uint extractionOverflow;
void main() {
    extractionOverflow=overflow;
    if(overflow!=0u) {vec2 p=vec2((gl_VertexID==1)?3:-1,(gl_VertexID==2)?3:-1);gl_Position=vec4(p,0,1);return;}
    SurfaceVertex v=vertices[gl_VertexID];surfacePosition=v.position.xyz;surfaceNormal=v.normal.xyz;
    gl_Position=gl_ModelViewProjectionMatrix*vec4(v.position.xyz,1);
}
