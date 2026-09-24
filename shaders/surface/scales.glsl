struct ScaleSample { float height; float edge; float variation; float lod; vec3 gradient; };
float ScaleInterior(vec2 local,vec2 other,float edgeWidth,float aa) {
    float border=max(0.0,(length(other)-length(local))*.5);
    return smoothstep(0.0,edgeWidth+aa,border);
}
float ScaleProfile(vec2 local,vec2 other,vec3 rnd,vec4 shape,vec4 relief,float aa) {
    if(surfaceRecipe[7].y > 0.5) {
        float border = max(0.0, (length(other) - length(local)) * 0.5);
        float bevelWidth = max(relief.z * 1.5, 0.04);
        float bevel = smoothstep(0.0, bevelWidth + aa, border);
        float plateau = 0.90 + 0.10 * bevel;
        return bevel * plateau - (1.0 - bevel) * relief.y * 1.8;
    }
    float inner=ScaleInterior(local,other,relief.z,aa);
    float angle=(rnd.z-.5)*shape.w*.5;
    local=mat2(cos(angle),-sin(angle),sin(angle),cos(angle))*local;
    float radial=clamp(1.0-dot(local*vec2(1.55,1.4),local*vec2(1.55,1.4)),0.0,1.0);
    float dome=mix(sqrt(max(radial,0.0)),radial*radial,shape.z);
    float overlap=clamp(.75+local.y*.5,.3,1.0);
    float keel=exp(-abs(local.x)*16.0)*smoothstep(-.5,0.0,local.y)*(1.0-smoothstep(.15,.55,local.y));
    return inner*(dome*overlap+relief.w*keel)-(1.0-inner)*relief.y;
}
// Celdas alternadas con jitter acotado: domos direccionales, no fisuras de roca.
ScaleSample ScalePattern(vec2 coord,vec4 shape,vec4 relief,float footprint,uint seed) {
    float detail=1.0-smoothstep(.18,.65,footprint);
    ScaleSample s=ScaleSample(0.0,0.0,.5,detail,vec3(0));
    if(footprint>=.65)return s;
    ivec2 base=ivec2(floor(coord)); float first=1e8,second=1e8;
    vec2 local=vec2(0.0),other=vec2(0.0); vec3 rnd=vec3(.5);
    for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x) {
        ivec2 id=base+ivec2(x,y); vec3 random=CellHash(id,seed);
        vec2 center=vec2(id)+vec2(.5+.25*float((id.y&1)*2-1),.5);
        center+=(random.xy-.5)*.45*shape.w;
        vec2 delta=coord-center; float dist=dot(delta,delta);
        if(dist<first) { second=first; other=local; first=dist; local=delta; rnd=random; }
        else if(dist<second) { second=dist; other=delta; }
    }
    float aa=max(.008,footprint*.5);
    s.height=ScaleProfile(local,other,rnd,shape,relief,aa)*detail;
    s.edge=(1.0-ScaleInterior(local,other,relief.z,aa))*detail;
    s.variation=mix(.5,rnd.z,detail*detail);
    // Diferencias locales de perfil: reutilizan las dos celdas, sin repetir hashes
    // ni búsqueda. Evitan escalones de normales en bloques de fragmentos 2x2.
    if(relief.x<=0.0)return s;
    vec2 ex=vec2(.008,0),ey=ex.yx;
    s.gradient.xy=vec2(
        ScaleProfile(local+ex,other+ex,rnd,shape,relief,aa)-ScaleProfile(local-ex,other-ex,rnd,shape,relief,aa),
        ScaleProfile(local+ey,other+ey,rnd,shape,relief,aa)-ScaleProfile(local-ey,other-ey,rnd,shape,relief,aa))*(detail/.016);
    return s;
}
ScaleSample TriplanarScales(vec3 p,vec3 restN,vec4 shape,vec4 relief,uint seed) {
    if(shape.x<.001)return ScaleSample(0,0,.5,0,vec3(0));
    vec3 weights=pow(abs(normalize(restN)),vec3(6.0)); weights/=max(dot(weights,vec3(1)),1e-6);
    float size=max(shape.x,.004); vec3 q=p/size;
    float aspect=clamp(shape.y,.15,6.0);
    vec2 qx=q.zy*vec2(1.0/aspect,1.0),qy=q.xz*vec2(1.0,1.0/aspect),qz=q.xy*vec2(1.0,1.0/aspect);
    float fx=max(length(dFdx(qx)),length(dFdy(qx))),fy=max(length(dFdx(qy)),length(dFdy(qy))),fz=max(length(dFdx(qz)),length(dFdy(qz)));
    float footprint=max(fx,max(fy,fz));
    if(footprint>=.65)return ScaleSample(0,0,.5,0,vec3(0));
    ScaleSample x=ScaleSample(0,0,.5,0,vec3(0)),y=x,z=x;
    vec3 selected=vec3(1);
    if(footprint>=.12) {
        float weakest=min(weights.x,min(weights.y,weights.z));
        selected=step(vec3(weakest+.00001),weights);
    }
    if(footprint>=.42) {
        float strongest=max(weights.x,max(weights.y,weights.z));
        selected=step(vec3(strongest-.00001),weights);
    }
    weights*=selected;weights/=max(dot(weights,vec3(1)),1e-6);
    if(selected.x>.5)x=ScalePattern(qx,shape,relief,fx,seed);
    if(selected.y>.5)y=ScalePattern(qy,shape,relief,fy,seed);
    if(selected.z>.5)z=ScalePattern(qz,shape,relief,fz,seed);
    vec3 gradient=weights.x*vec3(0,x.gradient.y,x.gradient.x/aspect)+
        weights.y*vec3(y.gradient.x,0,y.gradient.y/aspect)+weights.z*vec3(z.gradient.x,z.gradient.y/aspect,0);
    return ScaleSample(dot(weights,vec3(x.height,y.height,z.height)),dot(weights,vec3(x.edge,y.edge,z.edge)),
        dot(weights,vec3(x.variation,y.variation,z.variation)),footprint<.12?0.0:footprint<.42?1.0:2.0,gradient/size);
}
