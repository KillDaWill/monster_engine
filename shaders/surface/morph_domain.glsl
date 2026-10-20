#include "pigment_regions.glsl"
// Coordenadas canónicas compartidas, transportadas por identidad anatómica.
uniform samplerBuffer morphMaterialData;
uniform int morphMaterialCount;
uniform mat3 morphWorldToView;
vec3 MorphRotate(vec4 q,vec3 v) {return v+2.0*cross(q.xyz,cross(q.xyz,v)+q.w*v);}
float MorphNodeLowerBound(int node,vec3 p) {
    int base=6*morphMaterialCount+3*node;
    vec4 lo=texelFetch(morphMaterialData,base),hi=texelFetch(morphMaterialData,base+1);
    vec3 delta=max(max(lo.xyz-p,p-hi.xyz),vec3(0));
    return dot(delta,delta)/max(lo.w*lo.w,1e-12);
}
void MorphBindings(vec3 p,out float scores[5],out int ids[5],out float params[5]) {
    for(int k=0;k<5;++k) {scores[k]=1e30;ids[k]=0;params[k]=0;}
    int stack[64],top=0;stack[top++]=0;
    while(top>0) {
        int node=stack[--top];
        if(MorphNodeLowerBound(node,p)>scores[4])continue;
        vec4 meta=texelFetch(morphMaterialData,6*morphMaterialCount+3*node+2);
        int count=int(meta.w),first=int(meta.z);
        if(count==0) {
            int left=int(meta.x),right=int(meta.y);
            if(MorphNodeLowerBound(left,p)<MorphNodeLowerBound(right,p)) {
                stack[top++]=right;stack[top++]=left;
            } else {stack[top++]=left;stack[top++]=right;}
            continue;
        }
        for(int i=first;i<first+count;++i) {
        vec4 a=texelFetch(morphMaterialData,6*i),b=texelFetch(morphMaterialData,6*i+1);
        vec3 ab=b.xyz-a.xyz;
        float t=clamp(dot(p-a.xyz,ab)/max(dot(ab,ab),1e-12),0.0,1.0);
        float radius=max(mix(a.w,b.w,t),1e-6);
        vec3 delta=p-(a.xyz+ab*t);float score=dot(delta,delta)/(radius*radius);
        for(int k=0;k<5;++k)if(score<scores[k]) {
            for(int j=4;j>k;--j) {scores[j]=scores[j-1];ids[j]=ids[j-1];params[j]=params[j-1];}
            scores[k]=score;ids[k]=i;params[k]=t;break;
        }
        }
    }
 }
void MorphMaterialFromBindings(vec3 p,vec3 worldNormal,float scores[5],int ids[5],float params[5],
    out vec3 coord,out vec3 normal,out vec4 pa,out vec4 pb,out float belly,out vec3 posedFlow) {
    coord=vec3(0);normal=vec3(0);posedFlow=vec3(0);pa=vec4(0);pb=vec4(0);
    float sum=0,trunk=0;
    PigmentClearRegions();
    for(int k=0;k<4;++k) {
        float support=max(0.0,1.0-scores[k]/max(scores[4],1e-6));
        float weight=support*support/(scores[k]*scores[k]+1e-4);
        if(k==0&&weight<1e-12)weight=1.0;
        if(weight<=0.0)continue;
        int i=ids[k];float t=params[k];
        vec4 a=texelFetch(morphMaterialData,6*i),b=texelFetch(morphMaterialData,6*i+1);
        vec4 ra=texelFetch(morphMaterialData,6*i+2),rb=texelFetch(morphMaterialData,6*i+3);
        vec4 q=texelFetch(morphMaterialData,6*i+4);
        vec3 delta=p-mix(a.xyz,b.xyz,t);
        float ratio=mix(ra.w,rb.w,t)/max(mix(a.w,b.w,t),1e-6);
        coord+=weight*(mix(ra.xyz,rb.xyz,t)+MorphRotate(q,delta)*ratio);
        normal+=weight*MorphRotate(q,worldNormal);
        posedFlow+=weight*normalize(b.xyz-a.xyz+vec3(1e-12));
        int region=clamp(int(texelFetch(morphMaterialData,6*i+5).x+.5),0,10);
        PigmentAccumulateRegion(region,weight);
        pa+=weight*surfaceRecipe[12+2*region];pb+=weight*surfaceRecipe[13+2*region];
        trunk+=weight*float(region==3||region==4);sum+=weight;
    }
    PigmentNormalizeRegions(sum);
    coord/=max(sum,1e-12);normal=normalize(normal+vec3(1e-12));
    pa/=max(sum,1e-12);pb/=max(sum,1e-12);
    belly=clamp(.5-.7*normal.y,0.0,1.0);
    pa=mix(pa,surfaceRecipe[20],belly*trunk/max(sum,1e-12));
    pb=mix(pb,surfaceRecipe[21],belly*trunk/max(sum,1e-12));
    posedFlow=normalize(posedFlow-worldNormal*dot(posedFlow,worldNormal)+vec3(1e-12));
}

void MorphMaterial(vec3 p,vec3 worldNormal,out vec3 coord,out vec3 normal,
    out vec4 pa,out vec4 pb,out float belly,out vec3 posedFlow) {
    float scores[5],params[5];int ids[5];MorphBindings(p,scores,ids,params);
    MorphMaterialFromBindings(p,worldNormal,scores,ids,params,coord,normal,pa,pb,belly,posedFlow);
}
