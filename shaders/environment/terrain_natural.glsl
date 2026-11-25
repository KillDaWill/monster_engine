uniform int uMaterialTier;

vec3 naturalTerrain(vec3 p, vec3 n, vec3 viewDir, vec4 weights, float depth, float fertility,
                    vec3 dpdx, vec3 dpdy) {
    // Evita muestreos de peso despreciable y mantiene las transiciones continuas.
    float cutoff = .035;
    if (uMaterialTier < 2) {
        vec2 nearPair = vec2(max(weights.x, weights.y), min(weights.x, weights.y));
        vec2 farPair = vec2(max(weights.z, weights.w), min(weights.z, weights.w));
        float third = max(min(min(nearPair.x, farPair.x), max(nearPair.y, farPair.y)), min(nearPair.y, farPair.y));
        float peak = max(nearPair.x, farPair.x);
        cutoff = mix(cutoff, max(cutoff, third), smoothstep(.09,.19,peak - third));
    }
    vec4 w = max(vec4(0), weights - cutoff);
    w /= max(dot(w, vec4(1)), 0.001);
    vec3 base[4] = vec3[4](vec3(.100,.160,.043), vec3(.214,.132,.067),
                          vec3(.168,.143,.116), vec3(.125,.100,.065));
    float tile[4] = float[4](2.1, 3.0, 4.4, 2.8);
    vec3 color = vec3(0), gradient = vec3(0);
    vec2 masks = vec2(0);
    float pixelSpan = max(length(dpdx), length(dpdy));
    for (int i = 0; i < 4; ++i) {
        if (w[i] <= 0.0) continue;
        GroundMaterial m = GroundMaterial(base[i], vec3(0), vec2(.8,.94));
        float footprint = pixelSpan / tile[i];
        float textureWeight = 1.0 - smoothstep(.065, .19, footprint);
        if (uHasMaterials && textureWeight > 0.0) {
            bool details = uMaterialTier > 0 && footprint < .14;
            if (i == 2 && details) m = rockMaterial(p, n, dpdx, dpdy, true);
            else m = sampleProjection(p, dpdx, dpdy, vec3(1,0,0), vec3(0,0,1),
                                      float(i), 1.0 / tile[i], details);
            m.color = mix(base[i], m.color, textureWeight);
            m.gradient *= textureWeight * (1.0 - smoothstep(.06, .14, footprint));
        }
        color += w[i] * m.color;
        gradient += w[i] * m.gradient;
        masks += w[i] * m.masks;
    }
    float macro = vnoise(p.xz * 0.057) * 0.65
                + vnoise(vec2(p.z, -p.x) * 0.129) * 0.35;
    color *= mix(.76, 1.23, macro) * (1.0 - .12 * w.x * fertility);
    float soaked = smoothstep(0.015, 0.48, depth);
    color *= 1.0 - 0.23 * soaked;
    return illuminateGround(color, materialNormal(n, gradient * .72), viewDir,
                            masks.x - .14 * soaked, masks.y);
}
