// Material del mismo impacto SDF para ambos productores de superficie.
uniform int sdfSurfaceDebug;
vec3 SDFSurfaceColorWithDomain(vec3 p,vec3 n,vec3 rd,vec3 displayPosition,vec3 displayNormal,vec4 sampleValue,SDFResult semantic,
    vec3 materialCoord,vec3 materialNormal,vec4 profileA,vec4 profileB,float ventral,vec3 flow) {
    if(sdfSurfaceDebug==25)return n*.5+.5;
    if(sdfSurfaceDebug==26)return vec3(1);
    vec3 viewDelta=displayPosition-cameraPosition;
    vec3 viewP=vec3(dot(viewDelta,cameraRight),dot(viewDelta,cameraUp),
        -dot(viewDelta,cameraForward));
    vec3 viewN=normalize(vec3(dot(displayNormal,cameraRight),dot(displayNormal,cameraUp),
        -dot(displayNormal,cameraForward)));
    if(sdfSurfaceDebug==28)return vec3(float(semantic.moduleId%256u),float(semantic.regionId),float(semantic.materialId&255u))/255.0;
    vec3 light=normalize(vec3(10,20,15)-p);
    vec3 color=sampleValue.xyz*(.4+max(dot(n,light),0.0));
    uint tissue=semantic.materialId&255u;
    if(tissue==5u||tissue==6u){
        if(sdfSurfaceDebug==27)return sampleValue.xyz;
        SurfaceResponse keratin=SurfaceResponse(sampleValue.xyz,viewN,tissue==5u?.38:.55,.30);
        return SurfaceLighting(keratin,viewP);
    }
    if(sdfHasSurface!=0) {
        float skinMask=clamp(semantic.skinWeight,0.0,1.0);
        int integument=int(surfaceRecipe[7].x+.5);
        if(integument==2) {
            vec3 pigment=EvaluatePigment(materialCoord,ventral,surfaceRecipe[3],pigmentSeed);
            float footprint=max(length(dFdx(materialCoord)),length(dFdy(materialCoord)));
            float grain=PigmentNoise(materialCoord*8.0,furSeed^0x4d4943u);
            grain=mix(grain,.5,smoothstep(.18,.70,footprint));
            vec3 albedo=pigment*(1.0-surfaceRecipe[11].x*.35)*(.94+.12*grain);
            albedo=mix(sampleValue.xyz,albedo,skinMask);
            if(sdfSurfaceDebug==27)return albedo;
            vec3 viewFlow=normalize(vec3(dot(flow,cameraRight),
                dot(flow,cameraUp),-dot(flow,cameraForward)));
            vec3 tangent=normalize(mix(viewN,viewFlow,.72));
            color=FurLighting(albedo,tangent,viewN,viewP,
                clamp(surfaceRecipe[10].z,.2,1.0),surfaceRecipe[10].w*.3,0.0);
            float coverage=surfaceRecipe[11].w*skinMask;
            if(sdfFurEnabled!=0&&coverage>.001) {
                vec3 fiberColor=vec3(0);float fiberWeight=0;
                for(int layer=0;layer<3;++layer) {
                    float h=(float(layer)+.5)/3.0;
                    FurSample fur=EvaluateFurStrands(materialCoord,materialNormal,n,flow,vec3(0,0,-1),h,
                        surfaceRecipe[8]*profileA,
                        surfaceRecipe[9]*vec4(1,profileB.z,profileB.w,profileB.x),
                        surfaceRecipe[10]*vec4(1,profileB.y,1,1),furSeed);
                    float occupancy=fur.undercoat+fur.guard;
                    float lengthWorld=surfaceRecipe[8].x*surfaceRecipe[11].z;
                    float opticalDepth=320.0*surfaceRecipe[8].y*occupancy*
                        lengthWorld/(3.0*max(.25,abs(dot(n,-rd))));
                    float alpha=(1.0-exp(-opticalDepth))*coverage;
                    float tip=mix(1.0-surfaceRecipe[11].x,
                        1.0+surfaceRecipe[11].y,h);
                    vec3 viewTangent=normalize(vec3(dot(fur.tangent,cameraRight),
                        dot(fur.tangent,cameraUp),-dot(fur.tangent,cameraForward)));
                    vec3 fiber=FurLighting(pigment*tip,viewTangent,viewN,
                        viewP,surfaceRecipe[10].z,
                        surfaceRecipe[10].w,h);
                    fiberColor+=fiber*alpha;fiberWeight+=alpha;
                }
                color=mix(color,fiberColor/max(fiberWeight,.00001),
                    1.0-exp(-fiberWeight));
            }
        } else {
            SurfaceInput materialInput=SurfaceInput(materialCoord,materialNormal,p,n,vec4(sampleValue.xyz,1),
                profileA,profileB,ventral,skinMask);
            ScaleSample scales;vec3 pigment;
            SurfaceResponse response=Surface_Evaluate(materialInput,false,scales,pigment);
            if(sdfSurfaceDebug==27)return response.albedo;
            response.normal=normalize(response.normal+displayNormal-n);
            response.normal=normalize(vec3(dot(response.normal,cameraRight),
                dot(response.normal,cameraUp),-dot(response.normal,cameraForward)));
            color=SurfaceLighting(response,viewP);
        }
    }
    if(sdfSurfaceDebug==27)return sampleValue.xyz;
    return color;
}

vec3 SDFSurfaceColorPosed(vec3 p,vec3 n,vec3 rd,vec3 displayPosition,vec3 displayNormal) {
    SDFResult semantic;vec4 value=sceneDetailed(p,semantic);
    vec3 coord=p/max(surfaceRecipe[11].z,.001),normal=n,flow=normalize(vec3(0,0,-1)-n*dot(vec3(0,0,-1),n)+vec3(1e-12));
    vec4 pa=vec4(1),pb=vec4(1);float ventral=clamp(.5-.7*n.y,0.0,1.0);
    if(morphMaterialCount>0)MorphMaterial(p,n,coord,normal,pa,pb,ventral,flow);
    return SDFSurfaceColorWithDomain(p,n,rd,displayPosition,displayNormal,value,semantic,coord,normal,pa,pb,ventral,flow);
}
vec3 SDFSurfaceColor(vec3 p,vec3 n,vec3 rd) {return SDFSurfaceColorPosed(p,n,rd,p,n);}
