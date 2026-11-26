uniform int uMaterialTier;
uniform sampler2D uLandscape;
uniform sampler2D uContactMap;
uniform vec3 uContactMapping;
uniform vec3 uLandscapeMapping; // origin xz, inverso de extensión

vec3 finishTerrain(vec3 p, vec3 n, vec3 viewDir, vec3 color, vec3 gradient, vec2 masks,
                   float depth, float patch, float damp, int debugMode, float visibility) {
    float soaked = smoothstep(.012,.56,depth);
    color *= mix(.84, 1.12, patch) * (1.0 - .22 * soaked);
    vec3 shadedNormal = materialNormal(n, gradient * .9);
    float contact = 0.0;
    if (uContactMapping.z > 0.0)
        contact = texture(uContactMap, (p.xz - uContactMapping.xy) * uContactMapping.z).r *
                  (1.0 - smoothstep(.008, .10, depth));
    float roughness = max(.48, masks.x - .22 * damp);
    if (debugMode == 8) return sceneOutput(color);
    if (debugMode == 9) return sceneDiagnostic(shadedNormal * .5 + .5);
    if (debugMode == 10) return sceneDiagnostic(vec3(roughness));
    SceneLightResponse light = sceneDielectricVisible(p, shadedNormal, viewDir, roughness, .022,
                                                      masks.y * (1.0 - contact * .74), visibility);
    return sceneObjectOutput(color * (light.sun + light.ambient) + light.specular, p);
}
