#include "fur_curve.glsl"
struct FurSample { float coverage; float undercoat; float guard; float fiberRandom; vec3 tangent; };
// Secciones circulares de folículos compuestos; filtro de área cuando son subpíxel.
vec3 FurPlane(vec2 q,vec2 flow,float h,vec4 p0,vec4 p1,vec4 p2,uint seed) {
    // Una deformación suave del dominio evita filas regulares sin saltos de plano.
    q+=vec2(sin(q.y*.73+float(seed&255u)),sin(q.x*.67))* .32;
    ivec2 cell=ivec2(floor(q));vec3 r=CellHash(cell,seed);
    vec2 local=fract(q)-.5-(r.xy-.5)*.50;
    float footprint=max(length(dFdx(q)),length(dFdy(q)));
    float variation=1.0+p1.z*(r.z-.5)*.6;
    vec2 lateral=vec2(-flow.y,flow.x);
    local-=lateral*(p1.y*(r.z-.5)*.35*h*h+p1.z*(r.z-.5)*.12*h*h*(1.0-h))*p0.x*180.0;
    float aa=max(.008,footprint*.5);
    float rad=.065*p0.z*max(0.0,1.0-h/variation);
    float guard=1.0-smoothstep(rad-aa,rad+aa,length(local));
    float filtered=smoothstep(.3,.9,footprint);
    guard=mix(guard,3.14159*rad*rad,filtered)*p2.y*(1.0-smoothstep(variation-.05,variation+.05,h));
    // Sector angular más próximo: seis secundarios sin bucle ni búsquedas vecinas.
    float angle=atan(local.y,local.x)-6.283185*r.x;
    float sector=floor(angle*(6.0/6.283185)+.5);
    float theta=sector*(6.283185/6.0)+6.283185*r.x;
    vec2 center=vec2(cos(theta),sin(theta))*.23*(1.0-p1.y*h);
    float uh=h/max(.1,p2.x*variation);
    float radius=.085*p0.z*sqrt(max(0.0,1.0-uh));
    float under=1.0-smoothstep(radius-aa,radius+aa,length(local-center));
    under=mix(under,6.0*3.14159*radius*radius,filtered)*p1.w*(1.0-smoothstep(.9,1.05,uh));
    return vec3(under,guard,r.z);
}
FurSample EvaluateFurStrands(vec3 p,vec3 restN,vec3 n,vec3 flow,float h,vec4 p0,vec4 p1,vec4 p2,uint seed) {
    vec3 w=pow(abs(normalize(restN)),vec3(4));w/=max(dot(w,vec3(1)),.0001);
    // Mezcla continua: ninguna proyección ganadora, identidad en reposo.
    vec3 q=p*180.0;
    vec3 a=FurPlane(q.yz,normalize(restFlowOut.yz+vec2(.00001)),h,p0,p1,p2,seed);
    vec3 b=FurPlane(q.xz,normalize(restFlowOut.xz+vec2(.00001)),h,p0,p1,p2,seed^0x231u);
    vec3 c=FurPlane(q.xy,normalize(restFlowOut.xy+vec2(.00001)),h,p0,p1,p2,seed^0x732u);
    vec3 f=a*w.x+b*w.y+c*w.z;
    vec3 tangent=normalize(FurCurveDerivative(n,flow,p0.w,p1.x,p1.y,p1.z,f.z,h));
    return FurSample(f.x+f.y,f.x,f.y,f.z,tangent);
}
vec3 FurLighting(vec3 albedo,vec3 tangent,vec3 normal,vec3 viewPos,float roughness,float sheen,float h) {
    vec3 L=normalize(vec3(10,20,15)-viewPos),V=normalize(-viewPos),T=normalize(tangent),N=normalize(normal);
    vec3 H=normalize(L+V);
    float primary=pow(sqrt(max(0.0,1.0-pow(clamp(dot(T,H)+.08,-1.0,1.0),2.0))),mix(140.0,24.0,roughness));
    float secondary=pow(sqrt(max(0.0,1.0-pow(clamp(dot(T,H)-.20,-1.0,1.0),2.0))),mix(45.0,10.0,roughness));
    float diffuse=sqrt(max(0.0,1.0-pow(dot(T,L),2.0)));
    float shadow=exp(-.28*surfaceRecipe[8].y*(1.0-h)/max(.3,abs(dot(N,L))));
    float transmission=pow(max(0.0,dot(-L,V)),4.0)*(1.0-abs(dot(T,L)))*.12;
    return albedo*(.32+.85*diffuse*shadow+transmission)+sheen*(vec3(.7)*primary+albedo*.35*secondary)*shadow;
}
