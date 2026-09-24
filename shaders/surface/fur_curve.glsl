// Curva común a volumen y cintas. Su derivada define la tangente óptica.
vec3 FurCurveOffset(vec3 n,vec3 flow,float len,float lay,float stiff,float clump,float irregular,float rnd,float h) {
    float bend=lay*(1.0-.8*stiff);
    float side=clump*(rnd-.5)*.35, wave=irregular*(rnd-.5)*.12;
    return len*(1.0+irregular*(rnd-.5)*.6)*(n*h+flow*bend*h*h+cross(n,flow)*(side*h*h+wave*h*h*(1.0-h)));
}
vec3 FurCurveDerivative(vec3 n,vec3 flow,float lay,float stiff,float clump,float irregular,float rnd,float h) {
    return n+flow*(2.0*lay*(1.0-.8*stiff)*h)+cross(n,flow)*(clump*(rnd-.5)*.7*h+irregular*(rnd-.5)*.12*(2.0*h-3.0*h*h));
}

// Identidad de mechón compartida; las representaciones consultan el mismo dominio.
float FurFieldRandom(vec3 p,vec3 n,uint seed) {
    vec3 w=pow(abs(normalize(n)),vec3(4));w/=max(dot(w,vec3(1)),.00001);
    vec3 q=p*180.0;
    vec2 a=q.yz,b=q.xz,c=q.xy;
    a+=vec2(sin(a.y*.73+float(seed&255u)),sin(a.x*.67))*.32;
    uint sb=seed^0x231u,sc=seed^0x732u;
    b+=vec2(sin(b.y*.73+float(sb&255u)),sin(b.x*.67))*.32;
    c+=vec2(sin(c.y*.73+float(sc&255u)),sin(c.x*.67))*.32;
    return dot(vec3(CellHash(ivec2(floor(a)),seed).z,CellHash(ivec2(floor(b)),sb).z,CellHash(ivec2(floor(c)),sc).z),w);
}
