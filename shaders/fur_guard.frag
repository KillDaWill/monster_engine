#version 330 compatibility
uniform vec4 surfaceRecipe[46];
uniform uint pigmentSeed,furSeed;
uniform int surfaceDebug;
uniform bool guardCoverage;
in vec3 restCoord,restNormal,viewPosition,viewNormal,posedFlowOut,restFlowOut;
in vec4 vertexColor,profileA,profileB;
in float ventral,skinMask,fragShellFraction,fragIntegumentMask,shellDelta,ribbonSide,ribbonFade;
out vec4 fragColor;
#include "surface/hash.glsl"
#include "surface/pigment.glsl"
#include "surface/fur.glsl"
void main() {
    float edge=1.0-smoothstep(.65,1.0,abs(ribbonSide));
    float alpha=edge*ribbonFade*fragIntegumentMask;
    if(alpha<.001)discard;
    float rootTip=mix(1.0-surfaceRecipe[11].x,1.0+surfaceRecipe[11].y,clamp(fragShellFraction*(.8+.4*shellDelta),0.0,1.0));
    vec3 pigment=EvaluatePigment(restCoord,ventral,surfaceRecipe[3],pigmentSeed);
    vec3 color=FurLighting(pigment*rootTip*(.94+.12*shellDelta),posedFlowOut,viewNormal,viewPosition,surfaceRecipe[10].z,surfaceRecipe[10].w,fragShellFraction);
    if(surfaceDebug==11||surfaceDebug==18)color=posedFlowOut*.5+.5;
    if(surfaceDebug==21)color=vec3(ribbonFade);
    if(surfaceDebug==22)color=vec3(shellDelta);
    fragColor=vec4(guardCoverage?color:color*alpha,alpha);
}
