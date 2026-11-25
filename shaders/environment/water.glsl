uniform vec3 uCamPos;
uniform float uTime;
uniform int uQuality;
uniform bool uContour;

in vec3 vWorldPos;
in float vDepth;
in vec2 vFlow;
in vec3 vSurfaceNormal;
flat in vec4 vOptics;
flat in vec4 vParallelView;
out vec4 fragColor;

vec2 flowNormal(vec2 uv, vec2 dx, vec2 dy, vec2 velocity, float time, vec2 offset, float layer) {
    float a = fract(time);
    float b = fract(time + 0.5);
    float weight = abs(2.0 * a - 1.0);
    vec2 s0 = unpackSlope(textureGrad(uMaterialNormal, vec3(uv - velocity * a, layer), dx, dy).rg);
    vec2 s1 = unpackSlope(textureGrad(uMaterialNormal, vec3(uv - velocity * b + offset, layer), dx, dy).rg);
    return mix(s0, s1, weight);
}

void main() {
    if (uContour) {
        fragColor = vec4(sceneDiagnostic(vec3(.93, .62, .17)), 1);
        return;
    }
    vec3 dx = dFdx(vWorldPos), dy = dFdy(vWorldPos);
    vec3 viewDir = vParallelView.w > 0.5 ? normalize(vParallelView.xyz) : normalize(uCamPos - vWorldPos);
    bool river = vOptics.w > 0.5;
    vec3 n = river ? normalize(vSurfaceNormal) : vec3(0,1,0);
    float baseUp = n.y;
    vec2 p = vWorldPos.xz;
    vec2 ripple0 = vec2(0);
    vec2 ripple1 = vec2(0);
    float footprint = max(length(dx.xz), length(dy.xz));
    float rippleFade = 1.0 - smoothstep(.045, .24, footprint);
    if (uQuality >= 1 && uHasMaterials && rippleFade > .002) {
        vec2 wind = vec2(.11, -.078);
        vec2 drift = river ? vFlow * .31 + wind * .22 : wind;
        ripple0 = flowNormal(p / 3.1, dx.xz / 3.1, dy.xz / 3.1, drift, uTime / 5.0, vec2(.13,.47), uMaterialPack == 2 ? 6.0 : 4.0);
        mat2 rotation = mat2(.6, .8, -.8, .6);
        ripple1 = transpose(rotation) * flowNormal(
            rotation * p / 1.55, rotation * dx.xz / 1.55, rotation * dy.xz / 1.55,
            rotation * (drift * .44 + vec2(-.09,.04)), uTime / 4.0, vec2(.37,.21),
            uMaterialPack == 2 ? 7.0 : 4.0);
        n = materialNormal(n, rippleFade * (vec3(ripple0.x, 0, ripple0.y) * (river ? .26 : .18)
                              + vec3(ripple1.x, 0, ripple1.y) * .14));
    }

    float facing = max(0.0, dot(n, viewDir));
    float travel = clamp(vDepth / max(.28, facing), 0.0, 12.0);
    vec3 transmission = exp(-vOptics.xyz * travel);
    float absorption = 1.0 - dot(transmission, vec3(.2126,.7152,.0722));
    float fresnel = .02 + .98 * pow(1.0 - facing, 5.0);
    // Suaviza el horizonte de un cielo hemisférico de baja frecuencia.
    vec3 reflected = reflect(-viewDir, n);
    vec3 sky = mix(sceneHaze.rgb * .86, sceneSkyFill.rgb * vec3(.47,.63,.78), smoothstep(0.0,.86,reflected.y));
    vec3 halfVector = normalize(viewDir + sceneSunWorld.xyz);
    float glint = pow(max(0.0, dot(n, halfVector)), river ? 30.0 : 40.0) *
                  max(dot(n, sceneSunWorld.xyz), 0.0) * 0.078;
    float visibility = sceneVisibility(vWorldPos, normalize(vSurfaceNormal));
    sky += sceneSunRadiance.rgb * (visibility * glint);

    vec3 scatter = mix(vec3(.045,.103,.122), vec3(.102,.119,.079),
                       clamp((vOptics.z - .23) * 2.6, 0., 1.));
    scatter *= mix(vec3(.74,.82,.94), vec3(1.13,1.03,.99), transmission);
    scatter *= mix(.75, 1.0, visibility);
    float reflection = fresnel * .68;
    float alpha = reflection + (1.0 - reflection) * absorption;
    vec3 color = (sky * reflection + scatter * ((1.0 - reflection) * absorption)) / max(alpha, .001);

    // La atenuación se aplica una sola vez mediante el alfa de la pasada.
    float shore = smoothstep(.002, .065, max(vDepth, 0.0));
    if (uQuality >= 2 && river) {
        float rapids = smoothstep(.065, .29, 1.0 - baseUp);
        float foam = rapids * smoothstep(.18,.31,ripple0.x + ripple1.y) *
                     smoothstep(.48,1.1,length(vFlow)) * .14;
        color = mix(color, vec3(.52,.61,.59), foam);
    }
    fragColor = vec4(sceneObjectOutput(color, vWorldPos), alpha * shore);
}
