#include "../environment/scene_lighting.glsl"
#include "../environment/scene_material_lighting.glsl"
vec3 SceneSurfaceLighting(SurfaceResponse s,vec3 viewPosition) {
    if (!sceneNatural()) return SurfaceLighting(s, viewPosition);
    vec3 world = sceneWorldPosition(viewPosition);
    vec3 n = normalize(mat3(sceneViewToWorld) * s.normal);
    vec3 g = normalize(mat3(sceneViewToWorld) * (gl_FrontFacing ? viewNormal : -viewNormal));
    float f0 = mix(.012, .060, clamp(s.specular * 2.0, 0.0, 1.0));
    return sceneOpaque(s.albedo, world, n, g, sceneViewDirection(world), s.roughness, f0,
                       1.0-clamp(macroOcclusion,0.0,.4),
                       vec3(.90, .23 + .05 * float(tissueMaterial), .19));
}
#define SurfaceLighting SceneSurfaceLighting
