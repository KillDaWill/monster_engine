// Interfaz de evaluación común para mallas y futuros ray hits con dominio de reposo.
struct SurfaceInput {
    vec3 materialCoord,materialNormal,position,normal;
    vec4 baseColor,profileA,profileB;
    float ventral,skinMask;
};
// Surcos multiescala en reposo. El pie de píxel atenúa las grietas finas a distancia.
float SurfaceWrinkleBand(float phase) {
    float f=1.0-smoothstep(.70,2.20,fwidth(phase));
    return pow(.5+.5*cos(phase),10.0)*f;
}

vec3 SurfaceWrinkles(vec3 n,vec3 p,vec3 coord,float mask,inout vec3 albedo) {
    vec3 params=surfaceRecipe[102].yzw;
    float strength=params.x*clamp(mask*wrinkleCoverage,0.0,1.0);
    if(strength<=.0001)return n;
    vec3 q=coord*max(params.y,.05);
    float warp=PigmentNoise(q*2.1,pigmentSeed^194u);
    float crossWarp=PigmentNoise(q*5.4,pigmentSeed^321u);
    float foldEnvelope=.24+.76*PigmentNoise(q*3.1,pigmentSeed^766u);
    float offAxis=dot(q,vec3(.81,.12,.65));
    float axis=mix(offAxis,q.y,2.0*fract(.5*params.z));
    float broad=SurfaceWrinkleBand(axis*18.0+warp*9.5+crossWarp*1.5);
    float medium=SurfaceWrinkleBand(axis*55.0+warp*14.0+crossWarp*4.7);
    float cracks=PigmentNoise(q*16.2+warp,pigmentSeed^814u);
    float fine=PigmentNoise(q*33.8+crossWarp,pigmentSeed^421u);
    float footprint=max(length(dFdx(q)),length(dFdy(q)));
    float crease=(1.0-smoothstep(.02,.10,abs(cracks-.53)))*(1.0-smoothstep(.014,.062,footprint));
    float micro=(1.0-smoothstep(.015,.13,abs(fine-.48)))*(1.0-smoothstep(.008,.031,footprint));
    float groove=(foldEnvelope*(.22*broad+.42*medium)+.36*crease+.15*micro)*strength;
    albedo*=1.0-groove*.34;
    float height=-groove*.010/max(params.y,.05);
    vec3 dx=dFdx(p),dy=dFdy(p),rx=cross(dy,n),ry=cross(n,dx);
    float determinant=dot(dx,rx);
    vec3 gradient=(dFdx(height)*rx+dFdy(height)*ry)*sign(determinant)/max(abs(determinant),1e-9);
    return normalize(n-gradient/max(1.0,length(gradient)/.8));
}

SurfaceResponse Surface_Evaluate(SurfaceInput surfaceInput,bool skipDetail,out ScaleSample s,out vec3 pigment) {
    vec3 n=normalize(surfaceInput.normal);
    vec4 shape=surfaceRecipe[4]; shape.xy*=surfaceInput.profileA.xy;
    shape.zw=clamp(shape.zw*surfaceInput.profileB.zy,0.0,1.0);
    vec4 relief=surfaceRecipe[5]; relief.x*=surfaceInput.profileA.z; relief.w*=surfaceInput.profileA.w;
    float coverage=surfaceRecipe[6].z*clamp(surfaceInput.skinMask,0.0,1.0);
    // Rechazo barato antes de Voronoi: tejidos orales y piel lisa no buscan celdas.
    if(coverage<=.0001 || skipDetail)shape.x=.000001;
    s=TriplanarScales(surfaceInput.materialCoord,surfaceInput.materialNormal,shape,relief,scaleSeed);
    if(surfaceRecipe[46].x>.5) {
        uint targetSeed=uint(surfaceRecipe[47].x)|(uint(surfaceRecipe[47].y)<<16u);
        if(targetSeed!=scaleSeed) {
            ScaleSample target=TriplanarScales(surfaceInput.materialCoord,surfaceInput.materialNormal,shape,relief,targetSeed);
            float t=surfaceRecipe[46].y;
            s.height=mix(s.height,target.height,t);s.edge=mix(s.edge,target.edge,t);
            s.variation=mix(s.variation,target.variation,t);s.gradient=mix(s.gradient,target.gradient,t);
        }
    }
    vec3 detailed=SurfaceBump(n,surfaceInput.position,surfaceInput.materialCoord,s.gradient*relief.x*coverage);
    pigment=EvaluatePigment(surfaceInput.materialCoord,clamp(surfaceInput.ventral,0.0,1.0),surfaceRecipe[3],pigmentSeed);
    vec3 albedo=pigment*(1.0+(s.variation*2.0-1.0)*surfaceRecipe[6].y*coverage);
    albedo*=1.0-s.edge*.12*coverage;
    albedo=mix(surfaceInput.baseColor.rgb,albedo,clamp(surfaceInput.skinMask,0.0,1.0));
    if(!skipDetail)detailed=SurfaceWrinkles(detailed,surfaceInput.position,surfaceInput.materialCoord,surfaceInput.skinMask,albedo);
    float roughness=clamp(surfaceRecipe[6].x*surfaceInput.profileB.x+(s.variation-.5)*.12*coverage,.12,1.0);
    return SurfaceResponse(albedo,detailed,roughness,.12+(1.0-roughness)*.3);
}
