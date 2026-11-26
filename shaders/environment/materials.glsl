uniform bool uHasMaterials;
uniform int uMaterialPack;
uniform sampler2DArray uMaterialAlbedo, uMaterialNormal, uMaterialMasks;

struct GroundMaterial {
    vec3 color;
    vec3 gradient;
    vec2 masks;
};

vec2 unpackSlope(vec2 encoded) {
    vec2 xy = (encoded * 255.0 - 128.0) / 127.0;
    return xy * inversesqrt(max(0.09, 1.0 - dot(xy, xy)));
}

GroundMaterial mixedMaterial(GroundMaterial a, GroundMaterial b, float t) {
    return GroundMaterial(mix(a.color, b.color, t), mix(a.gradient, b.gradient, t), mix(a.masks, b.masks, t));
}

vec3 materialMean(int layer) {
    const vec3 means[6] = vec3[6](vec3(0.15411,0.20220,0.08246), vec3(0.20482,0.12590,0.07317),
        vec3(0.18722,0.18724,0.16257), vec3(0.10960,0.09459,0.06834), vec3(0.26327,0.21405,0.09463), vec3(0.19599,0.16264,0.10343));
    return means[layer];
}

float materialPeriod(int layer) {
    const float periods[6] = float[6](2.7, 2.1, 3.6, 1.65, 2.55, 2.4);
    return periods[layer];
}

vec3 materialNormal(vec3 n, vec3 gradient) {
    // Proyección del gradiente de relieve al plano tangente de la superficie.
    return normalize(n + gradient - n * dot(n, gradient));
}

vec3 illuminateGround(vec3 color, vec3 p, vec3 n, vec3 geometricNormal,
                      vec3 viewDir, float roughness, float ao) {
    SceneLightResponse light = sceneDielectric(p, n, geometricNormal, viewDir, roughness, .022, ao);
    return color * (light.sun + light.ambient) + light.specular;
}
