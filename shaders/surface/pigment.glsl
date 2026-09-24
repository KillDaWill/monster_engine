float SamplePigmentPattern(int pat, vec3 p, float scale, float sharpness, vec3 dir, uint seed) {
    vec3 q = p * max(scale, 0.01);
    float sh = clamp(sharpness, 0.0, 1.0);
    float noise = PigmentNoise(q, seed);
    if (pat == 0) {
        return 1.0;
    } else if (pat == 1) {
        return noise;
    } else if (pat == 2) {
        float w = 0.16 * (1.0 - sh * 0.75);
        return smoothstep(0.56 - w, 0.56 + w, noise);
    } else if (pat == 3) {
        float phase = float(seed & 1023u) * (6.2831853 / 1024.0);
        float wave = 0.5 + 0.5 * sin(q.z * 4.5 + phase + noise * 2.0);
        float w = 0.18 * (1.0 - sh * 0.8);
        return smoothstep(0.50 - w, 0.50 + w, wave);
    } else if (pat == 4) {
        float phase = float(seed & 1023u) * (6.2831853 / 1024.0);
        float wave = 0.5 + 0.5 * sin(q.x * 6.5 + phase + noise * 1.8);
        float w = 0.16 * (1.0 - sh * 0.8);
        return smoothstep(0.50 - w, 0.50 + w, wave);
    } else if (pat == 5) {
        float n1 = PigmentNoise(q * 0.40, seed);
        float n2 = PigmentNoise(q * 0.85, seed + 77u);
        float combined = n1 * 0.70 + n2 * 0.30;
        float w = 0.15 * (1.0 - sh * 0.8);
        return smoothstep(0.48 - w, 0.48 + w, combined);
    } else if (pat == 6) {
        float ringDist = abs(noise - 0.54);
        float wRing = 0.10 * (1.0 - sh * 0.6);
        float ring = 1.0 - smoothstep(0.0, wRing, ringDist);
        float center = 1.0 - smoothstep(0.0, 0.06, abs(noise - 0.72));
        return max(ring, center);
    } else if (pat == 7) {
        vec3 d = length(dir) > 0.001 ? normalize(dir) : vec3(0.0, -1.0, 0.0);
        float grad = dot(p, d) * scale;
        return clamp(grad * 0.5 + 0.5, 0.0, 1.0);
    }
    return 0.0;
}

vec3 EvaluatePigment(vec3 p, float underside, vec4 params, uint seed) {
    int layerCount = int(surfaceRecipe[7].z + 0.5);
    vec3 color;
    if (layerCount <= 0) {
        vec3 q = p * params.w;
        float footprint = max(length(dFdx(q)), length(dFdy(q)));
        float noise = PigmentNoise(q, seed);
        float bandWave = .5 + .5 * sin(q.z * 5.0 + noise * 3.0);
        float bands = smoothstep(.48, .7, bandWave);
        float spots = smoothstep(.48, .7, noise);
        float pattern = mix(spots, bands, surfaceRecipe[6].w);
        pattern = mix(pattern, .35, smoothstep(.3, 1.0, footprint));
        color = mix(surfaceRecipe[0].rgb, surfaceRecipe[1].rgb, pattern * params.z);
        color *= 1.0 - params.x * (1.0 - underside);
        return mix(color, surfaceRecipe[2].rgb, underside * params.y);
    }

    color = surfaceRecipe[0].rgb;
    for (int i = 0; i < 4; ++i) {
        if (i >= layerCount) break;
        vec4 layerCol = surfaceRecipe[30 + 3 * i];
        vec4 layerParam = surfaceRecipe[31 + 3 * i];
        vec4 layerDir = surfaceRecipe[32 + 3 * i];
        uint layerSeed = seed ^ uint(layerParam.w) ^ uint(i * 101);
        float sampleVal = SamplePigmentPattern(int(layerParam.x + 0.5), p, layerParam.y, layerParam.z, layerDir.xyz, layerSeed);
        float blend = clamp(sampleVal * layerCol.a, 0.0, 1.0);
        color = mix(color, layerCol.rgb, blend);
    }
    color *= 1.0 - params.x * (1.0 - underside);
    return mix(color, surfaceRecipe[2].rgb, underside * params.y);
}
