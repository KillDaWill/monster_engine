vec3 tileHash(ivec2 p, int layer) {
    uvec2 n = uvec2(p);
    uint h = n.x * 0x8da6b343u ^ n.y * 0xd8163841u ^ uint(layer + 17) * 0xcb1ab31fu ^ uint(sceneSunWorld.w);
    h ^= h >> 16u; h *= 0x7feb352du;
    h ^= h >> 15u; h *= 0x846ca68bu; h ^= h >> 16u;
    return vec3(uvec3(h, h >> 10u, h >> 20u) & 1023u) / 1024.0;
}

GroundMaterial tileSample(vec2 uv, vec2 dx, vec2 dy, mat2 rotateUV, vec2 offset,
                          int layer, bool details) {
    int sourceLayer = uMaterialPack == 2 || layer < 4 ? layer : (layer == 4 ? 0 : 1);
    vec3 uvw = vec3(rotateUV * uv + offset, float(sourceLayer));
    vec2 sx = rotateUV * dx, sy = rotateUV * dy;
    vec3 color = textureGrad(uMaterialAlbedo, uvw, sx, sy).rgb;
    if (uMaterialPack == 1) {
        const vec3 originalMeans[4] = vec3[4](vec3(.12,.16,.03), vec3(.215,.14,.069),
                                             vec3(.166,.149,.12), vec3(.136,.104,.066));
        color *= materialMean(layer) / originalMeans[sourceLayer];
    }
    vec2 slope = vec2(0);
    vec2 masks = vec2(.82,.96);
    if (details) {
        slope = transpose(rotateUV) * unpackSlope(textureGrad(uMaterialNormal, uvw, sx, sy).rg);
        masks = textureGrad(uMaterialMasks, uvw, sx, sy).rg;
    }
    return GroundMaterial(color, vec3(slope, 0), masks);
}

float tilePhase(vec2 uv, int layer) {
    // Campo 2D continuo. Al atravesar un entero, la segunda transformación
    // de un intervalo es la primera del siguiente, con derivada de peso nula.
    vec2 grid = uv * .64;
    ivec2 cell = ivec2(floor(grid));
    vec2 f = fract(grid);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = tileHash(cell, layer).x;
    float b = tileHash(cell + ivec2(1,0), layer).x;
    float c = tileHash(cell + ivec2(0,1), layer).x;
    float d = tileHash(cell + ivec2(1,1), layer).x;
    return mix(mix(a,b,u.x), mix(c,d,u.x), u.y) * 8.0;
}

mat2 tileRotation(float angle) {
    float c = cos(angle), s = sin(angle);
    return mat2(c,s,-s,c);
}

GroundMaterial sampleProjection(vec3 p, vec3 dpdx, vec3 dpdy, vec3 axisU, vec3 axisV,
                                int layer, bool details, bool randomized) {
    float scale = 1.0 / materialPeriod(layer);
    vec2 uv = vec2(dot(p, axisU), dot(p, axisV)) * scale;
    vec2 dx = vec2(dot(dpdx, axisU), dot(dpdx, axisV)) * scale;
    vec2 dy = vec2(dot(dpdy, axisU), dot(dpdy, axisV)) * scale;
    GroundMaterial m;
    if (!randomized) m = tileSample(uv, dx, dy, mat2(1), vec2(0), layer, details);
    else {
        float phase = tilePhase(uv, layer);
        int id = int(floor(phase));
        float w = smoothstep(.12, .88, fract(phase));
        vec3 a = tileHash(ivec2(id, 713), layer);
        vec3 b = tileHash(ivec2(id + 1, 713), layer);
        GroundMaterial first = tileSample(uv, dx, dy, tileRotation(6.2831853 * a.z), a.xy, layer, details);
        GroundMaterial second = tileSample(uv, dx, dy, tileRotation(6.2831853 * b.z), b.xy, layer, details);
        m = mixedMaterial(first, second, w);
        m.color = max(vec3(0), materialMean(layer) + (m.color - materialMean(layer)) * (1.0 + .96 * w * (1.0 - w)));
    }
    m.gradient = axisU * m.gradient.x + axisV * m.gradient.y;
    return m;
}

GroundMaterial rockMaterial(vec3 p, vec3 n, vec3 dx, vec3 dy, bool details, bool randomizeTop) {
    vec3 w = max(vec3(0), abs(n) - vec3(.39,.14,.39));
    w *= w;
    w /= dot(w, vec3(1));
    GroundMaterial r = GroundMaterial(vec3(0), vec3(0), vec2(0));
    for (int i = 0; i < 3; ++i) {
        if (w[i] <= 0.0) continue;
        vec3 u = i == 0 ? vec3(0,0,1) : vec3(1,0,0);
        vec3 v = i == 1 ? vec3(0,0,1) : vec3(0,1,0);
        GroundMaterial m = sampleProjection(p, dx, dy, u, v, 2, details, randomizeTop && i == 1);
        r.color += w[i] * m.color;
        r.gradient += w[i] * m.gradient;
        r.masks += w[i] * m.masks;
    }
    return r;
}
