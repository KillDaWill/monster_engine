#version 330 compatibility
uniform vec4 surfaceRecipe[82];
uniform uint pigmentSeed,scaleSeed,furSeed;
uniform int surfaceDebug;
uniform float shellFraction;
uniform int furPass;
uniform int shellCount;

in vec3 restCoord,restNormal,viewPosition,viewNormal;
in vec4 vertexColor,profileA,profileB;
in float ventral,skinMask;
flat in int tissueMaterial;
in vec3 posedFlowOut,restFlowOut;
in float shellDelta;
uniform float guardVisibility;
in float fragShellFraction;
in float fragIntegumentMask;
in vec3 worldPosition;
#include "morph_handoff.glsl"
in vec3 ownershipRoot,ownershipNormal;
#include "surface/morph_domain.glsl"

out vec4 fragColor;

#include "surface/hash.glsl"
#include "surface/scales.glsl"
#include "surface/pigment.glsl"
#include "surface/fur.glsl"
#include "surface/bump.glsl"
#include "surface/lighting.glsl"
#include "surface/evaluate.glsl"

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
    if(surfaceDebug==24) {
        int owner;float coverage=HandoffCoverage(ownershipRoot,owner);
        fragColor=vec4(coverage>0.0&&coverage<1.0?vec3(1,1,0):vec3(0,.7,1),1);return;
    }
    if(surfaceDebug==23) {fragColor=vec4(0.0,0.9,1.0,1.0);return;}
    vec3 n = normalize(viewNormal);
    if (!gl_FrontFacing) n = -n;
    if(tissueMaterial==8||tissueMaterial==9){
        if(furPass==1)discard;
        SurfaceResponse keratin=SurfaceResponse(vertexColor.rgb,n,tissueMaterial==8?.38:.55,.30);
        fragColor=vec4(SurfaceLighting(keratin,viewPosition),vertexColor.a);return;
    }

    int integumentType = int(surfaceRecipe[7].x + 0.5);

    // Ruta de pelaje mamífero procedural (INTEGUMENT_FUR = 2)
    if (integumentType == 2) {
        if(furPass==1 && (shellDelta<=.000001 || fragIntegumentMask<=.001 || skinMask<.5))discard;
        vec3 pigment = EvaluatePigment(mappedCoord, clamp(mappedVentral, 0.0, 1.0), surfaceRecipe[3], pigmentSeed);

        if (furPass == 1) {
            // Pase de conchas exteriores (shell pass)
            if (fragIntegumentMask <= 0.001 || shellDelta<=.000001 || skinMask<.5) discard;

            FurSample fur = EvaluateFurStrands(
                mappedCoord, mappedNormal, n, mappedFlow, mappedRestFlow, fragShellFraction,
                surfaceRecipe[8] * vec4(mappedA.x, mappedA.y, mappedA.z, mappedA.w),
                surfaceRecipe[9] * vec4(1.0, mappedB.z, mappedB.w, mappedB.x),
                surfaceRecipe[10] * vec4(1.0,mappedB.y,1.0,1.0), furSeed);

            float cosine=abs(dot(n,normalize(-viewPosition)));
            float occupancy=fur.undercoat+fur.guard*(1.0-guardVisibility);
            float tau=32.0*surfaceRecipe[8].y*mappedA.y*occupancy*shellDelta/max(.25,cosine);
            float furAlpha=(1.0-exp(-tau))*fragIntegumentMask*skinMask*surfaceRecipe[11].w;
            if(furAlpha<.0001)discard;

            float rootTip = mix(1.0 - surfaceRecipe[11].x, 1.0 + surfaceRecipe[11].y, clamp(fragShellFraction*(.8+.4*fur.fiberRandom),0.0,1.0));
            float fiberVar = 0.94 + 0.12 * fur.fiberRandom;
            vec3 albedo = pigment * (rootTip * fiberVar);

            float roughness = clamp(surfaceRecipe[10].z, 0.1, 1.0);
            float sheen = surfaceRecipe[10].w;
            vec3 color = FurLighting(albedo, fur.tangent, n, viewPosition, roughness, sheen, fragShellFraction);

            // Modos de depuración de pelaje
            if (surfaceDebug == 1) color = vec3(mappedA.x * 0.4, mappedA.y * 0.35, mappedVentral);
            if (surfaceDebug == 2) color = fract(mappedCoord);
            if (surfaceDebug == 6) color = pigment;
            if (surfaceDebug == 7) color = n * 0.5 + 0.5;
            if (surfaceDebug == 11) color = mappedFlow * 0.5 + 0.5;
            if (surfaceDebug == 12) color = vec3(fragIntegumentMask);
            if (surfaceDebug == 13) color = vec3(fur.coverage);
            if (surfaceDebug == 14) color = vec3(fragShellFraction);
            if (surfaceDebug == 15) color = vec3(fur.undercoat);
            if (surfaceDebug == 16) color = vec3(fur.guard);
            if (surfaceDebug == 17) color = vec3(fur.undercoat, fur.guard, 0.0);

            if(surfaceDebug==18)color=fur.tangent*.5+.5;
            if(surfaceDebug==19)color=vec3(exp(-tau));
            if(surfaceDebug==20)color=vec3(float(shellCount)/32.0);
            if(surfaceDebug==21)color=vec3(guardVisibility);
            if(surfaceDebug==22)color=vec3(fur.fiberRandom);
            fragColor = vec4(color * furAlpha * vertexColor.a, furAlpha * vertexColor.a);
            return;
        } else {
            // Capa base de piel con micro-sombreado de pelaje
            float rootFactor = 1.0 - surfaceRecipe[11].x * 0.35;
            vec3 albedo = pigment * rootFactor;
            albedo = mix(vertexColor.rgb, albedo, clamp(skinMask, 0.0, 1.0));
            // El manto no puede depender únicamente de las conchas: a media
            // distancia las fibras son subpíxel. Este grano macroscópico y
            // filtrado mantiene dirección y variación sin dibujar ruido.
            float grainFootprint = max(length(dFdx(mappedCoord)), length(dFdy(mappedCoord)));
            float grain = PigmentNoise(mappedCoord * 8.0, furSeed ^ 0x4d4943u);
            grain = mix(grain, 0.5, smoothstep(0.18, 0.70, grainFootprint));
            albedo *= 0.94 + 0.12 * grain;
            float furRoughness = clamp(surfaceRecipe[10].z, 0.2, 1.0);
            float sheen = surfaceRecipe[10].w;
            vec3 furTangent = normalize(mix(n, mappedFlow, 0.72));
            vec3 color = FurLighting(albedo, furTangent, n, viewPosition, furRoughness, sheen * 0.30, 0.0);

            if (surfaceDebug == 1) color = vec3(mappedA.x * 0.4, mappedA.y * 0.35, mappedVentral);
            if (surfaceDebug == 2) color = fract(mappedCoord);
            if (surfaceDebug == 6) color = pigment;
            if (surfaceDebug == 7) color = n * 0.5 + 0.5;
            if (surfaceDebug == 11) color = mappedFlow * 0.5 + 0.5;
            if (surfaceDebug == 12) color = vec3(fragIntegumentMask);

            fragColor = vec4(color, vertexColor.a);
            return;
        }
    }

    // Ruta estándar para piel lisa y escamas (INTEGUMENT_SMOOTH_SKIN, INTEGUMENT_SCALES, INTEGUMENT_PLATES)
    SurfaceInput surfaceInput = SurfaceInput(mappedCoord, mappedNormal, viewPosition, n, vertexColor, mappedA, mappedB, mappedVentral, skinMask);
    ScaleSample s;
    vec3 pigment;
    SurfaceResponse response = Surface_Evaluate(surfaceInput, surfaceDebug == 6 || surfaceDebug == 7, s, pigment);
    if (surfaceDebug == 10) { response.albedo = vec3(0.57); response.roughness = 0.65; response.specular = 0.12; }
    vec3 detailed = response.normal;
    vec3 color = SurfaceLighting(response, viewPosition);
    if (surfaceDebug == 1) color = vec3(mappedA.x * 0.4, mappedA.y * 0.35, mappedVentral);
    if (surfaceDebug == 2) color = fract(mappedCoord);
    if (surfaceDebug == 3) color = vec3(s.variation);
    if (surfaceDebug == 4) color = vec3(s.height * 0.6 + 0.2);
    if (surfaceDebug == 5) color = vec3(s.edge);
    if (surfaceDebug == 6) color = pigment;
    if (surfaceDebug == 7) color = n * 0.5 + 0.5;
    if (surfaceDebug == 8) color = detailed * 0.5 + 0.5;
    if (surfaceDebug == 9) color = vec3(s.lod);
    fragColor = vec4(color, vertexColor.a);
}
