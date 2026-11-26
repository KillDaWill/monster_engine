#ifndef MONSTER_SCENE_MATERIAL_LIGHTING
#define MONSTER_SCENE_MATERIAL_LIGHTING
// Factores lineales de irradiancia relativa. sceneSunRadiance incluye el 1/pi
// de Lambert. La muestra del terreno guarda reflectancia y acceso estático al cielo.
vec3 sceneDiffuseEnvironment(vec3 world, float up) {
    if (sceneIrradianceMapping.w < .5) return sceneAmbient(up);
    vec2 uv = (world.xz - sceneIrradianceMapping.xy) * sceneIrradianceMapping.z;
    vec4 ground = textureLod(uSceneIrradiance, uv, 1.0);
    float upper = clamp(up * .5 + .5, 0.0, 1.0);
    vec3 relative = clamp(ground.rgb / .17, vec3(.55), vec3(1.55));
    vec3 reflected = sceneGroundFill.rgb * relative * mix(.67, 1.0, ground.a);
    vec3 lower = mix(sceneGroundFill.rgb, reflected, .46);
    return mix(lower, sceneSkyFill.rgb * mix(.74, 1.0, ground.a), upper);
}

float scenePow5(float x) { float x2 = x * x; return x2 * x2 * x; }

// El componente de cielo en fibras mates se ensancha al promediar hebras.
vec3 sceneFiberReflection(float viewCosine, float roughness, float sheen, float ao) {
    float grazing = 1.0 - clamp(abs(viewCosine), 0.0, 1.0);
    float f0 = mix(.010, .036, clamp(sheen, 0.0, 1.0));
    return sceneSkyFill.rgb * ((f0 + .055 * grazing * grazing) *
        mix(.23, .54, 1.0 - clamp(roughness, 0.0, 1.0)) * mix(.7, 1.0, ao));
}

struct SceneLightResponse {
    vec3 sun, ambient, specular;
    float visibility;
};

SceneLightResponse sceneDielectricVisible(vec3 world, vec3 n, vec3 v,
                                          float roughness, float f0, float ao, float visibility) {
    vec3 l = sceneSunWorld.xyz;
    vec3 h = (l + v) * inversesqrt(max(1e-6, dot(l + v, l + v)));
    float nl = max(0.0, dot(n, l));
    float nv = max(.005, dot(n, v));
    float nh = max(0.0, dot(n, h));
    float vh = clamp(dot(v, h), 0.0, 1.0);
    float r = clamp(roughness, .27, 1.0);
    float alpha = r * r;
    float alpha2 = alpha * alpha;
    float d = 1.0 + nh * nh * (alpha2 - 1.0);
    // Aproximación separable de Smith-Schlick para el lóbulo GGX.
    float k = .5 * alpha;
    float vis = .25 / max(.007, (nv * (1.0 - k) + k) * (nl * (1.0 - k) + k));
    float fresnel = f0 + (1.0 - f0) * scenePow5(1.0 - vh);
    vec3 sun = sceneSunRadiance.rgb * (visibility * nl);
    vec3 indirect = sceneDiffuseEnvironment(world, n.y) * ao;
    float reflectY = reflect(-v, n).y;
    vec3 reflected = mix(sceneGroundFill.rgb, sceneSkyFill.rgb, .5 + .5 * reflectY);
    float envF = f0 + (1.0 - f0) * scenePow5(1.0 - nv) * (1.0 - r);
    vec3 spec = sun * min(2.0, alpha2 * vis * fresnel / max(1e-6, d * d));
    spec += reflected * (envF * (1.0 - .7 * r) * .19) * mix(.7, 1.0, ao);
    return SceneLightResponse(sun * (1.0 - fresnel), indirect * (1.0 - f0), spec, visibility);
}

SceneLightResponse sceneDielectric(vec3 p, vec3 n, vec3 g, vec3 v, float r, float f0, float ao) {
    return sceneDielectricVisible(p,n,v,r,f0,ao,sceneVisibility(p,g));
}

vec3 sceneMaterialOutput(vec3 authored, vec3 base, SceneLightResponse light, vec3 world,
                         vec3 normal, vec3 geometric, float ao, float roughness, vec3 material) {
    vec3 linear = base * (light.sun + light.ambient) + light.specular;
    int mode = int(sceneExtras.w);
    if (mode == 1) return sceneDiagnostic(authored);
    if (mode == 2) return sceneDiagnostic(base);
    if (mode == 3) return sceneDiagnostic(linear);
    if (mode == 4) return sceneDiagnostic(geometric * .5 + .5);
    if (mode == 5) return sceneDiagnostic(normal * .5 + .5);
    if (mode == 6) return sceneOutput(light.sun);
    if (mode == 7) return sceneOutput(light.ambient);
    if (mode == 8) return sceneDiagnostic(vec3(light.visibility));
    if (mode == 9) return sceneDiagnostic(vec3(ao));
    if (mode == 10) return sceneOutput(light.specular);
    if (mode == 11) return sceneDiagnostic(vec3(roughness));
    if (mode == 12) return sceneDiagnostic(material);
    if (mode == 13) return sceneDiagnostic(vec3(sceneViewAxis.w * .5));
    return sceneObjectOutput(linear, world);
}

vec3 sceneOpaque(vec3 authored, vec3 world, vec3 n, vec3 g, vec3 view,
                 float roughness, float f0, float ao, vec3 material) {
    SceneLightResponse light = sceneDielectric(world, n, g, view, roughness, f0, ao);
    return sceneMaterialOutput(authored, sceneDecode(authored), light, world, n, g, ao, roughness, material);
}
#endif
