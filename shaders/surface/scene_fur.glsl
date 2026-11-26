#include "../environment/scene_lighting.glsl"
#include "../environment/scene_material_lighting.glsl"
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
    vec3 g = normalize(mat3(sceneViewToWorld) * (gl_FrontFacing ? viewNormal : -viewNormal));
    float visible = sceneVisibility(world, g);
    float direct = visible * (.28 + .72 * max(dot(N, L), 0.0));
    vec3 color = sceneDecode(albedo);
    float ao = mix(.85, 1.0, h) * (1.0 - clamp(macroOcclusion, 0.0, .4));
    vec3 ambient = sceneDiffuseEnvironment(world, wn.y) * ao;
    vec3 sun = sceneSunRadiance.rgb * direct;
    vec3 spec = sun * (sheen * (vec3(.10) * primary + color * .18 * secondary) * shadow);
    spec += sceneFiberReflection(dot(N, V), roughness, sheen, ao);
    SceneLightResponse light = SceneLightResponse(sun * (.22 + .69 * diffuse * shadow + transmission),
        ambient, spec, visible);
    return sceneMaterialOutput(albedo, color, light, world, wn, g, ao, roughness, vec3(.25,.85,.44));
}
#define FurLighting SceneFurLighting
