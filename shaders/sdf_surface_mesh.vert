struct SurfaceVertex {vec4 position;vec4 normal;vec4 material;};
layout(std430,binding=5) readonly buffer Vertices {SurfaceVertex vertices[];};
layout(std430,binding=4) readonly buffer Command {uint drawCount,instances,firstVertex,baseInstance,overflow;};
out vec3 surfacePosition,surfaceNormal,surfacePosedPosition,surfacePosedNormal;
out vec4 surfaceMaterial;
out vec3 surfaceDomainPosition,surfaceDomainNormal,surfaceDomainFlow;
out vec4 surfaceDomainProfileA,surfaceDomainProfileB;
out float surfaceDomainVentral;
flat out uint extractionOverflow;
flat out uint surfaceTissue;
uniform samplerBuffer surfacePoseData;
uniform int surfacePoseCount;
uniform vec4 surfaceCranialPlane;
uniform float surfaceCranialCollar;
uniform vec4 surfaceJawRotation;
uniform vec4 surfaceHeadRotation;
uniform vec3 surfaceHeadShift;
uniform vec3 surfaceJawPivot,surfaceJawTarget;
uniform bool surfaceJawActive;
vec3 PoseRotate(vec4 q,vec3 v) {return v+2.0*cross(q.xyz,cross(q.xyz,v)+q.w*v);}
float PoseNodeScore(int node,vec3 p) {
    int base=6*surfacePoseCount+3*node;
    vec4 lo=texelFetch(surfacePoseData,base),hi=texelFetch(surfacePoseData,base+1);
    vec3 delta=max(max(lo.xyz-p,p-hi.xyz),vec3(0));return dot(delta,delta)/max(lo.w*lo.w,1e-12);
}
vec3 SurfacePose(vec3 p,vec3 normal,float jawWeight,bool cranium,out vec3 posedNormal) {
    posedNormal=normal;
    if(surfacePoseCount==0)return p;
    float scores[5],params[5];int ids[5];
    for(int k=0;k<5;++k){scores[k]=1e30;params[k]=0;ids[k]=0;}
    if(morphMaterialCount==surfacePoseCount) {
        MorphBindings(p,scores,ids,params);
        MorphMaterialFromBindings(p,normal,scores,ids,params,surfaceDomainPosition,surfaceDomainNormal,
            surfaceDomainProfileA,surfaceDomainProfileB,surfaceDomainVentral,surfaceDomainFlow);
    }
    else {
    int stack[64],top=0;stack[top++]=0;
    while(top>0) {
        int node=stack[--top];
        if(PoseNodeScore(node,p)>scores[4])continue;
        vec4 meta=texelFetch(surfacePoseData,6*surfacePoseCount+3*node+2);
        int count=int(meta.w),first=int(meta.z);
        if(count==0) {
            int left=int(meta.x),right=int(meta.y);
            if(PoseNodeScore(left,p)<PoseNodeScore(right,p)){stack[top++]=right;stack[top++]=left;}
            else {stack[top++]=left;stack[top++]=right;}
            continue;
        }
        for(int i=first;i<first+count;++i) {
        vec4 a=texelFetch(surfacePoseData,6*i),b=texelFetch(surfacePoseData,6*i+3);
        vec3 ab=b.xyz-a.xyz;
        float t=clamp(dot(p-a.xyz,ab)/max(dot(ab,ab),1e-12),0.0,1.0);
        vec3 delta=p-mix(a.xyz,b.xyz,t);
        float radius=max(mix(a.w,b.w,t),1e-6),score=dot(delta,delta)/(radius*radius);
        for(int k=0;k<5;++k)if(score<scores[k]) {
            for(int j=4;j>k;--j){scores[j]=scores[j-1];params[j]=params[j-1];ids[j]=ids[j-1];}
            scores[k]=score;params[k]=t;ids[k]=i;break;
        }
    }
    }
    }
    if(jawWeight>=0.0&&surfaceJawActive) {
        // La mandíbula es rígida; el puente posterior conserva sus anclajes cefálicos.
        vec4 q=surfaceJawRotation;
        if(jawWeight<1.0) {
            if(dot(q,surfaceHeadRotation)<0.0)q=-q;
            q=normalize(mix(surfaceHeadRotation,q,jawWeight));
        }
        posedNormal=PoseRotate(q,normal);
        return PoseRotate(q,p-surfaceJawPivot)+surfaceJawTarget;
    }
    vec3 result=vec3(0),normalSum=vec3(0);float sum=0;
    for(int k=0;k<4;++k) {
        float support=max(0.0,1.0-scores[k]/max(scores[4],1e-6));
        float w=support*support/(scores[k]*scores[k]+1e-4);
        if(k==0&&w<1e-12)w=1;
        if(w<=0)continue;
        int i=6*ids[k];float t=params[k];
        vec3 a=PoseRotate(texelFetch(surfacePoseData,i+1),p)+texelFetch(surfacePoseData,i+2).xyz;
        vec3 b=PoseRotate(texelFetch(surfacePoseData,i+4),p)+texelFetch(surfacePoseData,i+5).xyz;
        normalSum+=w*mix(PoseRotate(texelFetch(surfacePoseData,i+1),normal),PoseRotate(texelFetch(surfacePoseData,i+4),normal),t);
        result+=w*mix(a,b,t);sum+=w;
    }
    result/=max(sum,1e-12);
    posedNormal=normalize(normalSum);
    // El morro y el cráneo siguen un único hueso; sólo el collar posterior mezcla piel.
    int nearest=6*ids[0],head=-1;
    if(texelFetch(surfacePoseData,nearest+2).w>0.5)head=nearest;
    else if(texelFetch(surfacePoseData,nearest+5).w>0.5)head=nearest+3;
    if((head>=0||cranium)&&surfaceCranialCollar>0.0) {
        float w=smoothstep(-surfaceCranialCollar,0.0,dot(p,surfaceCranialPlane.xyz)-surfaceCranialPlane.w);
        vec4 q=cranium?surfaceHeadRotation:texelFetch(surfacePoseData,head+1);
        vec3 shift=cranium?surfaceHeadShift:texelFetch(surfacePoseData,head+2).xyz;
        result=mix(result,PoseRotate(q,p)+shift,w);
        posedNormal=normalize(mix(posedNormal,PoseRotate(q,normal),w));
    }
    return result;
}
void main() {
    extractionOverflow=overflow;
    if(overflow!=0u) {vec2 p=vec2((gl_VertexID==1)?3:-1,(gl_VertexID==2)?3:-1);gl_Position=vec4(p,0,1);return;}
    SurfaceVertex v=vertices[gl_VertexID];surfacePosition=v.position.xyz;surfaceNormal=v.normal.xyz;surfaceMaterial=v.material;
    uint metadata=uint(v.material.w+.5),jaw=(metadata>>8)&511u;
    surfaceTissue=(metadata>>18)&15u;
    surfaceMaterial.w=float(metadata&255u)/255.0;
    // El material conserva coordenadas de reposo; solamente la posición visible lleva pose.
    surfaceDomainPosition=v.position.xyz/max(surfaceRecipe[11].z,.001);
    surfaceDomainNormal=v.normal.xyz;surfaceDomainProfileA=vec4(1);surfaceDomainProfileB=vec4(1);
    surfaceDomainVentral=clamp(.5-.7*v.normal.y,0.0,1.0);
    surfaceDomainFlow=normalize(vec3(0,0,-1)-v.normal.xyz*dot(vec3(0,0,-1),v.normal.xyz)+vec3(1e-12));
    if(morphMaterialCount>0 && morphMaterialCount!=surfacePoseCount)MorphMaterial(v.position.xyz,v.normal.xyz,surfaceDomainPosition,surfaceDomainNormal,
        surfaceDomainProfileA,surfaceDomainProfileB,surfaceDomainVentral,surfaceDomainFlow);
    surfacePosedPosition=SurfacePose(v.position.xyz,v.normal.xyz,jaw>0u?float(jaw-1u)/255.0:-1.0,
        (metadata&131072u)!=0u,surfacePosedNormal);
    gl_Position=gl_ModelViewProjectionMatrix*vec4(surfacePosedPosition,1);
}
