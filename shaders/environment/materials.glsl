uniform bool uHasMaterials, uEncodeSRGB;
uniform sampler2DArray uMaterialAlbedo, uMaterialNormal, uMaterialMasks;

const vec3 worldSun = vec3(-0.465, 0.736, 0.426);
const vec3 worldHaze = vec3(0.274, 0.393, 0.507);

vec3 environmentOutput(vec3 linearColor) {
    linearColor = max(linearColor, vec3(0));
    if (!uEncodeSRGB) return linearColor;
    return mix(1.055 * pow(linearColor, vec3(1.0 / 2.4)) - 0.055,
               12.92 * linearColor, lessThanEqual(linearColor, vec3(0.0031308)));
}

struct GroundMaterial {
    vec3 color;
    vec3 gradient;
    vec2 masks;
};

vec2 unpackSlope(vec2 encoded) {
    vec2 xy = (encoded * 255.0 - 128.0) / 127.0;
    return xy * inversesqrt(max(0.09, 1.0 - dot(xy, xy)));
}

GroundMaterial sampleProjection(vec3 p, vec3 dpdx, vec3 dpdy, vec3 axisU, vec3 axisV,
                                float layer, float scale, bool details) {
    vec2 uv = vec2(dot(p, axisU), dot(p, axisV)) * scale;
    vec2 dx = vec2(dot(dpdx, axisU), dot(dpdx, axisV)) * scale;
    vec2 dy = vec2(dot(dpdy, axisU), dot(dpdy, axisV)) * scale;
    vec3 uvw = vec3(uv, layer);
    GroundMaterial m;
    m.color = textureGrad(uMaterialAlbedo, uvw, dx, dy).rgb;
    m.gradient = vec3(0);
    m.masks = vec2(0.78, 0.95);
    if (details) {
        vec2 slope = unpackSlope(textureGrad(uMaterialNormal, uvw, dx, dy).rg);
        m.gradient = axisU * slope.x + axisV * slope.y;
        m.masks = textureGrad(uMaterialMasks, uvw, dx, dy).rg;
    }
    return m;
}

GroundMaterial rockMaterial(vec3 p, vec3 n, vec3 dx, vec3 dy, bool details) {
    vec3 w = max(vec3(0), abs(n) - 0.16);
    w *= w;
    w /= dot(w, vec3(1));
    GroundMaterial r = GroundMaterial(vec3(0), vec3(0), vec2(0));
    for (int i = 0; i < 3; ++i) {
        if (w[i] <= 0.0) continue;
        vec3 u = i == 0 ? vec3(0,0,1) : vec3(1,0,0);
        vec3 v = i == 1 ? vec3(0,0,1) : vec3(0,1,0);
        GroundMaterial m = sampleProjection(p, dx, dy, u, v, 2.0, 1.0 / 4.4, details);
        r.color += w[i] * m.color;
        r.gradient += w[i] * m.gradient;
        r.masks += w[i] * m.masks;
    }
    return r;
}

vec3 materialNormal(vec3 n, vec3 gradient) {
    // Proyección del gradiente de relieve al plano tangente de la superficie.
    return normalize(n + gradient - n * dot(n, gradient));
}

vec3 illuminateGround(vec3 color, vec3 n, vec3 viewDir, float roughness, float ao) {
    float direct = max(dot(n, worldSun), 0.0);
    vec3 hemi = mix(vec3(0.19,0.18,0.16), vec3(0.46,0.49,0.53), n.y * 0.5 + 0.5);
    vec3 halfVector = normalize(worldSun + viewDir);
    float glint = pow(max(dot(n, halfVector), 0.0), mix(76.0, 6.0, roughness));
    return color * (hemi * ao + vec3(0.68,0.61,0.48) * direct)
         + vec3(1.0,0.92,0.76) * (glint * direct * (1.0 - roughness) * 0.036);
}
