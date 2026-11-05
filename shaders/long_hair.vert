#version 330 compatibility
// Sólo diez puntos por guía; hijos y subdivisiones se evalúan en GPU.
uniform samplerBuffer guideData;
uniform int children, segments, guideDebug;
uniform float childFraction;
uniform float viewportHeight;
out vec4 hairColor;
out float ribbonSide, hairParameter;
out float strandCoverage;
out vec3 hairTangent;
out vec3 strandPosition;
uint Hash(uint x){x^=x>>16u;x*=0x7feb352du;x^=x>>15u;x*=0x846ca68bu;return x^(x>>16u);}
float Random(uint x){return float(Hash(x)&0xffffffu)/16777216.0;}
vec4 Row(int g,int r){return texelFetch(guideData,g*80+r);}
vec3 Curve(int g,float t,out vec3 derivative){
    float x=clamp(t,0.0,1.0)*9.0;int i=min(int(x),8);float u=x-float(i);
    vec3 a=Row(g,max(i-1,0)).xyz,b=Row(g,i).xyz,c=Row(g,i+1).xyz,d=Row(g,min(i+2,9)).xyz;
    vec3 linear=-a+c,quadratic=2.0*a-5.0*b+4.0*c-d,cubic=-a+3.0*b-3.0*c+d;
    derivative=4.5*(linear+2.0*quadratic*u+3.0*cubic*u*u);
    return .5*((2.0*b)+linear*u+quadratic*u*u+cubic*u*u*u);
}
vec3 Child(int g,float t,vec2 scatter,float phase,vec3 center,vec3 tangent){
    if(guideDebug>0)return center;
    vec4 groom=Row(g,14),frame=Row(g,12);
    // El marco nace en el plano cutáneo; el tubo se abre fuera de la piel.
    vec3 side=normalize(frame.xyz),normal=normalize(Row(g,13).xyz);
    vec3 rootAcross=normalize(cross(normal,side)+vec3(1e-8));
    vec3 curveAcross=cross(tangent,side);float magnitude=length(curveAcross);
    curveAcross=magnitude>1e-5?curveAcross/magnitude:rootAcross;
    if(dot(curveAcross,rootAcross)<0.0)curveAcross=-curveAcross;
    vec3 crossSide=normalize(mix(rootAcross,curveAcross,smoothstep(0.0,.35,t)));
    // Tubo volumétrico: abre al salir de la piel, conserva capas y cierra en mechones.
    float spread=frame.w*min(1.0,t*(5.0+15.0*Row(g,15).z))*(1.0-groom.y*t*.65);
    float x=t*9.0;int i=min(int(x),8);float u=x-float(i);
    vec4 b=Row(g,i),c=Row(g,i+1);float safe=mix(b.w,c.w,u);
    if(i==0){vec3 a=b.xyz,d=Row(g,2).xyz;
        // Cono de distancia desde el primer control; vuelve al certificado original.
        vec3 d0=.5*(c.xyz-a),d2=.5*(d-b.xyz),d1=3.0*(c.xyz-b.xyz)-d0-d2;
        float speed=max(length(d0),max(length(d1),length(d2)));
        safe=max(safe,max(0.0,Row(g,15).w-speed*(1.0-u))*(1.0-u));}
    spread=min(spread,safe);
    vec2 lock=vec2(cos(phase),sin(phase))*.55+vec2(sin(phase+t*7.0),cos(phase+t*5.0))*.06;
    scatter=mix(scatter,scatter*.18+lock,groom.y*smoothstep(0.0,.5,t));
    return center+(side*scatter.x+crossSide*scatter.y)*spread;
}
void main(){
    int g=gl_InstanceID/children,child=gl_InstanceID%children;
    vec4 groom=Row(g,14);float t=float(gl_VertexID/2)/float(segments);
    vec4 follicle=Row(g,16+child);
    uint seed=uint(Row(g,15).x)^uint(child)*1717u;
    strandPosition=Row(g,0).xyz;
    if(float(child)>=ceil(groom.z*childFraction)||(guideDebug==0&&Random(seed+333u)>groom.w)||groom.w<=0.0||(guideDebug>0&&child>0)||
       (guideDebug==0&&follicle.w>0.0&&Row(int(follicle.x),14).w<=0.0)){
        gl_Position=vec4(2,2,2,1);hairColor=vec4(0);ribbonSide=0;hairParameter=0;return;}
    float angle=Random(seed+13u)*6.2831853,r=sqrt(Random(seed+51u));
    vec2 scatter=vec2(cos(angle),sin(angle))*r;
    float lengthFactor=1.0-groom.x*Random(seed+123u)*.55;
    if(guideDebug>0){scatter=vec2(0);lengthFactor=1.0;}
    // Ocho fibras comparten un mechón; el resto de variaciones sigue siendo individual.
    float u=t*lengthFactor,phase=Random(uint(Row(g,15).x)^uint(child/8)*3851u)*6.2831853;
    // Una lectura de la curva y derivada analítica, compartida por ambas tangentes.
    vec3 derivative,center=Curve(g,u,derivative),ahead=center+derivative*(min(u+.005,1.0)-u),behind=center+derivative*(max(u-.005,0.0)-u);
    vec3 flow=normalize(derivative+vec3(1e-8));
    vec3 pos=Child(g,u,scatter,phase,center,flow),next=Child(g,min(u+.005,1.0),scatter,phase,ahead,flow),prev=Child(g,max(u-.005,0.0),scatter,phase,behind,flow);
    bool rooted=guideDebug==0&&follicle.w>0.0;
    if(rooted){
        int neighbor=int(follicle.x);float weight=follicle.y;
        vec3 otherDerivative,other=Curve(neighbor,u,otherDerivative);
        vec3 normal=normalize(mix(Row(g,13).xyz,Row(neighbor,13).xyz,weight));
        float fade=1.0-smoothstep(0.0,.35,u);
        float fadeDerivative=u<.35?-6.0*(u/.35)*(1.0-u/.35)/.35:0.0;
        pos=mix(center,other,weight)+normal*follicle.z*fade;
        derivative=mix(derivative,otherDerivative,weight)+normal*follicle.z*fadeDerivative;
        next=pos+derivative*.005;prev=pos-derivative*.005;
    }
    strandPosition=pos;
    vec3 viewPos=(gl_ModelViewMatrix*vec4(pos,1)).xyz;
    vec3 tangent=normalize(mat3(gl_ModelViewMatrix)*(next-prev)+vec3(1e-8));
    vec3 side=normalize(cross(tangent,normalize(-viewPos))+vec3(1e-8));
    ribbonSide=(gl_VertexID%2==0)?-1.0:1.0;
    float width=Row(g,13).w*(.7+.6*Random(seed+715u))*pow(max(1.0-t,.001),.55);
    if(!rooted)width*=1.0+Row(g,15).z*(1.0-smoothstep(0.0,.3,t));
    if(guideDebug>0)width=max(width*2.0,.002)*(t<.025?2.5:1.0);
    float pixel=2.0/(max(viewportHeight,1.0)*gl_ProjectionMatrix[1][1]);
    if(gl_ProjectionMatrix[3][3]==0.0)pixel*=abs(viewPos.z);
    float filteredWidth=max(width,pixel*.65);
    strandCoverage=width/filteredWidth;
    width=filteredWidth;
    viewPos+=side*ribbonSide*width;
    gl_Position=gl_ProjectionMatrix*vec4(viewPos,1);
    hairColor=mix(Row(g,10),Row(g,11),t);
    hairColor.rgb*=.82+.18*Random(seed+193u);
    if(guideDebug>0){float correction=Row(g,15).y;
        hairColor=vec4(t<.025?vec3(.1,.8,1):mix(vec3(.05,.9,.25),vec3(1,.12,.02),smoothstep(.002,.08,correction)),1);}
    hairParameter=t;
    hairTangent=tangent;
}
