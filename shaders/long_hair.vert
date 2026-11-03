#version 330 compatibility
// Sólo diez puntos por guía; hijos y subdivisiones se evalúan en GPU.
uniform samplerBuffer guideData;
uniform int children, segments, guideDebug;
uniform float childFraction;
out vec4 hairColor;
out float ribbonSide, hairParameter;
out vec3 strandPosition;
uint Hash(uint x){x^=x>>16u;x*=0x7feb352du;x^=x>>15u;x*=0x846ca68bu;return x^(x>>16u);}
float Random(uint x){return float(Hash(x)&0xffffffu)/16777216.0;}
vec4 Row(int g,int r){return texelFetch(guideData,g*16+r);}
vec3 Curve(int g,float t){
    float x=clamp(t,0.0,1.0)*9.0;int i=min(int(x),8);float u=x-float(i);
    vec3 a=Row(g,max(i-1,0)).xyz,b=Row(g,i).xyz,c=Row(g,i+1).xyz,d=Row(g,min(i+2,9)).xyz;
    return .5*((2.0*b)+(-a+c)*u+(2.0*a-5.0*b+4.0*c-d)*u*u+(-a+3.0*b-3.0*c+d)*u*u*u);
}
vec3 Child(int g,float t,vec2 scatter,float phase){
    vec4 groom=Row(g,14),frame=Row(g,12);
    vec3 tangent=normalize(Curve(g,min(t+.01,1.0))-Curve(g,max(t-.01,0.0))+vec3(1e-8));
    // Las raíces hijas ocupan el plano cutáneo, no un disco que atraviese la piel.
    vec3 side=normalize(frame.xyz),normal=normalize(Row(g,13).xyz);
    vec3 rootAcross=normalize(cross(normal,side)+vec3(1e-8));
    vec3 crossSide=normalize(mix(rootAcross,normalize(cross(tangent,side)+vec3(1e-8)),t)+vec3(1e-8));
    float spread=frame.w*(1.0-groom.y*t)*(.75+.25*sin(t*5.0+phase));
    return Curve(g,t)+(side*scatter.x+crossSide*scatter.y)*spread;
}
void main(){
    int g=gl_InstanceID/children,child=gl_InstanceID%children;
    vec4 groom=Row(g,14);float t=float(gl_VertexID/2)/float(segments);
    uint seed=uint(Row(g,15).x)^uint(child)*1717u;
    strandPosition=Row(g,0).xyz;
    if(float(child)>=ceil(groom.z*childFraction)||(guideDebug==0&&Random(seed+333u)>groom.w)||groom.w<=0.0||(guideDebug>0&&child>0)){
        gl_Position=vec4(2,2,2,1);hairColor=vec4(0);ribbonSide=0;hairParameter=0;return;}
    float angle=Random(seed+13u)*6.2831853,r=sqrt(Random(seed+51u));
    vec2 scatter=vec2(cos(angle),sin(angle))*r;
    float lengthFactor=1.0-groom.x*Random(seed+123u)*.55;
    if(guideDebug>0){scatter=vec2(0);lengthFactor=1.0;}
    float u=t*lengthFactor,phase=Random(seed+71u)*6.2831853;
    vec3 pos=Child(g,u,scatter,phase),next=Child(g,min(u+.005,1.0),scatter,phase),prev=Child(g,max(u-.005,0.0),scatter,phase);
    strandPosition=pos;
    vec3 viewPos=(gl_ModelViewMatrix*vec4(pos,1)).xyz;
    vec3 tangent=normalize(mat3(gl_ModelViewMatrix)*(next-prev)+vec3(1e-8));
    vec3 side=normalize(cross(tangent,normalize(-viewPos))+vec3(1e-8));
    ribbonSide=(gl_VertexID%2==0)?-1.0:1.0;
    float width=Row(g,13).w*pow(max(1.0-t,.01),.65);
    if(guideDebug>0)width=max(width*2.0,.002);
    viewPos+=side*ribbonSide*width;
    gl_Position=gl_ProjectionMatrix*vec4(viewPos,1);
    hairColor=mix(Row(g,10),Row(g,11),t);
    hairColor.rgb*=.82+.18*Random(seed+193u);
    if(guideDebug>0)hairColor=vec4(mix(vec3(.05,.9,.25),vec3(1,.2,.05),t),1);
    hairParameter=t;
}
