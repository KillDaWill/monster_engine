#version 330 compatibility
uniform vec4 surfaceRecipe[46];
uniform uint pigmentSeed,scaleSeed,furSeed;
uniform int surfaceDebug;
uniform float shellFraction;
uniform int furPass;
uniform int shellCount;

in vec3 restCoord,restNormal,viewPosition,viewNormal;
in vec4 vertexColor,profileA,profileB;
in float ventral,skinMask;
in vec3 posedFlowOut,restFlowOut;
in float shellDelta;
uniform float guardVisibility;
in float fragShellFraction;
in float fragIntegumentMask;

out vec4 fragColor;

#include "surface/hash.glsl"
#include "surface/scales.glsl"
#include "surface/pigment.glsl"
#include "surface/fur.glsl"
#include "surface/bump.glsl"
#include "surface/lighting.glsl"
#include "surface/evaluate.glsl"

void main() {
    vec3 n = normalize(viewNormal);
    if (!gl_FrontFacing) n = -n;

    int integumentType = int(surfaceRecipe[7].x + 0.5);

    // Ruta de pelaje mamífero procedural (INTEGUMENT_FUR = 2)
    if (integumentType == 2) {
        if(furPass==1 && (shellDelta<=.000001 || fragIntegumentMask<=.001 || skinMask<.5))discard;
        vec3 pigment = EvaluatePigment(restCoord, clamp(ventral, 0.0, 1.0), surfaceRecipe[3], pigmentSeed);

        if (furPass == 1) {
            // Pase de conchas exteriores (shell pass)
            if (fragIntegumentMask <= 0.001 || shellDelta<=.000001 || skinMask<.5) discard;

            FurSample fur = EvaluateFurStrands(
                restCoord, restNormal, n, posedFlowOut, fragShellFraction,
                surfaceRecipe[8] * vec4(profileA.x, profileA.y, profileA.z, profileA.w),
                surfaceRecipe[9] * vec4(1.0, profileB.z, profileB.w, profileB.x),
                surfaceRecipe[10] * vec4(1.0,profileB.y,1.0,1.0), furSeed);

            float cosine=abs(dot(n,normalize(-viewPosition)));
            float occupancy=fur.undercoat+fur.guard*(1.0-guardVisibility);
            float tau=32.0*surfaceRecipe[8].y*profileA.y*occupancy*shellDelta/max(.25,cosine);
            float furAlpha=(1.0-exp(-tau))*fragIntegumentMask*skinMask*surfaceRecipe[11].w;
            if(furAlpha<.0001)discard;

            float rootTip = mix(1.0 - surfaceRecipe[11].x, 1.0 + surfaceRecipe[11].y, clamp(fragShellFraction*(.8+.4*fur.fiberRandom),0.0,1.0));
            float fiberVar = 0.94 + 0.12 * fur.fiberRandom;
            vec3 albedo = pigment * (rootTip * fiberVar);

            float roughness = clamp(surfaceRecipe[10].z, 0.1, 1.0);
            float sheen = surfaceRecipe[10].w;
            vec3 color = FurLighting(albedo, fur.tangent, n, viewPosition, roughness, sheen, fragShellFraction);

            // Modos de depuración de pelaje
            if (surfaceDebug == 1) color = vec3(profileA.x * 0.4, profileA.y * 0.35, ventral);
            if (surfaceDebug == 2) color = fract(restCoord);
            if (surfaceDebug == 6) color = pigment;
            if (surfaceDebug == 7) color = n * 0.5 + 0.5;
            if (surfaceDebug == 11) color = posedFlowOut * 0.5 + 0.5;
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
            float grainFootprint = max(length(dFdx(restCoord)), length(dFdy(restCoord)));
            float grain = PigmentNoise(restCoord * 8.0, furSeed ^ 0x4d4943u);
            grain = mix(grain, 0.5, smoothstep(0.18, 0.70, grainFootprint));
            albedo *= 0.94 + 0.12 * grain;
            float furRoughness = clamp(surfaceRecipe[10].z, 0.2, 1.0);
            float sheen = surfaceRecipe[10].w;
            vec3 furTangent = normalize(mix(n, posedFlowOut, 0.72));
            vec3 color = FurLighting(albedo, furTangent, n, viewPosition, furRoughness, sheen * 0.30, 0.0);

            if (surfaceDebug == 1) color = vec3(profileA.x * 0.4, profileA.y * 0.35, ventral);
            if (surfaceDebug == 2) color = fract(restCoord);
            if (surfaceDebug == 6) color = pigment;
            if (surfaceDebug == 7) color = n * 0.5 + 0.5;
            if (surfaceDebug == 11) color = posedFlowOut * 0.5 + 0.5;
            if (surfaceDebug == 12) color = vec3(fragIntegumentMask);

            fragColor = vec4(color, vertexColor.a);
            return;
        }
    }

    // Ruta estándar para piel lisa y escamas (INTEGUMENT_SMOOTH_SKIN, INTEGUMENT_SCALES, INTEGUMENT_PLATES)
    SurfaceInput surfaceInput = SurfaceInput(restCoord, restNormal, viewPosition, n, vertexColor, profileA, profileB, ventral, skinMask);
    ScaleSample s;
    vec3 pigment;
    SurfaceResponse response = Surface_Evaluate(surfaceInput, surfaceDebug == 6 || surfaceDebug == 7, s, pigment);
    if (surfaceDebug == 10) { response.albedo = vec3(0.57); response.roughness = 0.65; response.specular = 0.12; }
    vec3 detailed = response.normal;
    vec3 color = SurfaceLighting(response, viewPosition);
    if (surfaceDebug == 1) color = vec3(profileA.x * 0.4, profileA.y * 0.35, ventral);
    if (surfaceDebug == 2) color = fract(restCoord);
    if (surfaceDebug == 3) color = vec3(s.variation);
    if (surfaceDebug == 4) color = vec3(s.height * 0.6 + 0.2);
    if (surfaceDebug == 5) color = vec3(s.edge);
    if (surfaceDebug == 6) color = pigment;
    if (surfaceDebug == 7) color = n * 0.5 + 0.5;
    if (surfaceDebug == 8) color = detailed * 0.5 + 0.5;
    if (surfaceDebug == 9) color = vec3(s.lod);
    fragColor = vec4(color, vertexColor.a);
}
