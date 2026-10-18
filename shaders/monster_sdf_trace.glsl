// Trazado del mismo campo compilado que utiliza el mallador CPU.
uniform vec3 boundsMin, boundsMax;
uniform bool referenceTrace;
uniform int referenceSteps;
uniform float referenceMaxStep;
uniform int traceRegionCount;
uniform vec3 traceRegionMin[16],traceRegionMax[16];
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
// Intersección conservadora del soporte anatómico: cilindro finito y extremos.
// Acota trabajo; el impacto sigue perteneciendo al campo SDF completo.
void capsuleSphereInterval(vec3 ro,vec3 rd,vec3 center,float radius,
    inout float first,inout float last) {
    vec3 v=ro-center;float b=dot(v,rd),c=dot(v,v)-radius*radius;
    float discriminant=b*b-c;
    if(discriminant<0.0)return;
    float root=sqrt(max(discriminant,0.0));
    first=min(first,-b-root);last=max(last,-b+root);
}
bool handoffRayInterval(int owner,vec3 ro,vec3 rd,inout float first,inout float last) {
    float lo=1e30,hi=-1e30;
    int begin=int(handoffMeta[owner].z),count=int(handoffMeta[owner].w);
    for(int j=begin;j<begin+count;++j) {
        vec4 a=texelFetch(handoffSegments,2*j),b=texelFetch(handoffSegments,2*j+1);
        float radius=a.w+max(handoffMeta[owner].x,.01)+hitTolerance;
        capsuleSphereInterval(ro,rd,a.xyz,radius,lo,hi);
        capsuleSphereInterval(ro,rd,b.xyz,radius,lo,hi);
        vec3 ab=b.xyz-a.xyz;float len=length(ab);
        if(len<1e-8)continue;
        vec3 axis=ab/len,v=ro-a.xyz;
        float axial=dot(v,axis),slope=dot(rd,axis);
        vec3 perpendicular=v-axis*axial,direction=rd-axis*slope;
        float qa=dot(direction,direction),qb=dot(perpendicular,direction);
        float qc=dot(perpendicular,perpendicular)-radius*radius;
        float start=-1e30,end=1e30;
        if(qa<1e-12) {if(qc>0.0)continue;}
        else {
            float discriminant=qb*qb-qa*qc;if(discriminant<0.0)continue;
            float root=sqrt(max(discriminant,0.0));
            start=(-qb-root)/qa;end=(-qb+root)/qa;
        }
        if(abs(slope)<1e-8) {if(axial<0.0||axial>len)continue;}
        else {
            float x=-axial/slope,y=(len-axial)/slope;
            start=max(start,min(x,y));end=min(end,max(x,y));
        }
        if(start<=end) {lo=min(lo,start);hi=max(hi,end);}
    }
    first=max(first,lo);last=min(last,hi);
    return first<=last;
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
    float entries[16],exits[16];float firstEntry=1e30,lastExit=-1.0;
    int intervalCount=max(traceRegionCount,1);
    for(int i=0;i<intervalCount;++i) {
        entries[i]=t;exits[i]=end;
        if(traceRegionCount==0) {firstEntry=t;lastExit=end;break;}
        if((i<handoffCount&&handoffRetire[i]!=0)||
           !rayBox(cameraPosition,rd,traceRegionMin[i],traceRegionMax[i],entries[i],exits[i])||
           (i<handoffCount&&!handoffRayInterval(i,cameraPosition,rd,entries[i],exits[i]))) {
            entries[i]=1e30;exits[i]=-1.0;
        }
        firstEntry=min(firstEntry,entries[i]);lastExit=max(lastExit,exits[i]);
    }
    if(lastExit<firstEntry)discard;
    t=max(t,firstEntry);end=min(end,lastExit);
    if(referenceTrace)MonsterSDFUseCompleteScene();
    else {
        classifyRay(rd,t,end);
        int tile=int(gl_FragCoord.y)/16*tileColumns+int(gl_FragCoord.x)/16;
        vec4 list=dataAt(tileDataBase+tile);tileOffset=int(list.x);tileCount=int(list.y);
    }
    spatialTrace=!referenceTrace;
    vec3 p=vec3(0);vec4 sampleValue=vec4(0);bool hit=false;
    float traceTolerance=referenceTrace?hitTolerance*.01:hitTolerance;
    float previousT=t,previousDistance=0;bool previousValid=false,rejectedRoot=false;
    for(int step=0;step<(referenceTrace?referenceSteps:256);++step) {
        bool inside=false;float next=1e30;
        for(int i=0;i<intervalCount;++i) {
            inside=inside||(t>=entries[i]&&t<=exits[i]);
            if(entries[i]>t)next=min(next,entries[i]);
        }
        if(!inside) {t=next;previousValid=false;rejectedRoot=false;if(t>end)break;}
        p=cameraPosition+rd*t;sampleValue.w=sceneDistance(p);
        bool rootFound=false;
        if(previousValid&&(previousDistance>0)!=(sampleValue.w>0)) {
            // Un campo implícito puede sobrepasar la primera raíz aunque el
            // valor escalar sea correcto. No se sigue marchando dentro del sólido.
            float lo=previousT,hi=t;bool outside=previousDistance>0;
            for(int root=0;root<24&&hi-lo>traceTolerance*.125;++root) {
                float mid=(lo+hi)*.5;if(mid==lo||mid==hi)break;
                if((sceneDistance(cameraPosition+rd*mid)>0)==outside)lo=mid;else hi=mid;
            }
            t=(lo+hi)*.5;p=cameraPosition+rd*t;rootFound=true;
        }
        if(!rejectedRoot&&abs(sampleValue.w)<traceTolerance)rootFound=true;
        if(rootFound) {
            if(HandoffSDFKeep(p)){hit=true;break;}
            // Una raíz ajena no termina el rayo: buscar el siguiente impacto
            // dentro de los intervalos restantes, sin repetir la misma banda.
            rejectedRoot=true;previousValid=false;
            t+=traceTolerance*.5;if(t>end)break;continue;
        }
        if(abs(sampleValue.w)>=traceTolerance)rejectedRoot=false;
        previousT=t;previousDistance=sampleValue.w;previousValid=true;
        float stepFactor=sampleValue.w>0.04?1.0:0.8;
        float advance=max(abs(sampleValue.w)*stepFactor,traceTolerance*.35);
        if(referenceTrace&&referenceMaxStep>0)advance=min(advance,referenceMaxStep);
        t+=advance;
        if(t>end)break;
    }
    if(!hit)discard;
    spatialTrace=false;
    float e=hitTolerance*.65;
    // Tetraedro simétrico: cuatro muestras con radio comparable a las seis originales.
    const vec3 k0=vec3(1,-1,-1),k1=vec3(-1,-1,1),k2=vec3(-1,1,-1),k3=vec3(1,1,1);
    e*=0.57735026919;
    vec3 gradient=k0*sceneDistance(p+k0*e)+k1*sceneDistance(p+k1*e)
        +k2*sceneDistance(p+k2*e)+k3*sceneDistance(p+k3*e);
    float norm2=dot(gradient,gradient);
    vec3 n=norm2>1e-20?gradient*inversesqrt(norm2):-rd;
    vec3 color=SDFSurfaceColor(p,n,rd);
    if(handoffDebug==23)color=vec3(1,0,1);
    if(handoffDebug==24) {
        int owner;float coverage=HandoffCoverage(p,owner);
        color=coverage>0.0&&coverage<1.0?vec3(1,1,0):vec3(1,0,1);
        for(int i=0;i<traceRegionCount;++i) {
            vec3 wall=min(abs(p-traceRegionMin[i]),abs(p-traceRegionMax[i]));
            if(all(greaterThanEqual(p,traceRegionMin[i]-vec3(hitTolerance)))&&
               all(lessThanEqual(p,traceRegionMax[i]+vec3(hitTolerance)))&&
               min(wall.x,min(wall.y,wall.z))<3.0*hitTolerance)color=vec3(1,0,0);
        }
    }
    fragmentColor=vec4(color,1);
    vec4 clip=viewProjection*vec4(p,1);gl_FragDepth=(clip.z/clip.w)*.5+.5;
}
