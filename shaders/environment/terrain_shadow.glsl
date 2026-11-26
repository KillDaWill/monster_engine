// Prueba cada centro de texel con la profundidad del plano receptor en ese
// punto. Un único z de referencia sobre cuatro texels causa bandas en laderas.
float sceneTerrainVisibility(vec3 p, vec3 n, vec3 dpdx, vec3 dpdy) {
    float texel = sceneSettings.z;
    if (texel == 0.0) return 1.0;
    vec3 q = (sceneWorldToShadow * vec4(p, 1)).xyz;
    if (any(lessThan(q, vec3(0))) || any(greaterThan(q, vec3(1)))) return 1.0;
    vec3 dx = mat3(sceneWorldToShadow) * dpdx;
    vec3 dy = mat3(sceneWorldToShadow) * dpdy;
    float det = dx.x * dy.y - dx.y * dy.x;
    if (abs(det) < 1e-12) return sceneVisibility(p, n);
    vec2 gradient = vec2(dx.z * dy.y - dy.z * dx.y, dx.x * dy.z - dy.x * dx.z) / det;
    float maxSlope = 5.0 * sceneSettings.w / texel;
    gradient = clamp(gradient, vec2(-maxSlope), vec2(maxSlope));

    vec2 texelPoint = q.xy / texel - .5;
    vec2 frac = fract(texelPoint);
    vec2 uv = (floor(texelPoint) + .5) * texel;
    float reference = q.z + dot(gradient, uv - q.xy) - .82 * sceneSettings.w;
    vec2 stepUV = vec2(texel, 0);
    vec2 stepZ = texel * gradient;
    float a = texture(uSceneShadow, vec3(uv, reference));
    float b = texture(uSceneShadow, vec3(uv + stepUV.xy, reference + stepZ.x));
    float c = texture(uSceneShadow, vec3(uv + stepUV.yx, reference + stepZ.y));
    float d = texture(uSceneShadow, vec3(uv + vec2(texel), reference + stepZ.x + stepZ.y));
    vec2 edge = min(q.xy, 1.0 - q.xy);
    float coverage = smoothstep(texel, 5.0 * texel, min(edge.x, edge.y));
    return mix(1.0, mix(mix(a,b,frac.x), mix(c,d,frac.x), frac.y), coverage);
}
