// Trazado del mismo campo compilado que utiliza el mallador CPU.
uniform vec3 cameraPosition, cameraForward, cameraRight, cameraUp;
uniform vec3 boundsMin, boundsMax;
uniform vec2 viewportSize;
uniform float tanHalfFov, aspectRatio, hitTolerance;
uniform mat4 viewProjection;
out vec4 fragmentColor;

bool containsBox(vec3 p,vec3 lo,vec3 hi) {return all(greaterThanEqual(p,lo))&&all(lessThanEqual(p,hi));}
bool pruneBox(vec3 p,vec3 lo,vec3 hi,float scale,float cutoff) {
    vec3 d=max(max(lo-p,p-hi),vec3(0));
    if(cutoff<=0.0)return any(greaterThan(d,vec3(0)));
    float safe=cutoff/max(scale,.0001);
    return dot(d,d)>=safe*safe;
}
vec4 joinField(vec4 a,vec4 b,float k) {
    float h=k<=.0001?(a.w<b.w?1.0:0.0):clamp(.5+.5*(b.w-a.w)/k,0.0,1.0);
    return vec4(mix(b.xyz,a.xyz,h),SDF_SmoothUnion(a.w,b.w,k));
}
vec4 cutField(vec4 a,float d,vec3 color,float k) {
    float h=k<=.0001?(-d>a.w?1.0:0.0):clamp(.5-.5*(a.w+d)/k,0.0,1.0);
    return vec4(mix(a.xyz,color,h),SDF_SmoothSubtract(a.w,d,k));
}
uniform int tileDataBase, tileColumns, validationMode, validationPointBase;
int tileOffset, tileCount;
vec4 scene(vec3 p) {
    vec4 result=vec4(1,1,1,1e6);
    if(axialCount>1)result=vec4(connectorCount>0?C_color(connectorBase):vec3(1),SDF_EllipticalSweepZ(p,axialBase,axialCount));
    for(int i=0;i<partCount;++i) {
        int b=partBase+i*P_STRIDE;
        result=joinField(result,vec4(P_color(b),SDF_Ellipsoid(p-P_center(b),P_radii(b))),bodySmoothness);
    }
    for(int i=0;i<tileCount;++i) {
        int c=connectorBase+(tileOffset<0?i:int(dataAt(tileOffset+i/4)[i%4]))*C_STRIDE;
        if(axialCount>1&&C_kind(c)==0)continue;
        float k=C_localSmoothness(c);
        if(pruneBox(p,C_bounds_start(c),C_bounds_end(c),C_distanceLowerBoundScale(c),result.w+k))continue;
        result=joinField(result,vec4(C_color(c),MonsterSDF_EvalConnectorDistance(c,p)),k);
    }
    for(int i=0;i<mouthCount;++i) {
        int m=mouthBase+i*M_STRIDE;
        vec3 local=M_inverseRotation(m)*(p-M_center(m));
        if(!M_anatomicalHead(m))result=joinField(result,vec4(M_skinColor(m),MonsterSDF_EvalMuzzleDistance(m,local)),M_muzzleSmoothness(m));
        result=joinField(result,vec4(M_skinColor(m),MonsterSDF_EvalUpperHeadDistance(m,local)),M_anatomicalHead(m)?M_headBodySmoothness(m):M_muzzleSmoothness(m));
    }
    for(int i=0;i<mouthCount;++i) {
        int m=mouthBase+i*M_STRIDE;
        vec3 local=M_inverseRotation(m)*(p-M_center(m));
        vec3 ext=M_entranceHalfExtents(m),r=M_hostRadii(m);
        float entrance=SDF_RoundedSlotExtruded(local-M_entranceCenterLocal(m),ext.x,ext.y,ext.z);
        float cavity=SDF_Ellipsoid(local-M_cavityCenterLocal(m),M_cavityRadii(m));
        float cutter=SDF_SmoothUnion(entrance,cavity,M_entranceToCavitySmoothness(m))+(1.0-M_cephalicDevelopment(m))*max(r.x,max(r.y,r.z))*2.0;
        result=cutField(result,cutter,M_insideColor(m),M_rimBevel(m));
        if(M_anatomicalHead(m)) {
            result=cutField(result,MonsterSDF_EvalOrbitCavities(m,local),vec3(24,28,19)/255.0,M_headUnionSmoothness(m)*.06);
            result=cutField(result,MonsterSDF_EvalNostrilCavities(m,local),vec3(18,20,14)/255.0,M_headUnionSmoothness(m)*.04);
            if(M_hasTympana(m))result=cutField(result,MonsterSDF_EvalTympanumCavities(m,local),vec3(25,24,16)/255.0,M_headUnionSmoothness(m)*.04);
        }
    }
    for(int i=0;i<mouthCount;++i) {
        int m=mouthBase+i*M_STRIDE;
        vec3 local=M_inverseRotation(m)*(p-M_center(m));
        float d=MonsterSDF_EvalPosedMouthDistance(m,local);
        if(d<result.w)result=vec4(M_skinColor(m),d);
    }
    return result;
}
float sceneDistance(vec3 p) {
    float result=1e6;
    if(axialCount>1)result=SDF_EllipticalSweepZ(p,axialBase,axialCount);
    for(int i=0;i<partCount;++i) {
        int b=partBase+i*P_STRIDE;
        result=SDF_SmoothUnion(result,SDF_Ellipsoid(p-P_center(b),P_radii(b)),bodySmoothness);
    }
    for(int i=0;i<tileCount;++i) {
        int c=connectorBase+(tileOffset<0?i:int(dataAt(tileOffset+i/4)[i%4]))*C_STRIDE;
        if(axialCount>1&&C_kind(c)==0)continue;
        float k=C_localSmoothness(c);
        if(pruneBox(p,C_bounds_start(c),C_bounds_end(c),C_distanceLowerBoundScale(c),result+k))continue;
        result=SDF_SmoothUnion(result,MonsterSDF_EvalConnectorDistance(c,p),k);
    }
    for(int i=0;i<mouthCount;++i) {
        int m=mouthBase+i*M_STRIDE;
        vec3 local=M_inverseRotation(m)*(p-M_center(m));
        if(!M_anatomicalHead(m))result=SDF_SmoothUnion(result,MonsterSDF_EvalMuzzleDistance(m,local),M_muzzleSmoothness(m));
        result=SDF_SmoothUnion(result,MonsterSDF_EvalUpperHeadDistance(m,local),M_anatomicalHead(m)?M_headBodySmoothness(m):M_muzzleSmoothness(m));
    }
    for(int i=0;i<mouthCount;++i) {
        int m=mouthBase+i*M_STRIDE;
        vec3 local=M_inverseRotation(m)*(p-M_center(m));
        vec3 ext=M_entranceHalfExtents(m),r=M_hostRadii(m);
        float entrance=SDF_RoundedSlotExtruded(local-M_entranceCenterLocal(m),ext.x,ext.y,ext.z);
        float cavity=SDF_Ellipsoid(local-M_cavityCenterLocal(m),M_cavityRadii(m));
        float cutter=SDF_SmoothUnion(entrance,cavity,M_entranceToCavitySmoothness(m))+(1.0-M_cephalicDevelopment(m))*max(r.x,max(r.y,r.z))*2.0;
        result=SDF_SmoothSubtract(result,cutter,M_rimBevel(m));
        if(M_anatomicalHead(m)) {
            result=SDF_SmoothSubtract(result,MonsterSDF_EvalOrbitCavities(m,local),M_headUnionSmoothness(m)*.06);
            result=SDF_SmoothSubtract(result,MonsterSDF_EvalNostrilCavities(m,local),M_headUnionSmoothness(m)*.04);
            if(M_hasTympana(m))result=SDF_SmoothSubtract(result,MonsterSDF_EvalTympanumCavities(m,local),M_headUnionSmoothness(m)*.04);
        }
    }
    for(int i=0;i<mouthCount;++i) {
        int m=mouthBase+i*M_STRIDE;
        vec3 local=M_inverseRotation(m)*(p-M_center(m));
        result=min(result,MonsterSDF_EvalPosedMouthDistance(m,local));
    }
    return result;
}
void main() {
    if(validationMode!=0) {
        tileOffset=-1;tileCount=connectorCount;
        vec3 point=dataAt(validationPointBase+int(gl_FragCoord.x)).xyz;
        fragmentColor=vec4(sceneDistance(point),0,0,1);gl_FragDepth=0;return;
    }
    vec2 uv=(gl_FragCoord.xy/viewportSize)*2.0-1.0;
    vec3 rd=normalize(cameraForward+cameraRight*(uv.x*aspectRatio*tanHalfFov)+cameraUp*(uv.y*tanHalfFov));
    vec3 t0=(boundsMin-cameraPosition)/rd,t1=(boundsMax-cameraPosition)/rd;
    vec3 nearT=min(t0,t1),farT=max(t0,t1);
    float t=max(max(nearT.x,nearT.y),max(nearT.z,0.0));
    float end=min(farT.x,min(farT.y,farT.z));
    if(t>end)discard;
    int tile=int(gl_FragCoord.y)/16*tileColumns+int(gl_FragCoord.x)/16;
    vec4 list=dataAt(tileDataBase+tile);tileOffset=int(list.x);tileCount=int(list.y);
    vec3 p=vec3(0);vec4 sampleValue=vec4(0);bool hit=false;
    for(int step=0;step<256;++step) {
        p=cameraPosition+rd*t;sampleValue.w=sceneDistance(p);
        if(sampleValue.w<hitTolerance){hit=true;break;}
        t+=max(sampleValue.w*.8,hitTolerance*.35);
        if(t>end)break;
    }
    if(!hit)discard;
    float e=hitTolerance*.65;
    vec3 n=normalize(vec3(sceneDistance(p+vec3(e,0,0))-sceneDistance(p-vec3(e,0,0)),
        sceneDistance(p+vec3(0,e,0))-sceneDistance(p-vec3(0,e,0)),
        sceneDistance(p+vec3(0,0,e))-sceneDistance(p-vec3(0,0,e))));
    sampleValue=scene(p);
    vec3 light=normalize(vec3(10,20,15)-p);
    fragmentColor=vec4(sampleValue.xyz*(.4+max(dot(n,light),0.0)),1);
    vec4 clip=viewProjection*vec4(p,1);gl_FragDepth=(clip.z/clip.w)*.5+.5;
}
