#version 330 compatibility
uniform vec4 surfaceRecipe[82];
uniform uint pigmentSeed,furSeed;
uniform int surfaceDebug;
uniform bool guardCoverage;
in vec3 restCoord,restNormal,viewPosition,viewNormal,posedFlowOut,restFlowOut;
in vec4 vertexColor,profileA,profileB;
in float ventral,skinMask,fragShellFraction,fragIntegumentMask,shellDelta,ribbonSide,ribbonFade;
in vec3 worldPosition;
#include "morph_handoff.glsl"
in vec3 ownershipRoot,ownershipNormal;
#include "surface/morph_domain.glsl"
out vec4 fragColor;
#include "surface/hash.glsl"
#include "surface/pigment.glsl"
#include "surface/fur.glsl"
void main() {
    vec3 mappedCoord=restCoord,mappedNormal=restNormal;
    vec4 mappedA=profileA,mappedB=profileB;
    float mappedVentral=ventral;
    vec3 mappedFlow=posedFlowOut,mappedRestFlow=restFlowOut;
    if(morphMaterialCount>0) {
        vec3 flow;
        MorphMaterial(ownershipRoot,normalize(ownershipNormal),mappedCoord,mappedNormal,
            mappedA,mappedB,mappedVentral,flow);
        mappedFlow=normalize(morphWorldToView*flow);
        mappedRestFlow=vec3(0,0,-1);
    }
    if(!HandoffMeshKeep(ownershipRoot))discard;
    if(surfaceDebug==24) {fragColor=vec4(0,.7,1,1);return;}
    if(surfaceDebug==23) {fragColor=vec4(0.0,0.9,1.0,1.0);return;}
    float edge=1.0-smoothstep(.65,1.0,abs(ribbonSide));
    float alpha=edge*ribbonFade*fragIntegumentMask;
    if(alpha<.001)discard;
    float rootTip=mix(1.0-surfaceRecipe[11].x,1.0+surfaceRecipe[11].y,clamp(fragShellFraction*(.8+.4*shellDelta),0.0,1.0));
    vec3 pigment=EvaluatePigment(mappedCoord,mappedVentral,surfaceRecipe[3],pigmentSeed);
    vec3 color=FurLighting(pigment*rootTip*(.94+.12*shellDelta),mappedFlow,viewNormal,viewPosition,surfaceRecipe[10].z,surfaceRecipe[10].w,fragShellFraction);
    if(surfaceDebug==11||surfaceDebug==18)color=mappedFlow*.5+.5;
    if(surfaceDebug==21)color=vec3(ribbonFade);
    if(surfaceDebug==22)color=vec3(shellDelta);
    fragColor=vec4(guardCoverage?color:color*alpha,alpha);
}
