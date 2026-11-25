#include "../environment/scene_lighting.glsl"
vec3 SceneSurfaceLighting(SurfaceResponse s,vec3 viewPosition) {
    if (!sceneNatural()) return SurfaceLighting(s, viewPosition);
    vec3 world = sceneWorldPosition(viewPosition);
    vec3 n = normalize(s.normal);
    float up = (mat3(sceneViewToWorld) * n).y;
    vec3 light = sceneSunView.xyz;
    vec3 view = sceneEyeDirection(viewPosition);
    float cosine = max(dot(n, light), 0.0);
    float direct = cosine * sceneVisibility(world, mat3(sceneViewToWorld) * normalize(viewNormal));
    float gloss = pow(max(dot(n, normalize(light + view)), 0.0), mix(160.0, 5.0, s.roughness * s.roughness));
    return sceneObjectOutput(sceneDecode(s.albedo) * (sceneAmbient(up) + sceneSunRadiance.rgb * direct)
                         + sceneSunRadiance.rgb * (gloss * s.specular * direct * .55), world);
}
#define SurfaceLighting SceneSurfaceLighting
