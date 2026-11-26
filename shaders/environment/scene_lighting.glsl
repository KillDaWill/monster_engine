#ifndef MONSTER_SCENE_LIGHTING
#define MONSTER_SCENE_LIGHTING
// Bloque std140 compartido con SceneLighting.c. El sol apunta hacia la fuente.
layout(std140) uniform SceneLightingFrame {
    mat4 sceneViewToWorld;
    mat4 sceneWorldToShadow;
    vec4 sceneSunWorld;
    vec4 sceneSunView;
    vec4 sceneSunRadiance;
    vec4 sceneSkyFill;
    vec4 sceneGroundFill;
    vec4 sceneHaze;
    vec4 sceneCamera; // xyz posición, w proyección ortográfica
    vec4 sceneViewAxis; // xyz eje +Z de vista, w exposición
    vec4 sceneExtras; // anti-tiling, PCF ampliado, tamaño de texel mundial
    vec4 sceneSettings; // natural, codificación manual, 1/resolución, sesgo en profundidad
    vec4 sceneIrradianceMapping; // origen XZ, inversa del ancho, activo
};
uniform sampler2DShadow uSceneShadow;
uniform sampler2D uSceneIrradiance;

bool sceneNatural() { return sceneSettings.x > 0.5; }
vec3 sceneViewDirection(vec3 p) {
    return sceneCamera.w > .5 ? sceneViewAxis.xyz : normalize(sceneCamera.xyz - p);
}
vec3 sceneEyeDirection(vec3 p) {
    return sceneNatural() && sceneCamera.w > .5 ? vec3(0,0,1) : normalize(-p);
}
vec3 sceneWorldPosition(vec3 p) { return (sceneViewToWorld * vec4(p, 1)).xyz; }
vec3 sceneAmbient(float up) {
    return mix(sceneGroundFill.rgb, sceneSkyFill.rgb, clamp(up * .5 + .5, 0.0, 1.0));
}
float sceneVisibility(vec3 p, vec3 geometricNormal) {
    float texel = sceneSettings.z;
    if (texel == 0.0) return 1.0;
    float slope = 1.0 - abs(dot(geometricNormal, sceneSunWorld.xyz));
    vec3 offset = geometricNormal * (sceneExtras.z * .16 * slope);
    vec3 q = (sceneWorldToShadow * vec4(p + offset, 1)).xyz;
    q.z -= sceneSettings.w * (.55 + .8 * slope);
    if (any(lessThan(q, vec3(0))) || any(greaterThan(q, vec3(1)))) return 1.0;
    vec2 edge = min(q.xy, 1.0 - q.xy);
    float coverage = smoothstep(texel, 5.0 * texel, min(edge.x, edge.y));
    if (sceneExtras.y < .5) return mix(1.0, texture(uSceneShadow, q), coverage);
    // Cuatro comparaciones bilineales separadas medio texel.
    vec2 d = vec2(.64 * texel, 0);
    float visible = texture(uSceneShadow, q + vec3(d.x, d.x, 0))
                  + texture(uSceneShadow, q + vec3(d.x,-d.x, 0))
                  + texture(uSceneShadow, q + vec3(-d.x,d.x, 0))
                  + texture(uSceneShadow, q + vec3(-d.x,-d.x, 0));
    return mix(1.0, visible * .25, coverage);
}
vec3 sceneDecode(vec3 c) {
    c = max(c, vec3(0));
    return mix(pow((c + .055) / 1.055, vec3(2.4)), c / 12.92, lessThanEqual(c, vec3(.04045)));
}
vec3 sceneEncode(vec3 c) {
    c = max(c, vec3(0));
    return mix(1.055 * pow(c, vec3(1.0 / 2.4)) - .055, 12.92 * c,
               lessThanEqual(c, vec3(.0031308)));
}
vec3 sceneFog(vec3 c, vec3 p) {
    float distanceToEye = sceneCamera.w > .5 ? 0.0 : length(sceneCamera.xyz - p);
    return mix(c, sceneHaze.rgb, smoothstep(75.0, 420.0, distanceToEye) * sceneHaze.w);
}
vec3 sceneOutput(vec3 c) {
    c = max(vec3(0), c * sceneViewAxis.w);
    return sceneSettings.y > .5 ? sceneEncode(c) : c;
}
vec3 sceneDiagnostic(vec3 encoded) { return sceneSettings.y > .5 ? encoded : sceneDecode(encoded); }
vec3 sceneObjectOutput(vec3 c, vec3 world) { return sceneOutput(sceneFog(c, world)); }
#endif
