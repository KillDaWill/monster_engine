#version 330 compatibility
layout(location=8) in vec3 materialCoord;
layout(location=9) in vec3 materialNormal;
layout(location=10) in vec4 surfaceRegion;
layout(location=11) in float anatomicalMaterial;
layout(location=12) in vec3 restFlowDirection;
layout(location=13) in float vertexIntegumentMask;

uniform vec4 surfaceRecipe[46];
uniform float shellFraction;
uniform int furPass;
uniform int shellCount;
uniform float shellResolution;
uniform bool regionalShellLOD;
uniform vec2 shellSamples[32];
#include "surface/hash.glsl"
#include "surface/fur_curve.glsl"

out vec3 restCoord, restNormal, viewPosition, viewNormal;
out vec4 vertexColor, profileA, profileB;
out float ventral, skinMask;
out vec3 posedFlowOut,restFlowOut;
out float shellDelta;
out float fragShellFraction;
out float fragIntegumentMask;

vec3 RotateFromTo(vec3 fromDir, vec3 toDir, vec3 v) {
    vec3 c = cross(fromDir, toDir);
    float d = dot(fromDir, toDir);
    if (d < -0.9999) return -v;
    float k = 1.0 / (1.0 + d);
    return v + cross(c, v) + cross(c, cross(c, v)) * k;
}

void main() {
    restCoord = materialCoord;
    restNormal = materialNormal;
    vertexColor = gl_Color;
    float height=furPass==1?shellSamples[gl_InstanceID].x:0.0;
    shellDelta=furPass==1?shellSamples[gl_InstanceID].y:0.0;
    fragShellFraction = height;
    restFlowOut=restFlowDirection;
    fragIntegumentMask = vertexIntegumentMask;

    int a = clamp(int(surfaceRegion.x + 0.5), 0, 10);
    int b = clamp(int(surfaceRegion.y + 0.5), 0, 10);
    float blend = clamp(surfaceRegion.z, 0.0, 1.0);

    // Filas 12..33: Perfiles regionales (12 = 8 globales + 4 pelaje)
    profileA = mix(surfaceRecipe[12 + 2 * a], surfaceRecipe[12 + 2 * b], blend);
    profileB = mix(surfaceRecipe[13 + 2 * a], surfaceRecipe[13 + 2 * b], blend);

    float trunkA = float(a == 3 || a == 4), trunkB = float(b == 3 || b == 4);
    float belly = surfaceRegion.w * mix(trunkA, trunkB, blend);
    // Región 4 (VENTRAL_TRUNK) -> fila 12 + 2*4 = 20
    profileA = mix(profileA, surfaceRecipe[20], belly);
    profileB = mix(profileB, surfaceRecipe[21], belly);

    if(furPass==1 && regionalShellLOD) {
        float localResolution=clamp(shellResolution*profileA.x,4.0,float(shellCount));
        float low=exp2(floor(log2(localResolution))),high=min(float(shellCount),low*2.0);
        float t=clamp((localResolution-low)/low,0.0,1.0);
        float coarse=float(abs(height*low-round(height*low))<.01);
        float fine=float(abs(height*high-round(height*high))<.01);
        shellDelta=mix(coarse/low,fine/high,t)*min(1.0,shellResolution/4.0);
    }
    ventral = surfaceRegion.w;
    skinMask = 1.0 - step(0.5, anatomicalMaterial);

    vec3 nGeom = normalize(gl_Normal);
    vec3 nRest = normalize(materialNormal);
    vec3 posedFlow = RotateFromTo(nRest, nGeom, restFlowDirection);
    posedFlow -= nGeom * dot(posedFlow, nGeom);
    float flen = length(posedFlow);
    posedFlow = flen > 1e-4 ? posedFlow / flen : vec3(0.0, 0.0, -1.0);
    posedFlowOut = normalize(gl_NormalMatrix * posedFlow);

    if (furPass == 1 && height > 0.0) {
        float furLen = surfaceRecipe[8].x * profileA.x * surfaceRecipe[11].z;
        float lay = surfaceRecipe[8].w * profileA.w;
        float h = height;
        vec3 disp = FurCurveOffset(nGeom,posedFlow,furLen,lay,surfaceRecipe[9].x,0.0,0.0,.5,h) * vertexIntegumentMask;
        vec4 finalVertex = vec4(gl_Vertex.xyz + disp, 1.0);
        gl_Position = gl_ModelViewProjectionMatrix * finalVertex;
        viewPosition = (gl_ModelViewMatrix * finalVertex).xyz;
        viewNormal = normalize(gl_NormalMatrix * nGeom);
    } else {
        gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
        viewPosition = (gl_ModelViewMatrix * gl_Vertex).xyz;
        viewNormal = normalize(gl_NormalMatrix * gl_Normal);
    }
}
