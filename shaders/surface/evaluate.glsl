// Interfaz de evaluación común para mallas y futuros ray hits con dominio de reposo.
struct SurfaceInput {
    vec3 materialCoord,materialNormal,position,normal;
    vec4 baseColor,profileA,profileB;
    float ventral,skinMask;
};
SurfaceResponse Surface_Evaluate(SurfaceInput surfaceInput,bool skipDetail,out ScaleSample s,out vec3 pigment) {
    vec3 n=normalize(surfaceInput.normal);
    vec4 shape=surfaceRecipe[4]; shape.xy*=surfaceInput.profileA.xy;
    shape.zw=clamp(shape.zw*surfaceInput.profileB.zy,0.0,1.0);
    vec4 relief=surfaceRecipe[5]; relief.x*=surfaceInput.profileA.z; relief.w*=surfaceInput.profileA.w;
    float coverage=surfaceRecipe[6].z*clamp(surfaceInput.skinMask,0.0,1.0);
    // Rechazo barato antes de Voronoi: tejidos orales y piel lisa no buscan celdas.
    if(coverage<=.0001 || skipDetail)shape.x=.000001;
    s=TriplanarScales(surfaceInput.materialCoord,surfaceInput.materialNormal,shape,relief,scaleSeed);
    vec3 detailed=SurfaceBump(n,surfaceInput.position,surfaceInput.materialCoord,s.gradient*relief.x*coverage);
    pigment=EvaluatePigment(surfaceInput.materialCoord,clamp(surfaceInput.ventral,0.0,1.0),surfaceRecipe[3],pigmentSeed);
    vec3 albedo=pigment*(1.0+(s.variation*2.0-1.0)*surfaceRecipe[6].y*coverage);
    albedo*=1.0-s.edge*.12*coverage;
    albedo=mix(surfaceInput.baseColor.rgb,albedo,clamp(surfaceInput.skinMask,0.0,1.0));
    float roughness=clamp(surfaceRecipe[6].x*surfaceInput.profileB.x+(s.variation-.5)*.12*coverage,.12,1.0);
    return SurfaceResponse(albedo,detailed,roughness,.12+(1.0-roughness)*.3);
}
