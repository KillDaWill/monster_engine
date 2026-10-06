// Una sola propiedad anatómica para piel, raíces de pelaje e impactos SDF.
uniform int handoffCount,handoffPass,handoffDebug;
uniform int handoffRetire[16];
uniform vec4 handoffOrigin[16],handoffNormal[16],handoffMeta[16];
uniform vec3 handoffWorkMin[16],handoffWorkMax[16];
uniform samplerBuffer handoffSegments;
float HandoffDistance(int i,vec3 p) {
    // Poda conservadora del soporte; la caja no define la propiedad visible.
    if(any(lessThan(p,handoffWorkMin[i]))||any(greaterThan(p,handoffWorkMax[i])))return -1e6;
    vec3 delta=p-handoffOrigin[i].xyz;
    float longitudinal=dot(delta,handoffNormal[i].xyz)-handoffNormal[i].w;
    float proximal=max(longitudinal,length(delta)-2.0*handoffOrigin[i].w);
    if(proximal< -max(handoffMeta[i].x,.01))return proximal;
    int first=int(handoffMeta[i].z),count=int(handoffMeta[i].w);
    float support=-1e6;
    for(int j=first;j<first+count;++j) {
        vec4 segmentA=texelFetch(handoffSegments,2*j),segmentB=texelFetch(handoffSegments,2*j+1);
        vec3 a=segmentA.xyz,ab=segmentB.xyz-a;
        float t=clamp(dot(p-a,ab)/max(dot(ab,ab),1e-12),0.0,1.0);
        support=max(support,segmentA.w-distance(p,a+ab*t));
    }
    return min(support,proximal);
}
float HandoffCoverage(vec3 p,out int owner) {
    float result=0.0;owner=-1;
    for(int i=0;i<handoffCount;++i) {
        float width=max(handoffMeta[i].x,.01);
        float coverage=smoothstep(-width,width,HandoffDistance(i,p))*handoffMeta[i].y;
        if(coverage>result) {result=coverage;owner=i;}
    }
    return result;
}
float HandoffThreshold() {
    ivec2 pixel=ivec2(gl_FragCoord.xy);
    int rank=0;
    for(int bit=0;bit<3;++bit) {
        int x=(pixel.x>>bit)&1,y=(pixel.y>>bit)&1;
        rank+=((x^y)*2+y)<<(2*(2-bit));
    }
    return (float(rank)+.5)/64.0;
}
bool HandoffMeshKeep(vec3 root) {
    int owner;return HandoffThreshold()>=HandoffCoverage(root,owner);
}
bool HandoffSDFKeep(vec3 hit) {
    if(handoffPass== -1)return true;
    int owner;float coverage=HandoffCoverage(hit,owner);
    return owner>=0&&handoffRetire[owner]==0&&
        (handoffPass== -2||owner==handoffPass)&&HandoffThreshold()<coverage;
}
