vec3 terrainPalette(vec3 p, vec3 n, vec3 viewDir, vec4 physical, float depth,
                    vec3 dpdx, vec3 dpdy, int debugMode) {
    if (debugMode == 12) return sceneDiagnostic(vec3(sceneVisibility(p, n)));
    vec2 fieldUV = (p.xz - uLandscapeMapping.xy) * uLandscapeMapping.z;
    vec4 climate = textureGrad(uLandscape, fieldUV, dpdx.xz * uLandscapeMapping.z, dpdy.xz * uLandscapeMapping.z);
    float patch = climate.a;
    float damp = max(smoothstep(.012,.25,depth), climate.z * physical.w * .88);
    float rock = smoothstep(.25, .92, physical.z + (patch - .48) * .28);
    float meadow = smoothstep(.29, .68, climate.x + (patch - .50) * .43);
    float dry = smoothstep(.12,.46, climate.y + (patch - .50) * .25);
    float ground = (1.0 - rock) * (1.0 - damp);
    float sediment = (1.0 - rock) * damp;
    float gravel = sediment * smoothstep(.13,.78,depth) * .77;
    vec4 a = vec4(ground * meadow, ground * (1.0 - meadow) * (1.0 - dry), rock, sediment - gravel);
    vec2 b = vec2(ground * (1.0 - meadow) * dry, gravel);
    // Se adelgazan sólo los pesos pequeños, sin seleccionar un ID discontinuo.
    a = max(a - .047, vec4(0));
    b = max(b - .047, vec2(0));
    float sum = dot(a, vec4(1)) + dot(b, vec2(1));
    a /= sum; b /= sum;
    if (debugMode == 11)
        return sceneDiagnostic(vec3(a.z + .45 * a.y, a.x + .55 * b.x, a.w + .6 * b.y));

    vec3 color = a.x * materialMean(0) + a.y * materialMean(1) + a.z * materialMean(2) +
                 a.w * materialMean(3) + b.x * materialMean(4) + b.y * materialMean(5);
    vec3 gradient = vec3(0);
    vec2 masks = vec2(.84,.96);
    float pixelSpan = max(length(dpdx), length(dpdy));
    // En la vista panorámica el ancho del píxel hace desaparecer el relieve
    // fino. El valor límite es exactamente la media lineal de cada mapa.
    float textureEnd = uMaterialTier == 2 ? .19 : .134;
    float textureFade = 1.0 - smoothstep(.049, textureEnd, pixelSpan);
    if (uHasMaterials && textureFade > .001) {
        float weights[6] = float[6](a.x, a.y, a.z, a.w, b.x, b.y);
        float largest = 0.0, runnerUp = 0.0;
        int layer = 0;
        for (int i = 0; i < 6; ++i) {
            float w = weights[i];
            if (w > largest) layer = i;
            runnerUp = max(runnerUp, min(largest, w));
            largest = max(largest, w);
        }
        float response = largest * textureFade * smoothstep(.02, .24, largest - runnerUp);
        if (response > .002) {
            float normalFade = 1.0 - smoothstep(.017, .069, pixelSpan);
            bool details = uMaterialTier > 0 && normalFade > .002;
            bool randomize = uMaterialTier > 0 && sceneExtras.x > .5;
            GroundMaterial tile;
            if (layer == 2 && n.y < .78 && uMaterialTier == 2)
                tile = rockMaterial(p, n, dpdx, dpdy, details, randomize);
            else
                tile = sampleProjection(p, dpdx, dpdy, vec3(1,0,0), vec3(0,0,1), layer, details, randomize);
            color += (tile.color - materialMean(layer)) * response;
            gradient = tile.gradient * (response * normalFade);
            masks += (tile.masks - vec2(.84,.96)) * response;
        }
    }
    return finishTerrain(p, n, viewDir, color, gradient, masks, depth, patch, damp, debugMode);
}
