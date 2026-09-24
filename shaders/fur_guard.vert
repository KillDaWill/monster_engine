#version 330 compatibility
layout(location=0) in vec3 rootIndices;
layout(location=1) in vec3 rootBarycentric;
layout(location=2) in vec2 rootRandom;
layout(location=3) in vec3 rootCoord;
layout(location=4) in vec3 rootNormal;
layout(location=5) in vec3 rootFlow;
layout(location=6) in vec4 rootRegion;
layout(location=7) in float rootMask;
uniform samplerBuffer geometryData,surfaceData;
uniform vec4 surfaceRecipe[46];
uniform float guardVisibility;
uniform uint furSeed;
out vec3 restCoord,restNormal,viewPosition,viewNormal,posedFlowOut,restFlowOut;
out vec4 vertexColor,profileA,profileB;
out float ventral,skinMask,fragShellFraction,fragIntegumentMask,shellDelta;
out float ribbonSide,ribbonFade;
#include "surface/hash.glsl"
#include "surface/fur_curve.glsl"
vec3 Rest(int i) {return vec3(texelFetch(surfaceData,i*15).r,texelFetch(surfaceData,i*15+1).r,texelFetch(surfaceData,i*15+2).r);}
void main() {
    int ia=int(rootIndices.x),ib=int(rootIndices.y),ic=int(rootIndices.z);
    restCoord=rootCoord;restNormal=normalize(rootNormal);restFlowOut=normalize(rootFlow);
    vec4 ga=texelFetch(geometryData,ia*2),gb=texelFetch(geometryData,ib*2),gc=texelFetch(geometryData,ic*2);
    vec3 pa=ga.xyz,pb=gb.xyz,pc=gc.xyz;
    vec3 na=vec3(ga.w,texelFetch(geometryData,ia*2+1).xy),nb=vec3(gb.w,texelFetch(geometryData,ib*2+1).xy),nc=vec3(gc.w,texelFetch(geometryData,ic*2+1).xy);
    vec3 pos=pa*rootBarycentric.x+pb*rootBarycentric.y+pc*rootBarycentric.z;
    vec3 n=normalize(na*rootBarycentric.x+nb*rootBarycentric.y+nc*rootBarycentric.z);
    // Jacobiano del triángulo: transporta también el giro alrededor de la normal.
    vec3 e1=Rest(ib)-Rest(ia),e2=Rest(ic)-Rest(ia);
    float aa=dot(e1,e1),ab=dot(e1,e2),bb=dot(e2,e2),det=max(aa*bb-ab*ab,1e-16);
    vec2 coeff=vec2(dot(restFlowOut,e1)*bb-dot(restFlowOut,e2)*ab,dot(restFlowOut,e2)*aa-dot(restFlowOut,e1)*ab)/det;
    vec3 flow=(pb-pa)*coeff.x+(pc-pa)*coeff.y;flow=normalize(flow-n*dot(n,flow)+vec3(1e-9));
    int a=clamp(int(rootRegion.x+.5),0,10),b=clamp(int(rootRegion.y+.5),0,10);
    profileA=mix(surfaceRecipe[12+2*a],surfaceRecipe[12+2*b],rootRegion.z);
    profileB=mix(surfaceRecipe[13+2*a],surfaceRecipe[13+2*b],rootRegion.z);
    float belly=rootRegion.w*mix(float(a==3||a==4),float(b==3||b==4),rootRegion.z);
    profileA=mix(profileA,surfaceRecipe[20],belly);profileB=mix(profileB,surfaceRecipe[21],belly);
    vec4 p0=surfaceRecipe[8]*profileA;vec4 p1=surfaceRecipe[9]*vec4(1,profileB.z,profileB.w,profileB.x);
    float fieldRandom=FurFieldRandom(restCoord,restNormal,furSeed);
    float h=float(gl_VertexID/2)/4.0;
    float len=p0.x*surfaceRecipe[11].z;
    vec3 offset=FurCurveOffset(n,flow,len,p0.w,p1.x,p1.y,p1.z,fieldRandom,h);
    vec3 tangent=normalize(FurCurveDerivative(n,flow,p0.w,p1.x,p1.y,p1.z,fieldRandom,h));
    vec3 vp=(gl_ModelViewMatrix*vec4(pos+offset,1)).xyz;
    vec3 tv=normalize(gl_NormalMatrix*tangent);
    vec3 side=normalize(cross(tv,normalize(-vp))+vec3(1e-8));
    ribbonSide=float(gl_VertexID%2)*2.0-1.0;
    float width=.0015*p0.z*surfaceRecipe[11].z*(1.0-h)*(1.0-h);
    vp+=side*ribbonSide*width;
    float threshold=guardVisibility*clamp(p0.y*.25*surfaceRecipe[10].y*profileB.y,0.0,1.0);
    ribbonFade=smoothstep(rootRandom.x-.025,rootRandom.x+.025,threshold)*surfaceRecipe[11].w;
    if(threshold<=0.0)ribbonFade=0.0;
    gl_Position=gl_ProjectionMatrix*vec4(vp,1);
    viewPosition=vp;viewNormal=normalize(gl_NormalMatrix*n);posedFlowOut=tv;
    fragShellFraction=h;fragIntegumentMask=rootMask;skinMask=1;ventral=rootRegion.w;vertexColor=vec4(1);shellDelta=fieldRandom;
}
