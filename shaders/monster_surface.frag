#version 330 compatibility
uniform vec4 surfaceRecipe[28];
uniform uint pigmentSeed,scaleSeed;
uniform int surfaceDebug;
in vec3 restCoord,restNormal,viewPosition,viewNormal;
in vec4 vertexColor,profileA,profileB;
in float ventral,skinMask;
out vec4 fragColor;
#include "surface/hash.glsl"
#include "surface/scales.glsl"
#include "surface/pigment.glsl"
#include "surface/bump.glsl"
#include "surface/lighting.glsl"
#include "surface/evaluate.glsl"
void main() {
    vec3 n=normalize(viewNormal); if(!gl_FrontFacing)n=-n;
    SurfaceInput surfaceInput=SurfaceInput(restCoord,restNormal,viewPosition,n,vertexColor,profileA,profileB,ventral,skinMask);
    ScaleSample s; vec3 pigment;
    SurfaceResponse response=Surface_Evaluate(surfaceInput,surfaceDebug==6 || surfaceDebug==7,s,pigment);
    vec3 detailed=response.normal;
    vec3 color=SurfaceLighting(response,viewPosition);
    if(surfaceDebug==1)color=vec3(profileA.x*.4,profileA.y*.35,ventral);
    if(surfaceDebug==2)color=fract(restCoord);
    if(surfaceDebug==3)color=vec3(s.variation);
    if(surfaceDebug==4)color=vec3(s.height*.6+.2);
    if(surfaceDebug==5)color=vec3(s.edge);
    if(surfaceDebug==6)color=pigment;
    if(surfaceDebug==7)color=n*.5+.5;
    if(surfaceDebug==8)color=detailed*.5+.5;
    if(surfaceDebug==9)color=vec3(s.lod);
    fragColor=vec4(color,vertexColor.a);
}
