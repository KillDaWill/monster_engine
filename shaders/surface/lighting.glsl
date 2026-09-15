struct SurfaceResponse { vec3 albedo; vec3 normal; float roughness; float specular; };
vec3 SurfaceLighting(SurfaceResponse s,vec3 viewPosition) {
    vec3 light=normalize(vec3(-.45,.75,.8)),view=normalize(-viewPosition);
    vec3 halfVector=normalize(light+view);
    float diffuse=max(dot(s.normal,light),0.0);
    float exponent=mix(160.0,5.0,s.roughness*s.roughness);
    float spec=pow(max(dot(s.normal,halfVector),0.0),exponent)*s.specular*diffuse;
    float fill=max(dot(s.normal,normalize(vec3(.7,.2,-.3))),0.0);
    return s.albedo*(.26+.7*diffuse+.12*fill)+vec3(spec);
}
