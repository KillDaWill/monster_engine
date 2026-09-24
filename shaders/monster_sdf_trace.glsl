// Trazado del mismo campo compilado que utiliza el mallador CPU.
uniform vec3 cameraPosition, cameraForward, cameraRight, cameraUp;
uniform vec3 boundsMin, boundsMax;
uniform vec2 viewportSize;
uniform float tanHalfFov, aspectRatio, hitTolerance;
uniform mat4 viewProjection;
out vec4 fragmentColor;

// Intervalo cerrado; para direcciones casi paralelas se conserva todo el
// desplazamiento del segmento, sin dividir por cero ni descartar incertidumbre.
bool rayBox(vec3 ro,vec3 rd,vec3 lo,vec3 hi,inout float start,inout float end) {
    for(int axis=0;axis<3;++axis) {
        if(abs(rd[axis])<1e-8) {
            float a=ro[axis]+rd[axis]*start,b=ro[axis]+rd[axis]*end;
            if(max(a,b)<lo[axis]||min(a,b)>hi[axis])return false;
        } else {
            float a=(lo[axis]-ro[axis])/rd[axis],b=(hi[axis]-ro[axis])/rd[axis];
            start=max(start,min(a,b));end=min(end,max(a,b));
            if(start>end)return false;
        }
    }
    return true;
}
bool rayIntersectsAABB(vec3 ro,vec3 rd,vec3 lo,vec3 hi,float start,float end) {
    if(any(isnan(lo))||any(isnan(hi))||any(isinf(lo))||any(isinf(hi))||any(greaterThan(lo,hi)))return true;
    return rayBox(ro,rd,lo,hi,start,end);
}
// Casos degenerados ejecutados por --validate-gpu con la misma función GLSL.
bool validateRayBoxes() {
    vec3 lo=vec3(-1),hi=vec3(1);
    if(!rayIntersectsAABB(vec3(2,0,0),vec3(-1,0,0),lo,hi,0.0,4.0))return false;
    if(!rayIntersectsAABB(vec3(0),vec3(0,1,0),lo,hi,0.0,4.0))return false;
    if(!rayIntersectsAABB(vec3(1,0,0),vec3(0,1,0),lo,hi,0.0,1.0))return false;
    if(rayIntersectsAABB(vec3(2,0,0),vec3(0,1,0),lo,hi,0.0,4.0))return false;
    if(rayIntersectsAABB(vec3(2,0,0),vec3(-1,0,0),lo,hi,0.0,.5))return false;
    if(!rayIntersectsAABB(vec3(2,0,0),vec3(-1e-9,0,0),lo,hi,0.0,2e9))return false;
    return true;
}
// Agregados sin límite de bocas: una intersección activa toda la fase.
// Las normales reutilizan estos indicadores, nunca clasifican puntos vecinos.
bool rayHeadActive=true,rayMouthActive=true,rayJawActive=true;
void classifyRay(vec3 rd,float start,float end) {
    rayHeadActive=false;rayMouthActive=false;rayJawActive=false;
    for(int i=0;i<mouthCount;++i) {
        int m=mouthBase+i*M_STRIDE;
        // La ruta heredada carece de una caja cefálica certificada.
        if(!M_anatomicalHead(m)||M_cephalicDevelopment(m)<0.0||M_cephalicDevelopment(m)>1.0){rayHeadActive=true;rayMouthActive=true;rayJawActive=true;continue;}
        vec3 lo=M_headBounds_start(m),hi=M_headBounds_end(m);
        rayHeadActive=rayHeadActive||rayIntersectsAABB(cameraPosition,rd,lo,hi,start,end);
        rayMouthActive=rayMouthActive||rayIntersectsAABB(cameraPosition,rd,
            M_influenceBounds_start(m),M_influenceBounds_end(m),start,end);
        rayJawActive=rayJawActive||rayIntersectsAABB(cameraPosition,rd,
            M_visualBounds_start(m),M_visualBounds_end(m),start,end);
    }
}
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
        if(C_isHeadNeck(c)||(axialCount>1&&C_kind(c)==0))continue;
        float k=C_localSmoothness(c);
        if(pruneBox(p,C_bounds_start(c),C_bounds_end(c),C_distanceLowerBoundScale(c),result.w+k))continue;
        result=joinField(result,vec4(C_color(c),MonsterSDF_EvalConnectorDistance(c,p)),k);
    }
    for(int i=0;i<mouthCount;++i) {
        if(!rayHeadActive)continue;
        int m=mouthBase+i*M_STRIDE;
        vec3 local=M_inverseRotation(m)*(p-M_center(m));
        if(!M_anatomicalHead(m))result=joinField(result,vec4(M_skinColor(m),MonsterSDF_EvalMuzzleDistance(m,local)),M_muzzleSmoothness(m));
        vec3 headColor=M_skinColor(m);
        if(M_hasNasalPad(m) && SDF_Ellipsoid(local-M_noseCenterLocal(m),M_noseRadii(m))<M_headUnionSmoothness(m)*.5) headColor=vec3(30,27,24)/255.0;
        result=joinField(result,vec4(headColor,MonsterSDF_EvalUpperHeadDistance(m,local)),M_anatomicalHead(m)?M_headBodySmoothness(m):M_muzzleSmoothness(m));
    }
    for(int i=0;i<mouthCount;++i) {
        if(!(rayHeadActive||rayMouthActive))continue;
        int m=mouthBase+i*M_STRIDE;
        vec3 local=M_inverseRotation(m)*(p-M_center(m));
        if(rayMouthActive) {
            vec3 ext=M_entranceHalfExtents(m),r=M_hostRadii(m);
            float entrance=SDF_RoundedSlotExtruded(local-M_entranceCenterLocal(m),ext.x,ext.y,ext.z);
            float cavity=SDF_Ellipsoid(local-M_cavityCenterLocal(m),M_cavityRadii(m));
            if(M_sweptSkull(m)) {
                float wall=max(ext.y*2.0,M_faceMidRadii(m).x*.12);
                cavity=max(cavity,SDF_EllipticalSweepZ(local,M_headStations(m),6)+wall);
            }
            float devMouth=clamp((M_cephalicDevelopment(m)-0.05)/0.25,0.0,1.0);
            float cutter=SDF_SmoothUnion(entrance,cavity,M_entranceToCavitySmoothness(m))+(1.0-devMouth)*max(r.x,max(r.y,r.z))*2.0;
            result=cutField(result,cutter,M_insideColor(m),M_rimBevel(m));
        }
        if(rayHeadActive&&M_anatomicalHead(m)) {
            result=cutField(result,MonsterSDF_EvalOrbitCavities(m,local),vec3(24,28,19)/255.0,M_headUnionSmoothness(m)*.06);
            result=cutField(result,MonsterSDF_EvalNostrilCavities(m,local),vec3(18,20,14)/255.0,M_headUnionSmoothness(m)*.04);
            if(M_hasTympana(m))result=cutField(result,MonsterSDF_EvalTympanumCavities(m,local),vec3(25,24,16)/255.0,M_headUnionSmoothness(m)*.04);
        }
    }
    for(int i=0;i<mouthCount;++i) {
        if(!rayJawActive)continue;
        int m=mouthBase+i*M_STRIDE;
        vec3 local=M_inverseRotation(m)*(p-M_center(m));
        float d=MonsterSDF_EvalPosedMouthDistance(m,local);
        if(d<result.w) {
            vec3 col=M_skinColor(m);
            if(M_taperedMandible(m)) {
                float scale=M_visualJawScale(m);
                vec3 pivot=M_visualJawPivot(m),rel=local-pivot;
                float angle=M_visualJawAngle(m),c=cos(angle),s=sin(angle);
                vec3 rest=pivot+vec3(rel.x,c*rel.y+s*rel.z,-s*rel.y+c*rel.z)/max(scale,0.0001);
                float tongueD=MonsterSDF_EvalTongueDistance(m,rest)*scale;
                if(tongueD<=d+0.005)col=M_insideColor(m);
            }
            result=vec4(col,d);
        }
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
        if(C_isHeadNeck(c)||(axialCount>1&&C_kind(c)==0))continue;
        float k=C_localSmoothness(c);
        if(pruneBox(p,C_bounds_start(c),C_bounds_end(c),C_distanceLowerBoundScale(c),result+k))continue;
        result=SDF_SmoothUnion(result,MonsterSDF_EvalConnectorDistance(c,p),k);
    }
    for(int i=0;i<mouthCount;++i) {
        if(!rayHeadActive)continue;
        int m=mouthBase+i*M_STRIDE;
        vec3 local=M_inverseRotation(m)*(p-M_center(m));
        if(!M_anatomicalHead(m))result=SDF_SmoothUnion(result,MonsterSDF_EvalMuzzleDistance(m,local),M_muzzleSmoothness(m));
        result=SDF_SmoothUnion(result,MonsterSDF_EvalUpperHeadDistance(m,local),M_anatomicalHead(m)?M_headBodySmoothness(m):M_muzzleSmoothness(m));
    }
    for(int i=0;i<mouthCount;++i) {
        if(!(rayHeadActive||rayMouthActive))continue;
        int m=mouthBase+i*M_STRIDE;
        vec3 local=M_inverseRotation(m)*(p-M_center(m));
        if(rayMouthActive) {
            vec3 ext=M_entranceHalfExtents(m),r=M_hostRadii(m);
            float entrance=SDF_RoundedSlotExtruded(local-M_entranceCenterLocal(m),ext.x,ext.y,ext.z);
            float cavity=SDF_Ellipsoid(local-M_cavityCenterLocal(m),M_cavityRadii(m));
            if(M_sweptSkull(m)) {
                float wall=max(ext.y*2.0,M_faceMidRadii(m).x*.12);
                cavity=max(cavity,SDF_EllipticalSweepZ(local,M_headStations(m),6)+wall);
            }
            float devMouth=clamp((M_cephalicDevelopment(m)-0.05)/0.25,0.0,1.0);
            float cutter=SDF_SmoothUnion(entrance,cavity,M_entranceToCavitySmoothness(m))+(1.0-devMouth)*max(r.x,max(r.y,r.z))*2.0;
            result=SDF_SmoothSubtract(result,cutter,M_rimBevel(m));
        }
        if(rayHeadActive&&M_anatomicalHead(m)) {
            result=SDF_SmoothSubtract(result,MonsterSDF_EvalOrbitCavities(m,local),M_headUnionSmoothness(m)*.06);
            result=SDF_SmoothSubtract(result,MonsterSDF_EvalNostrilCavities(m,local),M_headUnionSmoothness(m)*.04);
            if(M_hasTympana(m))result=SDF_SmoothSubtract(result,MonsterSDF_EvalTympanumCavities(m,local),M_headUnionSmoothness(m)*.04);
        }
    }
    for(int i=0;i<mouthCount;++i) {
        if(!rayJawActive)continue;
        int m=mouthBase+i*M_STRIDE;
        vec3 local=M_inverseRotation(m)*(p-M_center(m));
        result=min(result,MonsterSDF_EvalPosedMouthDistance(m,local));
    }
    return result;
}
void main() {
    if(validationMode!=0) {
        if(!validateRayBoxes()){fragmentColor=vec4(1e6);return;}
        tileOffset=-1;tileCount=connectorCount;
        vec3 point=dataAt(validationPointBase+int(gl_FragCoord.x)).xyz;
        float full=sceneDistance(point);
        vec3 delta=point-cameraPosition;
        if(dot(delta,delta)>1e-20) {
            vec3 direction=normalize(delta);float start=0.0,end=1e30;
            if(rayBox(cameraPosition,direction,boundsMin,boundsMax,start,end))classifyRay(direction,start,end);
        }
        fragmentColor=vec4(full,sceneDistance(point),0,1);gl_FragDepth=0;return;
    }
    vec2 uv=(gl_FragCoord.xy/viewportSize)*2.0-1.0;
    vec3 rd=normalize(cameraForward+cameraRight*(uv.x*aspectRatio*tanHalfFov)+cameraUp*(uv.y*tanHalfFov));
    float t=0.0,end=1e30;
    if(!rayBox(cameraPosition,rd,boundsMin,boundsMax,t,end))discard;
    classifyRay(rd,t,end);
    int tile=int(gl_FragCoord.y)/16*tileColumns+int(gl_FragCoord.x)/16;
    vec4 list=dataAt(tileDataBase+tile);tileOffset=int(list.x);tileCount=int(list.y);
    vec3 p=vec3(0);vec4 sampleValue=vec4(0);bool hit=false;
    for(int step=0;step<256;++step) {
        p=cameraPosition+rd*t;sampleValue.w=sceneDistance(p);
        if(sampleValue.w<hitTolerance){hit=true;break;}
        float stepFactor=sampleValue.w>0.04?1.0:0.8;
        t+=max(sampleValue.w*stepFactor,hitTolerance*.35);
        if(t>end)break;
    }
    if(!hit)discard;
    float e=hitTolerance*.65;
    // Tetraedro simétrico: cuatro muestras con radio comparable a las seis originales.
    const vec3 k0=vec3(1,-1,-1),k1=vec3(-1,-1,1),k2=vec3(-1,1,-1),k3=vec3(1,1,1);
    e*=0.57735026919;
    vec3 gradient=k0*sceneDistance(p+k0*e)+k1*sceneDistance(p+k1*e)
        +k2*sceneDistance(p+k2*e)+k3*sceneDistance(p+k3*e);
    float norm2=dot(gradient,gradient);
    vec3 n=norm2>1e-20?gradient*inversesqrt(norm2):-rd;
    sampleValue=scene(p);
    vec3 light=normalize(vec3(10,20,15)-p);
    fragmentColor=vec4(sampleValue.xyz*(.4+max(dot(n,light),0.0)),1);
    vec4 clip=viewProjection*vec4(p,1);gl_FragDepth=(clip.z/clip.w)*.5+.5;
}
