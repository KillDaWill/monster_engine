#include "../environment/scene_lighting.glsl"
vec3 SceneFurLighting(vec3 albedo,vec3 tangent,vec3 normal,vec3 viewPos,float roughness,float sheen,float h) {
    if (!sceneNatural()) return FurLighting(albedo, tangent, normal, viewPos, roughness, sheen, h);
    vec3 L=sceneSunView.xyz,V=sceneEyeDirection(viewPos),T=normalize(tangent),N=normalize(normal);
    vec3 H=normalize(L+V);
    float primary=pow(sqrt(max(0.0,1.0-pow(clamp(dot(T,H)+.08,-1.0,1.0),2.0))),mix(140.0,24.0,roughness));
    float secondary=pow(sqrt(max(0.0,1.0-pow(clamp(dot(T,H)-.20,-1.0,1.0),2.0))),mix(45.0,10.0,roughness));
    float diffuse=sqrt(max(0.0,1.0-pow(dot(T,L),2.0)));
    float shadow=exp(-.28*surfaceRecipe[8].y*(1.0-h)/max(.3,abs(dot(N,L))));
    float transmission=pow(max(0.0,dot(-L,V)),4.0)*(1.0-abs(dot(T,L)))*.12;
    vec3 world = sceneWorldPosition(viewPos);
    vec3 wn = mat3(sceneViewToWorld) * N;
    float direct = sceneVisibility(world, wn) * (.28 + .72 * max(dot(N, L), 0.0));
    vec3 color = sceneDecode(albedo);
    return sceneObjectOutput(color * sceneAmbient(wn.y) +
        sceneSunRadiance.rgb * direct * (color * (diffuse * shadow + transmission) +
        sheen * (vec3(.22) * primary + color * .25 * secondary) * shadow), world);
}
#define FurLighting SceneFurLighting
